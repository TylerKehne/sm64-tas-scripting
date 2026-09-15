#pragma once

#include <atomic>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <tasfw/Inputs.hpp>
#include <tasfw/SharedLib.hpp>

#include <cstdlib>

//#include <tasfw/Script.hpp>

#ifndef RESOURCE_H
#define RESOURCE_H

template <class TState>
class Resource;

// Process-wide savestate memory (ROADMAP 3.5): a budget the application sets once, before
// it creates resources, and the balance left of it. A resource subtracts its limit from the
// balance when it is created, or throws if the balance is too low, and adds it back when it
// dies. No budget, the default: no limit.
struct SlotBudget
{
	static inline std::atomic<int64_t> budget = 0;
	static inline std::atomic<int64_t> balance = 0;

	static void Set(int64_t bytes)
	{
		balance += bytes - budget;
		budget = bytes;
	}
	static void Take(int64_t bytes)
	{
		if (budget == 0)
			return;
		if (balance.fetch_sub(bytes) < bytes)
		{
			balance += bytes;
			throw std::runtime_error("savestate budget too low for another resource (SlotBudget; resources.savestateBudgetMB in the pipeline)");
		}
	}
	static void Give(int64_t bytes)
	{
		if (budget != 0)
			balance += bytes;
	}
};

// What a resource has done since it was constructed and what it cost, in rdtsc cycles, the
// framework's one unit for every duration (ROADMAP 3.6). Script snapshots the cycle fields
// around a child to fill its status, the perf suite and bitfs-turn read a snapshot per
// workload and per stage, and the cost model (shouldSave / shouldLoad) reads the averages.
struct ResourceWork
{
	uint64_t frameAdvances = 0;
	uint64_t saves = 0;
	uint64_t loads = 0;
	uint64_t advanceCycles = 0;
	uint64_t saveCycles = 0;
	uint64_t loadCycles = 0;
	// The slot manager: the most slots and bytes live at once, saves that recycled a pooled
	// state instead of allocating, and slots evicted to stay under the memory limit.
	uint64_t slotsLiveMax = 0;
	uint64_t slotBytesMax = 0;
	uint64_t poolReuses = 0;
	uint64_t evictions = 0;

	// The work between an earlier snapshot and this one. The two maxima are not differences;
	// this snapshot's values are kept.
	ResourceWork operator-(const ResourceWork& earlier) const
	{
		ResourceWork d = *this;
		d.frameAdvances -= earlier.frameAdvances;
		d.saves -= earlier.saves;
		d.loads -= earlier.loads;
		d.advanceCycles -= earlier.advanceCycles;
		d.saveCycles -= earlier.saveCycles;
		d.loadCycles -= earlier.loadCycles;
		d.poolReuses -= earlier.poolReuses;
		d.evictions -= earlier.evictions;
		return d;
	}
};

template <class TState>
class ImportedSave
{
public:
	TState state;
	int64_t initialFrame;

	ImportedSave(TState state, int64_t initialFrame) : state(state), initialFrame(initialFrame) {}
};

// Holds the saved states. A state's contents are written by Resource::save and die when its
// slot is erased, whether the state is then pooled for reuse or freed; a state type that
// holds a reference to something outside itself (LibSm64Mem's baseline) defines dispose()
// and EraseSlot calls it at that point, resolved at compile time like LevelStack's Reset().
template <class TState>
class SlotManager
{
public:
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
};

// Interface for the state machine that represents the game. Can either contain the state machine itself, or be a client to an external state machine.
template <class TState>
class Resource
{
public:
	// Counts and cycles of every FrameAdvance, SaveState and LoadState, plus the slot
	// manager's own counters. Read it; the resource and its slot manager write it.
	ResourceWork work;

	TState startSave = TState();
	int64_t initialFrame = -1;
	SlotManager<TState> slotManager = SlotManager<TState>(this);

	// When false, shouldSave/shouldLoad answer false: no automatic saves during replays and
	// no loading ahead, only explicit saves and replays. The cost model's decisions depend on
	// measured timings, so this is the switch that makes a run timing-independent; it costs
	// performance and exists for diagnosis (ROADMAP 4.5) and tests.
	bool useCostModel = true;

	// A resource is built with its savestate limit, which the base takes from the process
	// budget (SlotBudget); derived resources pass the number and never touch the slot manager.
	explicit Resource(int64_t savestateLimit) { slotManager.SetLimit(savestateLimit); }
	// Resources are polymorphic; a derived one deleted through std::unique_ptr<Derived> is fine,
	// but clang (-Wdelete-non-abstract-non-virtual-dtor) is right that a base with virtual
	// functions should own its destructor. Not a hot path.
	virtual ~Resource() = default;

	Resource(const Resource<TState>&) = delete;
	Resource(Resource<TState>&&) = default;
	Resource& operator= (const Resource<TState>&) = delete;

	int64_t SaveState();
	void LoadState(int64_t slotId);
	void FrameAdvance();
	bool shouldSave(int64_t framesSinceLastSave) const;
	bool shouldLoad(int64_t framesAhead) const;

	// A saved state's slot, from outside: erase it (the state's dispose() runs if it has one),
	// or ask whether it still exists (it may have been evicted).
	void DisposeState(int64_t slotId) { slotManager.EraseSlot(slotId); }
	bool HasState(int64_t slotId) const { return slotManager.isValid(slotId); }

	//Return a conversion of the current state for the user to do with as they like (e.g. pass to a new top-level script)
	//Requires a matching constructor in the return type that will convert TState to the return type
	template <class UState, typename... Us>
		requires(std::constructible_from<UState, const Resource<TState>&, Us...>)
	UState State(Us&&... params)
	{
		return UState(*this, std::forward<Us>(params)...);
	}

	virtual void save(TState& state) const = 0;
	virtual void load(const TState& state) = 0;
	virtual void advance() = 0;
	// Write the controller inputs that the next advance() will see. Called once per frame;
	// implementations must not do any lookup here (cache pointers at construction).
	virtual void setInputs(const Inputs& inputs) = 0;
	// Symbol lookup for scripts. LibSm64 resolves a name through the OS loader once and
	// answers from its own table after, so a script may ask per execution; one that needs
	// a symbol every frame still caches the pointer.
	virtual void* addr(const char* symbol) const = 0;
	virtual std::size_t getStateSize(const TState& state) const = 0;
	//TODO: make this resource-agnostic
	virtual uint32_t getCurrentFrame() const = 0;
};

//Include template method implementations
#include "tasfw/Resource.t.hpp"

#endif