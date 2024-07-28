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
    using SimpleQuery = Query::Read<std::size_t, MyComponent>;

    EcsStorage<Simple> storage;

    auto simple = storage.Create<Simple>();

    /*
    for (auto [id, myComp] : storage.RunQuery<SimpleQuery>())
    {

    }
    */
}

int main()
{
    const auto poolSize = 1 * 1024 * 1024 * 1024; // 1 GB
    MemoryPool::Initialize(poolSize / BLOCK_SIZE);

    test();

    MemoryPool::Destroy();
}