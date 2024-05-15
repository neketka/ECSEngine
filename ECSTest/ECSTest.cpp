#include <iostream>

import Allocator;

class MyComponent
{
    int x;
};

class MyComponent2
{
    int x;
};

int main()
{
    std::cout << "Hello World!" << std::endl;
}

/*


    PageAllocationIndexer indexer;
    PageAllocator<int> allocator;

    auto allocOp = indexer.Allocate(1);
    allocator.Allocate(allocOp);
    allocOp = indexer.Allocate(64);

    allocator.Allocate(allocOp);
    allocator.Get(allocOp.LastIndex).second = 42;

    auto allocatorElements = indexer.GetAllocatorElements(allocator);

    auto [page, pageIndex, count] = allocatorElements.GetPage(1);
    std::cout << page.Data[0] << " " << pageIndex.DeletedBitset << " " << count << std::endl;

    indexer.Delete(allocOp.LastIndex);
    std::cout << pageIndex.DeletedBitset << std::endl;

    auto delOp = indexer.CleanupUnsync();
    allocator.CleanupDeletedUnsync(delOp);
    allocator.Get(0);


*/