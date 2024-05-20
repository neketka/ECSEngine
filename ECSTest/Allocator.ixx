module;

#include <concepts>
#include <array>
#include <atomic>
#include <semaphore>
#include <vector>
#include <shared_mutex>
#include <bitset>
#include <span>

export module Allocator;

const std::size_t PageOffsetBits = 6; // 64 max blocks
const std::size_t PageBlockCount = 1 << PageOffsetBits;
const std::size_t PageOffsetMask = PageBlockCount - 1;

export class PageAllocationOp
{
public:
	std::size_t FirstIndex;
	std::size_t LastIndex;
};

export class PageDeletionOp
{
public:
	std::vector<std::pair<std::size_t, std::size_t>> SrcToDestMoveIndices;
	std::vector<std::size_t> DeletedIndices;
};

export class AllocatedPageIndex
{
public:
	std::atomic_int64_t DeletedBitset = 0;
	std::array<std::shared_mutex, PageBlockCount> Locks;
};

export
template<std::movable T>
class AllocatedPage
{
public:
	AllocatedPage() : Locks(), Data(new T[PageBlockCount]) {}

	std::unique_ptr<T[]> Data;
	std::array<std::shared_mutex, PageBlockCount> Locks;
};

template<size_t MaxPages>
class PageAllocationIndexer;

export 
template<std::movable T, size_t MaxPages=128>
class PageAllocator
{
public:
	PageAllocator()
	{
		for (auto& ptr : m_allocationAwaiters)
			ptr = std::make_unique<std::atomic_bool>(false);
	}

	void Allocate(const PageAllocationOp& op)
	{
		const auto firstPage = op.FirstIndex >> PageOffsetBits;
		const auto lastPage = op.LastIndex >> PageOffsetBits;

		for (auto curPage = firstPage; curPage <= lastPage; ++curPage)
		{
			auto& curPagePtr = m_pages[curPage];

			const auto block0 = curPage << PageOffsetBits;
			const auto firstBlock = std::max(block0, op.FirstIndex);
			const auto lastBlock = std::min(block0 | ~(~0 << PageOffsetBits), op.LastIndex);
			auto& existenceFlag = *m_allocationAwaiters[curPage].get();

			if (block0 >= firstBlock && block0 <= lastBlock)
			{
				if (!curPagePtr.get())
				{
					curPagePtr = std::make_unique<AllocatedPage<T>>();
					existenceFlag.store(true);
					existenceFlag.notify_all();
				}
			}
			else
			{
				while (!existenceFlag)
					existenceFlag.wait(true);
			}

			auto firstBlockOffset = firstBlock & PageOffsetMask;
			auto lastBlockOffset = lastBlock & PageOffsetMask;

			for (auto curBlock = firstBlockOffset; curBlock <= lastBlockOffset; ++curBlock)
			{
				new(&curPagePtr->Data[curBlock]) T;
			}
		}
	}

	void CleanupDeletedUnsync(const PageDeletionOp& op)
	{
		// Ignore locks here since this is externally-synchronized

		for (auto delIndex : op.DeletedIndices)
		{
			auto [_, delBlock] = Get(delIndex);
			delBlock.~T();
		}

		for (auto [src, dest] : op.SrcToDestMoveIndices)
		{
			auto [_, srcBlock] = Get(src);
			auto [__, destBlock] = Get(dest);
			destBlock = srcBlock;
		}
	}

	std::pair<std::shared_mutex&, T&> Get(size_t index)
	{
		auto& page = m_pages[index >> PageOffsetBits];
		auto offset = index & PageOffsetMask;
		return { page->Locks[offset], page->Data[offset] };
	}
private:
	friend class PageAllocationIndexer<MaxPages>;

	std::array<std::unique_ptr<std::atomic_bool>, MaxPages> m_allocationAwaiters;
	std::array<std::unique_ptr<AllocatedPage<T>>, MaxPages> m_pages;
};

export
template<class T>
class PageAllocatorElements
{
public:
	PageAllocatorElements(std::unique_ptr<AllocatedPage<T>> *pages, AllocatedPageIndex *indexes, std::size_t firstFreeIndex) : 
		m_pages(pages), m_indexes(indexes), 
		m_lastPageSize(1 + ((firstFreeIndex - 1) & PageOffsetBits)),
		m_pageCount(firstFreeIndex == 0 ? 0 : (1 + ((firstFreeIndex - 1) >> PageOffsetBits)))
	{
	}

	std::tuple<AllocatedPage<T>&, AllocatedPageIndex&, std::size_t> GetPage(std::size_t pageIndex)
	{
		auto pageSize = pageIndex == m_pageCount - 1 ? m_lastPageSize : PageBlockCount;
		return { *m_pages[pageIndex].get(), m_indexes[pageIndex], pageSize };
	}

	std::size_t GetPageCount()
	{
		return m_pageCount;
	}
private:
	AllocatedPageIndex *m_indexes;
	std::unique_ptr<AllocatedPage<T>> *m_pages;
	std::size_t m_pageCount;
	std::size_t m_lastPageSize;
};


export template<size_t MaxPages = 128>
class PageAllocationIndexer
{
public:
	PageAllocationOp Allocate(std::size_t count)
	{
		PageAllocationOp op;
		op.FirstIndex = m_firstFreeAddress.fetch_add(count);
		op.LastIndex = op.FirstIndex + count - 1;

		return op;
	}

	void Delete(size_t index)
	{
		auto& pageIndex = m_pageIndexer[index >> PageOffsetBits];
		auto offset = index & PageOffsetMask;

		std::lock_guard<std::shared_mutex> guard(pageIndex.Locks[offset]);
		pageIndex.DeletedBitset |= 1ull << offset;
	}

	std::shared_mutex& GetLock(size_t index)
	{
		return m_pageIndexer[index >> PageOffsetBits].Locks[index & PageOffsetMask];
	}

	bool IsDeletedUnsync(size_t index)
	{
		auto& pageIndex = m_pageIndexer[index >> PageOffsetBits];
		auto offset = index & PageOffsetMask;

		return (pageIndex.DeletedBitset >> offset) & 1;
	}

	PageDeletionOp CleanupUnsync()
	{
		PageDeletionOp op;

		if (m_firstFreeAddress == 0) return op;

		for (std::size_t curIndex = m_firstFreeAddress - 1;; --curIndex)
		{
			auto& pageIndex = m_pageIndexer[curIndex >> PageOffsetBits];
			auto offset = curIndex & PageOffsetMask;

			if ((pageIndex.DeletedBitset >> offset) & 1)
			{
				--m_firstFreeAddress;
				op.DeletedIndices.push_back(curIndex);
				if (m_firstFreeAddress != curIndex)
				{
					op.SrcToDestMoveIndices.push_back({ m_firstFreeAddress, curIndex });
				}
				pageIndex.DeletedBitset |= 1ull << offset;
			}

			// Avoid underflow
			if (curIndex == 0) {
				break;
			}
		}

		return op;
	}

	template<class T>
	PageAllocatorElements<T> GetAllocatorElements(PageAllocator<T>& allocator)
	{
		return PageAllocatorElements<T>(allocator.m_pages.data(), m_pageIndexer.data(), m_firstFreeAddress);
	}
private:
	std::array<AllocatedPageIndex, MaxPages> m_pageIndexer;
	std::atomic_size_t m_firstFreeAddress;
};