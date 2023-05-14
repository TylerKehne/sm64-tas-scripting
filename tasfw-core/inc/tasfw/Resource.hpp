#pragma once

#include <cstring>
#include <iostream>
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

	SlotManager(Resource<TState>* resource) : _resource(resource) { }

	int64_t CreateSlot();
	void EraseOldestSlot();
	void EraseSlot(int64_t slotId);
	void LoadSlot(int64_t slotId);
	bool isValid(int64_t slotId);
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
	int64_t initialFrame = 0;
	SlotManager<TState> slotManager = SlotManager<TState>(this);

	Resource() = default;

	Resource(const Resource<TState>&) = delete;
	Resource& operator= (const Resource<TState>&) = delete;

	int64_t SaveState();
	void LoadState(int64_t slotId);
	void FrameAdvance();
	bool shouldSave(int64_t framesSinceLastSave) const;
	bool shouldLoad(int64_t framesAhead) const;

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
	virtual void* addr(const char* symbol) const = 0;
	virtual std::size_t getStateSize(const TState& state) const = 0;
	//TODO: make this resource-agnostic
	virtual uint32_t getCurrentFrame() const = 0;
};

//Include template method implementations
#include "tasfw/Resource.t.hpp"

#endif