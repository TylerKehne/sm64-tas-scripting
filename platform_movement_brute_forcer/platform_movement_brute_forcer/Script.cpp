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

	BaseStatus.verificationDuration = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count();

	//Revert state regardless of verification results
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

	BaseStatus.executionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count();
	
	//Revert state if there are any execution errors
	if (!executed)
		Load(_initialFrame);

	BaseStatus.executed = executed;
	return executed;
}

bool Script::validate()
{
	bool validated = false;

	auto start = std::chrono::high_resolution_clock::now();
	try
	{
		validated = validation();
	}
	catch (exception& e)
	{
		BaseStatus.validationThrew = true;

		//Revert state only if validation throws exception
		Load(_initialFrame);
	}
	auto finish = std::chrono::high_resolution_clock::now();

	BaseStatus.validationDuration = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count();

	BaseStatus.validated = validated;
	return validated;
}

void Script::CopyVec3f(Vec3f dest, Vec3f source)
{
	dest[0] = source[0];
	dest[1] = source[1];
	dest[2] = source[2];
}

void Script::AdvanceFrameRead()
{
	SetInputs(GetInputs(game->getCurrentFrame()));
	game->advance_frame();
	BaseStatus.nFrameAdvances++;
}

void Script::AdvanceFrameWrite(Inputs inputs)
{
	//Save inputs to diff
	BaseStatus.m64Diff.frames[game->getCurrentFrame()] = inputs;

	//Erase all saves after this point
	uint64_t currentFrame = game->getCurrentFrame();
	auto firstInvalidFrame = saveBank.upper_bound(currentFrame);
	saveBank.erase(firstInvalidFrame, saveBank.end());

	//Set inputs and advance frame
	SetInputs(inputs);
	game->advance_frame();
	BaseStatus.nFrameAdvances++;
}

void Script::Apply(const M64Diff& m64Diff)
{
	if (!m64Diff.frames.size())
		return;

	uint64_t firstFrame = m64Diff.frames.begin()->first;
	uint64_t lastFrame = m64Diff.frames.rbegin()->first;

	Load(firstFrame);

	//Erase all saves after this point
	uint64_t currentFrame = game->getCurrentFrame();
	auto firstInvalidFrame = saveBank.upper_bound(currentFrame);
	saveBank.erase(firstInvalidFrame, saveBank.end());

	while (currentFrame <= lastFrame)
	{
		//Use default inputs if diff doesn't override them
		auto inputs = GetInputs(currentFrame);
		if (m64Diff.frames.count(currentFrame))
		{
			inputs = m64Diff.frames.at(currentFrame);
			BaseStatus.m64Diff.frames[currentFrame] = inputs;
		}

		SetInputs(inputs);
		game->advance_frame();
		BaseStatus.nFrameAdvances++;

		currentFrame = game->getCurrentFrame();
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

std::pair<uint64_t, Slot*> Script::GetLatestSave(Script* script, uint64_t frame)
{
	auto save = script->saveBank.size() ? std::prev(script->saveBank.upper_bound(frame)) : script->saveBank.end();
	if (save == script->saveBank.end())
	{
		Script* parentScript = script->_parentScript;
		//Don't search past start of m64 diff to avoid desync
		uint64_t earlyFrame = script->BaseStatus.m64Diff.frames.size() ? min(script->BaseStatus.m64Diff.frames.begin()->first, frame) : frame;
		if (parentScript)
			return GetLatestSave(parentScript, earlyFrame);
		else
			return std::pair<uint64_t, Slot*>(0, &game->startSave);
	}

	return std::pair<uint64_t, Slot*>((*save).first, &(*save).second);
}

void Script::Load(uint64_t frame)
{
	//Load most recent save at or before frame. Check child saves before parent.
	if (frame < game->getCurrentFrame())
	{
		game->load_state(GetLatestSave(this, frame).second);
		BaseStatus.nLoads++;
	}

	//If save is before target frame, play back until frame is reached
	uint64_t currentFrame = game->getCurrentFrame();
	while (currentFrame < frame)
	{
		AdvanceFrameRead();
		currentFrame = game->getCurrentFrame();
	}
}

void Script::Rollback(uint64_t frame)
{
	//Roll back diff and savebank to target frame. Note that rollback on diff includes target frame.
	BaseStatus.m64Diff.frames.erase(BaseStatus.m64Diff.frames.lower_bound(frame), BaseStatus.m64Diff.frames.end());
	saveBank.erase(saveBank.upper_bound(frame), saveBank.end());

	//Load most recent save at or before frame. Check child saves before parent.
	if (frame < game->getCurrentFrame())
	{
		game->load_state(GetLatestSave(this, frame).second);
		BaseStatus.nLoads++;
	}

	//If save is before target frame, play back until frame is reached
	uint64_t currentFrame = game->getCurrentFrame();
	while (currentFrame < frame)
	{
		AdvanceFrameRead();
		currentFrame = game->getCurrentFrame();
	}
}

void Script::Save()
{
	auto start = std::chrono::high_resolution_clock::now();

	uint64_t currentFrame = game->getCurrentFrame();
	saveBank[currentFrame] = game->alloc_slot();
	game->save_state(&saveBank[currentFrame]);
	BaseStatus.nSaves++;

	auto finish = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count();

	//TODO: time in game class
	if (!game->nAllocSlots)
		game->_avgAllocSlotTime = duration;
	else
		game->_avgAllocSlotTime = (game->_avgAllocSlotTime * game->nAllocSlots + duration) / (game->nAllocSlots + 1);
		
	game->nAllocSlots++;
}

void Script::Save(uint64_t frame)
{
	Load(frame);
	Save();
}

void Script::OptionalSave()
{
	OptionalSave(game->getCurrentFrame());
}

void Script::OptionalSave(uint64_t frame)
{
	uint64_t currentFrame = game->getCurrentFrame();
	if (game->shouldSave(currentFrame - GetLatestSave(this, frame).first))
		Save();
}

void Script::SetInputs(Inputs inputs)
{
	uint16_t* buttonDllAddr = (uint16_t*)game->addr("gControllerPads");
	buttonDllAddr[0] = inputs.buttons;

	int8_t* xStickDllAddr = (int8_t*)(game->addr("gControllerPads") + 2);
	xStickDllAddr[0] = inputs.stick_x;

	int8_t* yStickDllAddr = (int8_t*)(game->addr("gControllerPads") + 3);
	yStickDllAddr[0] = inputs.stick_y;
}

// Load method specifically for Script.Execute(), checks for desyncs
void Script::Revert(uint64_t frame, const M64Diff& m64)
{
	//Check if script altered state
	bool desync = m64.frames.size() && (m64.frames.begin()->first < game->getCurrentFrame());

	//Load most recent save at or before frame. Check child saves before parent.
	if (desync || frame < game->getCurrentFrame())
	{
		game->load_state(GetLatestSave(this, frame).second);
		BaseStatus.nLoads++;
	}

	//If save is before target frame, play back until frame is reached
	uint64_t currentFrame = game->getCurrentFrame();
	while (currentFrame < frame)
	{
		AdvanceFrameRead();
		currentFrame = game->getCurrentFrame();
	}
}

