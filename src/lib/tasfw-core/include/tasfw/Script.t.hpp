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
		resource = _parentScript->resource;

	startSaveHandle = SlotHandle<TResource>(resource, -1);
	_initialFrame = GetCurrentFrame();
}

template <derived_from_specialization_of<Resource> TResource>
bool Script<TResource>::checkPreconditions()
{
	bool validated = false;

	auto start = std::chrono::high_resolution_clock::now();
	try
	{
		validated = validation();
	}
	catch (std::exception& e)
	{
		BaseStatus[_adhocLevel].validationThrew = true;
	}
	auto finish = std::chrono::high_resolution_clock::now();

	BaseStatus[_adhocLevel].validationDuration =
		std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
		.count();

	// Revert state regardless of validation results
	Restore(_initialFrame);

	BaseStatus[_adhocLevel].validated = validated;
	return validated;
}

template <derived_from_specialization_of<Resource> TResource>
bool Script<TResource>::execute()
{
	bool executed = false;

	auto start = std::chrono::high_resolution_clock::now();
	try
	{
		executed = execution();
	}
	catch (std::exception& e)
	{
		BaseStatus[_adhocLevel].executionThrew = true;
	}
	auto finish = std::chrono::high_resolution_clock::now();

	BaseStatus[_adhocLevel].executionDuration =
		std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
		.count();

	// Revert state if there are any execution errors
	if (!executed)
		Restore(_initialFrame);

	BaseStatus[_adhocLevel].executed = executed;
	return executed;
}

template <derived_from_specialization_of<Resource> TResource>
bool Script<TResource>::checkPostconditions()
{
	bool asserted = false;

	auto start = std::chrono::high_resolution_clock::now();
	try
	{
		asserted = assertion();
	}
	catch (std::exception& e)
	{
		BaseStatus[_adhocLevel].assertionThrew = true;

		// Revert state only if assertion throws exception
		Restore(_initialFrame);
	}
	auto finish = std::chrono::high_resolution_clock::now();

	BaseStatus[_adhocLevel].assertionDuration =
		std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
		.count();

	BaseStatus[_adhocLevel].asserted = asserted;
	return asserted;
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
	SetInputs(GetInputsMetadataAndCache(GetCurrentFrame()).inputs);
	resource->FrameAdvance();
	BaseStatus[_adhocLevel].nFrameAdvances++;
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
	loadTracker[_adhocLevel].erase(loadTracker[_adhocLevel].upper_bound(currentFrame), loadTracker[_adhocLevel].end());

	// Set inputs and advance frame
	SetInputs(inputs);
	resource->FrameAdvance();
	BaseStatus[_adhocLevel].nFrameAdvances++;
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
	loadTracker[_adhocLevel].erase(loadTracker[_adhocLevel].upper_bound(currentFrame), loadTracker[_adhocLevel].end());

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

		currentFrame = GetCurrentFrame();
	}
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::ApplyChildDiff(const BaseScriptStatus& status, int64_t initialFrame)
{
	uint64_t firstFrame = status.m64Diff.frames.begin()->first;
	uint64_t lastFrame = status.m64Diff.frames.rbegin()->first;

	// Erase all saves, cached saves, and frame counters after this point
	inputsCache[_adhocLevel].erase(inputsCache[_adhocLevel].lower_bound(firstFrame), inputsCache[_adhocLevel].end());
	frameCounter[_adhocLevel].erase(frameCounter[_adhocLevel].upper_bound(firstFrame), frameCounter[_adhocLevel].end());
	saveBank[_adhocLevel].erase(saveBank[_adhocLevel].upper_bound(firstFrame), saveBank[_adhocLevel].end());
	saveCache[_adhocLevel].erase(saveCache[_adhocLevel].upper_bound(firstFrame), saveCache[_adhocLevel].end());
	loadTracker[_adhocLevel].erase(loadTracker[_adhocLevel].upper_bound(firstFrame), loadTracker[_adhocLevel].end());

	//Apply diff. State is already synced from child script, so no need to update it
	int64_t frame = firstFrame;
	while (frame <= lastFrame)
	{
		if (status.m64Diff.frames.count(frame))
			BaseStatus[_adhocLevel].m64Diff.frames[frame] = status.m64Diff.frames.at(frame);

		frame++;
	}

	//Forward state to end of diff
	Load(lastFrame + 1);
}

