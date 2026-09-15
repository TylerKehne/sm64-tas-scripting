#pragma once
#ifndef RESOURCE_H
#error "Resource.t.hpp should only be included by Resource.hpp"
#else

template <class TState>
int64_t Resource<TState>::SaveState()
{
	auto start = get_time();
	int64_t slotId = slotManager.CreateSlot();
	work.saveCycles += get_time() - start;

	work.saves++;

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

	work.loadCycles += get_time() - start;

	work.loads++;
}

template <class TState>
void Resource<TState>::FrameAdvance()
{
	auto start = get_time();
	advance();
	work.advanceCycles += get_time() - start;

	work.frameAdvances++;
}

template <class TState>
bool Resource<TState>::shouldSave(int64_t estFrameAdvances) const
{
	if (!useCostModel || estFrameAdvances == 0)
		return false;

	if (work.saves == 0 || work.frameAdvances == 0 || estFrameAdvances < 0)
		return true;

	double estTimeToSave = double(work.saveCycles) / work.saves;
	double estTimeToFrameAdvance =
		(double(work.advanceCycles) / work.frameAdvances) * estFrameAdvances;

	return estTimeToSave < estTimeToFrameAdvance;
}

template <class TState>
bool Resource<TState>::shouldLoad(int64_t framesAhead) const
{
	if (!useCostModel || framesAhead == 0)
		return false;

	if (work.loads == 0 || work.frameAdvances == 0 || framesAhead < 0)
		return true;

	double estTimeToLoad = double(work.loadCycles) / work.loads;
	double estTimeToFrameAdvance =
		(double(work.advanceCycles) / work.frameAdvances) * framesAhead;

	return estTimeToLoad < estTimeToFrameAdvance;
}

template <class TState>
void Resource<TState>::SaveStart(int64_t frame)
{
	save(startSave);
	initialFrame = frame;
}

#endif
