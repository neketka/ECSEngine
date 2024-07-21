#pragma once

#include "MemoryPool.h"
#include <atomic>

template<std::size_t MinBits>
class AtomicBitset
{
public:
	bool Get(std::size_t index);
	void Set(std::size_t index, bool value);
	std::size_t AllocateOne();
private:
	void Grow();

	struct AtomicBitsetBlock
	{
		std::atomic_size_t Bits[BLOCK_SIZE / sizeof(std::atomic_size_t)];
	};

	static const std::size_t BLOCK_COUNT = std::max(MinBits, BLOCK_SIZE) / BLOCK_SIZE;

	static const std::size_t BLOCK_BITS = std::bit_width(BLOCK_COUNT);
	static const std::size_t OFFSET_BITS = std::bit_width(BLOCK_SIZE / sizeof(std::atomic_size_t));
	static const std::size_t INTERNAL_SHIFT_BITS = std::bit_width(sizeof(std::size_t) << 3);

	inline std::tuple<std::uint16_t, std::uint8_t, std::uint8_t> GetComponents(std::size_t index)
	{
		auto internal = index & ~(~0ull << INTERNAL_SHIFT_BITS);
		auto offset = (index >> INTERNAL_SHIFT_BITS) & ~(~0ull << OFFSET_BITS);
		auto block = (index >> (INTERNAL_SHIFT_BITS + OFFSET_BITS)) & ~(~0ull << BLOCK_BITS);

		return { block, offset, internal };
	}

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

	if (value) bits |= 1ull << bit;
	else bits ^= 1ull << bit;
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
		for (auto block = blockCount - 1; block >= 0; --block)
		{
			auto& block = m_blocks[block];
		}
	}
}

template<std::size_t MinBits>
inline void AtomicBitset<MinBits>::Grow()
{
	auto [block, offset, bit] = GetComponents(m_count.fetch_add(64));
	

}
