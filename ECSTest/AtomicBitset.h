#pragma once

#include "MemoryPool.h"
#include <atomic>

template<std::size_t MinBits>
class AtomicBitset
{
private:
	struct AtomicBitsetBlock
	{
		std::atomic_size_t Bits[BLOCK_SIZE / sizeof(std::atomic_size_t)];
	};

	static const std::size_t BLOCK_COUNT = std::max(MinBits / 8, BLOCK_SIZE) / (BLOCK_SIZE);

	static const std::size_t BLOCK_BITS = std::bit_width(BLOCK_COUNT);
	static const std::size_t OFFSET_BITS = std::bit_width(BLOCK_SIZE / sizeof(std::atomic_size_t));
	static const std::size_t INTERNAL_SHIFT_BITS = 6;

	inline static std::tuple<std::uint16_t, std::uint8_t, std::uint8_t> GetComponents(std::size_t index)
	{
		auto internal = index & ~(~0ull << INTERNAL_SHIFT_BITS);
		auto offset = (index >> INTERNAL_SHIFT_BITS) & ~(~0ull << OFFSET_BITS);
		auto block = (index >> (INTERNAL_SHIFT_BITS + OFFSET_BITS)) & ~(~0ull << BLOCK_BITS);

		return { block, offset, internal };
	}

	inline static std::size_t ToIndex(std::uint16_t block, std::uint8_t offset, std::uint8_t bit)
	{
		return bit | (offset << INTERNAL_SHIFT_BITS) | (block << (INTERNAL_SHIFT_BITS + OFFSET_BITS));
	}
public:
	class OnesIterator
	{
	public:
		using iterator = OnesIterator;
		using reference = std::size_t&;
		using pointer = std::size_t *;

		using iterator_category = std::input_iterator_tag;
		using value_type = std::size_t;
		using difference_type = std::ptrdiff_t;

		OnesIterator(AtomicBitset *bitset, std::size_t index) 
			: m_onesLeft(bitset->m_oneCount), m_curIndex(0), m_curBitIndex(0), m_curBlockIndex(0)
		{
			if (m_onesLeft == 0) return;

			m_curBlock = bitset->m_blocks[0].Load();
			if (m_curBlock)
			{
				m_curBitvec = &m_curBlock->Bits[0];
				if (!(m_curBitvec->load() & 1))
				{
					FindNextOne();
				}
			}
		}

		OnesIterator() : m_onesLeft(0)
		{
		}

		iterator operator++(int)
		{
			iterator old = *this;
			++(*this);
			return old;
		}

		iterator& operator++()
		{
			if (m_onesLeft > 1)
			{
				FindNextOne();
				*m_curBitvec ^= 1 << m_curBitIndex;
				--m_onesLeft;
			}

			return *this;
		}

		value_type operator*()
		{
			return m_curIndex;
		}

		auto operator<=>(const iterator& other) const
		{
			return other.m_onesLeft <=> m_onesLeft;
		}

		auto operator==(const iterator& other) const
		{
			return m_onesLeft == other.m_onesLeft;
		}
	private:
		void FindNextOne()
		{
			++m_curIndex;

			do
			{
				auto [blockIndex, offsetIndex, bitIndex] = GetComponents(m_curIndex);

				if (blockIndex != m_curBlockIndex)
				{
					m_curBlockIndex = blockIndex;
					m_curBlock = m_bitset->m_blocks[blockIndex].Load();

					m_curBitvecIndex = offsetIndex;
					m_curBitvec = &m_curBlock->Bits[offsetIndex];
				}

				m_curBitvecIndex = offsetIndex;
				m_curBitvec = &m_curBlock->Bits[offsetIndex];
				
				auto curVal = m_curBitvec->load() & (~0ull << bitIndex);
				if (curVal == 0)
				{
					m_curIndex += 64 - bitIndex;
				}
				else
				{
					m_curBitIndex = std::countr_zero(curVal);
					break;
				}

			} while (true);
		}

		AtomicBitset *m_bitset;

		AtomicBitsetBlock *m_curBlock;
		std::atomic_size_t *m_curBitvec;
		
		std::size_t m_curIndex;
		std::size_t m_curBlockIndex;
		std::size_t m_curBitvecIndex;
		std::size_t m_curBitIndex;

		std::size_t m_onesLeft;
	};

	bool Get(std::size_t index);
	void Set(std::size_t index, bool value);
	std::size_t AllocateOne();
	std::size_t GetSize();
	std::size_t GetOneCount();
	void GrowBitsTo(std::size_t minBitCount);

	OnesIterator begin();
	OnesIterator end();
private:
	void Grow();

	std::array<MemoryPool::Ptr<AtomicBitsetBlock>, BLOCK_COUNT> m_blocks;
	std::atomic_size_t m_count;
	std::atomic_size_t m_oneCount;
};

template<std::size_t MinBits>
inline bool AtomicBitset<MinBits>::Get(std::size_t index)
{
	auto [block, offset, bit] = GetComponents(index);
	return (m_blocks[block]->Bits[offset] >> bit) & 1;
}

template<std::size_t MinBits>
inline void AtomicBitset<MinBits>::Set(std::size_t index, bool value)
{
	auto [block, offset, bit] = GetComponents(index);
	auto& bits = m_blocks[block]->Bits[offset];

	if (value)
	{
		auto oldBits = bits.fetch_or(1ull << bit);
		if (!(oldBits & (1ull << bit)))
			++m_oneCount;
	}
	else
	{
		auto oldBits = bits.fetch_xor(1ull << bit);
		if (oldBits & (1ull << bit))
			--m_oneCount;
	}
}

template<std::size_t MinBits>
inline std::size_t AtomicBitset<MinBits>::GetSize()
{
	return m_count;
}

template<std::size_t MinBits>
inline std::size_t AtomicBitset<MinBits>::GetOneCount()
{
	return m_oneCount;
}

template<std::size_t MinBits>
inline void AtomicBitset<MinBits>::GrowBitsTo(std::size_t minBitCount)
{
	while (m_count < minBitCount)
		Grow();
}

template<std::size_t MinBits>
inline AtomicBitset<MinBits>::OnesIterator AtomicBitset<MinBits>::begin()
{
	return OnesIterator(this, 0);
}

template<std::size_t MinBits>
inline AtomicBitset<MinBits>::OnesIterator AtomicBitset<MinBits>::end()
{
	return OnesIterator();
}

template<std::size_t MinBits>
inline void AtomicBitset<MinBits>::Grow()
{
	auto [block, offset, bit] = GetComponents(m_count.fetch_add(BLOCK_SIZE * 8));
	
	auto allocBlock = MemoryPool::RequestBlock<AtomicBitsetBlock>();
	std::fill_n(allocBlock->Bits, BLOCK_SIZE / sizeof(std::atomic_size_t), 0ull);

	m_blocks[block] = std::move(allocBlock);
	m_blocks[block].NotifyNonnull();
}