template <derived_from_specialization_of<Resource> TResource>
Inputs Script<TResource>::GetInputs(int64_t frame)
{
	return GetInputsMetadataAndCache(frame).inputs;
}

template <derived_from_specialization_of<Resource> TResource>
InputsMetadata<TResource> Script<TResource>::GetInputsMetadata(int64_t frame)
{
	if (!_parentScript)
		throw std::runtime_error("Failed to get inputs because of missing parent script");

	//State owner determines what frame counter needs to be incremented
	Script* stateOwner = nullptr;
	int64_t stateOwnerAdhocLevel = -1;

	//Check ad-hoc script hierarchy first, then current script
	for (int64_t adhocLevel = _adhocLevel; adhocLevel >= 0; adhocLevel--)
	{
		if (!stateOwner)
		{
			if (!BaseStatus[adhocLevel].m64Diff.frames.empty() && BaseStatus[adhocLevel].m64Diff.frames.begin()->first <= frame)
			{
				stateOwner = this;
				stateOwnerAdhocLevel = adhocLevel;
			}
		}

		if (BaseStatus[adhocLevel].m64Diff.frames.contains(frame))
			return InputsMetadata<TResource>(BaseStatus[adhocLevel].m64Diff.frames[frame], frame, stateOwner, stateOwnerAdhocLevel);

		if (inputsCache[adhocLevel].contains(frame))
		{
			InputsMetadata<TResource> metadata = inputsCache[adhocLevel][frame];
			if (stateOwner)
			{
				metadata.stateOwner = stateOwner;
				metadata.stateOwnerAdhocLevel = stateOwnerAdhocLevel;
			}

			return metadata;
		}
	}

	//Then check parent script
	InputsMetadata metadata = _parentScript->GetInputsMetadata(frame);
	if (stateOwner)
	{
		metadata.stateOwner = stateOwner;
		metadata.stateOwnerAdhocLevel = stateOwnerAdhocLevel;
	}
	return metadata;

	//This should be impossible
	throw std::runtime_error("Failed to get inputs, possible error in recursion logic.");
	return InputsMetadata<TResource>();
}

template <derived_from_specialization_of<Resource> TResource>
InputsMetadata<TResource> TopLevelScript<TResource>::GetInputsMetadata(int64_t frame)
{
	//State owner determines what frame counter needs to be incremented
	int64_t stateOwnerAdhocLevel = -1;

	//Check ad-hoc script hierarchy first, then current script
	for (int64_t adhocLevel = this->_adhocLevel; adhocLevel >= 0; adhocLevel--)
	{
		if (stateOwnerAdhocLevel == -1)
		{
			if (!this->BaseStatus[adhocLevel].m64Diff.frames.empty() && this->BaseStatus[adhocLevel].m64Diff.frames.begin()->first <= frame)
				stateOwnerAdhocLevel = adhocLevel;
		}

		if (this->BaseStatus[adhocLevel].m64Diff.frames.contains(frame))
			return InputsMetadata<TResource>(this->BaseStatus[adhocLevel].m64Diff.frames[frame], frame, this, stateOwnerAdhocLevel);

		if (this->inputsCache[adhocLevel].contains(frame))
		{
			InputsMetadata<TResource> metadata = this->inputsCache[adhocLevel][frame];
			if (stateOwnerAdhocLevel == -1)
				metadata.stateOwnerAdhocLevel = adhocLevel;

			return metadata;
		}
	}

	if (stateOwnerAdhocLevel == -1)
		stateOwnerAdhocLevel = 0;

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

	//Return value BEFORE incrementing
	return cachedInputs.stateOwner->frameCounter[cachedInputs.stateOwnerAdhocLevel][cachedInputs.frame]++;
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
					auto loadAfterCachedSave = loadTracker[_adhocLevel].lower_bound(cachedSave->first);
					if (loadAfterCachedSave == loadTracker[_adhocLevel].end() || *loadAfterCachedSave < frame)
						bestSave = cachedSave->second;
					else
						return cachedSave->second;
				}

			}
			else
				saveCache[adhocLevel].erase(cachedSave->first); // Delete stale cached save	
		}

		// Don't search past start of m64 diff to avoid desync
		earlyFrame = !BaseStatus[adhocLevel].m64Diff.frames.empty() ?
			(std::min)(BaseStatus[adhocLevel].m64Diff.frames.begin()->first, (uint64_t)earlyFrame) : frame;

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
	return SaveMetadata<TResource>(this);
}

