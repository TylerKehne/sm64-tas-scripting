#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

#include <LibSm64.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/Script.hpp>
#include <sm64/ObjectFields.hpp>
#include <sm64/Types.hpp>

// Replays each diff from `startFrame` in a sandbox and writes the resulting movie, named by
// index, the pyramid normal and Mario's speed at the end of the diff.
class ExportSolutions : public TopLevelScript<LibSm64>
{
public:
	ExportSolutions(int64_t startFrame, const std::vector<M64Diff>& diffs, std::filesystem::path directory)
		: _startFrame(startFrame), _diffs(diffs), _directory(std::move(directory)) {}

	bool validation() override { return true; }

	bool execution() override
	{
		MarioState* marioState = *(MarioState**)(resource->addr("gMarioState"));
		Object* objectPool = (Object*)(resource->addr("gObjectPool"));
		Object* pyramid = &objectPool[84]; // level-specific index; AGENTS.md, known problems

		LongLoad(_startFrame);

		int index = 0;
		for (const M64Diff& diff : _diffs)
		{
			ExecuteAdhoc([&]()
				{
					Apply(diff);

					char name[192];
					std::snprintf(name, sizeof name, "%04d_%f_%f_%f_%f_%f.m64", index,
						pyramid->oTiltingPyramidNormalX, pyramid->oTiltingPyramidNormalY, pyramid->oTiltingPyramidNormalZ,
						marioState->forwardVel, marioState->vel[1]);
					ExportM64(_directory / name);
					return true;
				});
			index++;
		}
		return true;
	}

	bool assertion() override { return true; }

private:
	int64_t _startFrame;
	const std::vector<M64Diff>& _diffs;
	std::filesystem::path _directory;
};
