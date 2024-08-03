#include <iostream>

#include "EcsStorage.h"

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
    using Simple = Archetype<MyComponent>;
    using SimpleQuery = Query::Read<std::size_t>::Read<MyComponent>;

    EcsStorage<Simple> storage;
    
    clock_t startCreate = clock();
    for (auto [id, comp] : storage.Create<Simple>(2000000))
        comp.x = 51;
    clock_t endCreate = clock();

    clock_t startIter = clock();
    std::size_t exId = 0;
    for (auto [id, myComp] : storage.RunQuery<SimpleQuery>())
    {
        exId = id;
    }
    clock_t endIter = clock();

    auto createTime = static_cast<double>(endCreate - startCreate) / CLOCKS_PER_SEC * 1000.0;
    auto iterTime = static_cast<double>(endIter - startIter) / CLOCKS_PER_SEC * 1000.0;

    std::cout << exId << " " << "Create " << createTime << "ms Iter " << iterTime << "ms" << std::endl;
}

int main()
{
    const auto poolSize = 1 * 1024 * 1024 * 1024; // 1 GB
    MemoryPool::Initialize(poolSize / BLOCK_SIZE);

    test();

    MemoryPool::Destroy();
}