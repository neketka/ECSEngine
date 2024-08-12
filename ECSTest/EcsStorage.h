#pragma once

#include "ParallelPooledStore.h"

#include <type_traits>
#include <range/v3/view/concat.hpp>
#include <tuple>

using ObjectId = std::size_t;

template<
	typename TLevelTraverseRelation, typename TExcludedArch, typename TContainsOrExprs,
	typename TRelationArchPath, typename TUsedComponentsArch, typename... TReadsWrites
>
class QueryImpl;

template<typename TExcludedArch, typename TContainsOrExprs, typename TUsedComponentsArch, typename... TStores>
auto FilterStores(std::tuple<TStores...>& stores)
{
	auto filterFunc = []<typename TStore>(TStore& store)
	{
		if constexpr (
			TExcludedArch::template AnyIn<TStore::ArchType> ||
			!(TUsedComponentsArch::template IsSubsetOf<TStore::ArchType>) ||
			(TContainsOrExprs::template MeetsAnyCriterion<TStore::ArchType> && !std::same_as<TContainsOrExprs, EmptyArchetype>)
		)
			return std::make_tuple();
		else
			return std::forward_as_tuple(store);
	};

	return std::apply(
		[&filterFunc](TStores&... stores)
		{
			return std::tuple_cat(filterFunc(stores)...);
		}, stores
	);
}

// Simple sequential
template<
	typename TExcludedArch, typename TContainsOrExprs,
	typename TUsedComponentsArch, typename... TReadsWrites
>
class QueryImpl<std::monostate, TExcludedArch, TContainsOrExprs, EmptyArchetype, TUsedComponentsArch, TReadsWrites...>
{
public:
	template<typename... TStores>
	static auto GetView(std::tuple<TStores...>& stores)
	{
		auto getView =
			[]<typename TStore>(TStore& store)
		{
			return store.template GetView<TReadsWrites...>();
		};
		
		auto filtered = FilterStores<TExcludedArch, TContainsOrExprs, TUsedComponentsArch, TStores...>(stores);

		static_assert(std::tuple_size_v<decltype(filtered)> > 0, "Query must match at least one component!");

		return std::apply([&getView]<typename... TFilteredStores>(TFilteredStores&... filteredStores)
		{
			return ranges::concat_view(getView(filteredStores)...);
		}, filtered);
	}

	template<typename... TStores>
	static auto GetView(std::tuple<TStores...>& stores, std::size_t id)
	{
		auto getViewAt =
			[&id]<typename TStore>(TStore& store)
			{
				return store.template GetViewAt<TReadsWrites...>(id);
			};

		auto filtered = FilterStores<TExcludedArch, TContainsOrExprs, TUsedComponentsArch, TStores...>(stores);

		static_assert(std::tuple_size_v<decltype(filtered)> > 0, "Query must accept at least one component!");

		return std::apply([&getViewAt]<typename... TFilteredStores>(TFilteredStores&... filteredStores)
		{
			return ranges::concat_view(getViewAt(id)...);
		}, filtered);
	}
};

// Relational
template<
	typename TExcludedArch, typename TContainsOrExprs, typename TRelationArchPath,
	typename TUsedComponentsArch, typename... TReadsWrites
>
class QueryImpl<std::monostate, TExcludedArch, TContainsOrExprs, TRelationArchPath, TUsedComponentsArch, TReadsWrites...>
{
public:
	// TODO: implement
};

// BFS Relation Tree
template<
	typename TLevelTraverseRelation, typename TExcludedArch, typename TContainsOrExprs,
	typename TUsedComponentsArch, typename... TReadsWrites
>
class QueryImpl<TLevelTraverseRelation, TExcludedArch, TContainsOrExprs, EmptyArchetype, TUsedComponentsArch, TReadsWrites...>
{
public:
	// TODO: implement
};

template<
	typename TLevelTraverseRelation, typename TExcludedArch, typename TContainsOrExprs, 
	typename TRelationArchPath, typename TUsedComponentsArch, typename... TReadsWrites
