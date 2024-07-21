#include <iostream>

#include "PooledStore.h"

struct MyComponent
{
    int x;
};

struct MyComponent2
{
    int x;
};

int main()
{
    MemoryPool::Initialize(10);
    PooledStore<int> store;

    auto myIter = store.Get(0);

    MemoryPool::Destroy();
}