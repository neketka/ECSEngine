#pragma once

#include "PooledStore.h"

template<StoreCompatible... Ts>
class ParallelPooledStore;

template<typename... TComponents>
class Archetype
{
private:
	template<typename TTarget, typename... TComps>
	struct ContainsInternal;

	template<typename TTarget>
	struct ContainsInternal<TTarget>
	{
		static inline constexpr bool Value = false;
	};

	template<typename TTarget, typename TComp>
	struct ContainsInternal<TTarget, TComp>
	{
		static inline constexpr bool Value = std::is_same_v<TTarget, TComp>;
	};

	template<typename TTarget, typename TComp, typename... TComps>
	struct ContainsInternal<TTarget, TComp, TComps...>
	{
		static inline constexpr bool Value = 
			ContainsInternal<TTarget, TComp>::Value || ContainsInternal<TTarget, TComps...>::Value;
	};

	template<typename TArchOther, typename... TComps>
	struct UnionParts;

	template<typename TArchOther, typename TComp>
	struct UnionParts<TArchOther, TComp>
	{
		using Type = std::conditional_t<TArchOther::template Contains<TComp>, TArchOther, typename TArchOther::template AppendNoUnion<TComp>>;
	};

	template<typename TArchOther, typename TComp, typename... TComps>
	struct UnionParts<TArchOther, TComp, TComps...>
	{
		using Type = Archetype<TComponents...>::template UnionParts<
			Archetype<TComponents...>::template UnionParts<TArchOther, TComp>::Type, TComps...
		>::Type;
	};

public:
	using Tuple = std::tuple<TComponents...>;

	template<typename... TComps>
	using AppendNoUnion = Archetype<TComponents..., TComps...>;

	template<typename... TAddComponents>
	using Append = UnionParts<Archetype<TComponents...>, TAddComponents...>::Type;

	template<typename TArchOther>
	using Union = TArchOther::template Append<TComponents...>;

	using StoreType = ParallelPooledStore<TComponents...>;

	template<typename TComp>
	static inline constexpr bool Contains = Archetype<TComponents...>::template ContainsInternal<TComp, TComponents...>::Value;

	template<typename TArchSuperset>
	static inline constexpr bool IsSubsetOf = ((TArchSuperset::template Contains<TComponents>) && ...);

	template<typename TArchSuperset>
	static inline constexpr bool AnyIn = ((TArchSuperset::template Contains<TComponents>) || ...);

	template<typename TArch>
	static inline constexpr bool MeetsAnyCriterion = ((TComponents::template IsSubsetOf<TArch>) || ...);
};

using EmptyArchetype = Archetype<>;