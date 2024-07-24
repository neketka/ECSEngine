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
	using StoreIterator = PooledStore<std::remove_const_t<T>>::template Iterator<T>;

	using iterator = ParallelPooledStoreIterator<Ts...>;
	using reference = std::tuple<Ts&...>;
	using pointer = std::tuple<Ts *...>;

	using iterator_category = std::forward_iterator_tag;
	using value_type = std::tuple<Ts...>;
	using difference_type = std::ptrdiff_t;

	using UnconstReference = std::tuple<std::remove_const_t<Ts>&...>;

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
			(++elem, ...);
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
			((elem += diff), ...);
		}, m_curs);

		m_curIndex += diff;
		return *this;
	}

	reference operator*()
	{
		return std::apply([&](StoreIterator<Ts>&... elem)
		{
			return std::forward_as_tuple(*elem...);
		}, m_curs);
	}

	UnconstReference GetMutableExclusive() requires !std::same_as<UnconstReference, reference>
	{
		return std::apply([&](StoreIterator<Ts>&... elem)
		{
			return std::forward_as_tuple(const_cast<std::remove_const_t<Ts>&>(*elem)...);
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
	static_assert(sizeof...(Ts) <= 15, "Up to 15 components are acceptable per pool (to fit inside a block)");

	static const auto MAX_ENTRIES = PooledStore<std::size_t>::MAX_T_PER_STORE;

	ParallelPooledStore() : m_curCount(0), m_prefix(0)
	{
	}

	void SetIdPrefix(std::size_t prefix)
	{
		m_prefix = prefix << 24; // 40 bit prefix
	}

	ParallelPooledStoreIterator<const size_t, Ts...> Emplace()
	{
		auto& idStore = std::get<0>(m_stores);

		std::size_t newId = m_sparseFree.AllocateOne();
		std::size_t index = 0;

		if (newId == ~0ull)
		{
			while (newId == ~0ull)
			{
				index = m_curCount++;
				auto newCount = index + 1;

				m_sparseFree.GrowBitsTo(newCount);
				m_occupiedBits.GrowBitsTo(newCount);
				m_sparseMapSize += 1;
				m_sparseMap.Emplace(index, 1);

				newId = m_sparseFree.AllocateOne();
			}

			*idStore.Emplace(index, 1) = m_prefix | newId;
			*m_sparseMap.Get(newId) = index;
		}
		else
		{
			index = *m_sparseMap.GetConst(newId);
		}

		m_occupiedBits.Set(index, true);

		return 
			std::apply([&](PooledStore<std::size_t>& idStore, PooledStore<Ts>&... elem)
			{
				return ParallelPooledStoreIterator<const std::size_t, Ts...>(index, idStore.GetConst(index), elem.Emplace(index, 1)...);
			}, m_stores);
	}

	void Delete(std::size_t id)
	{
		id &= ~(~0ull << 24);

		auto index = *m_sparseMap.GetConst(id);
		m_occupiedBits.Set(id, false);
		m_sparseFree.Set(id, true);
	}

	template<bool RefCounted, typename... TQueries>
	class View
	{
	public:
		View(ParallelPooledStore<Ts...>& store, std::size_t beginIndex, std::size_t endIndex) 
			: m_store(store), m_beginIndex(beginIndex), m_endIndex(endIndex)
		{
			IncrementRefcount();
		}

		View(const View& copied) : m_store(copied.m_store), m_beginIndex(copied.m_beginIndex), m_endIndex(copied.m_endIndex)
		{
			IncrementRefcount();
		}

		View(View&& moved) : m_store(moved.store), m_beginIndex(moved.m_beginIndex), m_endIndex(moved.m_endIndex)
		{
			IncrementRefcount();
		}

		View& operator=(const View& copied)
		{
			m_store = copied.store;
			m_beginIndex = copied.m_beginIndex;
			m_endIndex = copied.m_endIndex;
			IncrementRefcount();
		}

		View& operator=(View&& moved)
		{
			m_store = moved.store;
			m_beginIndex = moved.m_beginIndex;
			m_endIndex = moved.m_endIndex;
			IncrementRefcount();
		}

		~View()
		{
			if constexpr (RefCounted)
			{
				auto newRefCount = --m_store.m_refCount;
				if (newRefCount == 0)
				{
					m_store.m_viewCreationLock.lock();
					while ((newRefCount = m_store.m_refCount) > 0)
						m_store.m_refCount.wait(newRefCount);

					m_store.ExclusiveCleanup();
					m_store.m_viewCreationLock.unlock();
				}
			}
		}

		ParallelPooledStoreIterator<TQueries...> begin()
		{
			return CreateIterator(m_beginIndex);
		}

		ParallelPooledStoreIterator<TQueries...> end()
		{
			return CreateIterator(m_endIndex);
		}

		operator bool() const
		{
			return m_beginIndex < m_endIndex;
		}
	private:
		ParallelPooledStore<Ts...>& m_store;
		std::size_t m_beginIndex;
		std::size_t m_endIndex;

		ParallelPooledStoreIterator<TQueries...> CreateIterator(std::size_t index)
		{
			// Convoluted to fix ambiguous syntax errors
			return std::make_from_tuple<ParallelPooledStoreIterator<TQueries...>>(
				std::forward_as_tuple(index, std::get<PooledStore<std::remove_const_t<TQueries>>>(m_store.m_stores)...)
			);
		}

		void IncrementRefcount()
		{
			if constexpr (RefCounted)
			{
				m_store.m_viewCreationLock.lock_shared();
				++m_store.m_refCount;
				m_store.m_viewCreationLock.unlock_shared();
			}
		}
	};

	template<typename... TQueries>
	View<true, TQueries...> GetView()
	{
		return View<true, TQueries...>(*this, 0, m_curCount.load());
	}

	template<typename... TQueries>
	View<true, TQueries...> GetViewAt(std::size_t id)
	{
		id &= ~(~0ull << 24);

		if (!m_sparseFree.Get(id))
			return View<true, TQueries...>(*this, -1, -1);

		auto index = *m_sparseMap.GetConst(id);

		return View<true, TQueries...>(*this, index, std::min(index + 1, m_curCount.load()));
	}
private:
	AtomicBitset<MAX_ENTRIES> m_sparseFree;
	AtomicBitset<MAX_ENTRIES> m_occupiedBits;
	PooledStore<std::size_t> m_sparseMap;
	std::atomic_size_t m_sparseMapSize;
	std::size_t m_prefix;

	std::tuple<PooledStore<std::size_t>, PooledStore<Ts>...> m_stores;
	std::atomic_size_t m_curCount;

	std::shared_mutex m_viewCreationLock;
	std::atomic_size_t m_refCount;

	void ExclusiveCleanup()
	{
		auto fun =
			[&](PooledStore<std::size_t> idStore, PooledStore<Ts>&... elem)
			{
				idStore.ReclaimBlocks();
				(elem.ReclaimBlocks(), ...);

				std::size_t leftPtr = 0;
				std::size_t rightPtr = m_curCount - 1;

				// break constness since this function is exclusive
				auto constView = View<false, const std::size_t, const Ts...>(*this, 0, m_curCount.load());
				auto curIter = constView.begin();
				auto endIter = constView.end();
				endIter += -1;

				for (std::size_t freeIndex : m_occupiedBits)
				{
					--m_curCount;

					if (freeIndex >= rightPtr) break;

					auto indexDiff = freeIndex - leftPtr;
					leftPtr = freeIndex;
					curIter += indexDiff;

					auto unconst = std::apply([](const std::size_t& id, const Ts&... rest)
						{
							return std::forward_as_tuple(const_cast<std::size_t&>(id), const_cast<Ts&>(rest)...);
						}, *curIter);


					unconst = *endIter;
					std::size_t id = std::get<std::size_t&>(unconst);

					m_occupiedBits.Set(freeIndex, true);
					const_cast<std::size_t&>(*m_sparseMap.GetConst(std::get<std::size_t&>(unconst))) = freeIndex;
					m_occupiedBits.Set(rightPtr, false);
				}
			};

		std::apply(fun, m_stores);
	}
};