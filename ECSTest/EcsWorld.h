#pragma once

#include <cstddef>
#include <concepts>
#include <vector>

#include "EcsStorage.h"
#include "Archetype.h"

template<typename TItemTarget, typename TItem, typename... TItems>
constexpr std::size_t GetTypeIndex(std::size_t baseIndex=0)
{
	if constexpr (std::same_as<TItemTarget, TItem>)
		return baseIndex;
	else
		return GetTypeIndex<TItemTarget, TItems...>(baseIndex + 1);
}

template<typename... TArchetypes>
using ArchetypeList = Archetype<TArchetypes...>;

template<typename... TMessageQueries>
using MessageList = Archetype<TMessageQueries...>;

template<typename T, template<typename> typename... TSystems>
using SystemList = Archetype<TSystems<T>...>;

template<typename TStorage>
class EcsWorld
{
private:
	TStorage m_storage;
};