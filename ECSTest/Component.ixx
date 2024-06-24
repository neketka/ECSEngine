module;

#include <mutex>
#include <algorithm>
#include <unordered_map>
#include <array>
#include <vector>
#include <shared_mutex>
#include <optional>
#include <concurrent_unordered_map.h>

export module Component;

import Allocator;

export using ObjId = std::size_t;

class Archetype
{
public:
	std::size_t ArchetypeIndex;
	std::vector<std::size_t> ComponentIndices;
	PageAllocationIndexer<> Indexer;
};

template<std::movable ...TComponent> 
class ComponentStorage;

template<std::movable TComponent>
class ComponentStorage<TComponent>
{
public:
	ComponentStorage(std::size_t index) : m_index(index) {}
	ComponentStorage() : m_index(0) {}

	template<class... TComponents>
	void Create(const Archetype& arch, const PageAllocationOp& op, const std::size_t firstId)
	{
		if constexpr ((std::same_as<TComponent, TComponents> || ...))
		{
			auto& allocator = m_archAllocators[arch.ArchetypeIndex];
			allocator.Allocate(op, firstId);
		}
	}

	template<std::same_as<TComponent> T>
	TComponent& GetByAllocId(const Archetype& arch, size_t allocId)
	{
		return m_archAllocators[arch.ArchetypeIndex].Get(allocId);
	}

	template<std::same_as<TComponent> T>
	std::vector<PageAllocatorElements<TComponent>> GetPages(const std::vector<Archetype>& archetypes, const std::vector<size_t>& indices)
	{
		std::vector<PageAllocatorElements<TComponent>> allocs;
		allocs.reserve(archetypes.size());

		for (size_t& archIndex : archetypes)
		{
			auto& arch = archetypes[archIndex];
			allocs.push_back(arch.Indexer.GetAllocatorElements(m_archAllocators[archIndex]));
		}

		return allocs;
	}

	void CleanupUnsync(const Archetype& arch, const PageDeletionOp& op)
	{
		if (m_archAllocators.contains(arch.ArchetypeIndex))
			m_archAllocators[arch.ArchetypeIndex].CleanupDeletedUnsync(op);
	}

	template<std::same_as<TComponent> T>
	std::size_t GetIndex()
	{
		return m_index;
	}
private:
	std::size_t m_index;
	std::unordered_map<std::size_t, PageAllocator<TComponent>> m_archAllocators;
};

template<std::movable TComponent, std::movable ...TRest>
class ComponentStorage<TComponent, TRest...>
{
public:
	ComponentStorage(std::size_t index) : m_storageCur(index), m_storageRest(index + 1) {}
	ComponentStorage() : m_storageCur(0), m_storageRest(1) {}

	template<class... TComponents>
	void Create(const Archetype& arch, const PageAllocationOp& op, const std::size_t firstId)
	{
		m_storageCur.Create<TComponents...>(arch, op, firstId);
		m_storageRest.Create<TComponents...>(arch, op, firstId);
	}

	template<class T>
	T& GetByAllocId(const Archetype& arch, size_t allocId)
	{
		if constexpr (std::same_as<TComponent, T>)
			return m_storageCur.GetByAllocId<T>(arch, allocId);
		else
			return m_storageRest.GetByAllocId<T>(arch, allocId);
	}

	template<class T>
	std::vector<PageAllocatorElements<T>> GetPages(const std::vector<Archetype>& archetypes, const std::vector<size_t>& indices)
	{
		if constexpr (std::same_as<T, TComponent>)
			return m_storageCur.GetAllocators(archetypes, indices);
		else
			return m_storageRest.GetAllocators<T>(archetypes, indices);
	}

	void CleanupUnsync(const Archetype& arch, const PageDeletionOp& op)
	{
		m_storageCur.CleanupUnsync(arch, op);
		m_storageRest.CleanupUnsync(arch, op);
	}

	template<class T>
	std::size_t GetIndex()
	{
		if constexpr (std::same_as<TComponent, T>)
			return m_storageCur.GetIndex<T>();
		else 
			return m_storageRest.GetIndex<T>();
	}
private:
	ComponentStorage<TComponent> m_storageCur;
	ComponentStorage<TRest...> m_storageRest;
};

template<class ...TComponents>
using QueryBlocks = std::tuple<std::vector<PageAllocatorElements<TComponents>>...>;

template<class ...TComponents>
struct QueryBlock
{
	const std::size_t PageIndex;
	const std::size_t BlockIndex;
	const std::atomic_size_t& PageDeletionBits;
	std::shared_mutex& PageLock;
	std::shared_mutex& BlockLock;
	std::tuple<std::reference_wrapper<TComponents>...> Elements;
};

