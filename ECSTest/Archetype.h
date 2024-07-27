#pragma once

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

	template<typename TArch>
	static inline constexpr bool MeetsAnyCriterion = TComponents::template IsSubsetOf<TArch> || ...;
};

using EmptyArchetype = Archetype<>;