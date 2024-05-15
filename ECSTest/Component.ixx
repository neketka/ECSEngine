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

template<std::movable TComponent, std::movable ...TRest>
class ComponentStorage
{
public:
	template<class... TComponents>
	std::vector<size_t> Create(const Archetype& arch, const PageAllocationOp& op)
	{
		if constexpr ((std::same_as<TComponent, TComponents> || ...))
		{
			auto allocator = m_archAllocators[arch.ArchetypeIndex];
			allocator.Allocate(op);
			
			m_storageRest.Create<TComponents...>(arch, count, lockIndex + 1);
			return allocIds;
		}
		else 
			return m_storageRest.Create<TComponents...>(arch, op);
	}

	template<class T>
	std::pair<std::shared_mutex&, T&> GetByAllocId(const Archetype& arch, size_t allocId)
	{
		if constexpr (std::same_as<TComponent, T>)
			return m_archAllocators[archetypeId].Get(allocId);
		else
			return m_storageRest.GetByAllocId<T>(arch, allocId);
	}

	template<class T>
	std::vector<PageAllocatorElements<T>> GetPages(const std::vector<Archetype>& archetypes, const std::vector<size_t>& indices)
	{
		if constexpr (std::same_as<T, TComponent>) 
		{
			std::vector<PageAllocatorElements<T>> allocs;
			allocs.reserve(archetypes.size());

			for (size_t& archIndex : archetypes) 
			{
				auto& arch = archetypes[archIndex];
				allocs.push_back(arch.Indexer.GetAllocatorElements(m_archAllocators[archIndex]));
			}

			return allocs;
		}
		else
			return m_storageRest.GetAllocators<T>(archetypes, indices);
	}

	void CleanupUnsync(const Archetype& arch, const PageDeletionOp& op)
	{
		if (m_archAllocators.contains(arch.ArchetypeIndex))
			m_archAllocators[arch.ArchetypeIndex].CleanupDeletedUnsync(op);

		m_storageRest.CleanupUnsync(arch, op);
	}

	template<class T>
	static std::size_t GetIndex(std::size_t initialIndex=0)
	{
		if constexpr (std::same_as<TComponent, T>)
			return initialIndex;
		else 
			decltype(m_storageRest)::template GetIndex<T>(initialIndex + 1);
	}
private:
	ComponentStorage<TRest...> m_storageRest;
	std::unordered_map<std::size_t, PageAllocator<TComponent>> m_archAllocators;
};

export
template<std::movable ...TComponents>
class ComponentSet
{
public:
	template<class ...TComponents>
	using QueryBlocks = std::tuple<std::vector<PageAllocatorElements<TComponents>>...>;

	template<class ...TComponents>
	void PrepareQuery()
	{
	}

	template<class ...TComponents>
	void PrepareArchetype()
	{
	}

	template<class ...TComponents>
	QueryBlocks<TComponents...> GetQueryBlocks()
	{
		auto& [_, archetypes] = m_queriesToCompsArchs[queryIndex];
		auto allocators = std::make_tuple((m_storage.template GetPages<TComponents>(m_archetypes, archetypes))...);

		return { allocators };
	}

	template<class ...TComponents>
	std::optional<std::tuple<std::shared_mutex&, std::pair<std::shared_mutex&, TComponents&>...>> GetQueryBlockById(ObjId id)
	{
		auto [archId, allocId] = m_objToArchetypeAllocationPair[id];
		Archetype& arch = m_archetypes[archId];

		auto& lock = arch.Indexer.GetLock(allocId);
		lock.lock_shared();
		if (arch.Indexer.IsDeletedUnsync(allocId)) {
			lock.unlock_shared();
			return std::nullopt;
		}

		return std::make_tuple(lock, m_storage.GetByAllocId<TComponents>(arch, allocId)...);
	}

	template<class ...TComponents>
	std::vector<ObjId> CreateObjects(std::size_t count)
	{
		
	}

	void Delete(ObjId id)
	{
		auto [archId, alloc] = m_objToArchetypeAllocation[id];
		auto& arch = m_archetypes[archId];
		auto& lock = arch.Indexer.GetLock(alloc);

		std::lock_guard<std::shared_mutex> guard(lock);
		arch.Indexer.Delete(alloc);
	}

	void CleanupUnsync()
	{
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
	ComponentStorage<ObjId, TComponents...> m_storage;

	std::vector<Archetype> m_archetypes;
	std::vector<std::pair<std::vector<std::size_t>, std::vector<std::size_t>> m_queriesToCompsArchs;

	concurrency::concurrent_unordered_map<AllocationId, ObjId> m_allocationToObj;
	concurrency::concurrent_unordered_map<ObjId, std::pair<std::size_t, std::size_t>> m_objToArchetypeAllocation;
};