#include "Script.hpp"
#include <chrono>

bool Script::verify()
{
	bool verified = false;

	auto start = std::chrono::high_resolution_clock::now();
	try
	{
		verified = verification();
	}
	catch (exception& e)
	{
		BaseStatus.verificationThrew = true;
	}
	auto finish = std::chrono::high_resolution_clock::now();

	BaseStatus.verificationDuration =
		std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
			.count();

	// Revert state regardless of verification results
	Load(_initialFrame);

	BaseStatus.verified = verified;
	return verified;
}

bool Script::execute()
{
	bool executed = false;

	auto start = std::chrono::high_resolution_clock::now();
	try
	{
		executed = execution();
	}
	catch (exception& e)
	{
		BaseStatus.executionThrew = true;
	}
	auto finish = std::chrono::high_resolution_clock::now();

	BaseStatus.executionDuration =
		std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
			.count();

	// Revert state if there are any execution errors
	if (!executed)
		Load(_initialFrame);

	BaseStatus.executed = executed;
	return executed;
}

bool Script::assert()
{
	bool asserted = false;

	auto start = std::chrono::high_resolution_clock::now();
	try
	{
		asserted = assertion();
	}
	catch (exception& e)
	{
		BaseStatus.assertionThrew = true;

		// Revert state only if assertion throws exception
		Load(_initialFrame);
	}
	auto finish = std::chrono::high_resolution_clock::now();

	BaseStatus.assertionDuration =
		std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
			.count();

	BaseStatus.asserted = asserted;
	return asserted;
}

Inputs TopLevelScript::GetInputs(uint64_t frame)
{
	if (BaseStatus.m64Diff.frames.count(frame))
		return BaseStatus.m64Diff.frames[frame];
	else if (_m64.frames.count(frame))
		return _m64.frames[frame];

	return Inputs(0, 0, 0);
}

void Script::CopyVec3f(Vec3f dest, Vec3f source)
{
	dest[0] = source[0];
	dest[1] = source[1];
	dest[2] = source[2];
}

uint64_t Script::GetCurrentFrame()
{
	return game->getCurrentFrame();
}

void Script::AdvanceFrameRead()
{
	SetInputs(GetInputs(GetCurrentFrame()));
	game->advance_frame();
	BaseStatus.nFrameAdvances++;
}

void Script::AdvanceFrameWrite(Inputs inputs)
{
	// Save inputs to diff
	BaseStatus.m64Diff.frames[GetCurrentFrame()] = inputs;

	// Erase all saves after this point
	uint64_t currentFrame	 = GetCurrentFrame();
	auto firstInvalidFrame = saveBank.upper_bound(currentFrame);
	saveBank.erase(firstInvalidFrame, saveBank.end());

	// Set inputs and advance frame
	SetInputs(inputs);
	game->advance_frame();
	BaseStatus.nFrameAdvances++;
}

void Script::Apply(const M64Diff& m64Diff)
{
	if (m64Diff.frames.empty())
		return;

	uint64_t firstFrame = m64Diff.frames.begin()->first;
	uint64_t lastFrame	= m64Diff.frames.rbegin()->first;

	Load(firstFrame);

	// Erase all saves after this point
	uint64_t currentFrame	 = GetCurrentFrame();
	auto firstInvalidFrame = saveBank.upper_bound(currentFrame);
	saveBank.erase(firstInvalidFrame, saveBank.end());

	while (currentFrame <= lastFrame)
	{
		// Use default inputs if diff doesn't override them
		auto inputs = GetInputs(currentFrame);
		if (m64Diff.frames.count(currentFrame))
		{
			inputs																	= m64Diff.frames.at(currentFrame);
			BaseStatus.m64Diff.frames[currentFrame] = inputs;
		}

		SetInputs(inputs);
		game->advance_frame();
		BaseStatus.nFrameAdvances++;

		currentFrame = GetCurrentFrame();
	}
}

Inputs Script::GetInputs(uint64_t frame)
{
	if (BaseStatus.m64Diff.frames.count(frame))
		return BaseStatus.m64Diff.frames[frame];
	else if (_parentScript)
		return _parentScript->GetInputs(frame);

	return Inputs(0, 0, 0);
}

std::pair<uint64_t, Slot*> Script::GetLatestSave(uint64_t frame)
{
	auto save =
		saveBank.size() ? std::prev(saveBank.upper_bound(frame)) : saveBank.end();
	if (save == saveBank.end())
	{
		Script* parentScript = _parentScript;
		// Don't search past start of m64 diff to avoid desync
		uint64_t earlyFrame = !BaseStatus.m64Diff.frames.empty() ?
			min(BaseStatus.m64Diff.frames.begin()->first, frame) :
			frame;
		if (parentScript)
			return parentScript->GetLatestSave(earlyFrame);
		else
			return {0, &game->startSave};
	}

	return {(*save).first, &(*save).second};
}

