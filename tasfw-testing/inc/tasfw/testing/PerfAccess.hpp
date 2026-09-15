#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <tasfw/Resource.hpp>

// Friend of Scattershot (declared in Scattershot.hpp) and of Resource and SlotManager
// (tasfw/Resource.hpp). Lets the benchmarks and tests whose subject is one of those reach its
// internals directly, without changing their visibility for everyone else.
class PerfAccess
{
public:
	// The slot manager, for the tests and benchmarks that measure or pin it; everything else
	// reaches a slot through Resource's operations (SaveState, LoadState, DisposeState, HasState).
	template <class TState>
	static SlotManager<TState>& Slots(Resource<TState>& resource) { return resource.slotManager; }

	template <class TState>
	static const TState& SlotState(Resource<TState>& resource, int64_t slotId) { return resource.slotManager.slotsById.at(slotId); }

	template <class TState>
	static std::size_t LiveSlots(Resource<TState>& resource) { return resource.slotManager.slotsById.size(); }

	template <class TState>
	static int64_t LiveBytes(Resource<TState>& resource) { return resource.slotManager._currentSaveMem; }

	// The limit as a test sets it, past the process budget the constructor took it from.
	template <class TState>
	static void SetSlotLimit(Resource<TState>& resource, int64_t bytes) { resource.slotManager._saveMemLimit = bytes; }

	template <class TState>
	static void SetMaxPooledStates(Resource<TState>& resource, std::size_t count) { resource.slotManager._maxPooledStates = count; }

	template <class TState>
	static std::size_t PooledStates(Resource<TState>& resource) { return resource.slotManager._pool.size(); }

	template <class TState>
	static int64_t PooledBytes(Resource<TState>& resource) { return resource.slotManager._pooledMem; }

	// Releases the pool, so that the next saves allocate fresh storage.
	template <class TState>
	static void DropPool(Resource<TState>& resource)
	{
		resource.slotManager._pool.clear();
		resource.slotManager._pooledMem = 0;
	}

	template <class TScattershot, class T>
	static uint64_t GetHash(TScattershot& scattershot, const T& value, bool ignoreFillerBytes)
	{
		return scattershot.GetHash(value, ignoreFillerBytes);
	}

	template <class TScattershot, typename... Args>
	static bool UpsertBlock(TScattershot& scattershot, Args&&... args)
	{
		return scattershot.UpsertBlock(std::forward<Args>(args)...);
	}

	template <class TScattershot>
	static std::size_t BlockCount(TScattershot& scattershot)
	{
		return scattershot.Blocks.size();
	}

	// The run's totals (Scattershot prints them at the end; the mock scattershot test reads them).
	template <class TScattershot>
	static uint64_t TotalShots(const TScattershot& scattershot)
	{
		return scattershot.TotalShots;
	}

	template <class TScattershot>
	static uint64_t ScriptCount(const TScattershot& scattershot)
	{
		return scattershot.ScriptCount;
	}

	template <class TScattershot>
	static uint64_t ValidationFailures(const TScattershot& scattershot)
	{
		return scattershot.ValidationFailures;
	}
};