template <derived_from_specialization_of<Resource> TResource>
SaveMetadata<TResource> Script<TResource>::GetLatestSaveAndCache(int64_t frame)
{
	SaveMetadata<TResource> save = GetLatestSave(frame);
	saveCache[_adhocLevel][save.frame] = save; // Cache save to save recursion time later

	//Tracked loads between this frame and the cached save frame are obsoloted by this one
	auto loadAfterCachedSave = loadTracker[_adhocLevel].lower_bound(save.frame);
	if (loadAfterCachedSave != loadTracker[_adhocLevel].end() && *loadAfterCachedSave < frame)
		loadTracker[_adhocLevel].erase(loadAfterCachedSave);

	//Track load to mark cached save as optimal
	if (!loadTracker[_adhocLevel].contains(frame))
		loadTracker[_adhocLevel].insert(frame);

	return save;
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::Load(uint64_t frame)
{
	// Load most recent save at or before frame. Check child saves before
	// parent. If target frame is in future, check if faster to frame advance or load.
	int64_t currentFrame = GetCurrentFrame();
	auto latestSave = GetLatestSaveAndCache(frame);
	if (frame < currentFrame)
	{
		resource->LoadState(latestSave.GetSlotHandle()->slotId);
		BaseStatus[_adhocLevel].nLoads++;
	}
	else if (latestSave.frame > currentFrame && resource->shouldLoad(latestSave.frame - currentFrame))
		resource->LoadState(latestSave.GetSlotHandle()->slotId);

	// If save is before target frame, play back until frame is reached
	currentFrame = GetCurrentFrame();
	uint64_t frameCounter = 0;
	while (currentFrame++ < frame)
	{
		auto cachedInputs = GetInputsMetadataAndCache(currentFrame);
		frameCounter += IncrementFrameCounter(cachedInputs);
		AdvanceFrameRead();

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
void Script<TResource>::Revert(uint64_t frame, const M64Diff& m64)
{
	// Check if script altered state
	int64_t currentFrame = GetCurrentFrame();
	bool desync =
		(!m64.frames.empty()) && (m64.frames.begin()->first < currentFrame);

	// Load most recent save at or before frame. Check child saves before
	// parent. If target frame is in future, check if faster to frame advance or load.
	auto latestSave = GetLatestSaveAndCache(frame);
	if (desync || frame < currentFrame)
	{
		resource->LoadState(latestSave.GetSlotHandle()->slotId);
		BaseStatus[_adhocLevel].nLoads++;
	}
	else if (latestSave.frame > frame && resource->shouldLoad(latestSave.frame - currentFrame))
		resource->LoadState(latestSave.GetSlotHandle()->slotId);

	// If save is before target frame, play back until frame is reached
	currentFrame = GetCurrentFrame();
	uint64_t frameCounter = 0;
	while (currentFrame++ < frame)
	{
		auto cachedInputs = GetInputsMetadataAndCache(currentFrame);
		frameCounter += IncrementFrameCounter(cachedInputs);
		AdvanceFrameRead();

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
		loadTracker[_adhocLevel].erase(loadTracker[_adhocLevel].upper_bound(firstFrame), loadTracker[_adhocLevel].end());
	}

	Load(frame);
}

// Same as Rollback, but starts from current frame. Useful for scripts that edit past frames
template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::RollForward(int64_t frame)
{
	// Roll back diff and savebank to current frame. Note that roll forward on diff
	// includes current frame.
	uint64_t currentFrame = GetCurrentFrame();

	if (!BaseStatus[_adhocLevel].m64Diff.frames.empty())
	{
		int64_t firstFrame = BaseStatus[_adhocLevel].m64Diff.frames.begin()->first;

		auto inputsUpperBound = BaseStatus[_adhocLevel].m64Diff.frames.upper_bound(frame);
		if (inputsUpperBound == BaseStatus[_adhocLevel].m64Diff.frames.begin() && BaseStatus[_adhocLevel].m64Diff.frames.size() == 1)
			inputsUpperBound = BaseStatus[_adhocLevel].m64Diff.frames.end();
		else
			inputsUpperBound = std::prev(inputsUpperBound);

		BaseStatus[_adhocLevel].m64Diff.frames.erase(BaseStatus[_adhocLevel].m64Diff.frames.begin(), inputsUpperBound);

		inputsCache[_adhocLevel].erase(inputsCache[_adhocLevel].lower_bound(firstFrame), inputsCache[_adhocLevel].end());
		frameCounter[_adhocLevel].erase(frameCounter[_adhocLevel].upper_bound(firstFrame), frameCounter[_adhocLevel].end());
		saveBank[_adhocLevel].erase(saveBank[_adhocLevel].upper_bound(firstFrame), saveBank[_adhocLevel].end());
		saveCache[_adhocLevel].erase(saveCache[_adhocLevel].upper_bound(firstFrame), saveCache[_adhocLevel].end());
		loadTracker[_adhocLevel].erase(loadTracker[_adhocLevel].upper_bound(firstFrame), loadTracker[_adhocLevel].end());
	}

	Load(frame);
}

// Load and clear diff and savebank
template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::Restore(int64_t frame)
{
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
		loadTracker[_adhocLevel].erase(loadTracker[_adhocLevel].upper_bound(firstFrame), loadTracker[_adhocLevel].end());
	}

	Load(frame);
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::Save()
{
	Save(_adhocLevel);
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

	//Return metadat for caching
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
	for (int64_t frame = latestSaveFrame; frame <= currentFrame; frame++)
	{
		if (resource->shouldSave(frameCounter * 2))
		{
			//Create save in this script at the current frame
			Save();
			break;
		}

		//Increment AFTER save check. We want to know if a save on the NEXT frame is worthwhile based on the counter from this frame
		frameCounter += GetFrameCounter(GetInputsMetadataAndCache(frame));
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

template <derived_from_specialization_of<Resource> TResource>
bool Script<TResource>::IsDiffEmpty()
{
	return BaseStatus[_adhocLevel].m64Diff.frames.empty();
}

template <derived_from_specialization_of<Resource> TResource>
M64Diff Script<TResource>::GetDiff()
{
	return BaseStatus[_adhocLevel].m64Diff;
}

template <derived_from_specialization_of<Resource> TResource>
AdhocBaseScriptStatus Script<TResource>::ExecuteAdhoc(AdhocScript auto adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	BaseScriptStatus status = ExecuteAdhocBase(adhocScript);
	Revert(initialFrame, status.m64Diff);

	return AdhocBaseScriptStatus(status);
}

template <derived_from_specialization_of<Resource> TResource>
template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
AdhocScriptStatus<TAdhocCustomScriptStatus> Script<TResource>::ExecuteAdhoc(F adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
	BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });
	Revert(initialFrame, baseStatus.m64Diff);

	return AdhocScriptStatus<TAdhocCustomScriptStatus>(baseStatus, customStatus);
}

template <derived_from_specialization_of<Resource> TResource>
AdhocBaseScriptStatus Script<TResource>::ModifyAdhoc(AdhocScript auto adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	auto status = ExecuteAdhocBase(adhocScript);

	// Revert state if assertion fails, otherwise apply diff
	if (!status.asserted || status.m64Diff.frames.empty())
		Revert(initialFrame, status.m64Diff);
	else
		ApplyChildDiff(status, initialFrame);

	return AdhocBaseScriptStatus(status);
}

template <derived_from_specialization_of<Resource> TResource>
template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
AdhocScriptStatus<TAdhocCustomScriptStatus> Script<TResource>::ModifyAdhoc(F adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
	BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });

	// Revert state if assertion fails, otherwise apply diff
	if (!baseStatus.asserted || baseStatus.m64Diff.frames.empty())
		Revert(initialFrame, baseStatus.m64Diff);
	else
		ApplyChildDiff(baseStatus, initialFrame);

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

//Root method for Compare() template recursion. This will be called when there are no scripts left to compare the incumbent to.
template <derived_from_specialization_of<Resource> TResource>
template <class TCompareStatus>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script<TResource>::Compare(const AdhocScriptStatus<TCompareStatus>& status1)
{
	return status1;
}

//Main recursive method for comparing ad hoc scripts. Only the root and leaf use different methods
template <derived_from_specialization_of<Resource> TResource>
template <class TCompareStatus, AdhocCustomStatusScript<TCompareStatus> F, AdhocCustomStatusScript<TCompareStatus>... G>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script<TResource>::Compare(const AdhocScriptStatus<TCompareStatus>& status1, F&& adhocScript2, G&&... adhocScripts)
{
	auto status2 = ExecuteAdhoc<TCompareStatus>(std::forward<F>(adhocScript2));
	if (status2.executed && status2.Terminator(status2))
		return status2;

	if (!status1.executed)
	{
		if (!status2.executed)
			return Compare<TCompareStatus>(status1, std::forward<G>(adhocScripts)...);

		return Compare<TCompareStatus>(status2, std::forward<G>(adhocScripts)...);
	}

	return Compare<TCompareStatus>(status1.Comparator(status1, status2), std::forward<G>(adhocScripts)...);
}

//Leaf method for comparing ad-hoc scripts. This is the one the user will call.
template <derived_from_specialization_of<Resource> TResource>
template <class TCompareStatus,
	AdhocCustomStatusScript<TCompareStatus> F,
	AdhocCustomStatusScript<TCompareStatus> G,
	AdhocCustomStatusScript<TCompareStatus>... H>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script<TResource>::Compare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts)
{
	auto status1 = ExecuteAdhoc<TCompareStatus>(std::forward<F>(adhocScript1));
	if (status1.executed && status1.Terminator(status1))
		return status1;

	auto status2 = ExecuteAdhoc<TCompareStatus>(std::forward<G>(adhocScript2));
	if (status2.executed && status2.Terminator(status2))
		return status2;

	if (!status1.executed)
	{
		if (!status2.executed)
			return Compare<TCompareStatus>(status1, std::forward<H>(adhocScripts)...);

		return Compare<TCompareStatus>(status2, std::forward<H>(adhocScripts)...);
	}

	return Compare<TCompareStatus>(status1.Comparator(status1, status2), std::forward<H>(adhocScripts)...);
}

//Root method for ModifyCompare() template recursion. This will be called when there are no scripts left to compare the incumbent to.
template <derived_from_specialization_of<Resource> TResource>
template <class TCompareStatus>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script<TResource>::ModifyCompare(int64_t initialFrame, const AdhocScriptStatus<TCompareStatus>& status1)
{
	// Revert state if execution failed or diff is empty, otherwise apply diff
	if (!status1.executed && status1.m64Diff.frames.empty())
		Revert(initialFrame, status1.m64Diff);
	else
		ApplyChildDiff(status1, initialFrame);

	return status1;
}

//Main recursive method for ModifyCompare(). Only the root and leaf use different methods
template <derived_from_specialization_of<Resource> TResource>
template <class TCompareStatus, AdhocCustomStatusScript<TCompareStatus> F, AdhocCustomStatusScript<TCompareStatus>... G>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script<TResource>::ModifyCompare(int64_t initialFrame, const AdhocScriptStatus<TCompareStatus>& status1, F&& adhocScript2, G&&... adhocScripts)
{
	//Revert state before executing new script
	Revert(initialFrame, status1.m64Diff);
	auto status2 = ExecuteAdhoc<TCompareStatus>(std::forward<F>(adhocScript2));
	if (status2.executed && status2.Terminator(status2))
	{
		// Revert state if execution failed or diff is empty, otherwise apply diff
		if (!status2.executed && status2.m64Diff.frames.empty())
			Revert(initialFrame, status2.m64Diff);
		else
			ApplyChildDiff(status2, initialFrame);

		return status2;
	}

	if (!status1.executed)
	{
		if (!status2.executed)
			return ModifyCompare<TCompareStatus>(initialFrame, status1, std::forward<G>(adhocScripts)...);

		return ModifyCompare<TCompareStatus>(initialFrame, status2, std::forward<G>(adhocScripts)...);
	}

	return ModifyCompare<TCompareStatus>(initialFrame, status1.Comparator(status1, status2), std::forward<G>(adhocScripts)...);
}

//Leaf method for comparing ad-hoc scripts and applying the result. This is the one the user will call.
template <derived_from_specialization_of<Resource> TResource>
template <class TCompareStatus,
	AdhocCustomStatusScript<TCompareStatus> F,
	AdhocCustomStatusScript<TCompareStatus> G,
	AdhocCustomStatusScript<TCompareStatus>... H>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script<TResource>::ModifyCompare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts)
{
	int64_t initialFrame = GetCurrentFrame();
	auto status1 = ExecuteAdhocNoRevert<TCompareStatus>(std::forward<F>(adhocScript1));
	if (status1.executed && status1.Terminator(status1))
	{
		// Revert state if diff is empty, otherwise apply diff
		if (status1.m64Diff.frames.empty())
			Revert(initialFrame, status1.m64Diff);
		else
			ApplyChildDiff(status1, initialFrame);

		return status1;
	}

	//Revert state before executing new script
	Revert(initialFrame, status1.m64Diff);
	auto status2 = ExecuteAdhocNoRevert<TCompareStatus>(std::forward<G>(adhocScript2));
	if (status2.executed && status2.Terminator(status2))
	{
		// Revert state if diff is empty, otherwise apply diff
		if (status2.m64Diff.frames.empty())
			Revert(initialFrame, status2.m64Diff);
		else
			ApplyChildDiff(status2, initialFrame);

		return status2;
	}

	if (!status1.executed)
	{
		if (!status2.executed)
			return ModifyCompare<TCompareStatus>(initialFrame, status1, std::forward<H>(adhocScripts)...);

		return ModifyCompare<TCompareStatus>(initialFrame, status2, std::forward<H>(adhocScripts)...);
	}

	return ModifyCompare<TCompareStatus>(initialFrame, status1.Comparator(status1, status2), std::forward<H>(adhocScripts)...);
}

