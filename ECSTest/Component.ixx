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

using ObjId = std::size_t;

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
	void Create(const Archetype& arch, const PageAllocationOp& op)
	{
		if constexpr ((std::same_as<TComponent, TComponents> || ...))
		{
			auto allocator = m_archAllocators[arch.ArchetypeIndex];
			allocator.Allocate(op);
		}
	}

	template<std::same_as<TComponent> T>
	std::pair<std::shared_mutex&, TComponent&> GetByAllocId(const Archetype& arch, size_t allocId)
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
	static std::size_t GetIndex()
	{
		return m_index;
	}
private:
	std::size_t m_index;
	std::unordered_map<std::size_t, PageAllocator<TComponent>> m_archAllocators;
};

export 
template<std::movable TComponent, std::movable ...TRest>
class ComponentStorage<TComponent, TRest...>
{
public:
	ComponentStorage(std::size_t index) : m_storageCur(index), m_storageRest(index + 1) {}
	ComponentStorage() : m_storageCur(0), m_storageRest(1) {}

	template<class... TComponents>
	void Create(const Archetype& arch, const PageAllocationOp& op)
	{
		m_storageCur.Create<TComponents...>(arch, op);
		m_storageRest.Create<TComponents...>(arch, op);
	}

	template<class T>
	std::pair<std::shared_mutex&, T&> GetByAllocId(const Archetype& arch, size_t allocId)
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

export
template<std::movable ...TComponents>
class ComponentSet
{
public:
	template<class ...TComponents>
	using QueryBlocks = std::tuple<std::vector<PageAllocatorElements<TComponents>>...>;

	template<class ...TComponents>
	std::vector<std::size_t> BuildIndexList()
	{
		return { m_storage.template GetIndex<TComponents>()... };
	}

	template<class ...TComponents>
	std::size_t PrepareQuery()
	{
		std::lock_guard<std::shared_mutex> glock(m_globalLock);
		std::vector<std::size_t> indexList = BuildIndexList<TComponents...>();

		std::size_t queryIndex = 0;
		for (auto [comps, _] : m_queriesToCompsArchs)
		{
			if (comps == indexList)
				return queryIndex;
			++queryIndex;
		}

		auto& [comps, archs] = m_queriesToCompsArchs.emplace_back();
		comps.insert(comps.begin(), indexList);

		std::size_t archIndex = 0;
		for (auto& arch : m_archetypes)
		{
			if (std::includes(arch.ComponentIndices.begin(), arch.ComponentIndices.end(), comps.begin(), comps.end()))
				archs.push_back(archIndex);
			++archIndex;
		}

		return queryIndex;
	}

	template<class ...TComponents>
	std::size_t PrepareArchetype()
	{
		std::lock_guard<std::shared_mutex> glock(m_globalLock);
		std::vector<std::size_t> indexList = BuildIndexList<TComponents...>();

		std::size_t archIndex = 0;
		for (auto& arch : m_archetypes)
		{
			if (arch.ComponentIndices == indexList)
				return archIndex;
			++archIndex;
		}

		auto& arch = m_archetypes.emplace_back();
		arch.ArchetypeIndex = archIndex;
		arch.ComponentIndices = indexList;

		for (auto [comps, archs] : m_queriesToCompsArchs)
		{
			if (std::includes(indexList.begin(), indexList.end(), comps.begin(), comps.end()))
				archs.push_back(archIndex);
		}

		return archIndex;
	}

	template<class ...TComponents>
	QueryBlocks<TComponents...> GetQueryBlocks(std::size_t queryIndex)
	{
		m_globalLock.lock_shared();
		auto& [_, archetypes] = m_queriesToCompsArchs[queryIndex];
		auto allocators = std::make_tuple((m_storage.template GetPages<TComponents>(m_archetypes, archetypes))...);

		m_globalLock.unlock_shared();

		return allocators;
	}

	template<class ...TComponents>
	std::optional<std::tuple<std::shared_mutex&, std::pair<std::shared_mutex&, TComponents&>...>> GetQueryBlockByIdWithLock(ObjId id)
	{
		m_globalLock.lock_shared();
		auto [archId, allocId] = m_objToArchetypeAllocation[id];
		Archetype& arch = m_archetypes[archId];

		auto& lock = arch.Indexer.GetLock(allocId);
		lock.lock_shared();
		if (arch.Indexer.IsDeletedUnsync(allocId)) {
			lock.unlock_shared();
			return std::nullopt;
		}

		auto tup = std::make_tuple(lock, m_storage.GetByAllocId<TComponents>(arch, allocId)...);
		m_globalLock.unlock_shared();

		return tup;
	}

	template<class ...TComponents>
	std::vector<ObjId> CreateObjects(std::size_t archId, std::size_t count)
	{
		std::vector<ObjId> objIds(count);
		auto firstId = m_objIdCounter.fetch_add(count);

		m_globalLock.lock_shared();

		auto& arch = m_archetypes[archId];
		auto allocOp = arch.Indexer.Allocate(count);
		m_storage.template Create<TComponents...>(arch, allocOp);

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

	void Delete(ObjId id)
	{
		m_globalLock.lock_shared();
		auto [archId, alloc] = m_objToArchetypeAllocation[id];
		auto& arch = m_archetypes[archId];
		auto& lock = arch.Indexer.GetLock(alloc);

		std::lock_guard<std::shared_mutex> guard(lock);
		arch.Indexer.Delete(alloc);
		m_globalLock.unlock_shared();
	}

	void Cleanup()
	{
		std::lock_guard<std::shared_mutex> glock(m_globalLock);
		for (Archetype& arch : m_archetypes)
		{
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

	std::vector<Archetype> m_archetypes;
	std::vector<std::pair<std::vector<std::size_t>, std::vector<std::size_t>>> m_queriesToCompsArchs;

	concurrency::concurrent_unordered_map<std::size_t, ObjId> m_allocationToObj;
	concurrency::concurrent_unordered_map<ObjId, std::pair<std::size_t, std::size_t>> m_objToArchetypeAllocation;

	std::atomic_size_t m_objIdCounter = 0;
};