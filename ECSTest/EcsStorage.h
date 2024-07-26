#pragma once

#include "ParallelPooledStore.h"

#include <type_traits>

template<typename... TComponents>
class Archetype
{
private:
	template<typename TTarget, typename TComp>
	static inline constexpr bool ContainsInternal = std::is_same_v<TTarget, TComp>;

	template<typename TTarget, typename TComp, typename... TComps>
	static inline constexpr bool ContainsInternal = ContainsInternal<TTarget, TComp> || ContainsInternal<TTarget, TComps...>;

	template<typename TArchOther, typename TComp>
	using UnionParts = std::conditional<TArchOther::Contains<TComp>, TArchOther, TArchOther::AppendNoUnion<TComp>>;

	template<typename TArchOther, typename TComp, typename... TComps>
	using UnionParts = UnionParts<UnionParts<TArchOther, TComp>, TComps...>;
public:
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
	static inline constexpr bool IsSubsetOf = TArchSuperset::Contains<TComponents> && ...;

	template<typename TArchSuperset>
	static inline constexpr bool AnyIn = TArchSuperset::Contains<TComponents> || ...;
};

using EmptyArchetype = Archetype<>;

template<
	typename TLevelTraverseRelation, typename TExcludedArch, typename TContainsOrExprs, 
	typename TRelationArchPath, typename TUsedComponentsArch, typename... TReadsWrites
>
class QueryBase
{
public:
	template<typename... TComponents> requires !TUsedComponentsArch::AnyIn<Archetype<TComponents...>>
	using Read = 
		QueryBase<
			TLevelTraverseRelation, TExcludedArch, TContainsOrExprs, TRelationArchPath,
			TUsedComponentsArch::Append<TComponents...>, TReadsWrites..., const TComponents...
		>;

	template<typename... TComponents> requires !TUsedComponentsArch::AnyIn<Archetype<TComponents...>>
	using Write =
		QueryBase<
			TLevelTraverseRelation, TExcludedArch, TContainsOrExprs, TRelationArchPath,
			TUsedComponentsArch::Append<TComponents...>, TReadsWrites..., TComponents...
		>;

	template<typename... TComponents>
	using ContainingAll =
		QueryBase<
			TLevelTraverseRelation, TExcludedArch, TContainsOrExprs::Append<Archetype<TComponents...>>,
			TRelationArchPath, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename... TComponents>
	using Exclude =
		QueryBase<
			TLevelTraverseRelation, TExcludedArch::Append<TComponents...>, TContainsOrExprs,
			TRelationArchPath, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename TTreeRelationType> requires std::same_as<TRelationArchPath, EmptyArchetype>
	using LevelTraverse = 
		QueryBase<
			TTreeRelationType, TExcludedArch, TContainsOrExprs,
			TRelationArchPath, TUsedComponentsArch, TReadsWrites...
		>;

	template<typename ...TRelationTypes> requires std::same_as<TLevelTraverseRelation, std::monostate>
	using FollowRelations = 
		QueryBase<
			std::monostate, TExcludedArch, TContainsOrExprs,
			TRelationArchPath, TUsedComponentsArch, TReadsWrites...
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