//Same as Compare(), but with no diff returned
template <derived_from_specialization_of<Resource> TResource>
template <class TCompareStatus,
	AdhocCustomStatusScript<TCompareStatus> F,
	AdhocCustomStatusScript<TCompareStatus> G,
	AdhocCustomStatusScript<TCompareStatus>... H>
	requires(std::derived_from<TCompareStatus, CompareStatus<TCompareStatus>>)
AdhocScriptStatus<TCompareStatus> Script<TResource>::TestCompare(F&& adhocScript1, G&& adhocScript2, H&&... adhocScripts)
{
	auto status = Compare<TCompareStatus>(std::forward<F>(adhocScript1), std::forward<G>(adhocScript2), std::forward<H>(adhocScripts)...);
	status.m64Diff = M64Diff();

	return status;
}

template <derived_from_specialization_of<Resource> TResource>
template <typename F>
BaseScriptStatus Script<TResource>::ExecuteAdhocBase(F adhocScript)
{
	//Save state if performant
	OptionalSave();

	//Increment adhoc level
	_adhocLevel++;
	BaseStatus[_adhocLevel];
	saveBank[_adhocLevel];
	frameCounter[_adhocLevel];
	saveCache[_adhocLevel];
	inputsCache[_adhocLevel];
	loadTracker[_adhocLevel];

	BaseStatus[_adhocLevel].validated = true;

	auto start = std::chrono::high_resolution_clock::now();
	try
	{
		BaseStatus[_adhocLevel].executed = adhocScript();
	}
	catch (std::exception& e)
	{
		BaseStatus[_adhocLevel].executionThrew = true;
	}
	auto finish = std::chrono::high_resolution_clock::now();

	BaseStatus[_adhocLevel].executionDuration =
		std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
		.count();

	BaseStatus[_adhocLevel].asserted = BaseStatus[_adhocLevel].executed;

	//Decrement adhoc level, revert state and return status
	BaseScriptStatus status = BaseStatus[_adhocLevel];
	BaseStatus.erase(_adhocLevel);
	saveBank.erase(_adhocLevel);
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

//Only to be used by ModifyCompare(). Will desync if used alone, as it assumes the caller will call Revert().
template <derived_from_specialization_of<Resource> TResource>
template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
AdhocScriptStatus<TAdhocCustomScriptStatus> Script<TResource>::ExecuteAdhocNoRevert(F adhocScript)
{
	// Save state if performant
	OptionalSave();

	TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
	BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });

	return AdhocScriptStatus<TAdhocCustomScriptStatus>(baseStatus, customStatus);
}

