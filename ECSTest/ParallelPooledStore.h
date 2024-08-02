#pragma once

#include "PooledStore.h"
#include "AtomicBitset.h"
#include "Archetype.h"

#include <tuple>
#include <ranges>
#include <atomic>

const auto ID_MASK = ~(~0ull << 24);

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
	using value_type = std::tuple<Ts&...>;
	using difference_type = std::ptrdiff_t;

	using UnconstReference = std::tuple<std::remove_const_t<Ts>&...>;

	ParallelPooledStoreIterator(std::size_t index, PooledStore<std::remove_const_t<Ts>>&... stores) 
		: m_curIndex(index), m_curs(stores.GetIterator<Ts>(index)...)
	{
		// TODO: skip deleted
	}

	ParallelPooledStoreIterator(std::size_t index, StoreIterator<Ts>... storeIters)
		: m_curIndex(index), m_curs(storeIters...)
	{
		// TODO: skip deleted
	}

	ParallelPooledStoreIterator()
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
		// TODO: skip deleted

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
		return std::apply([](StoreIterator<Ts>&... elem)
		{
			return std::forward_as_tuple(*elem...);
		}, m_curs);
	}

	reference operator*() const
	{
		return std::apply([](const StoreIterator<Ts>&... elem)
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
	using ArchType = Archetype<std::size_t, Ts...>;

	static const auto MAX_ENTRIES = PooledStore<std::size_t>::MAX_T_PER_STORE;

	ParallelPooledStore() : m_curCount(0), m_prefix(0)
	{
	}

	ParallelPooledStore(const ParallelPooledStore<Ts...>&) = delete;
	ParallelPooledStore& operator=(const ParallelPooledStore<Ts...>&) = delete;

	void SetIdPrefix(std::size_t prefix)
	{
		m_prefix = prefix << 24; // 40 bit prefix
	}

	auto Emplace(std::size_t count)
	{
		const auto index = m_curCount.fetch_add(count);
		const auto newCount = index + count;
		const auto loadedIdMapSize = m_idMapSize.load();

		if (loadedIdMapSize < newCount)
		{
			const auto diff = newCount - loadedIdMapSize;
			const auto index = m_idMapSize.fetch_add(diff);
			m_idMap.Emplace(index, diff);
		}

		m_deletedBits.GrowBitsTo(newCount);

		std::apply([&](PooledStore<std::size_t>& idStore, PooledStore<Ts>&... elem)
		{
			idStore.Emplace(index, count);
			((elem.Emplace(index, count)), ...);

			auto cur = idStore.GetConst(index);
			auto end = idStore.GetConst(index + count);

			for (; cur < end; ++cur)
			{
				const_cast<std::atomic_size_t&>(*m_idMap.GetConst(*cur & ID_MASK)).store(cur.GetIndex());
			}
		}, m_stores);

		return View<true, const std::size_t, Ts...>(*this, index, index + count + 1);
	}

	void Delete(std::size_t id)
	{
		id &= ~(~0ull << 24);

		auto index = m_idMap.GetConst(id)->load();
		m_deletedBits.Set(index, true);
	}

	template<bool RefCounted, typename... TQueries>
	class View : public std::ranges::view_interface<View<RefCounted, TQueries...>>
	{
	public:
		using Iterator = ParallelPooledStoreIterator<TQueries...>;

		View(ParallelPooledStore<Ts...>& store, std::size_t beginIndex, std::size_t endIndex) 
			: m_store(store), m_beginIndex(beginIndex), m_endIndex(endIndex)
		{
			IncrementRefcount();
		}

		View(const View& copied) : m_store(copied.m_store), m_beginIndex(copied.m_beginIndex), m_endIndex(copied.m_endIndex)
		{
			IncrementRefcount();
		}

		View(View&& moved) : m_store(moved.m_store), m_beginIndex(moved.m_beginIndex), m_endIndex(moved.m_endIndex)
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

		auto begin()
		{
			return CreateIterator(m_beginIndex);
		}

		auto end()
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

		auto index = *m_idMap.GetConst(id);

		if (m_deletedBits.Get(index))
			return View<true, TQueries...>(*this, -1, -1);

		return View<true, TQueries...>(*this, index, std::min(index + 1, m_curCount.load()));
	}
private:
	AtomicBitset<MAX_ENTRIES> m_deletedBits;
	PooledStore<std::atomic_size_t> m_idMap;
	std::atomic_size_t m_idMapSize;
	std::size_t m_prefix;

	std::tuple<PooledStore<std::size_t>, PooledStore<Ts>...> m_stores;
	std::atomic_size_t m_curCount;

	std::shared_mutex m_viewCreationLock;
	std::atomic_size_t m_refCount;

	void ExclusiveCleanup()
	{
		auto fun =
			[&](PooledStore<std::size_t>& idStore, PooledStore<Ts>&... elem)
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

				for (std::size_t deletedIndex : m_deletedBits)
				{
					--m_curCount;

					if (deletedIndex >= rightPtr) break;

					auto indexDiff = deletedIndex - leftPtr;
					leftPtr = deletedIndex;
					curIter += indexDiff;

					auto mutDeletedObj = std::apply([](const std::size_t& id, const Ts&... rest)
					{ 
						// Cast away constness of mutable object (so that RCU is not unnecessarily used)
						return std::forward_as_tuple(const_cast<std::size_t&>(id), const_cast<Ts&>(rest)...);
					}, *curIter);

					std::size_t deadId = std::get<std::size_t&>(mutDeletedObj); // Get id of deleted obj
					mutDeletedObj = *endIter; // Move from right ptr to cur deleted free one (fill empty slot)
					std::size_t movedId = std::get<std::size_t&>(mutDeletedObj); // Get id of moved object

					const_cast<std::size_t&>(std::get<const std::size_t&>(*endIter)) = deadId; // Recycle dead id
					const_cast<std::atomic_size_t&>(*m_idMap.GetConst(movedId & ID_MASK)) = deletedIndex; // Update index of moved obj
				}
			};

		std::apply(fun, m_stores);
	}
};