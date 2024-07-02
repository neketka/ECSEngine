#pragma once

#include <cstddef>
#include <concepts>
#include <vector>
#include <memory>
#include <array>
#include <atomic>
#include <tuple>
#include <concurrent_queue.h>
#include <optional>

using ObjId = std::size_t; // 32 bit ArchetypeID, 24 bit Index, 8 bit Generation ID
using ArchId = std::uint32_t;

const auto MAX_COMPONENT_BLOCKS = 256;
const auto SMALL_BLOCK_SIZE = 8;
const auto LARGE_BLOCK_SIZE = 64;

template<std::movable T>
struct ComponentBlock
{
	std::unique_ptr<T[]> Storage;
};

template<std::movable T>
struct ComponentBlockStorage
{
	std::array<ComponentBlock<T>, MAX_COMPONENT_BLOCKS> Blocks;
};

class IComponentStorage
{
public:
	virtual void ConstructDynamic(ArchId archetypeId, std::size_t block, std::size_t index) = 0;
};

template<std::movable T>
class ComponentStorage : public IComponentStorage
{
public:
	void PushBlock(ArchId archetypeId, bool large)
	{
	}

	void PopBlock(ArchId archetypeId)
	{
	}

	void Move(ArchId archetypeId, std::size_t srcBlock, std::size_t srcIndex, std::size_t destBlock, std::size_t destIndex)
	{
	}

	void Copy(ArchId archetypeId, std::size_t srcBlock, std::size_t srcIndex, std::size_t destBlock, std::size_t destIndex)
	{
	}

	void Construct(ArchId archetypeId, std::size_t block, std::size_t index)
	{
	}

	T& Get(std::size_t block, std::size_t index)
	{
	}

	std::vector<ComponentBlock<T>>& GetBlocks(ArchId archetypeId)
	{
	}

	void ConstructDynamic(ArchId archetypeId, std::size_t block, std::size_t index) override
	{
		Construct(archetypeId, block, index);
	}
private:
	std::vector<ArchId> m_sparseMap;
	std::vector<std::unique_ptr<ComponentBlockStorage<T>>> m_archetypes;

	concurrency::concurrent_queue<std::unique_ptr<T[]>> m_smallBlocks;
	concurrency::concurrent_queue<std::unique_ptr<T[]>> m_largeBlocks;
};

struct ArchetypeData
{
	std::vector<std::size_t> ComponentIndices;
	ObjId UniqueId;
};

template<std::movable... TComponents>
class ComponentSet
{
public:
	template<typename TComponent>
	static std::size_t GetComponentIndex()
	{
		return GetTypeIndex<TComponent, TComponents...>();
	}

	template<typename... TArchComponents>
	ArchId GetArchetype(ObjId uniqueId = 0)
	{
	}

	ArchId GetArchetypeDynamic(std::vector<std::size_t> compIndices, ObjId uniqueId = 0)
	{
	}

	void DeleteArchetype(ArchId archtypeId)
	{
	}

	template<typename... TArchComponents>
	ObjId Create(ArchId archetypeId)
	{
	}

	ObjId CreateDynamic(ArchId archetypeId)
	{
	}

	ObjId Copy(ObjId id)
	{
	}

	template<typename... TRemappedComponents>
	ObjId CopyPartial(ObjId srcId, ArchId archetypeId)
	{
	}

	ObjId CopyPartialDynamic(ObjId srcId, ArchId archetypeId, std::vector<std::size_t> compIndices)
	{
	}

	template<typename... TArchComponents>
	std::tuple<TArchComponents&...> Get(ObjId id)
	{
	}

	template<typename... TArchComponents>
	std::vector<std::tuple<ComponentBlock<TArchComponents>&...>> GetBlocks(std::optional<ArchId> archetypeId = std::nullopt)
	{
	}

	void Delete(ObjId id)
	{
	}

	void CleanupDeleted()
	{
	}
};