template <derived_from_specialization_of<Resource> TResource>
template <std::derived_from<TopLevelScript<TResource>> TTopLevelScript, typename... Ts>
	requires(std::constructible_from<TTopLevelScript, Ts...> && std::constructible_from<TResource>)
ScriptStatus<TTopLevelScript> TopLevelScript<TResource>::Main(M64& m64, Ts&&... params)
{
	TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
	TResource resource = TResource();
	resource.save(resource.startSave);
	resource.initialFrame = 0;

	script._m64 = &m64;
	script.resource = &resource;
	script.Initialize(nullptr);

	if (script.checkPreconditions() && script.execute())
		script.checkPostconditions();

	//Dispose of slot handles before resource goes out of scope because they trigger destructor events in the resource.
	script.saveBank[0].erase(script.saveBank[0].begin(), script.saveBank[0].end());

	return ScriptStatus<TTopLevelScript>(script.BaseStatus[0], script.CustomStatus);
}

template <derived_from_specialization_of<Resource> TResource>
template <std::derived_from<TopLevelScript<TResource>> TTopLevelScript, class TState, typename... Ts>
	requires(std::constructible_from<TTopLevelScript, Ts...>
		&& std::constructible_from<TResource>
		&& std::derived_from<TResource, Resource<TState>>)
