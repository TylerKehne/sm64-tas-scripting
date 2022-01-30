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

	//Rollback state regardless of verification results
	load_state(&_initialSave);

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
	
	//Rollback state if there are any execution errors
	if (!executed)
		load_state(&_initialSave);

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

		//Rollback state only if validation throws exception
		load_state(&_initialSave);
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

void Script::advance_frame()
{
	Inputs::set_inputs(GetInputs(game.getCurrentFrame()));
	game.advance_frame();
}

void Script::advance_frame(Inputs inputs)
{
	//Save inputs to diff
	BaseStatus.m64Diff.frames[game.getCurrentFrame()] = inputs;

	Inputs::set_inputs(inputs);
	game.advance_frame();
}

void Script::load_state(Slot* saveState)
{
	game.load_state(saveState);

	uint64_t currentFrame = game.getCurrentFrame();
	auto firstInvalidFrame = BaseStatus.m64Diff.frames.upper_bound(currentFrame);
	BaseStatus.m64Diff.frames.erase(firstInvalidFrame, BaseStatus.m64Diff.frames.end());
}

void Script::Apply(M64Diff* m64Diff)
{
	if (!m64Diff->frames.size())
	{
		game.load_state(&_initialSave);
		return;
	}

	uint64_t firstFrame = m64Diff->frames.begin()->first;
	uint64_t lastFrame = m64Diff->frames.rbegin()->first;
	uint64_t currentFrame = game.getCurrentFrame();

	//Replay from start if diff precedes script state
	if (currentFrame > firstFrame)
	{
		game.load_state(&game.startSave);
		currentFrame = 0;
	}	

	while (currentFrame <= lastFrame)
	{
		//Use default inputs if diff doesn't override them
		auto inputs = GetInputs(currentFrame);
		if (m64Diff->frames.count(currentFrame))
		{
			inputs = m64Diff->frames[currentFrame];
			BaseStatus.m64Diff.frames[currentFrame] = inputs;
		}

		Inputs::set_inputs(inputs);
		game.advance_frame();

		currentFrame = game.getCurrentFrame();
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

void Script::GoToFrame(uint64_t frame)
{
	uint64_t currentFrame = game.getCurrentFrame();

	if (currentFrame > frame)
		StartToFrame(frame);
	else
	{
		while (currentFrame < frame)
		{
			advance_frame();
			currentFrame = game.getCurrentFrame();
		}
	}
}

void Script::StartToFrame(uint64_t frame)
{
	game.load_state(&game.startSave);

	for (uint64_t i = 0; i < frame; i++)
	{
		advance_frame();
	}
}
