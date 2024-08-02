#pragma once

#include "MemoryPool.h"

template<typename T>
concept StoreCompatible = sizeof(T) >= sizeof(size_t);

template<StoreCompatible T>
class PooledStore
{
private:
	static const std::size_t T_PER_BLOCK = BLOCK_SIZE / sizeof(T);
	static const std::size_t MAX_INDICES_PER_STORE = 84;

	struct Block
	{
		T Data[T_PER_BLOCK];
	};

	static const std::size_t BLOCKS_PER_INDEX = (BLOCK_SIZE) / (2 * sizeof(std::atomic<Block *>));
	static const std::size_t T_PER_INDEX = T_PER_BLOCK * BLOCKS_PER_INDEX;

	struct BlockIndexNode
	{
		std::shared_mutex WriterLock[BLOCKS_PER_INDEX];
		MemoryPool::Ptr<Block> Block[BLOCKS_PER_INDEX];
	};

	static std::tuple<std::int32_t, std::int16_t, std::int16_t> GetInternalIndices(std::size_t index)
	{
		auto [indexNodeIndex, indexNodeOffset] = std::div(static_cast<long long>(index), T_PER_INDEX);
		auto [blockIndex, blockOffset] = std::div(static_cast<long long>(index), T_PER_BLOCK);

		return { indexNodeIndex, blockIndex, blockOffset };
	}
public:
	static const std::size_t MAX_T_PER_STORE = MAX_INDICES_PER_STORE * T_PER_INDEX;

	template<typename TIter> requires std::same_as<T, TIter> || std::same_as<const T, TIter>
	class Iterator
	{
	public:
		using iterator = Iterator<TIter>;
		using reference = TIter&;
		using pointer = TIter *;

		using iterator_category = std::forward_iterator_tag;
		using value_type = TIter;
		using difference_type = std::ptrdiff_t;

		static constexpr inline bool IsConst = std::is_const_v<TIter>;

		Iterator(const Iterator<TIter>& other)
		{
			m_store = other.m_store;
			m_updateBlock = nullptr;
			m_undefinedBlock = true;

			m_curNode = other.m_curNode;

			m_curIndex = other.m_curIndex;
			m_curNodeIndex = other.m_curNodeIndex;
			m_curBlockIndex = other.m_curBlockIndex;
			m_curTIndex = other.m_curTIndex;
		}

		Iterator<TIter>& operator=(const Iterator<TIter>& other)
		{
			m_store = other.m_store;
			m_updateBlock = nullptr;
			m_undefinedBlock = true;

			m_curNode = other.m_curNode;

			m_curIndex = other.m_curIndex;
			m_curNodeIndex = other.m_curNodeIndex;
			m_curBlockIndex = other.m_curBlockIndex;
			m_curTIndex = other.m_curTIndex;
		}

		Iterator(Iterator<TIter>&& other)
		{
			m_store = other.m_store;
			m_updateBlock = std::move(other.m_updateBlock);
			m_undefinedBlock = other.m_undefinedBlock;

			m_curNode = other.m_curNode;
			m_curBlock = other.m_curBlock;
			m_curT = other.m_curT;

			m_curIndex = other.m_curIndex;
			m_curNodeIndex = other.m_curNodeIndex;
			m_curBlockIndex = other.m_curBlockIndex;
			m_curTIndex = other.m_curTIndex;
		}

		Iterator<TIter>& operator=(Iterator<TIter>&& other)
		{
			m_store = other.m_store;
			m_updateBlock = std::move(other.m_updateBlock);
			m_undefinedBlock = other.m_undefinedBlock;

			m_curNode = other.m_curNode;
			m_curBlock = other.m_curBlock;
			m_curT = other.m_curT;

			m_curIndex = other.m_curIndex;
			m_curNodeIndex = other.m_curNodeIndex;
			m_curBlockIndex = other.m_curBlockIndex;
			m_curTIndex = other.m_curTIndex;
		}