>
class QueryBase : 
	public QueryImpl<TLevelTraverseRelation, TExcludedArch, TContainsOrExprs, TRelationArchPath, TUsedComponentsArch, TReadsWrites...>
{
public:
	template<typename... TComponents> requires !(TUsedComponentsArch::template AnyIn<Archetype<TComponents...>>)
	using Read = 
		QueryBase<
			TLevelTraverseRelation, TExcludedArch, TContainsOrExprs, TRelationArchPath,
			typename TUsedComponentsArch::template Append<TComponents...>, TReadsWrites..., const TComponents...
		>;

	template<typename... TComponents> requires !TUsedComponentsArch::template AnyIn<Archetype<TComponents...>>
	using Write =
		QueryBase<
			TLevelTraverseRelation, TExcludedArch, TContainsOrExprs, TRelationArchPath,
			typename TUsedComponentsArch::template Append<TComponents...>, TReadsWrites..., TComponents...
		>;

	template<typename... TComponents>
	using ContainingAll =
		QueryBase<
			TLevelTraverseRelation, TExcludedArch, typename TContainsOrExprs::template Append<Archetype<TComponents...>>,
			TRelationArchPath, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename... TComponents>
	using ContainingAny =
		QueryBase<
		TLevelTraverseRelation, TExcludedArch, typename TContainsOrExprs::template Append<Archetype<TComponents>...>,
		TRelationArchPath, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename... TComponents>
	using Exclude =
		QueryBase<
			TLevelTraverseRelation, typename TExcludedArch::template Append<TComponents...>, TContainsOrExprs,
			TRelationArchPath, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename TTreeRelationType> requires std::same_as<TRelationArchPath, EmptyArchetype>
	using LevelTraverse = 
		QueryBase<
			TTreeRelationType, TExcludedArch, TContainsOrExprs,
			TRelationArchPath, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename ...TRelationTypes> requires std::same_as<TLevelTraverseRelation, std::monostate>
	using Join =
		QueryBase<
			std::monostate, TExcludedArch, TContainsOrExprs,
			typename TRelationArchPath::template Append<Archetype<TRelationTypes...>>, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename ...TRelationTypes> requires std::same_as<TLevelTraverseRelation, std::monostate>
	using JoinRecursive = 
		QueryBase<
			std::monostate, TExcludedArch, TContainsOrExprs,
			typename TRelationArchPath::template Append<const Archetype<TRelationTypes...>>, TUsedComponentsArch, TReadsWrites...
		>;
};

using Query = QueryBase<std::monostate, EmptyArchetype, EmptyArchetype, EmptyArchetype, EmptyArchetype>;

template<typename... TArchetypes>
class EcsStorage
{
public:
	EcsStorage()
	{
		// Set prefixes on all
		std::apply(
			[]<typename... TStores>(TStores&... store)
			{
				std::size_t i = 1ull << 63;
				((store.SetIdPrefix(i++)), ...);
			}, m_stores
		);
	}

	template<typename TQuery>
	auto RunQuery()
	{
		return TQuery::GetView(m_stores);
	}

	template<typename TQuery>
	auto RunQuery(std::size_t rootId)
	{
		return TQuery::GetView(m_stores, rootId);
	}

	template<typename TArchetype>
	auto Create(std::size_t count)
	{
		return std::get<typename TArchetype::StoreType>(m_stores).Emplace(count);
	}

	template<typename TArchetype>
	void Delete(std::size_t objId)
	{
		return std::get<typename TArchetype::StoreType>(m_stores).Delete(objId);
	}

	template<typename TArchetypeSrc, typename TArchetypeDest>
	auto Transfer(std::size_t objId)
	{
		// TODO: Implement
	}

	template<typename TArchetypeSrc, typename TArchetypeDest>
	auto Copy(std::size_t objId)
	{
		// TODO: Implement
	}

	std::size_t FindComponentIdDynamic(std::string_view componentName)
	{
		// TODO: Implement
		return 0;
	}

	template<std::ranges::input_range TRange>
	std::size_t FindArchetypeIdDynamic(TRange componentIds) requires std::same_as<std::ranges::range_value_t<TRange>, std::size_t>
	{
		// TODO: Implement
		return 0;
	}

	std::size_t CreateDynamic(std::size_t archetypeId)
	{
		// TODO: Implement
		return 0;
	}

	void DeleteDynamic(std::size_t objId)
	{
		// TODO: Implement
	}

	void AddComponentDynamic(std::size_t objId, std::size_t componentId)
	{
		// TODO: Implement
	}

	void RemoveComponentDynamic(std::size_t objId, std::size_t componentId)
	{
		// TODO: Implement
	}
private:
	std::tuple<typename TArchetypes::StoreType...> m_stores; // TODO: implement component order agnostic archetypes
};