#pragma once

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
#include <tasfw/ResourceWork.hpp>
#include <tasfw/SlotBudget.hpp>
#include <tasfw/SlotManager.hpp>

#ifndef RESOURCE_H
#define RESOURCE_H

template <class TState>
class ImportedSave
{
public:
	TState state;
	int64_t initialFrame;

	ImportedSave(TState state, int64_t initialFrame) : state(std::move(state)), initialFrame(initialFrame) {}
};

// Interface for the state machine that represents the game. Can either contain the state machine itself, or be a client to an external state machine.
template <class TState>
class Resource
{
public:
	// Counts and cycles of every FrameAdvance, SaveState and LoadState, plus the slot
	// manager's own counters. Read it; the resource and its slot manager write it.
	ResourceWork work;

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

	// The start save: the state a top-level run begins from and returns to. TopLevelScript
	// saves it the first time it runs on a resource, at the frame InitialFrame then reports
	// (-1 until then), and loads it back, uncounted, to reset a resource it imports again;
	// a script's own load of it is LoadState(-1), counted like any load.
	void SaveStart(int64_t frame);
	void LoadStart() { load(startSave); }
	int64_t InitialFrame() const { return initialFrame; }

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

protected:
	// Whether a state being saved is the start save rather than a slot's; what a derived
	// resource may ask about it (LibSm64's dirty mode does not count it as a live slot).
	bool IsStartSave(const TState& state) const { return &state == &startSave; }

private:
	friend class PerfAccess;

	TState startSave = TState();
	int64_t initialFrame = -1;
	SlotManager<TState> slotManager = SlotManager<TState>(this);
};

//Include template method implementations
#include "tasfw/Resource.t.hpp"

#endif
