#include <iostream>

import Component;

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
    ComponentSet<MyComponent, MyComponent2> compSet;

    auto myArch = compSet.template PrepareArchetype<MyComponent>();
    auto myObjs = compSet.template CreateObjects<MyComponent>(myArch, 10);

    for (auto objId : myObjs)
    {
        auto block = compSet.template GetQueryBlockById<MyComponent>(objId);
    }
    
    std::cout << "Hello World!" << std::endl;
}