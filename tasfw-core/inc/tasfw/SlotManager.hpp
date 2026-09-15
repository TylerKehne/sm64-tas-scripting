#pragma once
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef SLOTMANAGER_H
#define SLOTMANAGER_H

template <class TState>
class Resource;

// The friend through which the tests and benchmarks whose subject is the slot manager reach it
// (tasfw/testing/PerfAccess.hpp); everything else reaches a slot through Resource's operations.
class PerfAccess;

// Holds the saved states. A state's contents are written by Resource::save and die when its
// slot is erased, whether the state is then pooled for reuse or freed; a state type that
// holds a reference to something outside itself (LibSm64Mem's baseline) defines dispose()
// and EraseSlot calls it at that point, resolved at compile time like LevelStack's Reset().
template <class TState>
class SlotManager
{
public:
	SlotManager(Resource<TState>* resource) : _resource(resource) { }
	~SlotManager() { SlotBudget::Give(_saveMemLimit); }

	// The resource's limit, taken from the process budget; Resource's constructor sets it once.
	void SetLimit(int64_t bytes)
	{
		SlotBudget::Take(bytes);
		_saveMemLimit = bytes;
	}

	int64_t CreateSlot();
	void EraseOldestSlot();
	void EraseSlot(int64_t slotId);
	void LoadSlot(int64_t slotId);
	bool isValid(int64_t slotId) const;

private:
	friend class PerfAccess;

	Resource<TState>* _resource = NULL;
	std::map<int64_t, TState> slotsById;
	std::map<int64_t, int64_t> slotIdsByLastAccess;
	std::map<int64_t, int64_t> slotLastAccessOrderById;
	int64_t nextSlotId = 1; //slot IDs are unique

	int64_t _saveMemLimit = 0;
	int64_t _currentSaveMem = 0;

	// Erased and evicted states are kept here and handed to the next CreateSlot, so a save
	// into a recycled state is one copy instead of allocating and zero-filling fresh buffers
	// (a full LibSm64 save was 8x its load before this; ROADMAP 3.9). Pooled memory counts
	// toward _saveMemLimit; the pool is bounded so idle memory does not pile up.
	std::vector<TState> _pool;
	int64_t _pooledMem = 0;
	size_t _maxPooledStates = 32;
};

//Include template method implementations
#include "tasfw/SlotManager.t.hpp"

#endif
