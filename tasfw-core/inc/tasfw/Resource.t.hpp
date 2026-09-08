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
		// A pooled state is reused without growing memory, so it is always admitted. A fresh
		// state is admitted while live plus pooled memory plus one average slot fits.
		bool reuse = !_pool.empty();
		int64_t additionalMem = slotsById.empty() ? 0 : _currentSaveMem / slotsById.size();
		if (reuse || _currentSaveMem + _pooledMem + additionalMem <= _saveMemLimit)
		{
			//NOTE: IDs/Order will not overflow on realistic timescales
			int64_t slotId = nextSlotId++;
			if (nextSlotId == 1)
				throw std::runtime_error("Max slot id exceeded.");

			TState* slot;
			if (reuse)
			{
				_pooledMem -= _resource->getStateSize(_pool.back());
				slot = &slotsById.emplace(slotId, std::move(_pool.back())).first->second;
				_pool.pop_back();
				nPoolReuses++;
			}
			else
				slot = &slotsById.emplace(slotId, TState()).first->second;

			//NOTE: Order will not overflow on realistic timescales
			int64_t newSlotOrder = slotIdsByLastAccess.size() == 0 ? 0 : std::prev(slotIdsByLastAccess.end())->first + 1;
			if (slotIdsByLastAccess.size() != 0 && newSlotOrder == 0)
				throw std::runtime_error("Max slot touches exceeded.");

			slotIdsByLastAccess[newSlotOrder] = slotId;
			slotLastAccessOrderById[slotId] = newSlotOrder;

			//Save memory into slot (a recycled state already has its buffers sized)
			_resource->save(*slot);
			_currentSaveMem += _resource->getStateSize(*slot);

			return slotId;
		}

		if (slotsById.size() == 0)
			throw std::runtime_error("Not enough resource slot memory allocated");

		// If save memory is full, remove the earliest save (into the pool) and try again
		EraseOldestSlot();
	}
}

template <class TState>
void SlotManager<TState>::EraseSlot(int64_t slotId)
{
	auto slot = slotsById.find(slotId);
	if (slot == slotsById.end())
		return;

	int64_t size = _resource->getStateSize(slot->second);
	_currentSaveMem -= size;
	if (_pool.size() < _maxPooledStates)
	{
		_pool.push_back(std::move(slot->second));
		_pooledMem += size;
	}

	int64_t slotOrder = slotLastAccessOrderById[slotId];
	slotsById.erase(slot);
	slotLastAccessOrderById.erase(slotId);
	slotIdsByLastAccess.erase(slotOrder);
}

template <class TState>
void SlotManager<TState>::LoadSlot(int64_t slotId)
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
}

template <class TState>
void SlotManager<TState>::EraseOldestSlot()
{
	int64_t slotId = slotIdsByLastAccess.begin() == slotIdsByLastAccess.end() ? -1 : slotIdsByLastAccess.begin()->second;
	if (slotId == -1)
		return;

	EraseSlot(slotId);
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
		slotManager.LoadSlot(slotId);

	_totalLoadStateTime += get_time() - start;
	
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
uint64_t Resource<TState>::GetTotalSaveStateTime()
{
	return _totalSaveStateTime;
}

template <class TState>
uint64_t Resource<TState>::GetTotalLoadStateTime()
{
	return _totalLoadStateTime;
}

template <class TState>
uint64_t Resource<TState>::GetTotalFrameAdvanceTime()
{
	return _totalFrameAdvanceTime;
}

template <class TState>
bool Resource<TState>::shouldSave(int64_t estFrameAdvances) const
{
	if (estFrameAdvances == 0)
		return false;

	if (nSaveStates == 0 || nFrameAdvances == 0 || estFrameAdvances < 0)
		return true;

	double estTimeToSave = double(_totalSaveStateTime) / nSaveStates;
	double estTimeToFrameAdvance =
		(double(_totalFrameAdvanceTime) / nFrameAdvances) * estFrameAdvances;

	return estTimeToSave < estTimeToFrameAdvance;
}

template <class TState>
bool Resource<TState>::shouldLoad(int64_t framesAhead) const
{
	if (framesAhead == 0)
		return false;

	if (nLoadStates == 0 || nFrameAdvances == 0 || framesAhead < 0)
		return true;

	double estTimeToLoad = double(_totalLoadStateTime) / nLoadStates;
	double estTimeToFrameAdvance =
		(double(_totalFrameAdvanceTime) / nFrameAdvances) * framesAhead;

	return estTimeToLoad < estTimeToFrameAdvance;
}

#endif