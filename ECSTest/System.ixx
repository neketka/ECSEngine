export module GameSystem;
import std;
import Allocator;

template<class ...TComponentAccesses>
class ObjectQuery
{

};

export
template<bool Parallel, class TComponentSet, class ...TSystemDeps>
class GameSystem 
{
public:
	const bool IsParallel = Parallel;

	std::size_t PrepareGroups(std::size_t maxGroupCount)
	{
		
	}

	void ExecuteGroup(std::size_t groupIndex)
	{

	}
protected:
	template<class ...TComponentAccesses>
	void Query() 
	{

	}
};

