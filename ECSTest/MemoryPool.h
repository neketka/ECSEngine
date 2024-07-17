#pragma once

#include <tuple>
#include <span>
#include <atomic>
#include <shared_mutex>
#include <optional>
#include <concurrent_queue.h>
#include <vector>
#include <array>
#include <concepts>

const auto BLOCK_SIZE = 512;
class MemoryPool
{
public:
	MemoryPool(std::size_t blockCount);

	template<typename T>
	T *RequestBlock();

	template<typename T>
	void ReturnBlock(T *block);
private:
	concurrency::concurrent_queue<std::size_t *> m_blocks;
};

template<typename T>
concept trivial = std::is_trivial<T>::value;

template<trivial T>
class PooledStore
{
private:
	static const std::size_t T_PER_BLOCK = BLOCK_SIZE / sizeof(T);

	struct Block
	{
		std::size_t Data[T_PER_BLOCK * sizeof(T) / sizeof(std::size_t)];
	};

	static const std::size_t BLOCKS_PER_INDEX = (BLOCK_SIZE) / (2 * sizeof(std::atomic<Block *>));
	static const std::size_t T_PER_INDEX = T_PER_BLOCK * BLOCKS_PER_INDEX;

	struct BlockIndexNode
	{
		std::recursive_mutex WriterLock[BLOCKS_PER_INDEX];
		std::atomic<Block *> Block[BLOCKS_PER_INDEX];
	};

	static std::tuple<std::int32_t, std::int16_t, std::int16_t> GetInternalIndices(std::size_t index)
	{
		auto [indexNodeIndex, indexNodeOffset] = std::div(static_cast<long long>(index), T_PER_INDEX);
		auto [blockIndex, blockOffset] = std::div(static_cast<long long>(index), T_PER_BLOCK);

		return { indexNodeIndex, blockIndex, blockOffset };
	}
public:
	template<typename TIter> requires std::same_as<T, TIter> || std::same_as<const T, TIter>
	class InternalIterator : public std::iterator<std::forward_iterator_tag, T>
	{
	public:
		using iterator = InternalIterator<TIter>;
		using reference = TIter&;
		using pointer = TIter *;
		static constexpr inline bool IsConst = std::same_as<const T, TIter>;
		
		InternalIterator(const InternalIterator<TIter>& other)
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

		InternalIterator<TIter>& operator=(const InternalIterator<TIter>& other)
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

		InternalIterator(InternalIterator<TIter>&& other)
		{
			m_store = other.m_store;
			m_updateBlock = other.m_updateBlock;
			m_undefinedBlock = other.m_undefinedBlock;

			m_curNode = other.m_curNode;
			m_curBlock = other.m_curBlock;
			m_curT = other.m_curT;

			m_curIndex = other.m_curIndex;
			m_curNodeIndex = other.m_curNodeIndex;
			m_curBlockIndex = other.m_curBlockIndex;
			m_curTIndex = other.m_curTIndex;
		}

		InternalIterator<TIter>& operator=(InternalIterator<TIter>&& other)
		{
			m_store = other.m_store;
			m_updateBlock = other.m_updateBlock;
			m_undefinedBlock = other.m_undefinedBlock;

			m_curNode = other.m_curNode;
			m_curBlock = other.m_curBlock;
			m_curT = other.m_curT;

			m_curIndex = other.m_curIndex;
			m_curNodeIndex = other.m_curNodeIndex;
			m_curBlockIndex = other.m_curBlockIndex;
			m_curTIndex = other.m_curTIndex;
		}

		InternalIterator(PooledStore<T>& store, std::size_t index) :
			m_store(&store), m_curIndex(index), m_updateBlock(nullptr),
			m_curNodeIndex(std::numeric_limits<std::size_t>::max()),
			m_curBlockIndex(std::numeric_limits<std::size_t>::max()),
			m_curTIndex(std::numeric_limits<std::size_t>::max()),
			m_undefinedBlock(true)
		{
			Next(0);
		}

		InternalIterator() :
			m_curIndex(std::numeric_limits<std::size_t>::max()), m_updateBlock(nullptr), m_undefinedBlock(true)
		{
		}

