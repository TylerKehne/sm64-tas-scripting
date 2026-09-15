#pragma once
#ifndef SCRIPT_H
#error "Script.t.hpp should only be included by Script.hpp"
#else

template <derived_from_specialization_of<Resource> TResource>
AdhocBaseScriptStatus Script<TResource>::ExecuteAdhoc(AdhocScript auto adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	BaseScriptStatus status = ExecuteAdhocBase(adhocScript);
	Revert(initialFrame, status.m64Diff, SaveBankIfCreated(*this, _adhocLevel + 1), this);

	return AdhocBaseScriptStatus(std::move(status));
}

template <derived_from_specialization_of<Resource> TResource>
template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
AdhocScriptStatus<TAdhocCustomScriptStatus> Script<TResource>::ExecuteAdhoc(F adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
	BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });
	Revert(initialFrame, baseStatus.m64Diff, SaveBankIfCreated(*this, _adhocLevel + 1), this);

	return AdhocScriptStatus<TAdhocCustomScriptStatus>(std::move(baseStatus), std::move(customStatus));
}

template <derived_from_specialization_of<Resource> TResource>
AdhocBaseScriptStatus Script<TResource>::ModifyAdhoc(AdhocScript auto adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	auto status = ExecuteAdhocBase(adhocScript);
	ApplyChildDiff(status, SaveBankIfCreated(*this, _adhocLevel + 1), initialFrame, this);

	return AdhocBaseScriptStatus(std::move(status));
}

