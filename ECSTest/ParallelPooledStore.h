#pragma once

#include "PooledStore.h"

#include <tuple>
#include <atomic>

template<trivial... Ts>
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
private:
	static const std::size_t BITSETS_PER_BLOCK = BLOCK_SIZE / sizeof(std::atomic_size_t);
	static const std::size_t BITS_PER_BLOCK = BITSETS_PER_BLOCK * 8;
	static const std::size_t BITSET_BLOCKS_PER_STORE = PooledStore<std::size_t>::MAX_T_PER_STORE / BITS_PER_BLOCK;

	struct AtomicBitsetBlock
	{
		std::atomic_size_t Bitsets[BITSETS_PER_BLOCK];
	};
public:
	ParallelPooledStore(MemoryPool& pool, std::tuple<PooledStore<Ts>&...> stores)
		: m_stores(stores), m_curCount(0), m_iterRefCount(0), m_pool(pool)
	{
	}

	ParallelPooledStoreIterator<Ts...> Emplace(std::size_t count)
	{
		auto firstIndex = (m_curCount += count);

		return std::apply([&](PooledStore<Ts>&... elem)
		{
			return ParallelPooledStoreIterator<Ts...>(firstIndex, elem.Emplace(firstIndex, count)...);
		}, m_curs);
	}

	void Delete(std::size_t index)
	{
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
	MemoryPool& m_pool;

	std::array<std::atomic<AtomicBitsetBlock *>, 256> m_deletedBits;
	std::array<std::atomic<AtomicBitsetBlock *>, 256> m_sparseFreeBits;
	
	std::tuple<PooledStore<Ts>&...> m_stores;
	std::atomic_size_t m_curCount;
	std::atomic_size_t m_iterRefCount;
	std::shared_mutex m_iterCreationLock;
};