export
template<std::movable ...TComponents>
class ComponentSet
{
public:
	template<class ...TComponents>
	std::vector<std::size_t> BuildIndexList()
	{
		return { m_storage.template GetIndex<TComponents>()... };
	}

	template<class ...TComponents>
	std::size_t PrepareArchetype()
	{
		std::lock_guard<std::shared_mutex> glock(m_globalLock);
		std::vector<std::size_t> indexList = BuildIndexList<TComponents...>();

		std::size_t archIndex = 0;
		for (auto& archPtr : m_archetypes)
		{
			if (std::ranges::equal(indexList, archPtr->ComponentIndices))
				return archIndex;
			++archIndex;
		}

		auto& arch = *m_archetypes.emplace_back(std::make_unique<Archetype>());
		arch.ArchetypeIndex = archIndex;
		arch.ComponentIndices = indexList;

		for (auto [comps, archs] : m_queriesToCompsArchs)
		{
			if (std::ranges::includes(indexList, comps))
				archs.push_back(archIndex);
		}

		return archIndex;
	}

	template<class ...TComponents>
	QueryBlocks<TComponents...> GetQueryBlocks(std::size_t queryIndex)
	{
		m_globalLock.lock_shared();
		auto& [_, archetypes] = m_queriesToCompsArchs[queryIndex];
		auto allocators = std::make_tuple(m_storage.template GetPages<TComponents>(m_archetypes, archetypes)...);

		m_globalLock.unlock_shared();

		return allocators;
	}

	template<class ...TComponents>
	QueryBlock<TComponents...> GetQueryBlockById(ObjId id)
	{
		m_globalLock.lock_shared();
		auto [archId, allocId] = m_objToArchetypeAllocation[id];
		Archetype& arch = *m_archetypes[archId];

		auto [pageIndex, blockIndex, pageLock, blockLock, deletionBits] = arch.Indexer.GetBlockByAlloc(allocId);

		QueryBlock<TComponents...> block = {
			pageIndex,
			blockIndex,
			deletionBits,
			pageLock,
			blockLock,
			std::make_tuple(std::ref(m_storage.GetByAllocId<TComponents>(arch, allocId))...)
		};

		m_globalLock.unlock_shared();

		return block;
	}

	template<class ...TComponents>
	std::vector<ObjId> CreateObjects(std::size_t archId, std::size_t count)
	{
		std::vector<ObjId> objIds(count);
		auto firstId = m_objIdCounter.fetch_add(count);

		m_globalLock.lock_shared();

		auto& arch = *m_archetypes[archId];
		auto allocOp = arch.Indexer.Allocate(count);
		m_storage.template Create<TComponents...>(arch, allocOp, firstId);

		for (std::size_t i = 0; i < count; ++i)
		{
			auto curId = objIds[i] = firstId + i;
			auto allocId = allocOp.FirstIndex + i;
			m_allocationToObj[allocId] = curId;
			m_objToArchetypeAllocation[curId] = { archId, allocId };
		}

		m_globalLock.unlock_shared();

		return objIds;
	}

	void Cleanup()
	{
		std::lock_guard<std::shared_mutex> glock(m_globalLock);
		for (auto& archPtr : m_archetypes)
		{
			Archetype& arch = *archPtr;

			auto op = arch.Indexer.CleanupUnsync();
			m_storage.CleanupUnsync(arch, op);
			for (auto deleted : op.DeletedIndices)
			{
				auto objId = m_allocationToObj[deleted];

				m_allocationToObj.unsafe_erase(deleted);
				m_objToArchetypeAllocation.unsafe_erase(objId);
			}

			for (auto [src, dest] : op.SrcToDestMoveIndices)
			{
				auto movedObjId = m_allocationToObj[src];
				auto [arch, _] = m_objToArchetypeAllocation[movedObjId];

				m_objToArchetypeAllocation[movedObjId] = { arch, dest };
				m_allocationToObj[dest] = movedObjId;

				m_allocationToObj.unsafe_erase(src);
			}
		}
	}
private:
	std::shared_mutex m_globalLock;
	ComponentStorage<ObjId, TComponents...> m_storage;

	std::vector<std::unique_ptr<Archetype>> m_archetypes;
	std::vector<std::pair<std::vector<std::size_t>, std::vector<std::size_t>>> m_queriesToCompsArchs;

	concurrency::concurrent_unordered_map<std::size_t, ObjId> m_allocationToObj;
	concurrency::concurrent_unordered_map<ObjId, std::pair<std::size_t, std::size_t>> m_objToArchetypeAllocation;

	std::atomic_size_t m_objIdCounter = 0;
};