		Iterator(PooledStore<T>& store, std::size_t index) :
			m_store(&store), m_curIndex(index), m_updateBlock(nullptr),
			m_curNodeIndex(std::numeric_limits<std::size_t>::max()),
			m_curBlockIndex(std::numeric_limits<std::size_t>::max()),
			m_curTIndex(std::numeric_limits<std::size_t>::max()),
			m_undefinedBlock(true)
		{
			Next(0);
		}

		Iterator() :
			m_curIndex(std::numeric_limits<std::size_t>::max()), m_updateBlock(nullptr), m_undefinedBlock(true)
		{
		}

		~Iterator()
		{
			if (!m_undefinedBlock && m_updateBlock)
				FlushUpdateBlock();
		}

		auto AsConst()
		{
			return Iterator<std::add_const_t<TIter>>(*m_store, m_curIndex);
		}

		auto AsMutable()
		{
			return Iterator<std::remove_const_t<TIter>>(*m_store, m_curIndex);
		}

		iterator operator++(int)
		{
			iterator old = *this;
			++(*this);
			return old;
		}

		iterator& operator++()
		{
			Next(1);
			return *this;
		}

		iterator operator+(difference_type diff)
		{
			iterator newIter = *this;
			newIter += diff;
			return newIter;
		}

		iterator& operator+=(difference_type diff)
		{
			Next(diff);
			return *this;
		}

		reference operator*() const
		{
			// Hack to get around iterator rules
			const_cast<PooledStore<T>::template Iterator<TIter> *>(this)->Deref();
			return *m_curT;
		}

		pointer operator->()
		{
			return &(**this);
		}

		auto operator<=>(const iterator& other) const
		{
			return m_curIndex <=> other.m_curIndex;
		}

		auto operator==(const iterator& other) const
		{
			return m_curIndex == other.m_curIndex;
		}

		inline std::size_t GetIndex() const
		{
			return m_curIndex;
		}
	private:
		PooledStore<T> *m_store;

		MemoryPool::Ptr<Block> m_updateBlock;
		bool m_undefinedBlock;

		BlockIndexNode *m_curNode;
		Block *m_curBlock;
		TIter *m_curT;

		std::size_t m_curNodeIndex;
		std::size_t m_curBlockIndex;
		std::size_t m_curTIndex;
		std::size_t m_curIndex;

		void Deref()
		{
			[[unlikely]]
			if (m_undefinedBlock)
			{
				m_curNode = m_store->m_nodes[m_curNodeIndex].Load();
				if constexpr (!IsConst)
				{
					m_updateBlock = MemoryPool::RequestBlock<Block>();
					m_curNode->WriterLock[m_curBlockIndex].lock();
				}

				m_curBlock = m_curNode->Block[m_curBlockIndex].Load();

				if constexpr (!IsConst)
				{
					std::copy_n(reinterpret_cast<T *>(m_curBlock->Data), T_PER_BLOCK, reinterpret_cast<T *>(m_updateBlock->Data));
					m_curBlock = m_updateBlock.Load();
				}

				m_curT = reinterpret_cast<TIter *>(m_curBlock->Data) + m_curTIndex;

				m_undefinedBlock = false;
			}
		}

		void FlushUpdateBlock()
		{
			// RCU swap here
			m_curNode->Block[m_curBlockIndex].WeakSwap(m_updateBlock);
			m_curNode->WriterLock[m_curBlockIndex].unlock();
			m_store->m_reclaimList.push(std::move(m_updateBlock));
		}

		void Next(std::size_t offset)
		{
			auto nextIndex = m_curIndex + offset;
			auto [nextNode, nextBlock, nextOffset] = GetInternalIndices(nextIndex);

			[[unlikely]]
			if (nextNode != m_curNodeIndex || nextBlock != m_curBlockIndex)
			{
				if (!IsConst && !m_undefinedBlock)
					FlushUpdateBlock();
				m_undefinedBlock = true;
				m_curNodeIndex = nextNode;
				m_curBlockIndex = nextBlock;
			}
			else if (!m_undefinedBlock)
			{
				// Block already in use
				m_curT = reinterpret_cast<TIter *>(m_curBlock->Data) + offset;
			}

			m_curTIndex = nextOffset;
			m_curIndex = nextIndex;
		}
	};

