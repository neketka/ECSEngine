#include <iostream>

#include "ParallelPooledStore.h"

struct MyComponent
{
    std::size_t x;
};

struct MyComponent2
{
    std::size_t x;
    std::size_t y;
    std::size_t z;
    std::size_t w;
};

int main()
{
    MemoryPool::Initialize(10);
    ParallelPooledStore<MyComponent, MyComponent2> store;

    std::atomic<MyComponent> myAtom;

    MemoryPool::Destroy();
}