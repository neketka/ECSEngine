#pragma once

#include "EcsWorld.h"

template<typename T>
class ExSystem
{
public:
	class MyMessageComponent
	{
	};

	using MyMessageQuery = Query::Read<MyMessageComponent>;
	using MyQuery = Query::Read<std::size_t>;
	using MyArchetype = Archetype<>;

	using ExecuteBefore = SystemList<T>;
	using ExecuteAfter = SystemList<T>;
	using Archetypes = ArchetypeList<MyArchetype>;
	using Messages = MessageList<MyMessageQuery>;

	void Execute(EcsWorld<T>& world)
	{
	}

	void Receive(EcsWorld<T>& world)
	{
	}
};