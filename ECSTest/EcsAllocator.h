#pragma once

#include <vector>
#include <memory>
#include <array>
#include <atomic>

template<class T, size_t SecondaryBlockSize = 64, size_t SecondaryMaxCount = 16>
class EcsAllocator
{
public:
private:
	struct SecondaryBlock
	{
		std::unique_ptr<T[]> Storage;
		std::atomic_bool IsAllocated;
	};

	std::unique_ptr<T[]> m_primaryStorage;
	std::array<SecondaryBlock, SecondaryBlockSize> m_secondaryStorage;
	std::atomic_size_t m_storageSize;
	size_t primaryCapacity;
};
