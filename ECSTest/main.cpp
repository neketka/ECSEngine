#include <iostream>
#include "EcsStorage.h"


int main()
{
    const auto poolSize = 256 * 1024 * 1024; // 256 MB
    MemoryPool::Initialize(poolSize / BLOCK_SIZE);


    MemoryPool::Destroy();
}