		~InternalIterator()
		{
			if (!m_undefinedBlock && m_updateBlock)
				FlushUpdateBlock();
		}

		std::size_t GetCurrentIndex()
		{
			return m_curIndex;
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

		reference operator*()
		{
			[[unlikely]]
			if (m_undefinedBlock)
			{
				m_curNode = m_store->m_nodes[m_curNodeIndex];
				if constexpr (!IsConst)
				{
					m_updateBlock = m_store->m_pool.RequestBlock<Block>();
					m_curNode->WriterLock[m_curBlockIndex].lock();
				}

				m_curBlock = m_curNode->Block[m_curBlockIndex];
				m_curT = reinterpret_cast<TIter *>(m_curBlock->Data) + m_curTIndex;

				// this will fail on non-trivials
				if constexpr (!IsConst)
					std::copy_n(reinterpret_cast<T *>(m_curBlock->Data), T_PER_BLOCK, reinterpret_cast<T *>(m_updateBlock->Data));

				m_undefinedBlock = false;
			}

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
	private:
		PooledStore<T> *m_store;

		Block *m_updateBlock;
		bool m_undefinedBlock;

		BlockIndexNode *m_curNode;
		Block *m_curBlock;
		TIter *m_curT;

		std::size_t m_curNodeIndex;
		std::size_t m_curBlockIndex;
		std::size_t m_curTIndex;
		std::size_t m_curIndex;

		void FlushUpdateBlock()
		{
			// RCU swap here
			m_curNode->Block[m_curBlockIndex].exchange(m_updateBlock);
			m_curNode->WriterLock[m_curBlockIndex].unlock();
			m_store->m_reclaimList.push(m_updateBlock);
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

	using iterator = InternalIterator<T>;
	using const_iterator = InternalIterator<const T>;

	PooledStore(MemoryPool& pool);
	~PooledStore();

	iterator Emplace(std::size_t index);
	void MoveDestruct(iterator&& src, iterator&& dest);
	iterator Get(std::size_t index);
	const_iterator GetConst(std::size_t index);
	void ReclaimBlocks();
private:
	MemoryPool& m_pool;
	std::array<std::atomic<BlockIndexNode *>, 256> m_nodes;
	concurrency::concurrent_queue<Block *> m_reclaimList;
};

inline MemoryPool::MemoryPool(std::size_t blockCount)
{
	for (std::size_t i = 0; i < blockCount; ++i)
	{
		m_blocks.push(new std::size_t[BLOCK_SIZE / sizeof(std::size_t)]);
	}
}

template<typename T>
inline T *MemoryPool::RequestBlock()
{
	std::size_t *block;
	if (m_blocks.try_pop(block))
	{
		return new(block) T;
	}

	return nullptr;
}

template<typename T>
inline void MemoryPool::ReturnBlock(T *block)
{
	block.~T();
	m_blocks.push(reinterpret_cast<std::size_t *>(block));
}

template<trivial T>
inline PooledStore<T>::PooledStore(MemoryPool& pool) : m_pool(pool)
{
}

template<trivial T>
inline PooledStore<T>::~PooledStore()
{
	for (auto& atomicNode : m_nodes)
	{
		auto node = atomicNode.load();
		for (auto& atomicBlock : node->Block)
		{
			auto block = atomicBlock.load();
			
		}
	}
}

template<trivial T>
inline PooledStore<T>::iterator PooledStore<T>::Emplace(std::size_t index)
{
	return iterator();
}

template<trivial T>
inline void PooledStore<T>::MoveDestruct(iterator&& src, iterator&& dest)
{
}

template<trivial T>
inline PooledStore<T>::iterator PooledStore<T>::Get(std::size_t index)
{
}

template<trivial T>
inline PooledStore<T>::const_iterator PooledStore<T>::GetConst(std::size_t index)
{
}

template<trivial T>
inline void PooledStore<T>::ReclaimBlocks()
{
	PooledStore<T>::Block *block;
	while (m_reclaimList.try_pop(block))
	{
		m_pool.ReturnBlock(block);
	}
}