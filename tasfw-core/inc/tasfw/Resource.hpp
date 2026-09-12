#pragma once

#include <cstring>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

#include <tasfw/Inputs.hpp>
#include <tasfw/SharedLib.hpp>

#include <cstdlib>
#include <chrono>

//#include <tasfw/Script.hpp>

#ifndef RESOURCE_H
#define RESOURCE_H

template <class TState>
class Resource;

template <class TState>
class ImportedSave
{
public:
	TState state;
	int64_t initialFrame;

	ImportedSave(TState state, int64_t initialFrame) : state(state), initialFrame(initialFrame) {}
};

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
	uint64_t nPoolReuses = 0;

	SlotManager(Resource<TState>* resource) : _resource(resource) { }

	int64_t CreateSlot();
	void EraseOldestSlot();
	void EraseSlot(int64_t slotId);
	void LoadSlot(int64_t slotId);
	bool isValid(int64_t slotId);
	size_t PooledStates() const { return _pool.size(); }
};

// Interface for the state machine that represents the game. Can either contain the state machine itself, or be a client to an external state machine.
template <class TState>
class Resource
{
public:
	uint64_t _totalFrameAdvanceTime = 0;
	uint64_t _totalLoadStateTime = 0;
	uint64_t _totalSaveStateTime = 0;
	uint64_t nFrameAdvances = 0;
	uint64_t nLoadStates = 0;
	uint64_t nSaveStates = 0;

	TState startSave = TState();
	int64_t initialFrame = -1;
	SlotManager<TState> slotManager = SlotManager<TState>(this);

	// When false, shouldSave/shouldLoad answer false: no automatic saves during replays and
	// no loading ahead, only explicit saves and replays. The cost model's decisions depend on
	// measured timings, so this is the switch that makes a run timing-independent; it costs
	// performance and exists for diagnosis (ROADMAP 4.5) and tests.
	bool useCostModel = true;

	Resource() = default;
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
	uint64_t GetTotalSaveStateTime();
	uint64_t GetTotalLoadStateTime();
	uint64_t GetTotalFrameAdvanceTime();

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
	// Symbol lookup for scripts. Not for per-frame use: LibSm64 resolves through the OS
	// loader. Cache the result where a script needs it every frame.
	virtual void* addr(const char* symbol) const = 0;
	virtual std::size_t getStateSize(const TState& state) const = 0;
	//TODO: make this resource-agnostic
	virtual uint32_t getCurrentFrame() const = 0;
};

//Include template method implementations
#include "tasfw/Resource.t.hpp"

#endif