template <derived_from_specialization_of<Resource> TResource>
template <class TAdhocCustomScriptStatus, AdhocCustomStatusScript<TAdhocCustomScriptStatus> F>
AdhocScriptStatus<TAdhocCustomScriptStatus> Script<TResource>::ModifyAdhoc(F adhocScript)
{
	int64_t initialFrame = GetCurrentFrame();

	TAdhocCustomScriptStatus customStatus = TAdhocCustomScriptStatus();
	BaseScriptStatus baseStatus = ExecuteAdhocBase([&]() { return adhocScript(customStatus); });
	ApplyChildDiff(baseStatus, SaveBankIfCreated(*this, _adhocLevel + 1), initialFrame, this);

	return AdhocScriptStatus<TAdhocCustomScriptStatus>(std::move(baseStatus), std::move(customStatus));
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

	// A tracker's own frames are never tracked: the root's TrackState is skipped here rather
	// than asked, which was a virtual call per frame of every tracker.
	InputsMetadata<TResource> inputsMetadata = GetInputsMetadataAndCache(currentFrame);
	if (!isStateTracker)
		_rootScript->TrackState(this, inputsMetadata);
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
	InputsMetadata<TResource> inputsMetadata = GetInputsMetadataAndCache(currentFrame);
	if (!isStateTracker)
		_rootScript->TrackState(this, inputsMetadata);
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

// Useful for exporting current output of script hieerarchy without terminating it
template <derived_from_specialization_of<Resource> TResource>
M64Diff Script<TResource>::GetTotalDiff()
{
	M64Diff totalDiff;
	for (Script<TResource>* script = this; script != nullptr; script = script->_parentScript)
	{
		for (int64_t adhocLevel = script->_adhocLevel; adhocLevel >= 0; adhocLevel--)
		{
			for (auto input : script->BaseStatus[adhocLevel].m64Diff.frames)
			{
				if (!totalDiff.frames.contains(input.first))
					totalDiff.frames[input.first] = input.second;
			}
		}
	}
	
	return totalDiff;
}

// TODO: Deprecate
template <derived_from_specialization_of<Resource> TResource>
M64Diff Script<TResource>::GetBaseDiff()
{
	return BaseStatus[0].m64Diff;
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
		InputsMetadata<TResource> inputsMetadata = GetInputsMetadataAndCache(currentFrame);
		if (!isStateTracker)
			_rootScript->TrackState(this, inputsMetadata);
	}
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
	outM64.metadata = GetM64Metadata(); // an export is a movie for the game the source movie is for
	for (int64_t frame = 0; frame < maxFrame; frame++)
	{
		outM64.frames[frame] = GetInputsMetadata(frame).inputs;
	}

	return (bool)outM64.save();
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
void Script<TResource>::Save()
{
	int64_t currentFrame = GetCurrentFrame();
	auto inputsMetadata = GetInputsMetadata(currentFrame);
	saveCache[_adhocLevel][currentFrame] = inputsMetadata.stateOwner->Save(inputsMetadata.stateOwnerAdhocLevel);
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
	// parent. If target frame is in future and a save lies between the cursor and it, check
	// if faster to frame advance or load (see LoadBase for the history of this condition).
	// Also, don't cache as it is unlikely the save will be needed again.
	auto latestSave = GetLatestSave(frame);
	if (frame < currentFrame)
	{
		resource->LoadState(latestSave.GetSlotHandle()->slotId);
		BaseStatus[_adhocLevel].nLoads++;
	}
	else if (latestSave.frame > currentFrame && resource->shouldLoad(latestSave.frame - currentFrame))
	{
		resource->LoadState(latestSave.GetSlotHandle()->slotId);
		BaseStatus[_adhocLevel].nLoads++;
	}

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
	InputsMetadata<TResource> inputsMetadata = GetInputsMetadataAndCache(frame);
	if (!isStateTracker)
		_rootScript->TrackState(this, inputsMetadata);

	// Create a save as it is likely that very many frames were advanced since the most recent one.
	Save();
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
void* Script<TResource>::ReadState(const char* symbol) const
{
	return resource->addr(symbol);
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
void Script<TResource>::Initialize(Script<TResource>* parentScript)
{
	_parentScript = parentScript;

	// Per-level containers (BaseStatus, saveBank, caches) are created on first use; see
	// LevelStack. A script that never saves never constructs a save bank.
	if (_parentScript)
	{
		resource = _parentScript->resource;
		_rootScript = _parentScript->_rootScript;
	}
	else
		_rootScript = this;

	startSaveHandle = SlotHandle<TResource>(resource, -1);
	_initialFrame = int64_t(GetCurrentFrame());
}

template <derived_from_specialization_of<Resource> TResource>
template <typename F>
BaseScriptStatus Script<TResource>::ExecuteAdhocBase(F adhocScript)
{
	//Increment adhoc level. The other per-level containers are created on first use.
	_adhocLevel++;
	BaseStatus[_adhocLevel].validated = true;

	uint64_t loadCyclesStart = resource->work.loadCycles;
	uint64_t saveCyclesStart = resource->work.saveCycles;
	uint64_t advanceCyclesStart = resource->work.advanceCycles;

	// Cycles, like every other duration (ROADMAP 3.6). This was the one place that recorded
	// milliseconds through std::chrono.
	uint64_t start = get_time();
	BaseStatus[_adhocLevel].executed = adhocScript();
	uint64_t finish = get_time();

	BaseStatus[_adhocLevel].loadDuration = resource->work.loadCycles - loadCyclesStart;
	BaseStatus[_adhocLevel].saveDuration = resource->work.saveCycles - saveCyclesStart;
	BaseStatus[_adhocLevel].advanceFrameDuration = resource->work.advanceCycles - advanceCyclesStart;

	BaseStatus[_adhocLevel].executionDuration = finish - start;

	BaseStatus[_adhocLevel].asserted = BaseStatus[_adhocLevel].executed;

	//Decrement adhoc level, revert state and return status
	//NOTE: saveBank is not popped here as the saves may be moved to the parent.
	//Caller is responsible for popping it.
	BaseScriptStatus status = std::move(BaseStatus[_adhocLevel]);
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
InputsMetadata<TResource> Script<TResource>::GetInputsMetadata(int64_t frame)
{
	InputsMetadata<TResource> metadata;
	GetInputsMetadata(frame, metadata);
	return metadata;
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::GetInputsMetadata(int64_t frame, InputsMetadata<TResource>& metadata)
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
				{
					metadata = InputsMetadata<TResource>(inputs, frame, this, stateOwnerAdhocLevel);
					return;
				}
			}
		}

		if (BaseStatus[adhocLevel].m64Diff.frames.contains(frame))
		{
			if (stateOwnerAdhocLevel != -1)
			{
				metadata = InputsMetadata<TResource>(alreadyFoundInputs ? inputs : BaseStatus[adhocLevel].m64Diff.frames[frame], frame, this, stateOwnerAdhocLevel);
				return;
			}

			if (!alreadyFoundInputs)
			{
				alreadyFoundInputs = true;
				inputs = BaseStatus[adhocLevel].m64Diff.frames[frame];
			}
		}

		if (inputsCache[adhocLevel].contains(frame))
		{
			metadata = inputsCache[adhocLevel][frame];
			if (stateOwnerAdhocLevel != -1)
			{
				metadata.stateOwner = this;
				metadata.stateOwnerAdhocLevel = stateOwnerAdhocLevel;
			}

			if (alreadyFoundInputs)
				metadata.inputs = inputs;

			return;
		}
	}

	//Then check parent script, which writes its answer into the same object
	_parentScript->GetInputsMetadata(frame, metadata);
	if (stateOwnerAdhocLevel != -1)
	{
		metadata.stateOwner = this;
		metadata.stateOwnerAdhocLevel = stateOwnerAdhocLevel;
	}

	if (alreadyFoundInputs)
		metadata.inputs = inputs;
}

template <derived_from_specialization_of<Resource> TResource>
InputsMetadata<TResource> Script<TResource>::GetInputsMetadataAndCache(int64_t frame)
{
	InputsMetadata<TResource> inputs = GetInputsMetadata(frame);
	inputsCache[_adhocLevel][frame] = inputs;
	return inputs;
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::SetInputs(Inputs inputs)
{
	// Was three addr("gControllerPads") lookups per frame (three GetProcAddress calls on
	// LibSm64); the resource now writes its own pad from a pointer cached at construction.
	resource->setInputs(inputs);
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
M64Metadata Script<TResource>::GetM64Metadata() const
{
	return _rootScript->GetM64Metadata(); // the root is a TopLevelScript, whose override answers from its movie
}

template <derived_from_specialization_of<Resource> TResource>
SaveMetadata<TResource> Script<TResource>::GetLatestSave(int64_t frame)
{
	if (resource->InitialFrame() > frame)
		throw std::runtime_error("Error: attempted to load frame prior to initial frame");

	//Check ad-hoc script hierarchy first, then current script
	int64_t earlyFrame = frame;
	SaveMetadata<TResource> bestSave;
	for (int64_t adhocLevel = _adhocLevel; adhocLevel >= 0; adhocLevel--)
	{
		while (true)
		{
			//Get most recent save in script
			auto save = saveBank[adhocLevel].empty() || earlyFrame < saveBank[adhocLevel].begin()->first
				? saveBank[adhocLevel].end()
				: std::prev(saveBank[adhocLevel].upper_bound(earlyFrame));

			//Verify save exists and select the more recent save
			if (save != saveBank[adhocLevel].end())
			{
				if (!save->second.isValid())
				{
					saveBank[adhocLevel].erase(save->first);
					continue;
				}
				else if (save->first >= bestSave.frame)
					bestSave = SaveMetadata<TResource>(this, save->first, adhocLevel);
			}

			break;
		}

		//Check for cached save
		while (true)
		{
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
				{
					saveCache[adhocLevel].erase(cachedSave->first); // Delete stale cached save
					continue;
				}
			}

			break;
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

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::DeleteSave(int64_t frame, int64_t adhocLevel)
{
	saveBank[adhocLevel].erase(frame);
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::LoadBase(uint64_t frame, bool desync)
{
	uint64_t currentFrame = GetCurrentFrame();
	if (!desync && currentFrame == frame)
		return;

	// Load most recent save at or before frame. Check child saves before
	// parent. If target frame is in future and a save lies between the cursor and it, check
	// if faster to load that save than to frame advance to it. The save is in sync with the
	// inputs about to be replayed (the lookup never returns one past a level's own writes).
	// Until 2026-09-13 this compared the save's frame with the target instead of the cursor,
	// which the lookup makes impossible, so a forward load never jumped: written that way on
	// 2022-03-22, corrected in Load on 2022-04-09, and lost when Load became this function on
	// 2022-06-14 (ROADMAP 3.11).
	auto latestSave = GetLatestSaveAndCache(frame);
	if (desync || frame < currentFrame)
	{
		resource->LoadState(latestSave.GetSlotHandle()->slotId);
		BaseStatus[_adhocLevel].nLoads++;
	}
	else if (latestSave.frame > static_cast<int64_t>(currentFrame) && resource->shouldLoad(latestSave.frame - static_cast<int64_t>(currentFrame)))
	{
		resource->LoadState(latestSave.GetSlotHandle()->slotId);
		BaseStatus[_adhocLevel].nLoads++;
	}

	// Run custom state tracker
	currentFrame = GetCurrentFrame();
	InputsMetadata<TResource> inputsMetadata = GetInputsMetadataAndCache(currentFrame);
	if (!isStateTracker)
		_rootScript->TrackState(this, inputsMetadata);

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
void Script<TResource>::Revert(uint64_t frame, const M64Diff& m64, FrameMap<int64_t, SlotHandle<TResource>>* childSaveBank, Script<TResource>* childScript)
{
	// Check if script altered state
	bool desync = (!m64.frames.empty()) && (m64.frames.begin()->first < GetCurrentFrame());

	// Keep the child's saves that are still in sync: those at or before the first frame the
	// child changed (the state at a frame does not depend on that frame's inputs). Every later
	// save was made with inputs that are being reverted and is dropped with the bank.
	// Until 2026-09-08 a child whose saves were all desynced had every one of them moved into
	// the parent's bank, where a later backwards load could pick one up (ROADMAP 4.5).
	if (childSaveBank && !childSaveBank->empty())
	{
		auto firstDesyncedSave = m64.frames.empty()
			? childSaveBank->end()
			: childSaveBank->upper_bound(static_cast<int64_t>(m64.frames.begin()->first));
		std::move(childSaveBank->begin(), firstDesyncedSave, std::insert_iterator(saveBank[_adhocLevel], saveBank[_adhocLevel].end()));
	}
	//If child is ad-hoc script, pop the save bank
	if (saveBank.contains(_adhocLevel + 1))
		saveBank.erase(_adhocLevel + 1);

	int64_t childAdhocLevel = this == childScript ? _adhocLevel + 1 : 0; // Ad-hoc script vs. regular script
	_rootScript->PopTrackedStatesContainer(childScript, childAdhocLevel);

	LoadBase(frame, desync);
}

template <derived_from_specialization_of<Resource> TResource>
void Script<TResource>::ApplyChildDiff(const BaseScriptStatus& status, FrameMap<int64_t, SlotHandle<TResource>>* childSaveBank, int64_t initialFrame, Script<TResource>* childScript)
{
	//Revert if script was unsuccessful
	if (!status.asserted)
	{
		Revert(initialFrame, status.m64Diff, childSaveBank, childScript);
		return;
	}	

	uint64_t firstFrame = 0;
	uint64_t lastFrame = 0;
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
	if (childSaveBank && !childSaveBank->empty())
		std::move(childSaveBank->begin(), childSaveBank->end(), std::insert_iterator(saveBank[_adhocLevel], saveBank[_adhocLevel].end()));
	if (saveBank.contains(_adhocLevel + 1))
		saveBank.erase(_adhocLevel + 1);

	int64_t childAdhocLevel = this == childScript ? _adhocLevel + 1 : 0; // Ad-hoc script vs. regular script
	_rootScript->MoveSyncedTrackedStates(childScript, childAdhocLevel, this, _adhocLevel);

	if (!status.m64Diff.frames.empty())
		Load(lastFrame + 1); //Forward state to end of diff
	else
		Load(initialFrame);
}

#endif
