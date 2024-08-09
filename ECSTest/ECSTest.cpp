#include <iostream>

#include "EcsStorage.h"
#include <unordered_set>

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
    using Simple = Archetype<MyComponent, MyComponent2>;
    using SimpleQuery = Query::Read<std::size_t>::Read<MyComponent, MyComponent2>;

    EcsStorage<Simple> storage;
    
    clock_t startCreate = clock();
    for (auto [id, myComp, myComp2] : storage.Create<Simple>(200000))
    {
        myComp.x = 51;
        myComp2.x = 14;
    }
    clock_t endCreate = clock();

    clock_t startIter = clock();
    std::size_t deletedCount = 0;
    for (auto [id, myComp, myComp2] : storage.RunQuery<SimpleQuery>())
    {
        storage.Delete<Simple>(id);
        ++deletedCount;
    }
    clock_t endIter = clock();

    std::size_t notDeletedCount = 0;
    for (auto _ : storage.RunQuery<Query>())
    {
        ++notDeletedCount;
    }

    auto createTime = static_cast<double>(endCreate - startCreate) / CLOCKS_PER_SEC * 1000.0;
    auto iterTime = static_cast<double>(endIter - startIter) / CLOCKS_PER_SEC * 1000.0;

    std::cout << "Attempted delete: " << deletedCount << " Not deleted: " << notDeletedCount << std::endl;
    std::cout << "Create " << createTime << "ms Iter " << iterTime << "ms" << std::endl;
}

int main()
{
    const auto poolSize = 256 * 1024 * 1024; // 256 MB
    MemoryPool::Initialize(poolSize / BLOCK_SIZE);

    test();

    MemoryPool::Destroy();
}