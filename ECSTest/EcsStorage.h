#pragma once

#include "ParallelPooledStore.h"

#include <type_traits>
#include <range/v3/view/concat.hpp>

template<typename... TComponents>
class Archetype
{
private:
	template<typename TTarget, typename TComp>
	static inline constexpr bool ContainsInternal = std::is_same_v<TTarget, TComp>;

	template<typename TTarget, typename TComp, typename... TComps>
	static inline constexpr bool ContainsInternal = ContainsInternal<TTarget, TComp> || ContainsInternal<TTarget, TComps...>;

	template<typename TArchOther, typename TComp>
	using UnionParts = std::conditional<TArchOther::template Contains<TComp>, TArchOther, TArchOther::template AppendNoUnion<TComp>>;

	template<typename TArchOther, typename TComp, typename... TComps>
	using UnionParts = UnionParts<UnionParts<TArchOther, TComp>, TComps...>;
public:
	using Tuple = std::tuple<TComponents...>;

	template<typename... TComps>
	using AppendNoUnion = Archetype<TComponents..., TComps...>;

	template<typename... TAddComponents>
	using Append = UnionParts<Archetype<TComponents...>, TAddComponents...>;

	template<typename TArchOther>
	using Union = TArchOther::template Append<TComponents...>;

	using StoreType = ParallelPooledStore<TComponents...>;

	template<typename TComp>
	static inline constexpr bool Contains = ContainsInternal<TComp, TComponents...>;

	template<typename TArchSuperset>
	static inline constexpr bool IsSubsetOf = TArchSuperset::template Contains<TComponents> && ...;

	template<typename TArchSuperset>
	static inline constexpr bool AnyIn = TArchSuperset::template Contains<TComponents> || ...;
};

using EmptyArchetype = Archetype<>;

template<
	typename TLevelTraverseRelation, typename TExcludedArch, typename TContainsOrExprs,
	typename TRelationArchPath, typename TUsedComponentsArch, typename... TReadsWrites
>
class QueryImpl;

template<typename TExcludedArch, typename TStore>
auto FilterExcludedOne(TStore& store)
{
	if constexpr (std::tuple_size_v<std::common_type<TExcludedArch::Tuple, TStore::Tuple>::type> > 0)
	{
		return std::make_tuple();
	}
	else
	{
		return store;
	}
}

template<typename TExcludedArch, typename... TStores>
auto FilterExcluded(std::tuple<TStores&...> stores)
{
	return std::apply(
		[](TStores&... stores)
		{
			return std::tuple_cat(FilterExcludedOne(stores)...);
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
	static auto GetView(std::tuple<TStores&...> stores)
	{

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
	template<typename TQuery>
	auto Query()
	{

	}
private:
	std::tuple<TArchetypes::StoreType...> m_stores;
};