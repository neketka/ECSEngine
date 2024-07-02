#pragma once

#include <cstddef>
#include <concepts>
#include <vector>

#include "ComponentSet.h"

template<typename TItemTarget, typename TItem, typename... TItems>
constexpr std::size_t GetTypeIndex(std::size_t baseIndex=0)
{
	if constexpr (std::same_as<TItemTarget, TItem>)
		return baseIndex;
	else
		return GetTypeIndex<TItemTarget, TItems...>(baseIndex + 1);
}

template<typename T>
concept ArchetypeLike = requires() {
	typename T::TableType;
};

template<typename T>
concept RelationLike = requires() {
	typename T::BaseArchetypeType;
	typename T::RelationKindType;
};

template<typename... TRelations>
struct RelationPath
{
	template<typename TRelation>
	static inline constexpr bool Contains = (std::same_as<TArchetype, TRelations> || ...);

	template<typename TRelation>
	using Cons = ArchetypePath<TArchetype, TRelations...>;
};

template<typename TRelation, typename TRelationPath>
concept CyclicRelation = RelationLike<TRelation> && TRelationPath::template Contains<TRelation>;

template<typename TRelation, typename TRelationPath>
concept AcyclicRelation = RelationLike<TRelation> && !TRelationPath::template Contains<TRelation>;

template<typename TRelation, typename TRelationPath>
concept CyclicRelationArray = CyclicRelation<TRelation, TRelationPath> && TRelation::IsArray;

template<typename TRelation, typename TRelationPath>
concept AcyclicRelationArray = AcyclicRelation<TRelation, TRelationPath> && TRelation::IsArray;

template<typename TRelationKind, ArchetypeLike TArchetype, bool Inverse=false>
struct Relation
{
	using RelationKindType = TRelationKind;
	using ArchetypeBaseType = std::remove_extent<TArchetype>;
	inline static constexpr bool IsArray = std::is_array_v<TArchetype>;
};

template<typename TRelationKind, ArchetypeLike TArchetype>
struct InverseRelation : public Relation<TRelationKind, TArchetype, true> {};

template<typename... TComponents>
struct Archetype 
{
	template<typename... TAddComponents>
	using Append = Archetype<TComponents..., TAddComponents...>;

	template<typename TArchOther>
	using Combine = TArchOther::template Append<TComponents...>;
};

class EcsWorld
{
};