static ScriptStatus<TTopLevelScript> TopLevelScript<TResource>::MainFromSave(M64& m64, ImportedSave<TState>& save, Ts&&... params)
{
	TTopLevelScript script = TTopLevelScript(std::forward<Ts>(params)...);
	TResource resource = TResource();
	resource.load(save.state);
	resource.save(resource.startSave);
	resource.initialFrame = save.initialFrame;

	script._m64 = &m64;
	script.resource = &resource;
	script.Initialize(nullptr);

	if (script.checkPreconditions() && script.execute())
		script.checkPostconditions();

	//Dispose of slot handles before resource goes out of scope because they trigger destructor events in the resource.
	script.saveBank[0].erase(script.saveBank[0].begin(), script.saveBank[0].end());

	return ScriptStatus<TTopLevelScript>(script.BaseStatus[0], script.CustomStatus);
}

template <derived_from_specialization_of<Resource> TResource>
SaveMetadata<TResource>::SaveMetadata(Script<TResource>* script)
{
	if (script)
	{
		this->script = script;
		isStartSave = true;
		frame = 0;
		adhocLevel = -1;
	}
}

template <derived_from_specialization_of<Resource> TResource>
SlotHandle<TResource>* SaveMetadata<TResource>::GetSlotHandle()
{
	if (!script)
		return nullptr;

	if (isStartSave)
		return &script->startSaveHandle;

	if (script->saveBank.size() <= adhocLevel)
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

#endif