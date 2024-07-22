#pragma once

#include "PooledStore.h"
#include "AtomicBitset.h"

#include <tuple>
#include <atomic>

template<StoreCompatible... Ts>
class ParallelPooledStoreIterator
{
public:
	template<typename T>
	using StoreIterator = PooledStore<std::remove_const_t<T>>::Iterator<T>;

	using iterator = ParallelPooledStoreIterator<Ts...>;
	using reference = std::tuple<Ts&...>;
	using pointer = std::tuple<Ts *...>;

	using iterator_category = std::forward_iterator_tag;
	using value_type = std::tuple<Ts...>;
	using difference_type = std::ptrdiff_t;

	ParallelPooledStoreIterator(std::size_t index, PooledStore<std::remove_const_t<Ts>>&... stores) 
		: m_curIndex(index), m_curs(stores.GetIterator<Ts>(index)...)
	{
	}

	ParallelPooledStoreIterator(std::size_t index, StoreIterator<Ts>... stores)
		: m_curIndex(index), m_curs(stores)
	{
	}

	iterator operator++(int)
	{
		iterator old = *this;
		++(*this);
		return old;
	}

	iterator& operator++()
	{
		std::apply([&](StoreIterator<Ts>&... elem)
		{
			((++elem, ...);
		}, m_curs);

		return *this;
	}

	iterator operator+(difference_type diff)
	{
		iterator copy = *this;
		copy += diff;
		return copy;
	}

	iterator& operator+=(difference_type diff)
	{
		std::apply([&](StoreIterator<Ts>&... elem)
		{
			((elem += diff, ...);
		}, m_curs);

		m_curIndex += diff;
		return *this;
	}

	reference operator*()
	{
		return std::apply([&](StoreIterator<Ts>&... elem)
		{
			return std::make_tuple<Ts...>(*elem...);
		}, m_curs);
	}

	auto operator<=>(const iterator& other) const
	{
		return m_curs <=> other.m_curs;
	}
private:
	std::tuple<StoreIterator<Ts>...> m_curs;
	std::size_t m_curIndex;
};

template<StoreCompatible... Ts>
class ParallelPooledStore
{
public:
	static const auto MAX_ENTRIES = PooledStore<std::size_t>::MAX_T_PER_STORE;

	ParallelPooledStore(std::tuple<PooledStore<Ts>&...> stores)
		: m_stores(stores), m_curCount(0), m_iterRefCount(0)
	{
	}

	ParallelPooledStoreIterator<Ts...> Emplace(std::size_t count)
	{
		auto firstIndex = (m_curCount += count);

		m_sparseFree.GrowBitsTo(count);

		return std::apply([&](PooledStore<Ts>&... elem)
		{
			return ParallelPooledStoreIterator<Ts...>(firstIndex, elem.Emplace(firstIndex, count)...);
		}, m_stores);
	}

	void Delete(std::size_t index)
	{
		m_deletedBits.Set(index, true);
	}

	template<typename... TQueries>
	class View
	{
	public:
		View(ParallelPooledStore<Ts...>& store) : m_store(store)
		{
		}

		ParallelPooledStoreIterator<TQueries> begin()
		{
		}

		ParallelPooledStoreIterator<TQueries> end()
		{
		}

		ParallelPooledStoreIterator<TQueries> Get(std::size_t index)
		{
		}
	private:
		ParallelPooledStore<Ts...>& m_store;
	};

	template<typename... TQueries>
	View<TQueries...> GetView()
	{
		return View(*this);
	}
private:
	void Cleanup()
	{
		std::lock_guard<std::shared_mutex> guard(m_iterCreationLock);

		const auto count = m_deletedBits.GetSize();

		for (std::size_t i = 0; i < count; ++i)
		{
			if (m_deletedBits.Get(i))
			{

				m_deletedBits.Set(i, false);
			}
		}
	}

	AtomicBitset<MAX_ENTRIES> m_sparseFree;
	AtomicBitset<MAX_ENTRIES> m_deletedBits;
	PooledStore<std::size_t> m_sparseMap;

	std::tuple<PooledStore<Ts>&...> m_stores;
	std::atomic_size_t m_curCount;
	std::atomic_size_t m_iterRefCount;
	std::shared_mutex m_iterCreationLock;
};