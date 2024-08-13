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
    using SimpleReadQuery = Query::Read<std::size_t, MyComponent, MyComponent2>;
    using SimpleWriteQuery = Query::Read<std::size_t>::Write<MyComponent, MyComponent2>;

    EcsStorage<Simple> storage;
    
    clock_t startCreate = clock();
    for (auto [id, myComp, myComp2] : storage.Create<Simple>(2000000))
    {
        myComp.x = 51;
        myComp2.x = 14;
    }
    clock_t endCreate = clock();

    std::size_t count = 0;
    clock_t startRead = clock();
    for (auto [id, myComp, myComp2] : storage.RunQuery<SimpleWriteQuery>())
    {
        ++count;
    }
    clock_t endRead = clock();

    clock_t startUpdate = clock();
    for (auto [id, myComp, myComp2] : storage.RunQuery<SimpleWriteQuery>())
    {
        myComp.x = count;
        myComp2.x = count;
        ++count;
    }
    clock_t endUpdate = clock();

    clock_t startDelete = clock();
    for (auto [id] : storage.RunQuery<Query::Read<std::size_t>>())
    {
        storage.Delete<Simple>(id);
        --count;
    }
    clock_t endDelete = clock();

    auto createTime = static_cast<double>(endCreate - startCreate) / CLOCKS_PER_SEC * 1000.0;
    auto deleteTime = static_cast<double>(endDelete - startDelete) / CLOCKS_PER_SEC * 1000.0;
    auto updateTime = static_cast<double>(endUpdate - startUpdate) / CLOCKS_PER_SEC * 1000.0;
    auto readTime = static_cast<double>(endRead - startRead) / CLOCKS_PER_SEC * 1000.0;

    std::cout 
        << "Objects " << count << std::endl
        << "Create " << createTime 
        << "ms Read " << updateTime 
        << "ms Update " << updateTime  
        << "ms Delete " << deleteTime
        << "ms" << std::endl;
}

int main_()
{
    const auto poolSize = 256 * 1024 * 1024; // 256 MB
    MemoryPool::Initialize(poolSize / BLOCK_SIZE);

    test();

    MemoryPool::Destroy();
}