#pragma once

#include <tasfw/Script.hpp>
#include <tasfw/resources/LibSm64.hpp>
#include <tasfw/resources/PyramidUpdate.hpp>
#include <tasfw/scripts/BitFSPyramidOscillation.hpp>

#ifndef BITFS_SC_APPROACH_H
#define BITFS_SC_APPROACH_H

class BitFsScApproach : public Script<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		bool diveLanded = false;
		bool drLanded = false;
		float diveRelHeight = 0;
		float drRelHeight = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsScApproach(int16_t roughTargetAngle, int quadrant, const ScriptStatus<BitFsPyramidOscillation>& oscStatus)
		: _oscStatus(oscStatus), _roughTargetAngle(roughTargetAngle), _quadrant(quadrant) { }

	bool validation();
	bool execution();
	bool assertion();

private:
	const ScriptStatus<BitFsPyramidOscillation>& _oscStatus;
	int16_t _roughTargetAngle = 0;
	int _quadrant = 1;
	int64_t _minFrame = -1;
	int64_t _maxFrame = -1;
	int64_t _minFramePrev = -1;
	int64_t _maxFramePrev = -1;
	float _targetXzSum = 0;
	float _prevMaxSpeed = 0;
	int16_t _lastOscAngle = 0;
};

class BitFsScApproach_AttemptDr_BF : public Script<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		bool diveLanded = false;
		bool drLanded = false;
		float diveRelHeight = 0;
		float drRelHeight = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsScApproach_AttemptDr_BF(int16_t roughTargetAngle, int64_t minFrame, int64_t maxFrame)
		: _roughTargetAngle(roughTargetAngle), _minFrame(minFrame), _maxFrame(maxFrame) { }

	bool validation();
	bool execution();
	bool assertion();

private:
	int16_t _roughTargetAngle = 0;
	int64_t _minFrame = -1;
	int64_t _maxFrame = -1;
	float _initialXzSum = 0;
};

class BitFsScApproach_AttemptDr : public Script<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		bool terminate = false;
		bool diveLanded = false;
		bool drLanded = false;
		float diveRelHeight = 0;
		float drRelHeight = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	BitFsScApproach_AttemptDr() { }

	bool validation();
	bool execution();
	bool assertion();
};

#endif