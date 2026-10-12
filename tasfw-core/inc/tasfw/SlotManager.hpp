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

	// A slot manager belongs to one resource: it points back at it and holds the budget it
	// took. It is never copied, and it moves only into its new owner, which Resource's move
	// constructor passes; the limit goes with it, so the old owner's destructor returns
	// nothing. (Before 2026-10-11 the implicit copy served a Resource move: it copied every
	// pooled state, left the pointer on the old resource and returned the limit twice.)
	SlotManager(const SlotManager&) = delete;
	SlotManager& operator=(const SlotManager&) = delete;
	SlotManager(SlotManager&& other, Resource<TState>* owner)
		: _resource(owner), slotsById(std::move(other.slotsById)), slotIdsByLastAccess(std::move(other.slotIdsByLastAccess)),
		  slotLastAccessOrderById(std::move(other.slotLastAccessOrderById)), nextSlotId(other.nextSlotId),
		  _saveMemLimit(std::exchange(other._saveMemLimit, 0)), _currentSaveMem(std::exchange(other._currentSaveMem, 0)),
		  _pool(std::move(other._pool)), _pooledMem(std::exchange(other._pooledMem, 0)), _maxPooledStates(other._maxPooledStates)
	{
	}

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

	// Erased and evicted states are kept here as the map's own nodes (std::map::extract) and
	// handed to the next CreateSlot, which links the node back in under its new id. The slot
	// manager therefore never copies, moves or reallocates a state, whatever its shape (the
	// maintainer, 2026-10-11: a state is copied by the resource's save and load only); a save
	// into a recycled state is that one copy. Before the pool every save allocated and
	// zero-filled fresh buffers (a full LibSm64 save was 8x its load; ROADMAP 3.9), and
	// before the nodes a pooled state was moved in and out, which copied a flat state whole
	// (the corner's 41 KB, three times per save; ROADMAP 4.13). Pooled memory counts toward
	// _saveMemLimit; the pool is bounded so idle memory does not pile up.
	std::vector<typename std::map<int64_t, TState>::node_type> _pool;
	int64_t _pooledMem = 0;
	size_t _maxPooledStates = 32;
};

//Include template method implementations
#include "tasfw/SlotManager.t.hpp"

#endif
