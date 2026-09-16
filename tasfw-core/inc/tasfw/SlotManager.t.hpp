#pragma once
#ifndef SLOTMANAGER_H
#error "SlotManager.t.hpp should only be included by SlotManager.hpp"
#else

#include <algorithm>

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
				_resource->work.poolReuses++;
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
			_resource->work.slotsLiveMax = (std::max)(_resource->work.slotsLiveMax, uint64_t(slotsById.size()));
			_resource->work.slotBytesMax = (std::max)(_resource->work.slotBytesMax, uint64_t(_currentSaveMem));

			return slotId;
		}

		if (slotsById.size() == 0)
			throw std::runtime_error("Not enough resource slot memory allocated");

		// If save memory is full, remove the least recently touched save (into the pool) and try again
		_resource->work.evictions++;
		EraseOldestSlot();
	}
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
void SlotManager<TState>::EraseSlot(int64_t slotId)
{
	auto slot = slotsById.find(slotId);
	if (slot == slotsById.end())
		return;

	// Its contents are dead, whether pooled or freed below.
	if constexpr (requires { slot->second.dispose(); })
		slot->second.dispose();
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
bool SlotManager<TState>::isValid(int64_t slotId) const
{
	return slotsById.contains(slotId);
}

#endif
