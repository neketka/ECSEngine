module;

#include <tuple>
#include <vector>
#include <string>
#include <typeinfo>
#include <optional>
#include <array>
#include <shared_mutex>
#include <map>
#include <stack>

export module GameSystem;
import Allocator;
import Component;

class EmptySystemSet
{
public:
	static const inline std::initializer_list<const char *> Systems = { };
};

export
template<class... TSystems>
class SystemSet
{
public:
	static const inline std::initializer_list<const char *> Systems = { typeid(TSystems).name()... };
};
export
class ParallelTask
{
public:
	virtual std::size_t GetGroupCount(std::size_t maxThreads) = 0;
	virtual void ExecuteTask(std::size_t groupIndex, std::size_t groupCount) = 0;
};

export
class QueryContext
{
public:
	QueryContext(std::size_t groupIndex, std::size_t groupCount) : m_groupIndex(groupIndex), m_groupCount(groupCount)
	{
	}

	void AcquirePageLock(std::size_t pageIndex, std::shared_mutex& lock, bool shared)
	{

	}

	void AcquireBlockLock(std::size_t pageIndex, std::size_t blockIndex, std::shared_mutex& lock, bool shared)
	{

	}

	void AcquireComponentLock(
		std::size_t pageIndex, std::size_t blockIndex, std::size_t compIndex,
		std::shared_mutex& lock, bool shared)
	{

	}

	void ReleasePageLock(std::size_t index)
	{

	}

	void ReleaseBlockLock(std::size_t pageIndex, std::size_t blockIndex)
	{

	}

	void ReleaseComponentLock(std::size_t pageIndex, std::size_t blockIndex, std::size_t compIndex)
	{

	}

	std::pair<std::size_t, std::size_t> GetPageOffsetCountForInvocation(std::size_t pageCount)
	{
		if (pageCount <= m_groupCount)
		{
			if (m_groupIndex >= pageCount)
				return { 0, 0 };
			else
				return { m_groupIndex, 1 };
		}
		else if (m_groupIndex < m_groupCount - 1)
		{
			const auto offset = m_groupIndex * pageCount / m_groupCount;
			return { offset, pageCount / m_groupCount };
		}
		else
		{
			const auto offset = m_groupIndex * pageCount / m_groupCount;
			return { offset, pageCount - offset };
		}
	}
private:
	struct HeldLock
	{
		std::shared_mutex& Lock;
		bool NeedsAcquire;
		bool Shared;
		std::size_t Counter;
	};

	struct BlockLock
	{
		std::optional<HeldLock> Lock;
		std::map<std::size_t, HeldLock> ComponentLocks;
	};

	struct PageLock
	{
		std::optional<HeldLock> Lock;
		std::map<std::size_t, BlockLock> BlockLocks;
	};

	std::size_t m_groupIndex;
	std::size_t m_groupCount;
	std::map<std::size_t, PageLock> m_pageLocks;
};

export
template<class T>
concept SystemLike = requires(T& sys, QueryContext& ctxt) {
	sys.Execute(ctxt);
};

export
template<SystemLike TSelf, bool Parallel, class TComponentSet, class TPreSystemSet, class TPostSystemSet>
class GameSystem : public ParallelTask
{
public:
	virtual std::size_t GetGroupCount(std::size_t maxThreads) override
	{
		return Parallel ? maxThreads : 1;
	}

	virtual void ExecuteTask(std::size_t groupIndex, std::size_t groupCount) override
	{
		QueryContext context(groupIndex, groupCount);
		TSelf::Execute(context);
	}

	std::vector<std::string>& GetPreSystems()
	{
		return TPreSystemSet::Systems;
	}

	std::vector<std::string>& GetPostSystems()
	{
		return TPostSystemSet::Systems;
	}
protected:
	template<class ...TComponents>
	class ComponentQuery
	{
	public:
		template<bool ReadOnly>
		class QueryMany
		{
		public:
			QueryMany(QueryContext& context, TComponentSet& compSet)
			{
				
			}

			void Next()
			{
			}

			std::optional<std::tuple<TComponents&...>>& GetData()
			{
			}
		};

		template<bool ReadOnly>
		class QueryOne
		{
		public:
			QueryOne(QueryContext& context, TComponentSet& compSet, ObjId id)
			{
				auto query = compSet.template GetQueryBlockByIdWithLock<TComponents...>(id);

				if (!query) return;

				m_lock = query.first;
				m_elements = query.second;

				if constexpr (ReadOnly)
					m_lock.value().lock_shared();
				else
					m_lock.value().unlock_shared();
			}

			~QueryOne()
			{
				if constexpr (ReadOnly)
				{
					if (m_lock)
						m_lock.value().unlock_shared();
				}
				else
				{
					if (m_lock)
						m_lock.value().unlock();
				}
			}

			std::optional<std::tuple<TComponents&...>>& GetData()
			{
				return m_elements;
			}
		private:
			std::optional<std::reference_wrapper<std::shared_mutex>> m_lock;
			std::optional<std::tuple<TComponents&...>> m_elements;
		};

		ComponentQuery(TComponentSet& compSet) : m_compSet(compSet)
		{
			m_queryId = m_compSet.template PrepareQuery<TComponents...>();
		}

		QueryMany<true> Read(QueryContext& context, bool sequential)
		{
		}

		QueryMany<false> Write(QueryContext& context, bool sequential)
		{
		}

		QueryOne<true> ReadOne(QueryContext& context, ObjId id)
		{
			return QueryOne<true>(context, m_compSet, id);
		}

		QueryOne<false> WriteOne(QueryContext& context, ObjId id)
		{
			return QueryOne<false>(context, m_compSet, id);
		}
	private:
		std::size_t m_queryId;
		TComponentSet& m_compSet;
	};

	template<class ...TComponents>
	class Archetype
	{
	public:
		Archetype(TComponentSet& compSet) : m_compSet(compSet)
		{
			m_archId = m_compSet.template PrepareArchetype<TComponents...>();
		}

		std::vector<ObjId> Create(size_t count)
		{
			m_compSet.template CreateObjects<TComponents...>(m_archId, count);
		}
	private:
		std::size_t m_archId;
		TComponentSet& m_compSet;
	};

	void Delete(ObjId id)
	{
		m_compSet.Delete(id);
	}
private:
	TComponentSet& m_compSet;
};

