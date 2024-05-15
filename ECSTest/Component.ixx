/*module;

#include <mutex>
#include <algorithm>
#include <unordered_map>
#include <array>
#include <vector>
#include <shared_mutex>

export module Component;

import Allocator;

using ObjId = std::size_t;

class Archetype
{
public:
	std::size_t ArchetypeIndex;
	std::vector<std::mutex> CreateLocks;
	std::vector<std::size_t> ComponentTypeHashes;
};

template<std::movable TComponent, std::movable ...TRest>
class ComponentStorage
{
public:
	template<class... TComponents>
	std::vector<size_t> Create(Archetype& arch, std::size_t count, int lockIndex=0)
	{
		if constexpr ((std::same_as<TComponent, TComponents> || ...))
		{
			auto allocator = m_archAllocators[archetypeId];
			auto [allocIds, _] = allocator.Allocate(count);
			arch[lockIndex].unlock();
			
			m_storageRest.Create<TComponents...>(arch, count, lockIndex + 1);
			return allocIds;
		}
		else 
		{
			return m_storageRest.Create<TComponents...>(arch, count, lockIndex);
		}	
	}

	template<class T>
	std::pair<std::shared_mutex&, T&> GetByAllocId(size_t allocId, Archetype& arch)
	{
		if constexpr (std::same_as<TComponent, T>)
		{
			return m_archAllocators[archetypeId].Get(allocId);
		}
		else
		{
			return m_storageRest.GetByAllocId<T>(allocId, arch);
		}
	}

	size_t DeleteAndReplace(size_t allocId, Archetype& arch)
	{
		if (m_archAllocators.contains(arch.ArchetypeIndex))
		{
			auto archetype = m_archAllocators[arch.ArchetypeIndex];
			size_t movedAlloc = archetype.DeleteAndReplace(allocId);
			m_storageRest.DeleteAndReplace(allocId, arch);
			return movedAlloc;
		}

		return m_storageRest.DeleteAndReplace(allocId, arch);
	}

	template<class T>
	std::vector<std::reference_wrapper<PageAllocator<T>>> GetAllocators(std::vector<std::size_t>& archetypes)
	{
		if constexpr (std::same_as<T, TComponent>) 
		{
			std::vector<std::reference_wrapper<PageAllocator<T>>> allocs;

			for (std::size_t arch : archetypes)
				allocs.push_back(m_archAllocators[arch]);

			return allocs;
		}
		else
		{
			return m_storageRest.GetAllocators<T>(archetypes);
		}
	}
private:
	ComponentStorage<TRest...> m_storageRest;
	std::unordered_map<std::size_t, PageAllocator<TComponent>> m_archAllocators;
};

template<class... T>
std::vector<std::size_t> GetHashCodes()
{
	std::vector<std::size_t> hashCodes = { typeid(T).hash_code()... };
	std::sort(hashCodes.begin(), hashCodes.end());

	return hashCodes;
}

export
template<std::movable ...TComponents>
class ComponentSet
{
public:
	template<class ...TComponents>
	using QueryBlocks = std::tuple<std::vector<std::reference_wrapper<PageAllocator<TComponents>>>...>;

	template<class ...TComponents>
	std::size_t GetQueryIndex()
	{
		const auto index = typeid(std::tuple<TComponents>).hash_code();

		if (!m_queryToArchetypes.contains(index)) 
		{
			m_objDataLock.lock();
			auto matchingComps = GetHashCodes<TComponents>();
			auto usedArchetypes = m_queryToArchetypes[index];
			for (auto& [comps, pair] : m_componentsToArchetypeQueriesPair)
			{
				if (std::includes(comps.begin(), comps.end(), matchingComps.begin(), matchingComps.end()))
				{
					pair.second.push_back(index);
					usedArchetypes.push_back(pair.first);
				}
			}
			m_objDataLock.unlock();
		} 

		return index;
	}

	template<class ...TComponents>
	QueryBlocks<TComponents...> GetQueryBlocks()
	{
		std::size_t queryIndex = GetQueryIndex<TComponents>();
		m_objDataLock.lock_shared();

		auto& archetypes = m_queryToArchetypes[queryIndex];
		auto allocators = std::make_tuple((m_storage.template GetAllocators<TComponents>(archetypes))...);

		m_objDataLock.unlock_shared();
		return { allocators };
	}

	bool IsObjectAvailable(ObjId id)
	{
		m_objDataLock.lock_shared();
		bool val = m_objToArchetypeAllocationPair.contains(id);
		m_objDataLock.unlock_shared();

		return val;
	}

	template<class ...TComponents>
	std::tuple<std::pair<std::shared_mutex&, TComponents&>...> GetQueryBlockById(ObjId id)
	{
		m_objDataLock.lock_shared();
		auto [archId, allocId] = m_objToArchetypeAllocationPair[id];
		m_objDataLock.unlock_shared();

		return std::make_tuple(m_storage.GetByAllocId<TComponents>(allocId, archId)...);
	}

	template<class ...TComponents>
	ObjId CreateObject()
	{

	}

	void Delete(ObjId id)
	{
		std::lock_guard<std::mutex> guard(m_deletionLock);
		m_deletionList.push_back(id);
	}

	void CleanupDeleted()
	{
		std::lock_guard<std::mutex> guard0(m_deletionLock);
		std::lock_guard<std::shared_mutex> guard1(m_objDataLock);

		for (auto id : m_deletionList) 
		{
			auto [arch, allocId] = m_objToArchetypeAllocationPair[id];
			std::size_t movedAllocId = m_storage.DeleteAndReplace(allocId, arch);

			if (allocId != movedAllocId) {
				auto movedObj = m_allocationToObj[movedAllocId];

				m_allocationToObj[allocId] = movedObj;
				m_objToArchetypeAllocationPair[movedObj] = { arch, allocId };
			}

			m_objToArchetypeAllocationPair.erase(id);
		}

		m_deletionList.clear();
	}
private:
	std::mutex m_deletionLock;
	std::vector<ObjId> m_deletionList;

	ComponentStorage<ObjId, TComponents...> m_storage;

	std::shared_mutex m_objDataLock;
	std::vector<Archetype> m_archetypes;
	std::unordered_map<std::vector<std::size_t>, std::pair<std::size_t, std::vector<std::size_t>>> m_componentsToArchetypeQueriesPair;
	std::unordered_map<std::size_t, std::vector<std::size_t>> m_queryToArchetypes;
	std::unordered_map<ObjId, std::pair<std::size_t, AllocationId>> m_objToArchetypeAllocationPair;
	std::unordered_map<AllocationId, ObjId> m_allocationToObj;
};*/