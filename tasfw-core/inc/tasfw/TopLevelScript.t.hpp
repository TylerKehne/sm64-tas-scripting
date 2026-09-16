#pragma once
#ifndef TOPLEVELSCRIPT_H
#error "TopLevelScript.t.hpp should only be included by TopLevelScript.hpp"
#else

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
M64Metadata TopLevelScript<TResource, TStateTracker>::GetM64Metadata() const
{
	return _m64->metadata;
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
void TopLevelScript<TResource, TStateTracker>::GetInputsMetadata(int64_t frame, InputsMetadata<TResource>& metadata)
{
	//State owner determines what frame counter needs to be incremented
	int64_t stateOwnerAdhocLevel = -1;
	bool alreadyFoundInputs = false; //True only if state owner is above inputs owner
	Inputs inputs;

	//Check ad-hoc script hierarchy first, then current script
	for (int64_t adhocLevel = this->_adhocLevel; adhocLevel >= 0; adhocLevel--)
	{
		if (stateOwnerAdhocLevel == -1)
		{
			if (!this->BaseStatus[adhocLevel].m64Diff.frames.empty()
				&& static_cast<int64_t>(this->BaseStatus[adhocLevel].m64Diff.frames.begin()->first) < frame)
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

		if (this->BaseStatus[adhocLevel].m64Diff.frames.contains(frame))
		{
			if (stateOwnerAdhocLevel != -1)
			{
				metadata = InputsMetadata<TResource>(alreadyFoundInputs ? inputs
					: this->BaseStatus[adhocLevel].m64Diff.frames[frame], frame, this, stateOwnerAdhocLevel);
				return;
			}

			if (!alreadyFoundInputs)
			{
				alreadyFoundInputs = true;
				inputs = this->BaseStatus[adhocLevel].m64Diff.frames[frame];
			}
		}

		if (this->inputsCache[adhocLevel].contains(frame))
		{
			metadata = this->inputsCache[adhocLevel][frame];
			if (stateOwnerAdhocLevel != -1)
				metadata.stateOwnerAdhocLevel = stateOwnerAdhocLevel;

			if (alreadyFoundInputs)
				metadata.inputs = inputs;

			return;
		}
	}

	if (stateOwnerAdhocLevel == -1)
		stateOwnerAdhocLevel = 0;

	//Return this if inputs have been found but state owner is root
	if (alreadyFoundInputs)
	{
		metadata = InputsMetadata<TResource>(inputs, frame, this, 0);
		return;
	}

	//Then check actual m64.
	//For the purposes of the frame counter, mark as adhoc level 0.
	if (_m64->frames.count(frame))
	{
		metadata = InputsMetadata<TResource>(_m64->frames[frame], frame, this, stateOwnerAdhocLevel, InputsMetadata<TResource>::InputsSource::ORIGINAL);
		return;
	}

	//Default to no input
	//For the purposes of the frame counter, mark as adhoc level 0.
	metadata = InputsMetadata<TResource>(Inputs(0, 0, 0), frame, this, stateOwnerAdhocLevel, InputsMetadata<TResource>::InputsSource::DEFAULT);
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
void TopLevelScript<TResource, TStateTracker>::TrackState(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata)
{
	if constexpr (std::is_same<TStateTracker, DefaultStateTracker<TResource>>::value)
		return;

	GetTrackedStateInternal(currentScript, inputsMetadata);
}

// Tracked states live in trackedStates[owner][adhocLevel][frame]. Entries are created on
// first insert; the read-only paths below use find() so that a script which never tracks
// anything (a tracker itself, a validation sandbox) never gets an entry.

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
bool TopLevelScript<TResource, TStateTracker>::TrackedStateExistsInternal(Script<TResource>* /*currentScript*/, const InputsMetadata<TResource>& inputsMetadata)
{
	auto owner = trackedStates.find(inputsMetadata.stateOwner);
	if (owner == trackedStates.end() || !owner->second.contains(inputsMetadata.stateOwnerAdhocLevel))
		return false;

	return owner->second[inputsMetadata.stateOwnerAdhocLevel].contains(inputsMetadata.frame);
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
void TopLevelScript<TResource, TStateTracker>::PopTrackedStatesContainer(Script<TResource>* currentScript, int64_t adhocLevel)
{
	if constexpr (std::is_same<TStateTracker, DefaultStateTracker<TResource>>::value)
		return;

	auto owner = trackedStates.find(currentScript);
	if (owner == trackedStates.end())
		return;

	if (adhocLevel == 0)
		trackedStates.erase(owner);
	else
		owner->second.erase(adhocLevel);
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
void TopLevelScript<TResource, TStateTracker>::MoveSyncedTrackedStates(Script<TResource>* sourceScript, int64_t sourceAdhocLevel, Script<TResource>* destScript, int64_t destAdhocLevel)
{
	if constexpr (std::is_same<TStateTracker, DefaultStateTracker<TResource>>::value)
		return;

	auto sourceOwner = trackedStates.find(sourceScript);
	if (sourceOwner != trackedStates.end() && sourceOwner->second.contains(sourceAdhocLevel))
	{
		// Take the reference before touching the destination: inserting a new owner may
		// rehash, which invalidates iterators but not references to elements.
		auto& source = sourceOwner->second[sourceAdhocLevel];
		if (!source.empty())
		{
			auto& dest = trackedStates[destScript][destAdhocLevel];
			std::move(source.begin(), source.end(), std::insert_iterator(dest, dest.end()));
		}
	}

	// If source was an ad-hoc script, pop the save bank
	auto destOwner = trackedStates.find(destScript);
	if (destOwner != trackedStates.end() && destOwner->second.contains(destAdhocLevel + 1))
		destOwner->second.erase(destAdhocLevel + 1);
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
void TopLevelScript<TResource, TStateTracker>::EraseTrackedStates(Script<TResource>* currentScript, int64_t adhocLevel, int64_t firstFrame)
{
	if constexpr (std::is_same<TStateTracker, DefaultStateTracker<TResource>>::value)
		return;

	auto owner = trackedStates.find(currentScript);
	if (owner == trackedStates.end() || !owner->second.contains(adhocLevel))
		return;

	auto& states = owner->second[adhocLevel];
	states.erase(states.upper_bound(firstFrame), states.end());
}

template <derived_from_specialization_of<Resource> TResource, std::derived_from<Script<TResource>> TStateTracker>
const typename TStateTracker::CustomScriptStatus& TopLevelScript<TResource, TStateTracker>
	::GetTrackedStateInternal(Script<TResource>* currentScript, const InputsMetadata<TResource>& inputsMetadata)
{
	if constexpr (std::is_same<TStateTracker, DefaultStateTracker<TResource>>::value)
	{
		static const typename TStateTracker::CustomScriptStatus none {};
		return none;
	}
	else
	{
		{
			auto& states = trackedStates[inputsMetadata.stateOwner][inputsMetadata.stateOwnerAdhocLevel];
			auto found = states.find(inputsMetadata.frame);
			if (found != states.end())
				return found->second;
		}

		// `template` is required: `currentScript` has a dependent type, so without it GCC and
		// Clang parse `<` as less-than. MSVC accepts the omission (docs/compilers.md).
		auto status = currentScript->template ExecuteStateTracker<TStateTracker>(inputsMetadata.frame, stateTrackerFactory);

		// Looked up again: the tracker may have tracked other frames meanwhile. A state that
		// was not asserted is stored as a default so it is not recomputed.
		auto& state = trackedStates[inputsMetadata.stateOwner][inputsMetadata.stateOwnerAdhocLevel][inputsMetadata.frame];
		if (status.asserted)
			state = std::move(static_cast<typename TStateTracker::CustomScriptStatus&>(status));
		else
			state = typename TStateTracker::CustomScriptStatus();

		return state;
	}
}

#endif