bool Script::RemoveEarliestSave(uint64_t earliestFrame)
{
	auto save						 = saveBank.begin();
	Script* parentScript = _parentScript;

	// Keep track of the earliest save frame as we go up the hierarchy
	if (save != saveBank.end())
	{
		if (!earliestFrame)
			earliestFrame = (*save).first;
		else
			earliestFrame = earliestFrame < (*save).first ?
				earliestFrame :
				(*save).first;	// Remove from parent if tied
	}

	// Remove save from the parent if there is an earlier one
	if (parentScript)
	{
		if (save == saveBank.end())
			return parentScript->RemoveEarliestSave(earliestFrame);

		if (!parentScript->RemoveEarliestSave(earliestFrame))
		{
			// Parent has no earlier saves, so remove from this script if it has
			// the earliest save
			if (earliestFrame == (*save).first)
			{
				saveBank.erase(earliestFrame);
				return true;
			}

			return false;
		}

		return true;
	}

	if (save == saveBank.end())
		return false;

	// This is the top level script, so remove from this script if it has the
	// earliest save
	if (earliestFrame == (*save).first)
	{
		saveBank.erase(earliestFrame);
		return true;
	}

	return false;
}

void Script::Load(uint64_t frame)
{
	// Load most recent save at or before frame. Check child saves before
	// parent.
	if (frame < GetCurrentFrame())
	{
		game->load_state(GetLatestSave(frame).second);
		BaseStatus.nLoads++;
	}

	// If save is before target frame, play back until frame is reached
	uint64_t currentFrame = GetCurrentFrame();
	while (currentFrame < frame)
	{
		AdvanceFrameRead();
		currentFrame = GetCurrentFrame();
	}
}

void Script::Rollback(uint64_t frame)
{
	// Roll back diff and savebank to target frame. Note that rollback on diff
	// includes target frame.
	BaseStatus.m64Diff.frames.erase(
		BaseStatus.m64Diff.frames.lower_bound(frame),
		BaseStatus.m64Diff.frames.end());
	saveBank.erase(saveBank.upper_bound(frame), saveBank.end());

	// Load most recent save at or before frame. Check child saves before
	// parent.
	if (frame < GetCurrentFrame())
	{
		game->load_state(GetLatestSave(frame).second);
		BaseStatus.nLoads++;
	}

	// If save is before target frame, play back until frame is reached
	uint64_t currentFrame = GetCurrentFrame();
	while (currentFrame < frame)
	{
		AdvanceFrameRead();
		currentFrame = GetCurrentFrame();
	}
}

void Script::Save()
{
	// If save memory is full, remove the earliest save and try again
	uint64_t currentFrame = GetCurrentFrame();
	while (!game->save_state(&saveBank[currentFrame]))
	{
		saveBank.erase(currentFrame);
		RemoveEarliestSave();
	}

	BaseStatus.nSaves++;
}

void Script::Save(uint64_t frame)
{
	Load(frame);
	Save();
}

void Script::OptionalSave()
{
	uint64_t currentFrame = GetCurrentFrame();
	if (game->shouldSave(currentFrame - GetLatestSave(currentFrame).first))
		Save();
}

void Script::SetInputs(Inputs inputs)
{
	uint16_t* buttonDllAddr = (uint16_t*) game->addr("gControllerPads");
	buttonDllAddr[0]				= inputs.buttons;

	int8_t* xStickDllAddr = (int8_t*) game->addr("gControllerPads") + 2;
	xStickDllAddr[0]			= inputs.stick_x;

	int8_t* yStickDllAddr = (int8_t*) game->addr("gControllerPads") + 3;
	yStickDllAddr[0]			= inputs.stick_y;
}

// Load method specifically for Script.Execute(), checks for desyncs
void Script::Revert(uint64_t frame, const M64Diff& m64)
{
	// Check if script altered state
	bool desync =
		m64.frames.size() && (m64.frames.begin()->first < GetCurrentFrame());

	// Load most recent save at or before frame. Check child saves before
	// parent.
	if (desync || frame < GetCurrentFrame())
	{
		game->load_state(GetLatestSave(frame).second);
		BaseStatus.nLoads++;
	}

	// If save is before target frame, play back until frame is reached
	uint64_t currentFrame = GetCurrentFrame();
	while (currentFrame < frame)
	{
		AdvanceFrameRead();
		currentFrame = GetCurrentFrame();
	}
}
