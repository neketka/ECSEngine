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

const size_t BLOCK_SIZE = 1024;
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
		Ptr(const Ptr&& other);
		Ptr<T>& operator=(const Ptr<T>& other);
		Ptr<T>& operator=(const Ptr<T>&& other);
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
		void Move(const Ptr<T>& other);
	};

	static void Initialize(std::size_t blockCount);
	static void Destroy();

	template<typename T>
	static Ptr<T> RequestBlock();
private:
	static MemoryPool *m_globalPool;
	MemoryPool(std::size_t blockCount);
	~MemoryPool();

	concurrency::concurrent_queue<std::size_t *> m_blocks;
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
	for (std::size_t i = 0; i < blockCount; ++i)
	{
		m_blocks.push(new std::size_t[BLOCK_SIZE / sizeof(std::size_t)]);
	}
}

inline MemoryPool::~MemoryPool()
{
	while (!m_blocks.empty())
	{
		std::size_t *block;
		if (m_blocks.try_pop(block))
		{
			delete[] block;
		}
	}
}

template<typename T>
inline static MemoryPool::Ptr<T> MemoryPool::RequestBlock()
{
	std::size_t *block;
	if (m_globalPool->m_blocks.try_pop(block))
	{
		std::fill_n(block, BLOCK_SIZE / sizeof(std::size_t), 0);
		return new(block) T;
	}

	return nullptr;
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

	if constexpr (!std::is_trivially_default_constructible_v<T>)
		val->~T();
	m_globalPool->m_blocks.push(reinterpret_cast<std::size_t *>(val));
	m_ptr = nullptr;
}

template<typename T>
inline MemoryPool::Ptr<T>::Ptr(const Ptr& other)
{
	Move(std::move(other));
}

template<typename T>
inline MemoryPool::Ptr<T>::Ptr(const Ptr&& other)
{
	Move(std::move(other));
}

template<typename T>
inline MemoryPool::Ptr<T>& MemoryPool::Ptr<T>::operator=(const Ptr<T>& other)
{
	Move(std::move(other));
	return *this;
}

template<typename T>
inline MemoryPool::Ptr<T>& MemoryPool::Ptr<T>::operator=(const Ptr<T>&& other)
{
	Move(std::move(other));
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
inline void MemoryPool::Ptr<T>::Move(const Ptr<T>& other)
{
	this->~Ptr(); // potentially bad practice
	WeakSwap(std::move(other));
}
