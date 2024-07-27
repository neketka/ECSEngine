#pragma once

#include "ParallelPooledStore.h"

#include <type_traits>
#include <range/v3/view/concat.hpp>

template<
	typename TLevelTraverseRelation, typename TExcludedArch, typename TContainsOrExprs,
	typename TRelationArchPath, typename TUsedComponentsArch, typename... TReadsWrites
>
class QueryImpl;

template<typename TExcludedArch, typename TContainsOrExprs, typename TUsedComponentsArch, typename... TStores>
auto FilterStores(std::tuple<TStores&...>& stores)
{
	auto filterFunc = []<typename TStore>(TStore& store)
	{
		if constexpr (
			TExcludedArch::template AnyIn<TStore::ArchType> ||
			!(TUsedComponentsArch::template IsSubsetOf<TStore::ArchType>) ||
			TContainsOrExprs::template MeetsAnyCriterion<TStore::ArchType>
		)
			return std::make_tuple();
		else
			return store;
	};

	return std::apply(
		[](TStores&... stores)
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
	static auto GetView(std::tuple<TStores&...>& stores)
	{
		auto filtered = FilterStores<TExcludedArch, TContainsOrExprs, TUsedComponentsArch, TStores...>(stores);

		return std::apply([]<template... TFilteredStores>(TFilteredStores&... filteredStores)
		{
			return ranges::concat_view(filteredStores.template GetView<TReadsWrites...>()...);
		}, filtered);
	}

	template<typename... TStores>
	static auto GetView(std::tuple<TStores&...>& stores, std::size_t id)
	{
		auto filtered = FilterStores<TExcludedArch, TContainsOrExprs, TUsedComponentsArch, TStores...>(stores);

		return std::apply([]<template... TFilteredStores>(TFilteredStores&... filteredStores)
		{
			return ranges::concat_view(filteredStores.template GetViewAt<TReadsWrites...>(id)...);
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
	
};

// BFS Relation Tree
template<
	typename TLevelTraverseRelation, typename TExcludedArch, typename TContainsOrExprs,
	typename TUsedComponentsArch, typename... TReadsWrites
>
class QueryImpl<TLevelTraverseRelation, TExcludedArch, TContainsOrExprs, EmptyArchetype, TUsedComponentsArch, TReadsWrites...>
{
public:

};

template<
	typename TLevelTraverseRelation, typename TExcludedArch, typename TContainsOrExprs, 
	typename TRelationArchPath, typename TUsedComponentsArch, typename... TReadsWrites
>
class QueryBase
{
public:
	template<typename... TComponents> requires !TUsedComponentsArch::template AnyIn<Archetype<TComponents...>>
	using Read = 
		QueryBase<
			TLevelTraverseRelation, TExcludedArch, TContainsOrExprs, TRelationArchPath,
			TUsedComponentsArch::template Append<TComponents...>, TReadsWrites..., const TComponents...
		>;

	template<typename... TComponents> requires !TUsedComponentsArch::template AnyIn<Archetype<TComponents...>>
	using Write =
		QueryBase<
			TLevelTraverseRelation, TExcludedArch, TContainsOrExprs, TRelationArchPath,
			TUsedComponentsArch::template Append<TComponents...>, TReadsWrites..., TComponents...
		>;

	template<typename... TComponents>
	using ContainingAll =
		QueryBase<
			TLevelTraverseRelation, TExcludedArch, TContainsOrExprs::template Append<Archetype<TComponents...>>,
			TRelationArchPath, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename... TComponents>
	using ContainingAny =
		QueryBase<
		TLevelTraverseRelation, TExcludedArch, TContainsOrExprs::template Append<Archetype<TComponents>...>,
		TRelationArchPath, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename... TComponents>
	using Exclude =
		QueryBase<
			TLevelTraverseRelation, TExcludedArch::template Append<TComponents...>, TContainsOrExprs,
			TRelationArchPath, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename TTreeRelationType> requires std::same_as<TRelationArchPath, EmptyArchetype>
	using LevelTraverse = 
		QueryBase<
			TTreeRelationType, TExcludedArch, TContainsOrExprs,
			TRelationArchPath, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename TRelationType> requires std::same_as<TLevelTraverseRelation, std::monostate>
	using FollowRelation =
		QueryBase<
			std::monostate, TExcludedArch, TContainsOrExprs,
			TRelationArchPath::template Append<TRelationType>, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename TRelationType> requires std::same_as<TLevelTraverseRelation, std::monostate>
	using FollowInverseRelation = 
		QueryBase<
			std::monostate, TExcludedArch, TContainsOrExprs,
			TRelationArchPath::template Append<const TRelationType>, TUsedComponentsArch, TReadsWrites...
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
	auto Create()
	{
		return std::get<TArchetype::StoreType>(m_stores).Emplace();
	}

	std::size_t CreateDynamic(std::size_t archetypeId)
	{
	}

	std::size_t FindComponentIdDynamic(std::string_view componentName)
	{

	}

	std::size_t FindArchetypeDynamic(std::vector<size_t> componentIds)
	{

	}

	void DeleteDynamic(std::size_t objId)
	{

	}
private:
	std::tuple<TArchetypes::StoreType...> m_stores;
};