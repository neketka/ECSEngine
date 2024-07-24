#pragma once

#include <tuple>
#include <span>
#include <atomic>
#include <shared_mutex>
#include <optional>
#include <concurrent_queue.h>
#include <vector>
#include <array>
#include <concepts>

const size_t BLOCK_SIZE = 4096;
class MemoryPool
{
public:
	template<typename T>
	class Ptr
	{
	public:
		Ptr(T *block);
		Ptr();
		~Ptr();
		Ptr(const Ptr& other);
		Ptr(Ptr&& other) noexcept;
		Ptr<T>& operator=(const Ptr<T>& other);
		Ptr<T>& operator=(Ptr<T>&& other) noexcept;
		operator bool() const;

		T& operator*();
		T *operator->();
		auto operator<=>(const Ptr<T>& other);

		T *Load();
		void WeakSwap(Ptr<T> other);
		void Store(T *ptr);
		void WaitNonnull();
		void NotifyNonnull();
	private:
		std::atomic<T *> m_ptr;
		void Move(Ptr<T>& other);
	};

	static void Initialize(std::size_t blockCount);
	static void Destroy();

	template<typename T>
	static Ptr<T> RequestBlock();
private:
	static MemoryPool *m_globalPool;
	MemoryPool(std::size_t blockCount);
	~MemoryPool();

	std::size_t *m_region;
	std::vector<std::size_t *> m_blocks;
	std::shared_mutex m_replenishLock;
	std::atomic_size_t m_blockTop;
};

inline void MemoryPool::Initialize(std::size_t blockCount)
{
	m_globalPool = new MemoryPool(blockCount);
}

inline void MemoryPool::Destroy()
{
	delete m_globalPool;
}

inline MemoryPool::MemoryPool(std::size_t blockCount)
{
	m_region = new std::size_t[blockCount * BLOCK_SIZE / sizeof(std::size_t)];
	m_blocks.resize(blockCount);
	m_blockTop = blockCount - 1;

	auto region = m_region;
	for (std::size_t i = 0; i < blockCount; ++i)
	{
		m_blocks[i] = region;
		region += BLOCK_SIZE / sizeof(std::size_t);
	}
}

inline MemoryPool::~MemoryPool()
{
	m_blocks.clear();
	delete[] m_region;
}

template<typename T>
inline static MemoryPool::Ptr<T> MemoryPool::RequestBlock()
{
	m_globalPool->m_replenishLock.lock_shared();
	auto block = m_globalPool->m_blocks[m_globalPool->m_blockTop--];
	m_globalPool->m_replenishLock.unlock_shared();

	return new(block) T;
}

template<typename T>
inline MemoryPool::Ptr<T>::Ptr(T *block) : m_ptr(block)
{
}

template<typename T>
inline MemoryPool::Ptr<T>::Ptr() : m_ptr(nullptr)
{
}

template<typename T>
inline MemoryPool::Ptr<T>::~Ptr()
{
	auto val = m_ptr.load();
	if (!val) return;

	if constexpr (!std::is_trivially_destructible_v<T>)
		val->~T();

	m_globalPool->m_replenishLock.lock();
	m_globalPool->m_blocks[++m_globalPool->m_blockTop] = reinterpret_cast<std::size_t *>(m_ptr.load());
	m_globalPool->m_replenishLock.unlock();

	m_ptr = nullptr;
}

template<typename T>
inline MemoryPool::Ptr<T>::Ptr(const Ptr& other)
{
	m_ptr = other.m_ptr.load();
}

template<typename T>
inline MemoryPool::Ptr<T>::Ptr(Ptr&& other) noexcept
{
	Move(other);
}

template<typename T>
inline MemoryPool::Ptr<T>& MemoryPool::Ptr<T>::operator=(const Ptr<T>& other)
{
	m_ptr = other.m_ptr.load();

	return *this;
}

template<typename T>
inline MemoryPool::Ptr<T>& MemoryPool::Ptr<T>::operator=(Ptr<T>&& other) noexcept
{
	Move(other);
	return *this;
}

template<typename T>
inline MemoryPool::Ptr<T>::operator bool() const
{
	return m_ptr;
}

template<typename T>
inline T& MemoryPool::Ptr<T>::operator*()
{
	return *m_ptr;
}

template<typename T>
inline T *MemoryPool::Ptr<T>::operator->()
{
	return m_ptr.load();
}

template<typename T>
inline auto MemoryPool::Ptr<T>::operator<=>(const Ptr<T>& other)
{
	return Load() <=> other.m_ptr.load();
}

template<typename T>
inline T *MemoryPool::Ptr<T>::Load()
{
	return m_ptr;
}

template<typename T>
inline void MemoryPool::Ptr<T>::WeakSwap(Ptr<T> other)
{
	auto self = m_ptr.exchange(other.m_ptr);
	other.m_ptr.exchange(self);
}

template<typename T>
inline void MemoryPool::Ptr<T>::Store(T *ptr)
{
	this->~Ptr();
	m_ptr = ptr;
}

template<typename T>
inline void MemoryPool::Ptr<T>::WaitNonnull()
{
	while (!m_ptr)
	{
		m_ptr.wait(nullptr);
	}
}

template<typename T>
inline void MemoryPool::Ptr<T>::NotifyNonnull()
{
	m_ptr.notify_all();
}

template<typename T>
inline void MemoryPool::Ptr<T>::Move(Ptr<T>& other)
{
	this->~Ptr(); // potentially bad practice
	m_ptr.store(other.m_ptr);
	other.m_ptr.store(nullptr);
}
