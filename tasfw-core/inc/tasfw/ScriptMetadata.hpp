#pragma once
#include <cstdint>
#include <tasfw/Concepts.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/Resource.hpp>
#include <tasfw/SlotHandle.hpp>

#ifndef SCRIPTMETADATA_H
#define SCRIPTMETADATA_H

template <derived_from_specialization_of<Resource> TResource>
class Script;

template <derived_from_specialization_of<Resource> TResource>
class InputsMetadata
{
public:
	enum class InputsSource : int8_t
	{
		DIFF = 0,
		ORIGINAL = 1,
		DEFAULT = 2
	};

	Inputs inputs;
	int64_t frame = -1;
	Script<TResource>* stateOwner = nullptr;
	int64_t stateOwnerAdhocLevel = -1;
	InputsSource source = InputsSource::DIFF;

	InputsMetadata() = default;

	InputsMetadata(Inputs inputs, int64_t frame, Script<TResource>* stateOwner, int64_t stateOwnerAdhocLevel, InputsSource source = InputsSource::DIFF)
		: inputs(inputs), frame(frame), stateOwner(stateOwner), stateOwnerAdhocLevel(stateOwnerAdhocLevel), source(source) {}
};

template <derived_from_specialization_of<Resource> TResource>
class SaveMetadata
{
public:
	Script<TResource>* script = nullptr; //ancestor script that won't go out of scope
	int64_t frame = -1;
	int64_t adhocLevel = -1;
	bool isStartSave = false;

	SaveMetadata() = default;

	SaveMetadata(Script<TResource>* script, int64_t frame, int64_t adhocLevel, bool isStartSave = false)
		: script(script), frame(frame), adhocLevel(adhocLevel), isStartSave(isStartSave) { }

	SlotHandle<TResource>* GetSlotHandle();
	bool IsValid();
};

//Include template method implementations
#include "tasfw/ScriptMetadata.t.hpp"

#endif
