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
		auto index = *m_sparseMap.GetConst(id);
		m_occupiedBits.Set(id, false);
		m_sparseFree.Set(id, true);
	}

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
				auto constView = this->GetView<const std::size_t, const Ts...>();
				auto curIter = constView.begin();
				auto endIter = constView.Get(rightPtr);

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

	template<typename... TQueries>
	class View
	{
	public:
		View(ParallelPooledStore<Ts...>& store) : m_store(store)
		{
		}

		ParallelPooledStoreIterator<TQueries...> begin()
		{
			return CreateIterator(0);
		}

		ParallelPooledStoreIterator<TQueries...> end()
		{
			return CreateIterator(m_store.m_curCount.load());
		}

		ParallelPooledStoreIterator<TQueries...> Get(std::size_t index)
		{
			return CreateIterator(index);
		}
	private:
		ParallelPooledStoreIterator<TQueries...> CreateIterator(std::size_t index)
		{
			// Convoluted to fix ambiguous syntax errors
			return std::make_from_tuple<ParallelPooledStoreIterator<TQueries...>>(
				std::forward_as_tuple(index, std::get<PooledStore<std::remove_const_t<TQueries>>>(m_store.m_stores)...)
			);
		}

		ParallelPooledStore<Ts...>& m_store;
	};

	template<typename... TQueries>
	View<TQueries...> GetView()
	{
		return View<TQueries...>(*this);
	}
private:
	AtomicBitset<MAX_ENTRIES> m_sparseFree;
	AtomicBitset<MAX_ENTRIES> m_occupiedBits;
	PooledStore<std::size_t> m_sparseMap;
	std::atomic_size_t m_sparseMapSize;
	std::size_t m_prefix;

	std::tuple<PooledStore<std::size_t>, PooledStore<Ts>...> m_stores;
	std::atomic_size_t m_curCount;
};