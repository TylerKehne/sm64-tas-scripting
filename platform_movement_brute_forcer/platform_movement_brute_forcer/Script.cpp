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
	game.load_state(&_initialSave);

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
		game.load_state(&_initialSave);

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
		game.load_state(&_initialSave);
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
