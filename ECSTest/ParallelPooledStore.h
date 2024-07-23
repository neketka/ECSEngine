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

	ParallelPooledStoreIterator(std::size_t index, StoreIterator<Ts>... storeIters)
		: m_curIndex(index), m_curs(storeIters...)
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
			return std::make_tuple<Ts&...>(*elem...);
		}, m_curs);
	}

	auto operator<=>(const iterator& other) const
	{
		return m_curs <=> other.m_curs;
	}

	auto operator==(const iterator& other) const
	{
		return m_curs == other.m_curs;
	}
private:
	std::tuple<StoreIterator<Ts>...> m_curs;
	std::size_t m_curIndex;
};

template<StoreCompatible... Ts>
class ParallelPooledStore
{
public:
	static_assert(sizeof...(Ts) <= 54, "Up to 54 components are acceptable per pool (to fit inside a block)");

	static const auto MAX_ENTRIES = PooledStore<std::size_t>::MAX_T_PER_STORE;

	ParallelPooledStore() : m_curCount(0)
	{
	}

	void SetIdPrefix(std::int32_t prefix)
	{
		m_prefix = prefix << 32;
	}

	ParallelPooledStoreIterator<const size_t, Ts...> Emplace()
	{
		auto& idStore = std::get<0>(m_stores);

		std::size_t newId = m_sparseFree.AllocateOne();
		std::size_t index = *m_sparseMap.GetConst(newId);

		if (newId == ~0ull)
		{
			while (newId == ~0ull)
			{
				index = m_curCount++;
				auto newCount = index + 1;

				m_sparseFree.GrowBitsTo(newCount);
				m_sparseMapSize += 1;
				m_sparseMap.Emplace(index, 1);

				newId = m_sparseFree.AllocateOne();
			}

			*idStore.Emplace(index, 1) = m_prefix | newId;
			*m_sparseMap.Get(newId) = index;
		}

		return 
			std::apply([&](PooledStore<std::size_t> idStore, PooledStore<Ts>&... elem)
			{
				return ParallelPooledStoreIterator<const std::size_t, Ts...>(index, idStore.GetConst(index), elem.Emplace(index, 1)...);
			}, m_stores);
	}

	void Delete(std::size_t id)
	{
		m_sparseFree.Set(id, true);
	}

	void UnsafeCleanup()
	{
		std::apply([&](PooledStore<std::size_t> idStore, PooledStore<Ts>&... elem)
		{
			idStore.ReclaimBlocks();
			((elem.ReclaimBlocks(), ...);

			for (std::size_t zeroIndex : m_sparseFree)
			{
			}
		}, m_stores);
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
			return CreateIterator(0);
		}

		ParallelPooledStoreIterator<TQueries> end()
		{
			return CreateIterator(m_store.m_curCount.load());
		}

		ParallelPooledStoreIterator<TQueries> Get(std::size_t index)
		{
			return CreateIterator(index);
		}
	private:
		ParallelPooledStoreIterator<TQueries> CreateIterator(std::size_t index)
		{
			return ParallelPooledStoreIterator<TQueries>(index, std::get<std::remove_const_t<TQueries>>(m_store.m_stores)... );
		}

		ParallelPooledStore<Ts...>& m_store;
	};

	template<typename... TQueries>
	View<TQueries...> GetView()
	{
		return View(*this);
	}
private:
	AtomicBitset<MAX_ENTRIES> m_sparseFree;
	PooledStore<std::size_t> m_sparseMap;
	std::atomic_size_t m_sparseMapSize;
	std::size_t m_prefix;

	std::tuple<PooledStore<std::size_t>, PooledStore<Ts>...> m_stores;
	std::atomic_size_t m_curCount;
};