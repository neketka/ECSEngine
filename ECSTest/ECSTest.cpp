#include <iostream>

#include "ParallelPooledStore.h"

struct MyComponent
{
    std::size_t x;
};

struct MyComponent2
{
    std::size_t x;
};

int main()
{
    MemoryPool::Initialize(10);
    ParallelPooledStore<MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent, MyComponent> store;


    MemoryPool::Destroy();
}