	using MutableIterator = Iterator<T>;
	using ConstIterator = Iterator<const T>;

	PooledStore();
	PooledStore(const PooledStore<T>&) = delete;
	PooledStore& operator=(const PooledStore<T>&) = delete;

	MutableIterator Emplace(std::size_t firstIndex, std::size_t count);
	MutableIterator Get(std::size_t index);
	ConstIterator GetConst(std::size_t index);
	void ReclaimBlocks();

	template<typename T>
	Iterator<T> GetIterator(std::size_t index)
	{
		return Iterator<T>(*this, index);
	}
private:
	std::array<MemoryPool::Ptr<BlockIndexNode>, MAX_INDICES_PER_STORE> m_nodes;
	concurrency::concurrent_queue<MemoryPool::Ptr<Block>> m_reclaimList;
};

template<StoreCompatible T>
inline PooledStore<T>::PooledStore()
{
}

template<StoreCompatible T>
inline PooledStore<T>::MutableIterator PooledStore<T>::Emplace(std::size_t firstIndex, std::size_t count)
{
	auto [firstNode, firstBlock, firstOffset] = GetInternalIndices(firstIndex);
	auto [lastNode, lastBlock, lastOffset] = GetInternalIndices(firstIndex + count - 1);

	auto firstInitNode = firstOffset == 0 ? firstNode : (firstNode + 1);

	// TODO: optimize this loop
	for (auto nodeIndex = firstNode; nodeIndex <= lastNode; ++nodeIndex)
	{
		auto& node = m_nodes[nodeIndex];

		auto blockIndex = nodeIndex > firstNode ? 0 : firstBlock;
		auto lastBlockIndex = nodeIndex < lastNode ? BLOCKS_PER_INDEX : lastBlock;

		if (!node)
		{
			if (blockIndex == 0)
			{
				node = MemoryPool::RequestBlock<BlockIndexNode>();
				node.NotifyNonnull();
			}
			else
			{
				node.WaitNonnull();
			}
		}

		auto loadedNode = node.Load();

		for (; blockIndex <= lastBlockIndex; ++blockIndex)
		{
			auto& lock = loadedNode->WriterLock[blockIndex];
			auto& block = loadedNode->Block[blockIndex];

			auto offset = blockIndex > firstBlock && nodeIndex > firstNode ? 0 : firstOffset;
			auto lastOffsetIndex = blockIndex < lastBlock && nodeIndex < lastNode ? T_PER_BLOCK : lastOffset;

			if (!block)
			{
				if (offset == 0)
				{
					auto newBlock = MemoryPool::RequestBlock<Block>();

					if constexpr (std::same_as<std::size_t, T>)
					{
						for (; offset <= lastOffsetIndex; ++offset)
							newBlock->Data[offset] = T_PER_BLOCK * blockIndex + offset;
					}
					else
					{
						for (; offset <= lastOffsetIndex; ++offset)
							new (newBlock->Data + offset) T();
					}

					block = newBlock;
					block.NotifyNonnull();
				}
				else
				{
					block.WaitNonnull();
				}
			}
		}
	}

	return MutableIterator(*this, firstIndex);
}

template<StoreCompatible T>
inline PooledStore<T>::MutableIterator PooledStore<T>::Get(std::size_t index)
{
	return GetIterator<T>(index);
}

template<StoreCompatible T>
inline PooledStore<T>::ConstIterator PooledStore<T>::GetConst(std::size_t index)
{
	return GetIterator<const T>(index);
}

template<StoreCompatible T>
inline void PooledStore<T>::ReclaimBlocks()
{
	m_reclaimList.clear();
}