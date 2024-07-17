#pragma once

#include <cstddef>
#include <concepts>
#include <vector>
#include <memory>
#include <array>
#include <atomic>
#include <tuple>
#include <concurrent_queue.h>
#include <concurrent_vector.h>
#include <concurrent_unordered_map.h>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <span>

using ObjId = std::size_t; // 32 bit ArchetypeID, 24 bit Index, 8 bit Generation
using ArchId = std::uint32_t;

const auto BLOCK_SIZE_EXP = 5u;
const auto BLOCK_SIZE_MASK = ~(~0u << BLOCK_SIZE_EXP);
const auto BLOCK_SIZE = 1u << BLOCK_SIZE_EXP;
const auto SCRATCH_BYTES = 512 * 1024 * 1024; // 512 MiB

template<std::movable T>
struct ComponentBlockStorage
{
	concurrency::concurrent_vector<ComponentBlock<T>> Blocks;
	std::size_t DynamicTypeSize;
};

struct CopyOp
{
	std::size_t SrcIndex;
	std::size_t DestIndex;
	ArchId SrcArchetypeId;
	ArchId DestArchetypeId;
	bool SrcInScratch;
	bool DestInScratch;
};

struct AllocationOp
{
	std::size_t Count;
	ArchId ArchetypeId;
};

struct CleanupOp
{
	std::size_t NewBufferSize;
	std::vector<std::pair<std::size_t, std::size_t>> MoveOps; // Src, Dest
	ArchId ArchetypeId;
};

class IComponentStorage
{
public:
	virtual void Allocate(AllocationOp& op) = 0;
	virtual void Copy(CopyOp& op) = 0;
	virtual void Cleanup(CleanupOp& op) = 0;
	
	virtual std::span<std::byte> GetComponentDynamic(ArchId archetypeId, std::size_t index, bool inScratch) = 0;
	virtual std::span<std::byte> GetIterableComponentsDynamic(ArchId archetypeId) = 0;

	virtual void SetScratch(void *begin, std::atomic_size_t& counter) = 0;
};

class IDynamicComponentLifecycle
{
	virtual void Construct(void *component) = 0;
	virtual void Destruct(void *component) = 0;
};

using DynamicComponent = unsigned char;

template<std::movable T>
class ComponentStorage : public IComponentStorage
{
public:
	inline static constexpr bool IsDynamic = std::same_as<T, DynamicComponent>;

	ComponentStorage() : m_dynTypeSize(1), m_inScratchCount(0)
	{ 
	}

	ComponentStorage(std::size_t dynTypeSize, IDynamicComponentLifecycle *lifecycle) 
		: m_dynTypeSize(IsDynamic ? dynTypeSize : false), m_lifecycle(lifecycle), m_inScratchCount(0)
	{
	}

	void Allocate(AllocationOp& op) override
	{
	}

	void Cleanup(CleanupOp& op) override
	{
	}

	void Copy(CopyOp& op) override
	{
		
	}

	T& GetComponent(ArchId archetypeId, std::size_t index, bool inScratch)
	{
		if (inScratch)
			return m_scratchBegin + index * m_dynTypeSize;
		return m_archetypes[archetypeId][index];
	}

	std::span<T> GetIterableComponents(ArchId archetypeId)
	{
		return m_archetypes[archetypeId];
	}

	std::span<std::byte> GetIterableComponentsDynamic(ArchId archetypeId) override
	{
		return std::as_writable_bytes(GetIterableComponents(archetypeId));
	}

	std::span<std::byte> GetComponentDynamic(ArchId archetypeId, std::size_t index, bool inScratch) override
	{
		return std::as_writable_bytes(std::span<T>(&GetComponent(archetypeId, index, inScratch), 1));
	}

	void SetScratch(void *begin, std::atomic_size_t& counter) override
	{
		m_scratchBegin = reinterpret_cast<T *>(begin);
		m_scratchByteOffset = &counter;
	}
private:
	concurrency::concurrent_unordered_map<ArchId, std::span<T>> m_archetypes;

	T *m_scratchBegin;
	std::atomic_size_t *m_scratchByteOffset;

	std::size_t m_dynTypeSize;
	IDynamicComponentLifecycle *m_lifecycle;
};

struct ArchetypeData
{
	ObjId UniqueId;
	std::vector<std::size_t> ComponentIndices;

	std::unique_ptr<std::atomic_size_t> EntryCount;
	std::unique_ptr<std::atomic_size_t> FreeBlockPtr;
	std::vector<std::pair<std::size_t, std::int8_t>> FreeBlockIndices;
	std::vector<std::atomic_size_t> ArchetypeDeletedBits;

	concurrency::concurrent_vector<std::int32_t> SparseMap;
};

template<std::movable... TComponents>
class ComponentSet
{
public:
	ComponentSet()
	{
		RegisterDynStorages<0, TComponents...>();
	}

	template<typename TComponent>
	inline static constexpr std::size_t ComponentIndex = GetTypeIndex<TComponent, TComponents...>();

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
	std::tuple<ComponentBlockQuery<TArchComponents>...> GetObjectBlock(ObjId id)
	{
	}

	// RW accesses + delete access to components externally synchronized
	template<typename... TArchComponents>
	std::vector<ComponentBlockQuery<TArchComponents...>> GetBlocks(
		ArchId archetypeId, std::optional<ArchId> uniqueId = std::nullopt)
	{
	}

	void Delete(ObjId id)
	{
	}

	void AddDynamicComponent(IComponentStorage *storage)
	{
	}

	// Externally synchronized
	void CleanupDeleted()
	{
	}
private:
	std::tuple<ComponentStorage<TComponents>...> m_storages;
	std::vector<IComponentStorage *> m_dynStorages;
	concurrency::concurrent_queue<std::size_t> m_freeArchetypes;
	concurrency::concurrent_vector<ArchetypeData> m_archetypeData;
	concurrency::concurrent_unordered_map<std::size_t, concurrency::concurrent_vector<std::size_t>> m_uidToArchMap;
	
	template<std::size_t Index, typename TComp, typename... TComps>
	void RegisterDynStorages()
	{
		m_dynStorages.push_back(std::get<Index>(m_storages));
		if constexpr (sizeof...(TComps) != 0)
			RegisterDynStorages<Index + 1, TComps...>();
	}
};