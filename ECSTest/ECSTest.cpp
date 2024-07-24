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

void test()
{
    ParallelPooledStore<MyComponent, MyComponent2> store;

    store.SetIdPrefix(111);
    auto id = std::get<const std::size_t&>(*store.Emplace());

    auto myCompMutView = store.GetView<MyComponent, MyComponent2>();
    auto myCompConstView = store.GetView<const std::size_t, const MyComponent, const MyComponent2>();

    for (auto [comp, comp2] : myCompMutView)
    {
        comp.x = 5;
        comp2.y = 10;
    }

    for (auto [idTest, comp, comp2] : myCompConstView)
    {
        std::cout << id << " " << idTest << " " << comp.x << " " << comp2.y << std::endl;
    }

    store.ExclusiveCleanup();
}

int main()
{
    const auto poolSize = 1 * 1024 * 1024 * 1024; // 1 GB
    MemoryPool::Initialize(poolSize / BLOCK_SIZE);

    test();

    MemoryPool::Destroy();
}