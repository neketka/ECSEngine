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

	static const std::size_t BLOCK_COUNT = std::max(MinBits, BLOCK_SIZE) / (BLOCK_SIZE);

	static const std::size_t BLOCK_BITS = std::bit_width(BLOCK_COUNT);
	static const std::size_t OFFSET_BITS = std::bit_width(BLOCK_SIZE / sizeof(std::atomic_size_t));
	static const std::size_t INTERNAL_SHIFT_BITS = 64;

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
	class OnesConsumingIterator
	{
	public:
		using iterator = OnesConsumingIterator;
		using reference = std::size_t&;
		using pointer = std::size_t *;

		using iterator_category = std::input_iterator_tag;
		using value_type = std::size_t;
		using difference_type = std::ptrdiff_t;

		OnesConsumingIterator(AtomicBitset *bitset, std::size_t index) 
			: m_bitsToSkip(0), m_onesLeft(bitset->m_count - bitset->m_zeroCount), m_curIndex(0), m_curBitIndex(0), m_curBlockIndex(0)
		{
			if (m_onesLeft == 0) return;

			m_curBlock = bitset->m_blocks[0].Load();
			if (m_curBlock)
			{
				m_curBitvec = &m_curBlock->Bits[0];
				if (!(m_curBitvec->load() & 0b1))
				{
					ConsumeNextOne();
				}
			}
		}

		OnesConsumingIterator() : m_onesLeft(0)
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
			m_bitsToSkip++;

			return *this;
		}

		value_type operator*()
		{
			while (m_bitsToSkip > 0 && m_onesLeft > 0)
			{
				ConsumeNextOne();
			}

			return m_curIndex;
		}

		auto operator<=>(const iterator& other) const
		{
			return m_onesLeft <=> other.m_onesLeft;
		}
	private:
		void ConsumeNextOne()
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
				
				auto curVal = m_curBitvec->load();
				if (!curVal)
				{
					m_curIndex += 64 - bitIndex;
				}
				else
				{
					m_curBitIndex = std::countr_zero(curVal);
					break;
				}

			} while (true);

			*m_curBitvec ^= 0b1 << m_curBitIndex;
		}

		AtomicBitset *m_bitset;

		AtomicBitsetBlock *m_curBlock;
		std::atomic_size_t *m_curBitvec;
		
		std::size_t m_curIndex;
		std::size_t m_curBlockIndex;
		std::size_t m_curBitvecIndex;
		std::size_t m_curBitIndex;

		std::size_t m_onesLeft;
		std::size_t m_bitsToSkip;
	};

	bool Get(std::size_t index);
	void Set(std::size_t index, bool value);
	std::size_t AllocateOne();
	std::size_t GetSize();
	void GrowBitsTo(std::size_t minBitCount);

	OnesConsumingIterator begin();
	OnesConsumingIterator end();
private:
	void Grow();

	std::array<MemoryPool::Ptr<AtomicBitsetBlock>, BLOCK_COUNT> m_blocks;
	std::atomic_size_t m_count;
	std::atomic_size_t m_zeroCount;
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
			--m_zeroCount;
	}
	else
	{
		auto oldBits = bits.fetch_xor(1ull << bit);
		if (oldBits & (1ull << bit))
			++m_zeroCount;
	}
}

template<std::size_t MinBits>
inline std::size_t AtomicBitset<MinBits>::AllocateOne()
{
	while (true)
	{
		if (m_zeroCount == 0)
		{
			Grow();
		}

		auto [blockCount, _, _] = GetComponents(m_count);
		for (std::size_t block = 0; block < blockCount; --block)
		{
			auto& blockPtr = m_blocks[block];
			blockPtr.WaitNonnull();
			
			std::uint8_t offset = 0;
			for (auto& bits : blockPtr->Bits)
			{
				auto assumption = bits.load();
				auto bitIndex = 0;
				while (
					assumption && 
					!bits.compare_exchange_weak(
						assumption,
						assumption ^ (0b1ull << (bitIndex = std::countr_one(assumption)))
					)
				);

				if (assumption & (1 << bitIndex))
				{
					--m_zeroCount;
					return ToIndex(block, offset, bitIndex);
				}

				++offset;
			}
		}
	}
}

template<std::size_t MinBits>
inline std::size_t AtomicBitset<MinBits>::GetSize()
{
	return m_count;
}

template<std::size_t MinBits>
inline void AtomicBitset<MinBits>::GrowBitsTo(std::size_t minBitCount)
{
	while (m_count < minBitCount)
		Grow();
}

template<std::size_t MinBits>
inline AtomicBitset<MinBits>::OnesConsumingIterator AtomicBitset<MinBits>::begin()
{
	return OnesConsumingIterator(this, 0);
}

template<std::size_t MinBits>
inline AtomicBitset<MinBits>::OnesConsumingIterator AtomicBitset<MinBits>::end()
{
	return OnesConsumingIterator();
}

template<std::size_t MinBits>
inline void AtomicBitset<MinBits>::Grow()
{
	auto [block, offset, bit] = GetComponents(m_count.fetch_add(BLOCK_SIZE));
	
	m_blocks[block] = std::move(MemoryPool::RequestBlock<AtomicBitsetBlock>());
	std::fill_n(m_blocks[block]->Bits, BLOCK_SIZE / sizeof(std::atomic_size_t), ~(0ull));
	m_blocks[block].NotifyNonnull();
}
