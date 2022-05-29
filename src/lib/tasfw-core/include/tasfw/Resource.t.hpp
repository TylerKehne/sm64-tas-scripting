#pragma once
#ifndef RESOURCE_H
#error "Resource.t.hpp should only be included by Resource.hpp"
#else

#include <chrono>

#ifdef _MSC_VER
#include <intrin.h>
#else
// Provides __rdtsc outside MSVC
#include <x86intrin.h>
#endif

#if 1
static inline uint64_t get_time()
{
	return __rdtsc();
}
#else
#include <sys/time.h>
static inline uint64_t get_time()
{
	struct timeval st
	{
	};
	gettimeofday(&st, nullptr);
	return st.tv_sec * 1000000 + st.tv_usec;
}
#endif

template <derived_from_specialization_of<Resource> TResource>
SlotHandle<TResource>::~SlotHandle()
{
	if (slotId != -1)
		resource->slotManager.EraseSlot(slotId);
}

template <derived_from_specialization_of<Resource> TResource>
bool SlotHandle<TResource>::isValid()
{
	return resource->slotManager.isValid(slotId);
}

template <class TState>
bool SlotManager<TState>::isValid(int64_t slotId)
{
	return slotsById.contains(slotId);
}

template <class TState>
int64_t SlotManager<TState>::CreateSlot()
{
	while (true)
	{
		int64_t additionalMem = sizeof(TState);
		if (_resource->_currentSaveMem + additionalMem <= _resource->_saveMemLimit)
		{
			//NOTE: IDs/Order will not overflow on realistic timescales
			int64_t slotId = nextSlotId++;
			if (nextSlotId == 1)
				throw std::runtime_error("Max slot id exceeded.");
			slotsById[slotId] = TState();

			//NOTE: Order will not overflow on realistic timescales
			int64_t newSlotOrder = slotIdsByLastAccess.size() == 0 ? 0 : std::prev(slotIdsByLastAccess.end())->first + 1;
			if (slotIdsByLastAccess.size() != 0 && newSlotOrder == 0)
				throw std::runtime_error("Max slot touches exceeded.");

			slotIdsByLastAccess[newSlotOrder] = slotId;
			slotLastAccessOrderById[slotId] = newSlotOrder;

			//Save memory into slot
			_resource->save(slotsById[slotId]);

			/*
			slotsById[slotId].buf1.resize(_resource->segment[0].length);
			slotsById[slotId].buf2.resize(_resource->segment[1].length);

			int64_t* temp = reinterpret_cast<int64_t*>(_resource->segment[0].address);
			memcpy(slotsById[slotId].buf1.data(), temp, _resource->segment[0].length);

			temp = reinterpret_cast<int64_t*>(_resource->segment[1].address);
			memcpy(slotsById[slotId].buf2.data(), temp, _resource->segment[1].length);
			*/

			_resource->_currentSaveMem += additionalMem;

			return slotId;
		}

		if (slotsById.size() == 0)
			throw std::runtime_error("Not enough resource slot memory allocated");

		// If save memory is full, remove the earliest save and try again
		EraseOldestSlot();
	}
}

template <class TState>
void SlotManager<TState>::EraseSlot(int64_t slotId)
{
	if (slotsById.contains(slotId))
	{
		int64_t slotOrder = slotLastAccessOrderById[slotId];
		slotsById.erase(slotId);
		slotLastAccessOrderById.erase(slotId);
		slotIdsByLastAccess.erase(slotOrder);
		_resource->_currentSaveMem -= sizeof(TState);
	}
}

template <class TState>
void SlotManager<TState>::UpdateSlot(int64_t slotId)
{
	int64_t slotOrder = slotLastAccessOrderById[slotId];
	slotIdsByLastAccess.erase(slotOrder);

	//NOTE: Order will not overflow on realistic timescales
	int64_t newSlotOrder = slotIdsByLastAccess.size() == 0 ? 0 : std::prev(slotIdsByLastAccess.end())->first + 1;
	if (slotIdsByLastAccess.size() != 0 && newSlotOrder == 0)
		throw std::runtime_error("Max slot touches exceeded.");

	slotIdsByLastAccess[newSlotOrder] = slotId;
	slotLastAccessOrderById[slotId] = newSlotOrder;

	//Load slot memory
	_resource->load(slotsById[slotId]);

	/*
	memcpy(_resource->segment[0].address, slotsById[slotId].buf1.data(), _resource->segment[0].length);
	memcpy(_resource->segment[1].address, slotsById[slotId].buf2.data(), _resource->segment[1].length);
	*/
}

template <class TState>
void SlotManager<TState>::EraseOldestSlot()
{
	int64_t slotId = slotIdsByLastAccess.begin() == slotIdsByLastAccess.end() ? -1 : slotIdsByLastAccess.begin()->second;
	if (slotId == -1)
		return;
}

template <class TState>
int64_t Resource<TState>::SaveState()
{
	auto start = get_time();
	int64_t slotId = slotManager.CreateSlot();
	_totalSaveStateTime += get_time() - start;

	nSaveStates++;

	return slotId;
}

template <class TState>
void Resource<TState>::LoadState(int64_t slotId)
{
	uint64_t start = get_time();

	if (slotId == -1)
		load(startSave);
	else
		slotManager.UpdateSlot(slotId);

	_totalLoadStateTime = get_time() - start;

	nLoadStates++;
}

template <class TState>
void Resource<TState>::FrameAdvance()
{
	auto start = get_time();

	advance();

	_totalFrameAdvanceTime += get_time() - start;
	nFrameAdvances++;
}

template <class TState>
bool Resource<TState>::shouldSave(int64_t estFrameAdvances) const
{
	if (nSaveStates == 0 || estFrameAdvances < 0)
		return true;

	double estTimeToSave = double(_totalSaveStateTime) / nSaveStates;
	double estTimeToFrameAdvance =
		(double(_totalFrameAdvanceTime) / nFrameAdvances) * estFrameAdvances;

	return estTimeToSave < estTimeToFrameAdvance;
}

template <class TState>
bool Resource<TState>::shouldLoad(int64_t framesAhead) const
{
	if (nLoadStates == 0 || framesAhead < 0)
		return true;

	double estTimeToLoad = double(_totalLoadStateTime) / nLoadStates;
	double estTimeToFrameAdvance =
		(double(_totalFrameAdvanceTime) / nFrameAdvances) * framesAhead;

	return estTimeToLoad < estTimeToFrameAdvance;
}

#endif