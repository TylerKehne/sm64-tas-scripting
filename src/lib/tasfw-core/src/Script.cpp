#include <tasfw/Script.hpp>
#include <chrono>

bool Script::checkPreconditions()
{
	bool validated = false;

	auto start = std::chrono::high_resolution_clock::now();
	try
	{
		validated = validation();
	}
	catch (std::exception& e)
	{
		BaseStatus.validationThrew = true;
	}
	auto finish = std::chrono::high_resolution_clock::now();

	BaseStatus.validationDuration =
		std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
			.count();

	// Revert state regardless of validation results
	Restore(_initialFrame);

	BaseStatus.validated = validated;
	return validated;
}

bool Script::execute()
{
	bool executed = false;

	auto start = std::chrono::high_resolution_clock::now();
	try
	{
		executed = execution();
	}
	catch (std::exception& e)
	{
		BaseStatus.executionThrew = true;
	}
	auto finish = std::chrono::high_resolution_clock::now();

	BaseStatus.executionDuration =
		std::chrono::duration_cast<std::chrono::milliseconds>(finish - start)
			.count();

	// Revert state if there are any execution errors
	if (!executed)
		Restore(_initialFrame);

	BaseStatus.executed = executed;
	return executed;
}

bool Script::checkPostconditions()
{
	bool asserted = false;

	auto start = std::chrono::high_resolution_clock::now();
	try
	{
		asserted = assertion();
	}
	catch (std::exception& e)
	{
		BaseStatus.assertionThrew = true;

		// Revert state only if assertion throws exception
		Restore(_initialFrame);
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
	if (BaseStatus.m64Diff.frames.contains(frame))
		return BaseStatus.m64Diff.frames[frame];
	else if (_m64.frames.contains(frame))
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
	uint64_t currentFrame = GetCurrentFrame();
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
	uint64_t lastFrame = m64Diff.frames.rbegin()->first;

	Load(firstFrame);

	// Erase all saves after this point
	uint64_t currentFrame = GetCurrentFrame();
	auto firstInvalidFrame = saveBank.upper_bound(currentFrame);
	saveBank.erase(firstInvalidFrame, saveBank.end());

	while (currentFrame <= lastFrame)
	{
		// Use default inputs if diff doesn't override them
		auto inputs = GetInputs(currentFrame);
		if (m64Diff.frames.contains(currentFrame))
		{
			inputs = m64Diff.frames.at(currentFrame);
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
	if (BaseStatus.m64Diff.frames.contains(frame))
		return BaseStatus.m64Diff.frames[frame];
	else if (_parentScript)
		return _parentScript->GetInputs(frame);

	return Inputs(0, 0, 0);
}

std::pair<uint64_t, SlotHandle*> Script::GetLatestSave(uint64_t frame)
{
	auto save =
		saveBank.empty() ? saveBank.end() : std::prev(saveBank.upper_bound(frame));
	if (save == saveBank.end())
	{
		Script* parentScript = _parentScript;
		// Don't search past start of m64 diff to avoid desync
		uint64_t earlyFrame = !BaseStatus.m64Diff.frames.empty() ?
			std::min(BaseStatus.m64Diff.frames.begin()->first, frame) :
			frame;
		if (parentScript)
			return parentScript->GetLatestSave(earlyFrame);
		else
			return {0, &game->startSaveHandle};
	}

	return {(*save).first, &(*save).second};
}

void Script::Load(uint64_t frame)
{
	// Load most recent save at or before frame. Check child saves before
	// parent.
	if (frame < GetCurrentFrame())
	{
		game->load_state(GetLatestSave(frame).second->slotId);
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

	Load(frame);
}

// Same as Rollback, but starts from current frame. Useful for scripts that edit past frames
void Script::RollForward(int64_t frame)
{
	// Roll back diff and savebank to current frame. Note that roll forward on diff
	// includes current frame.
	uint64_t currentFrame = GetCurrentFrame();
	BaseStatus.m64Diff.frames.erase(
		BaseStatus.m64Diff.frames.lower_bound(currentFrame),
		BaseStatus.m64Diff.frames.end());
	saveBank.erase(saveBank.upper_bound(currentFrame), saveBank.end());

	Load(frame);
}

// Load and clear diff and savebank
void Script::Restore(int64_t frame)
{
	// Clear diff and savebank
	BaseStatus.m64Diff.frames.erase(
		BaseStatus.m64Diff.frames.begin(),
		BaseStatus.m64Diff.frames.end());
	saveBank.erase(saveBank.begin(), saveBank.end());

	Load(frame);
}

void Script::Save()
{
	//Desyncs should always clear future saves, so if a save already exists there is no need to overwrite it
	uint64_t currentFrame = GetCurrentFrame();
	if (!saveBank.contains(currentFrame))
	{
		saveBank.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(currentFrame),
			std::forward_as_tuple(game, game->save_state(this, currentFrame)));
		BaseStatus.nSaves++;
	}
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

void Script::DeleteSave(int64_t frame)
{
	saveBank.erase(frame);
}

void Script::SetInputs(Inputs inputs)
{
	uint16_t* buttonDllAddr = (uint16_t*) game->addr("gControllerPads");
	buttonDllAddr[0] = inputs.buttons;

	int8_t* xStickDllAddr = (int8_t*) game->addr("gControllerPads") + 2;
	xStickDllAddr[0] = inputs.stick_x;

	int8_t* yStickDllAddr = (int8_t*) game->addr("gControllerPads") + 3;
	yStickDllAddr[0] = inputs.stick_y;
}

// Load method specifically for Script.Execute(), checks for desyncs
void Script::Revert(uint64_t frame, const M64Diff& m64)
{
	// Check if script altered state
	bool desync =
		(!m64.frames.empty()) && (m64.frames.begin()->first < GetCurrentFrame());

	// Load most recent save at or before frame. Check child saves before
	// parent.
	if (desync || frame < GetCurrentFrame())
	{
		game->load_state(GetLatestSave(frame).second->slotId);
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
