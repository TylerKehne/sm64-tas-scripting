#pragma once
#ifndef SCRIPT_H
#error "Script.t.hpp should only be included by Script.hpp"
#else

#include <chrono>

template <derived_from_specialization_of<Resource> TResource>
SlotHandle<TResource>::~SlotHandle()
{
	if (slotId != -1)
		resource->slotManager.EraseSlot(slotId);
}

template <derived_from_specialization_of<Resource> TResource>
bool SlotHandle<TResource>::isValid()
{
	//Start save handle is always valid
	if (resource && slotId == -1)
		return true;

	return resource->slotManager.isValid(slotId);
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::Initialize(Script<TResource>* parentScript)
{
	_parentScript = parentScript;

	BaseStatus[0];
	saveBank[0];
	frameCounter[0];
	saveCache[0];
	inputsCache[0];
	loadTracker[0];

	if (_parentScript)
	{
		resource = _parentScript->resource;
		_rootScript = _parentScript->_rootScript;
	}
	else
		_rootScript = this;

	_rootScript->PushTrackedStatesContainer(this, 0);

	startSaveHandle = SlotHandle<TResource>(resource, -1);
	_initialFrame = GetCurrentFrame();
}

template <derived_from_specialization_of<Resource> TResource>
bool Script<TResource>::Run()
{
	// Validate
	auto start = get_time();
	BaseStatus[_adhocLevel].validated = ExecuteAdhoc([&] { return validation(); }).executed;
	auto finish = get_time();

	BaseStatus[_adhocLevel].validationDuration = finish - start;

	if (!BaseStatus[_adhocLevel].validated)
		return false;

	// Execute
	start = get_time();
	BaseStatus[_adhocLevel].executed = ModifyAdhoc([&] { return execution(); }).executed;
	finish = get_time();

	BaseStatus[_adhocLevel].executionDuration = finish - start;

	if (!BaseStatus[_adhocLevel].executed)
		return false;

	// Assert
	start = get_time();
	BaseStatus[_adhocLevel].asserted = ExecuteAdhoc([&] { return assertion(); }).executed;
	finish = get_time();

	BaseStatus[_adhocLevel].assertionDuration = finish - start;

	return BaseStatus[_adhocLevel].asserted;
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::CopyVec3f(Vec3f dest, Vec3f source)
{
	dest[0] = source[0];
	dest[1] = source[1];
	dest[2] = source[2];
}

template <derived_from_specialization_of<Resource> TResource>
uint64_t Script<TResource>::GetCurrentFrame()
{
	return resource->getCurrentFrame();
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::AdvanceFrameRead()
{
	int64_t currentFrame = GetCurrentFrame();
	SetInputs(GetInputs(currentFrame++));
	resource->FrameAdvance();
	BaseStatus[_adhocLevel].nFrameAdvances++;

	_rootScript->TrackState(this, GetInputsMetadataAndCache(currentFrame));
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::AdvanceFrameWrite(Inputs inputs)
{
	// Save inputs to diff
	uint64_t currentFrame = GetCurrentFrame();
	BaseStatus[_adhocLevel].m64Diff.frames[currentFrame] = inputs;

	// Erase all saves, cached saves and inputs, tracked loads and frame counters after this point, as well as the cached input on this frame
	inputsCache[_adhocLevel].erase(inputsCache[_adhocLevel].lower_bound(currentFrame), inputsCache[_adhocLevel].end());
	frameCounter[_adhocLevel].erase(frameCounter[_adhocLevel].upper_bound(currentFrame), frameCounter[_adhocLevel].end());
	saveBank[_adhocLevel].erase(saveBank[_adhocLevel].upper_bound(currentFrame), saveBank[_adhocLevel].end());
	saveCache[_adhocLevel].erase(saveCache[_adhocLevel].upper_bound(currentFrame), saveCache[_adhocLevel].end());
	_rootScript->EraseTrackedStates(this, _adhocLevel, currentFrame);

	// Set inputs and advance frame
	SetInputs(inputs);
	resource->FrameAdvance();
	BaseStatus[_adhocLevel].nFrameAdvances++;

	currentFrame++;
	_rootScript->TrackState(this, GetInputsMetadataAndCache(currentFrame));
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::Apply(const M64Diff& m64Diff)
{
	if (m64Diff.frames.empty())
		return;

	uint64_t firstFrame = m64Diff.frames.begin()->first;
	uint64_t lastFrame = m64Diff.frames.rbegin()->first;

	Load(firstFrame);

	// Erase all saves, cached saves, and frame counters after this point
	uint64_t currentFrame = GetCurrentFrame();
	inputsCache[_adhocLevel].erase(inputsCache[_adhocLevel].lower_bound(currentFrame), inputsCache[_adhocLevel].end());
	frameCounter[_adhocLevel].erase(frameCounter[_adhocLevel].upper_bound(currentFrame), frameCounter[_adhocLevel].end());
	saveBank[_adhocLevel].erase(saveBank[_adhocLevel].upper_bound(currentFrame), saveBank[_adhocLevel].end());
	saveCache[_adhocLevel].erase(saveCache[_adhocLevel].upper_bound(currentFrame), saveCache[_adhocLevel].end());
	_rootScript->EraseTrackedStates(this, _adhocLevel, currentFrame);

	while (currentFrame <= lastFrame)
	{
		// Use default inputs if diff doesn't override them
		auto inputs = GetInputs(currentFrame);
		if (m64Diff.frames.contains(currentFrame))
		{
			inputs = m64Diff.frames.at(currentFrame);
			BaseStatus[_adhocLevel].m64Diff.frames[currentFrame] = inputs;
		}

		SetInputs(inputs);
		resource->FrameAdvance();
		BaseStatus[_adhocLevel].nFrameAdvances++;

		currentFrame++;
		_rootScript->TrackState(this, GetInputsMetadataAndCache(currentFrame));
	}
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::ApplyChildDiff(const BaseScriptStatus& status, std::map<int64_t, SlotHandle<TResource>>& childSaveBank, int64_t initialFrame, Script<TResource>* childScript)
{
	//Revert if script was unsuccessful
	if (!status.asserted)
	{
		Revert(initialFrame, status.m64Diff, childSaveBank, childScript);
		return;
	}	

	uint64_t firstFrame;
	uint64_t lastFrame;
	if (!status.m64Diff.frames.empty())
	{
		firstFrame = status.m64Diff.frames.begin()->first;
		lastFrame = status.m64Diff.frames.rbegin()->first;

		// Erase all saves, cached saves, and frame counters after this point
		inputsCache[_adhocLevel].erase(inputsCache[_adhocLevel].lower_bound(firstFrame), inputsCache[_adhocLevel].end());
		frameCounter[_adhocLevel].erase(frameCounter[_adhocLevel].upper_bound(firstFrame), frameCounter[_adhocLevel].end());
		saveBank[_adhocLevel].erase(saveBank[_adhocLevel].upper_bound(firstFrame), saveBank[_adhocLevel].end());
		saveCache[_adhocLevel].erase(saveCache[_adhocLevel].upper_bound(firstFrame), saveCache[_adhocLevel].end());
		_rootScript->EraseTrackedStates(this, _adhocLevel, firstFrame);

		//Apply diff. State is already synced from child script, so no need to update it
		for (uint64_t frame = firstFrame; frame <= lastFrame; frame++)
		{
			if (status.m64Diff.frames.count(frame))
				BaseStatus[_adhocLevel].m64Diff.frames[frame] = status.m64Diff.frames.at(frame);
		}
	}

	//Move child saves to parent because they are still synced
	//If child is ad-hoc script, pop the save bank
	std::move(childSaveBank.begin(), childSaveBank.end(), std::insert_iterator(saveBank[_adhocLevel], saveBank[_adhocLevel].end()));
	if (saveBank.contains(_adhocLevel + 1))
		saveBank.erase(_adhocLevel + 1);

	int childAdhocLevel = this == childScript ? _adhocLevel + 1 : 0; // Ad-hoc script vs. regular script
	_rootScript->MoveSyncedTrackedStates(childScript, childAdhocLevel, this, _adhocLevel);

	if (!status.m64Diff.frames.empty())
		Load(lastFrame + 1); //Forward state to end of diff
	else
		Load(initialFrame);
}

template <derived_from_specialization_of<Resource> TResource>
Inputs Script<TResource>::GetInputs(int64_t frame)
{
	return GetInputsMetadataAndCache(frame).inputs;
}

template <derived_from_specialization_of<Resource> TResource>
M64Diff Script<TResource>::GetInputs(int64_t firstFrame, int64_t lastFrame)
{
	M64Diff diff;
	for (int64_t frame = firstFrame; frame <= lastFrame; frame++)
		diff.frames[frame] = GetInputsMetadata(frame).inputs;

	return diff;
}

template <derived_from_specialization_of<Resource> TResource>
bool Script<TResource>::ExportM64(std::filesystem::path fileName)
{
	return ExportM64(fileName, GetCurrentFrame());
}

template <derived_from_specialization_of<Resource> TResource>
bool Script<TResource>::ExportM64(std::filesystem::path fileName, int64_t maxFrame)
{
	if (maxFrame == 0)
		return false;

	M64 outM64 = M64(fileName);
	for (int64_t frame = 0; frame < maxFrame; frame++)
	{
		outM64.frames[frame] = GetInputsMetadata(frame).inputs;
	}

	return (bool)outM64.save();
}

template <derived_from_specialization_of<Resource> TResource>
InputsMetadata<TResource> Script<TResource>::GetInputsMetadata(int64_t frame)
{
	if (!_parentScript)
		throw std::runtime_error("Failed to get inputs because of missing parent script");

	//State owner determines what frame counter needs to be incremented
	int64_t stateOwnerAdhocLevel = -1;
	bool alreadyFoundInputs = false; //True only if state owner is above inputs owner
	Inputs inputs;

	//Check ad-hoc script hierarchy first, then current script
	for (int64_t adhocLevel = _adhocLevel; adhocLevel >= 0; adhocLevel--)
	{
		if (stateOwnerAdhocLevel == -1)
		{
			if (!BaseStatus[adhocLevel].m64Diff.frames.empty() && static_cast<int64_t>(BaseStatus[adhocLevel].m64Diff.frames.begin()->first) < frame)
			{
				stateOwnerAdhocLevel = adhocLevel;

				// BUGFIX 5/22/23: Failure to return after finding state owner after inputs caused state owner to be set to root
				if (alreadyFoundInputs)
					return InputsMetadata<TResource>(inputs, frame, this, stateOwnerAdhocLevel);
			}
		}

		if (BaseStatus[adhocLevel].m64Diff.frames.contains(frame))
		{
			if (stateOwnerAdhocLevel != -1)
				return InputsMetadata<TResource>(alreadyFoundInputs ? inputs : BaseStatus[adhocLevel].m64Diff.frames[frame], frame, this, stateOwnerAdhocLevel);

			if (!alreadyFoundInputs)
			{
				alreadyFoundInputs = true;
				inputs = BaseStatus[adhocLevel].m64Diff.frames[frame];
			}
		}

		if (inputsCache[adhocLevel].contains(frame))
		{
			InputsMetadata<TResource> metadata = inputsCache[adhocLevel][frame];
			if (stateOwnerAdhocLevel != -1)
			{
				metadata.stateOwner = this;
				metadata.stateOwnerAdhocLevel = stateOwnerAdhocLevel;
			}

			if (alreadyFoundInputs)
				metadata.inputs = inputs;

			return metadata;
		}
	}

	//Then check parent script
	InputsMetadata metadata = _parentScript->GetInputsMetadata(frame);
	if (stateOwnerAdhocLevel != -1)
	{
		metadata.stateOwner = this;
		metadata.stateOwnerAdhocLevel = stateOwnerAdhocLevel;
	}

	if (alreadyFoundInputs)
		metadata.inputs = inputs;

	return metadata;

	//This should be impossible
	throw std::runtime_error("Failed to get inputs, possible error in recursion logic.");
	return InputsMetadata<TResource>();
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
InputsMetadata<TResource> TopLevelScript<TResource, TStateTracker>::GetInputsMetadata(int64_t frame)
{
	//State owner determines what frame counter needs to be incremented
	int64_t stateOwnerAdhocLevel = -1;
	bool alreadyFoundInputs = false; //True only if state owner is above inputs owner
	Inputs inputs;

	//Check ad-hoc script hierarchy first, then current script
	for (int64_t adhocLevel = ScriptFriend<TResource>::GetAdhocLevel(this); adhocLevel >= 0; adhocLevel--)
	{
		if (stateOwnerAdhocLevel == -1)
		{
			if (!ScriptFriend<TResource>::GetBaseStatus(this)[adhocLevel].m64Diff.frames.empty()
				&& static_cast<int64_t>(ScriptFriend<TResource>::GetBaseStatus(this)[adhocLevel].m64Diff.frames.begin()->first) < frame)
			{
				stateOwnerAdhocLevel = adhocLevel;

				// BUGFIX 5/22/23: Failure to return after finding state owner after inputs caused state owner to be set to root
				if (alreadyFoundInputs)
					return InputsMetadata<TResource>(inputs, frame, this, stateOwnerAdhocLevel);
			}
		}

		if (ScriptFriend<TResource>::GetBaseStatus(this)[adhocLevel].m64Diff.frames.contains(frame))
		{
			if (stateOwnerAdhocLevel != -1)
				return InputsMetadata<TResource>(alreadyFoundInputs ? inputs
					: ScriptFriend<TResource>::GetBaseStatus(this)[adhocLevel].m64Diff.frames[frame], frame, this, stateOwnerAdhocLevel);

			if (!alreadyFoundInputs)
			{
				alreadyFoundInputs = true;
				inputs = ScriptFriend<TResource>::GetBaseStatus(this)[adhocLevel].m64Diff.frames[frame];
			}
		}

		if (ScriptFriend<TResource>::GetInputsCache(this)[adhocLevel].contains(frame))
		{
			InputsMetadata<TResource> metadata = ScriptFriend<TResource>::GetInputsCache(this)[adhocLevel][frame];
			if (stateOwnerAdhocLevel != -1)
				metadata.stateOwnerAdhocLevel = stateOwnerAdhocLevel;

			if (alreadyFoundInputs)
				metadata.inputs = inputs;

			return metadata;
		}
	}

	if (stateOwnerAdhocLevel == -1)
		stateOwnerAdhocLevel = 0;

	//Return this if inputs have been found but state owner is root
	if (alreadyFoundInputs)
		return InputsMetadata<TResource>(inputs, frame, this, 0);

	//Then check actual m64.
	//For the purposes of the frame counter, mark as adhoc level 0.
	if (_m64->frames.count(frame))
		return InputsMetadata<TResource>(_m64->frames[frame], frame, this, stateOwnerAdhocLevel, InputsMetadata<TResource>::InputsSource::ORIGINAL);

	//Default to no input
	//For the purposes of the frame counter, mark as adhoc level 0.
	return InputsMetadata<TResource>(Inputs(0, 0, 0), frame, this, stateOwnerAdhocLevel, InputsMetadata<TResource>::InputsSource::DEFAULT);
}

template <derived_from_specialization_of<Resource> TResource>
InputsMetadata<TResource> Script<TResource>::GetInputsMetadataAndCache(int64_t frame)
{
	InputsMetadata<TResource> inputs = GetInputsMetadata(frame);
	inputsCache[_adhocLevel][frame] = inputs;
	return inputs;
}

template <derived_from_specialization_of<Resource> TResource>
uint64_t Script<TResource>::GetFrameCounter(InputsMetadata<TResource> cachedInputs)
{
	if (!cachedInputs.stateOwner->frameCounter[cachedInputs.stateOwnerAdhocLevel].contains(cachedInputs.frame))
		cachedInputs.stateOwner->frameCounter[cachedInputs.stateOwnerAdhocLevel][cachedInputs.frame] = 0;

	return cachedInputs.stateOwner->frameCounter[cachedInputs.stateOwnerAdhocLevel][cachedInputs.frame];
}

template <derived_from_specialization_of<Resource> TResource>
uint64_t Script<TResource>::IncrementFrameCounter(InputsMetadata<TResource> cachedInputs)
{
	if (!cachedInputs.stateOwner->frameCounter[cachedInputs.stateOwnerAdhocLevel].contains(cachedInputs.frame))
		cachedInputs.stateOwner->frameCounter[cachedInputs.stateOwnerAdhocLevel][cachedInputs.frame] = 0;

	//Return value AFTER incrementing
	return ++cachedInputs.stateOwner->frameCounter[cachedInputs.stateOwnerAdhocLevel][cachedInputs.frame];
}

template <derived_from_specialization_of<Resource> TResource>
SaveMetadata<TResource> Script<TResource>::GetLatestSave(int64_t frame)
{
	if (resource->initialFrame > frame)
		throw std::runtime_error("Error: attempted to load frame prior to initial frame");

	//Check ad-hoc script hierarchy first, then current script
	int64_t earlyFrame = frame;
	SaveMetadata<TResource> bestSave;
	for (int64_t adhocLevel = _adhocLevel; adhocLevel >= 0; adhocLevel--)
	{
		//Get most recent save in script
		auto save = saveBank[adhocLevel].empty() || earlyFrame < saveBank[adhocLevel].begin()->first
			? saveBank[adhocLevel].end()
			: std::prev(saveBank[adhocLevel].upper_bound(earlyFrame));

		//Verify save exists and select the more recent save
		if (save != saveBank[adhocLevel].end())
		{
			if (!save->second.isValid())
				saveBank[adhocLevel].erase(save->first);
			else if (save->first >= bestSave.frame)
				bestSave = SaveMetadata<TResource>(this, save->first, adhocLevel);
		}

		//Check for cached save
		auto cachedSave = saveCache[adhocLevel].empty() || earlyFrame < saveCache[adhocLevel].begin()->first
			? saveCache[adhocLevel].end()
			: std::prev(saveCache[adhocLevel].upper_bound(earlyFrame));

		if (cachedSave != saveCache[adhocLevel].end())
		{
			//This is the purpose of caching saves: end recursion when a cached save is found. Boosts performance.
			if (cachedSave->second.IsValid())
			{
				if (cachedSave->first >= bestSave.frame)
				{
					//However, if there was a load between the target frame and the cached save, it may not be optimal and we should continue recursion
					auto loadAfterCachedSave = loadTracker[adhocLevel].lower_bound(cachedSave->first);
					if (loadAfterCachedSave != loadTracker[adhocLevel].end() && *loadAfterCachedSave < frame)
						bestSave = cachedSave->second;
					else
						return cachedSave->second;
				}
			}
			else
				saveCache[adhocLevel].erase(cachedSave->first); // Delete stale cached save	
		}

		// Don't search past start of m64 diff to avoid desync
		earlyFrame = !BaseStatus[adhocLevel].m64Diff.frames.empty()
			? (std::min)(BaseStatus[adhocLevel].m64Diff.frames.begin()->first, (uint64_t)earlyFrame)
			: (std::min)(frame, earlyFrame);

		//If save is not before the start of the diff, we have the best possible save, so return it
		if (bestSave.frame >= 0 && bestSave.frame >= earlyFrame)
			return bestSave;
	}

	//Then check parent script
	if (_parentScript)
	{
		auto ancestorSave = _parentScript->GetLatestSave(earlyFrame);

		//Select the more recent save
		if (ancestorSave.frame >= bestSave.frame)
			return ancestorSave;
	}

	//Return most recent save if it exists
	if (bestSave.frame >= 0)
		return bestSave;

	//Default to initial save
	return SaveMetadata<TResource>(this, _initialFrame, 0, true);
}

template <derived_from_specialization_of<Resource> TResource>
SaveMetadata<TResource> Script<TResource>::GetLatestSaveAndCache(int64_t frame)
{
	SaveMetadata<TResource> save = GetLatestSave(frame);
	saveCache[_adhocLevel][save.frame] = save; // Cache save to save recursion time later

	//Track load to mark cached save as optimal
	if (!loadTracker[_adhocLevel].contains(frame))
		loadTracker[_adhocLevel].insert(frame);

	return save;
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::Load(uint64_t frame)
{
	LoadBase(frame, false);
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::LongLoad(int64_t frame)
{
	int64_t currentFrame = static_cast<int64_t>(GetCurrentFrame());
	if (currentFrame == frame)
		return;

	// Load most recent save at or before frame. Check child saves before
	// parent. If target frame is in future, check if faster to frame advance or load.
	// Also, don't cache as it is unlikely the save will be needed again.
	auto latestSave = GetLatestSave(frame);
	if (frame < currentFrame)
	{
		resource->LoadState(latestSave.GetSlotHandle()->slotId);
		BaseStatus[_adhocLevel].nLoads++;
	}
	else if (latestSave.frame > frame && resource->shouldLoad(latestSave.frame - currentFrame))
		resource->LoadState(latestSave.GetSlotHandle()->slotId);

	// If save is before target frame, play back until frame is reached
	currentFrame = GetCurrentFrame();
	while (currentFrame < frame)
	{
		// Advance frame
		SetInputs(GetInputsMetadata(currentFrame).inputs);
		resource->FrameAdvance();
		BaseStatus[_adhocLevel].nFrameAdvances++;
		currentFrame++;
	}

	// Resume state tracking
	_rootScript->TrackState(this, GetInputsMetadataAndCache(frame));

	// Create a save as it is likely that very many frames were advanced since the most recent one.
	Save();
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::LoadBase(uint64_t frame, bool desync)
{
	uint64_t currentFrame = GetCurrentFrame();
	if (!desync && currentFrame == frame)
		return;

	// Load most recent save at or before frame. Check child saves before
	// parent. If target frame is in future, check if faster to frame advance or load.
	auto latestSave = GetLatestSaveAndCache(frame);
	if (desync || frame < currentFrame)
	{
		resource->LoadState(latestSave.GetSlotHandle()->slotId);
		BaseStatus[_adhocLevel].nLoads++;
	}
	else if (latestSave.frame > static_cast<int64_t>(frame) && resource->shouldLoad(latestSave.frame - currentFrame))
		resource->LoadState(latestSave.GetSlotHandle()->slotId);

	// Run custom state tracker
	currentFrame = GetCurrentFrame();
	_rootScript->TrackState(this, GetInputsMetadataAndCache(currentFrame));

	// If save is before target frame, play back until frame is reached
	uint64_t frameCounter = 0;
	while (currentFrame++ < frame)
	{
		AdvanceFrameRead();

		auto cachedInputs = GetInputsMetadataAndCache(currentFrame);
		frameCounter += IncrementFrameCounter(cachedInputs);

		//Estimate future frame advances from aggregate of historical frame advances on this input segment
		//If it reaches a certain threshold, creating a save is performant
		if (resource->shouldSave(frameCounter))
		{
			SaveMetadata<TResource> cachedSave = cachedInputs.stateOwner->Save(cachedInputs.stateOwnerAdhocLevel);
			saveCache[_adhocLevel][currentFrame] = cachedSave;
			frameCounter = 0;
		}
	}
}

// Load method specifically for Script.Execute() and Script.Modify(), checks for desyncs
template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::Revert(uint64_t frame, const M64Diff& m64, std::map<int64_t, SlotHandle<TResource>>& childSaveBank, Script<TResource>* childScript)
{
	// Check if script altered state
	bool desync = (!m64.frames.empty()) && (m64.frames.begin()->first < GetCurrentFrame());

	auto lastSyncedSave = childSaveBank.end();
	if (!m64.frames.empty() && !childSaveBank.empty())
	{
		auto firstDesyncedSave = childSaveBank.upper_bound(m64.frames.begin()->first);
		if (firstDesyncedSave != childSaveBank.begin())
			lastSyncedSave = std::prev(firstDesyncedSave);
	}

	//Move child saves to parent that are not desynced
	//If child is ad-hoc script, pop the save bank
	std::move(childSaveBank.begin(), lastSyncedSave, std::insert_iterator(saveBank[_adhocLevel], saveBank[_adhocLevel].end()));
	if (saveBank.contains(_adhocLevel + 1))
		saveBank.erase(_adhocLevel + 1);

	int childAdhocLevel = this == childScript ? _adhocLevel + 1 : 0; // Ad-hoc script vs. regular script
	_rootScript->PopTrackedStatesContainer(childScript, childAdhocLevel);

	LoadBase(frame, desync);
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::Rollback(uint64_t frame)
{
	// Roll back diff and savebank to target frame. Note that rollback on diff
	// includes target frame.
	if (!BaseStatus[_adhocLevel].m64Diff.frames.empty())
	{
		int64_t firstFrame = BaseStatus[_adhocLevel].m64Diff.frames.lower_bound(frame)->first;

		BaseStatus[_adhocLevel].m64Diff.frames.erase(
			BaseStatus[_adhocLevel].m64Diff.frames.lower_bound(frame),
			BaseStatus[_adhocLevel].m64Diff.frames.end());

		inputsCache[_adhocLevel].erase(inputsCache[_adhocLevel].lower_bound(firstFrame), inputsCache[_adhocLevel].end());
		frameCounter[_adhocLevel].erase(frameCounter[_adhocLevel].upper_bound(firstFrame), frameCounter[_adhocLevel].end());
		saveBank[_adhocLevel].erase(saveBank[_adhocLevel].upper_bound(firstFrame), saveBank[_adhocLevel].end());
		saveCache[_adhocLevel].erase(saveCache[_adhocLevel].upper_bound(firstFrame), saveCache[_adhocLevel].end());
		_rootScript->EraseTrackedStates(this, _adhocLevel, firstFrame);
	}

	//Desyncs should be impossible for rollback because no inputs are changed prior to frame being loaded
	LoadBase(frame, false);
}

// Same as Rollback, but starts from current frame. Useful for scripts that edit past frames
template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::RollForward(int64_t frame)
{
	// Check if script altered state
	bool desync = (!BaseStatus[_adhocLevel].m64Diff.frames.empty()) && (BaseStatus[_adhocLevel].m64Diff.frames.begin()->first < GetCurrentFrame());

	if (!BaseStatus[_adhocLevel].m64Diff.frames.empty())
	{
		int64_t firstFrame = BaseStatus[_adhocLevel].m64Diff.frames.begin()->first;

		//Roll forward inputs through frame prior to target frame
		auto inputsUpperBound = BaseStatus[_adhocLevel].m64Diff.frames.upper_bound(frame - 1);
		if (inputsUpperBound == BaseStatus[_adhocLevel].m64Diff.frames.begin())
			inputsUpperBound = BaseStatus[_adhocLevel].m64Diff.frames.end();
		else
			inputsUpperBound = std::prev(inputsUpperBound);

		BaseStatus[_adhocLevel].m64Diff.frames.erase(BaseStatus[_adhocLevel].m64Diff.frames.begin(), inputsUpperBound);

		inputsCache[_adhocLevel].erase(inputsCache[_adhocLevel].lower_bound(firstFrame), inputsCache[_adhocLevel].end());
		frameCounter[_adhocLevel].erase(frameCounter[_adhocLevel].upper_bound(firstFrame), frameCounter[_adhocLevel].end());
		saveBank[_adhocLevel].erase(saveBank[_adhocLevel].upper_bound(firstFrame), saveBank[_adhocLevel].end());
		saveCache[_adhocLevel].erase(saveCache[_adhocLevel].upper_bound(firstFrame), saveCache[_adhocLevel].end());
		_rootScript->EraseTrackedStates(this, _adhocLevel, firstFrame);
	}

	LoadBase(frame, desync);
}

// Load and clear diff and savebank
template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::Restore(int64_t frame)
{
	// Check if script altered state
	bool desync = (!BaseStatus[_adhocLevel].m64Diff.frames.empty()) && (BaseStatus[_adhocLevel].m64Diff.frames.begin()->first < GetCurrentFrame());

	// Clear diff, frame counter and savebank
	if (!BaseStatus[_adhocLevel].m64Diff.frames.empty())
	{
		int64_t firstFrame = BaseStatus[_adhocLevel].m64Diff.frames.begin()->first;

		BaseStatus[_adhocLevel].m64Diff.frames.erase(
			BaseStatus[_adhocLevel].m64Diff.frames.lower_bound(frame),
			BaseStatus[_adhocLevel].m64Diff.frames.end());

		inputsCache[_adhocLevel].erase(inputsCache[_adhocLevel].lower_bound(firstFrame), inputsCache[_adhocLevel].end());
		frameCounter[_adhocLevel].erase(frameCounter[_adhocLevel].upper_bound(firstFrame), frameCounter[_adhocLevel].end());
		saveBank[_adhocLevel].erase(saveBank[_adhocLevel].upper_bound(firstFrame), saveBank[_adhocLevel].end());
		saveCache[_adhocLevel].erase(saveCache[_adhocLevel].upper_bound(firstFrame), saveCache[_adhocLevel].end());
		_rootScript->EraseTrackedStates(this, _adhocLevel, firstFrame);
	}

	LoadBase(frame, desync);
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::Save()
{
	int64_t currentFrame = GetCurrentFrame();
	auto inputsMetadata = GetInputsMetadata(currentFrame);
	saveCache[_adhocLevel][currentFrame] = inputsMetadata.stateOwner->Save(inputsMetadata.stateOwnerAdhocLevel);
}

//Internal version of Save() that specifies adhoc level, that can be called by a child script
template <derived_from_specialization_of<Resource> TResource>
SaveMetadata<TResource> Script<TResource>::Save(int64_t adhocLevel)
{
	//Desyncs should always clear future saves, so if a save already exists there is no need to overwrite it
	int64_t currentFrame = GetCurrentFrame();
	if (!saveBank[adhocLevel].contains(currentFrame))
	{
		saveBank[adhocLevel].emplace(
			std::piecewise_construct,
			std::forward_as_tuple(currentFrame),
			std::forward_as_tuple(resource, resource->SaveState()));
		BaseStatus[adhocLevel].nSaves++;
	}

	//Return metadata for caching
	return SaveMetadata<TResource>(this, currentFrame, adhocLevel);
}

//Do a cost-benefit analysis to decide whether a save should be created
//CBA is only for creating a save in the current script on tthe current frame
template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::OptionalSave()
{
	//Integrate frame counter, saving only if threshold is reached
	int64_t currentFrame = GetCurrentFrame();
	int64_t latestSaveFrame = GetLatestSaveAndCache(currentFrame).frame;
	uint64_t frameCounter = 0;
	for (int64_t frame = latestSaveFrame + 1; frame <= currentFrame; frame++)
	{
		auto cachedInputs = GetInputsMetadataAndCache(frame);
		frameCounter += GetFrameCounter(cachedInputs);

		if (resource->shouldSave(frameCounter / 2))
		{
			//Create save at the current frame in current frame state owner
			SaveMetadata<TResource> cachedSave = cachedInputs.stateOwner->Save(cachedInputs.stateOwnerAdhocLevel);
			saveCache[_adhocLevel][currentFrame] = cachedSave;
			break;
		}
	}
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::DeleteSave(int64_t frame, int64_t adhocLevel)
{
	saveBank[adhocLevel].erase(frame);
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::SetInputs(Inputs inputs)
{
	uint16_t* buttonDllAddr = (uint16_t*)resource->addr("gControllerPads");
	buttonDllAddr[0] = inputs.buttons;

	int8_t* xStickDllAddr = (int8_t*)resource->addr("gControllerPads") + 2;
	xStickDllAddr[0] = inputs.stick_x;

	int8_t* yStickDllAddr = (int8_t*)resource->addr("gControllerPads") + 3;
	yStickDllAddr[0] = inputs.stick_y;
}

// Only checks base diff, i.e. ad-hoc level 0
template <derived_from_specialization_of<Resource> TResource>
bool Script<TResource>::IsDiffEmpty()
{
	return BaseStatus[0].m64Diff.frames.empty();
}

template <derived_from_specialization_of<Resource> TResource>
M64Diff Script<TResource>::GetDiff()
{
	return BaseStatus[_adhocLevel].m64Diff;
}

template <derived_from_specialization_of<Resource> TResource>
M64Diff Script<TResource>::GetBaseDiff()
{
	return BaseStatus[0].m64Diff;
}

template <derived_from_specialization_of<Resource> TResource>
AdhocBaseScriptStatus Script<TResource>::ExecuteAdhoc(AdhocScript auto adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	BaseScriptStatus status = ExecuteAdhocBase(adhocScript);
	Revert(initialFrame, status.m64Diff, saveBank[_adhocLevel + 1], this);

	return AdhocBaseScriptStatus(status);
}

template <derived_from_specialization_of<Resource> TResource>
template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
AdhocScriptStatus<TAdhocCustomScriptStatus> Script<TResource>::ExecuteAdhoc(F adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
	BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });
	Revert(initialFrame, baseStatus.m64Diff, saveBank[_adhocLevel + 1], this);

	return AdhocScriptStatus<TAdhocCustomScriptStatus>(baseStatus, customStatus);
}

template <derived_from_specialization_of<Resource> TResource>
AdhocBaseScriptStatus Script<TResource>::ModifyAdhoc(AdhocScript auto adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	auto status = ExecuteAdhocBase(adhocScript);
	ApplyChildDiff(status, saveBank[_adhocLevel + 1], initialFrame, this);

	return AdhocBaseScriptStatus(status);
}

template <derived_from_specialization_of<Resource> TResource>
template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
AdhocScriptStatus<TAdhocCustomScriptStatus> Script<TResource>::ModifyAdhoc(F adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
	BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });
	ApplyChildDiff(baseStatus, saveBank[_adhocLevel + 1], initialFrame, this);

	return AdhocScriptStatus<TAdhocCustomScriptStatus>(baseStatus, customStatus);
}

template <derived_from_specialization_of<Resource> TResource>
template <AdhocScript TAdhocScript>
AdhocBaseScriptStatus Script<TResource>::TestAdhoc(TAdhocScript&& adhocScript)
{
	auto status = ExecuteAdhoc(std::forward<TAdhocScript>(adhocScript));
	status.m64Diff = M64Diff();

	return status;
}

template <derived_from_specialization_of<Resource> TResource>
template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
AdhocScriptStatus<TAdhocCustomScriptStatus> Script<TResource>::TestAdhoc(F&& adhocScript)
{
	auto status = ExecuteAdhoc<TAdhocCustomScriptStatus>(std::forward<F>(adhocScript));
	status.m64Diff = M64Diff();

	return status;
}

template <derived_from_specialization_of<Resource> TResource>
template <typename F>
BaseScriptStatus Script<TResource>::ExecuteAdhocBase(F adhocScript)
{
	//Increment adhoc level
	_adhocLevel++;
	BaseStatus[_adhocLevel];
	saveBank[_adhocLevel];
	frameCounter[_adhocLevel];
	saveCache[_adhocLevel];
	inputsCache[_adhocLevel];
	loadTracker[_adhocLevel];
	_rootScript->PushTrackedStatesContainer(this, _adhocLevel);

	BaseStatus[_adhocLevel].validated = true;

	uint64_t loadStateTimeStart = resource->GetTotalLoadStateTime();
	uint64_t saveStateTimeStart = resource->GetTotalSaveStateTime();
	uint64_t advanceFrameTimeStart = resource->GetTotalFrameAdvanceTime();

	auto start = std::chrono::high_resolution_clock::now();
	BaseStatus[_adhocLevel].executed = adhocScript();
	auto finish = std::chrono::high_resolution_clock::now();

	BaseStatus[_adhocLevel].loadDuration = resource->GetTotalLoadStateTime() - loadStateTimeStart;
	BaseStatus[_adhocLevel].saveDuration = resource->GetTotalSaveStateTime() - saveStateTimeStart;
	BaseStatus[_adhocLevel].advanceFrameDuration = resource->GetTotalFrameAdvanceTime() - advanceFrameTimeStart;

	BaseStatus[_adhocLevel].executionDuration =
		std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
		.count();

	BaseStatus[_adhocLevel].asserted = BaseStatus[_adhocLevel].executed;

	//Decrement adhoc level, revert state and return status
	//NOTE: saveBank is not popped here as the saves may be moved to the parent.
	//Caller is responsible for popping it.
	BaseScriptStatus status = BaseStatus[_adhocLevel];
	BaseStatus.erase(_adhocLevel);
	frameCounter.erase(_adhocLevel);
	saveCache.erase(_adhocLevel);
	inputsCache.erase(_adhocLevel);
	loadTracker.erase(_adhocLevel);
	_adhocLevel--;

	BaseStatus[_adhocLevel].nLoads += status.nLoads;
	BaseStatus[_adhocLevel].nSaves += status.nSaves;
	BaseStatus[_adhocLevel].nFrameAdvances += status.nFrameAdvances;

	return status;
}

template <derived_from_specialization_of<Resource> TResource>
SlotHandle<TResource>* SaveMetadata<TResource>::GetSlotHandle()
{
	if (!script)
		return nullptr;

	if (isStartSave)
		return &script->startSaveHandle;

	if (script->saveBank.size() <= static_cast<uint64_t>(adhocLevel))
		return nullptr;

	if (!script->saveBank[adhocLevel].contains(frame))
		return nullptr;

	return &script->saveBank[adhocLevel].find(frame)->second;
}

template <derived_from_specialization_of<Resource> TResource>
bool SaveMetadata<TResource>::IsValid()
{
	SlotHandle<TResource>* slotHandle = GetSlotHandle();
	if (slotHandle == nullptr)
		return false;

	if (!slotHandle->isValid())
	{
		script->saveBank[adhocLevel].erase(frame);
		return false;
	}

	return true;
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
void TopLevelScript<TResource, TStateTracker>::TrackState(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata)
{
	if constexpr (std::is_same<TStateTracker, DefaultStateTracker<TResource>>::value)
		return;

	if (!ScriptFriend<TResource>::IsStateTracker(currentScript))
		GetTrackedStateInternal(currentScript, inputsMetadata);
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
typename TStateTracker::CustomScriptStatus TopLevelScript<TResource, TStateTracker>
	::GetTrackedStateInternal(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata)
{
	if constexpr (std::is_same<TStateTracker, DefaultStateTracker<TResource>>::value)
		return typename TStateTracker::CustomScriptStatus();

	if (trackedStates[inputsMetadata.stateOwner][inputsMetadata.stateOwnerAdhocLevel].contains(inputsMetadata.frame))
		return trackedStates[inputsMetadata.stateOwner][inputsMetadata.stateOwnerAdhocLevel][inputsMetadata.frame];

	uint64_t currentFrame = ScriptFriend<TResource>::GetCurrentFrame(currentScript);

	auto status = ScriptFriend<TResource>::ExecuteStateTracker<TStateTracker>(inputsMetadata.frame, currentScript, stateTrackerFactory);
	if (!status.asserted)
		return typename TStateTracker::CustomScriptStatus();

	auto state = (typename TStateTracker::CustomScriptStatus)status;
	trackedStates[inputsMetadata.stateOwner][inputsMetadata.stateOwnerAdhocLevel][inputsMetadata.frame] = state;

	return state;
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
void TopLevelScript<TResource, TStateTracker>::PushTrackedStatesContainer(Script<TResource>* currentScript, int adhocLevel)
{
	if constexpr (std::is_same<TStateTracker, DefaultStateTracker<TResource>>::value)
		return;

	trackedStates[currentScript][adhocLevel];
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
void TopLevelScript<TResource, TStateTracker>::PopTrackedStatesContainer(Script<TResource>* currentScript, int adhocLevel)
{
	if constexpr (std::is_same<TStateTracker, DefaultStateTracker<TResource>>::value)
		return;

	if (adhocLevel == 0)
		trackedStates.erase(currentScript);
	else
		trackedStates[currentScript].erase(adhocLevel);
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
void TopLevelScript<TResource, TStateTracker>::MoveSyncedTrackedStates(Script<TResource>* sourceScript, int sourceAdhocLevel, Script<TResource>* destScript, int destAdhocLevel)
{
	if constexpr (std::is_same<TStateTracker, DefaultStateTracker<TResource>>::value)
		return;

	std::move(trackedStates[sourceScript][sourceAdhocLevel].begin(), trackedStates[sourceScript][sourceAdhocLevel].end(),
		std::insert_iterator(trackedStates[destScript][destAdhocLevel], trackedStates[destScript][destAdhocLevel].end()));

	// If source was an ad-hoc script, pop the save bank
	if (trackedStates[destScript].contains(destAdhocLevel + 1))
		trackedStates[destScript].erase(destAdhocLevel + 1);
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
void TopLevelScript<TResource, TStateTracker>::EraseTrackedStates(Script<TResource>* currentScript, int adhocLevel, int64_t firstFrame)
{
	if constexpr (std::is_same<TStateTracker, DefaultStateTracker<TResource>>::value)
		return;

	trackedStates[currentScript][adhocLevel].erase(
		trackedStates[currentScript][adhocLevel].upper_bound(firstFrame), trackedStates[currentScript][adhocLevel].end());
}

#endif
