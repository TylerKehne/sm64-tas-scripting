#pragma once

#include <LibSm64.hpp>
#include <tasfw/Inputs.hpp>
#include <tasfw/Script.hpp>

#include <cstdint>
#include <filesystem>
#include <utility>

// Joins two movies inside the framework. On a resource whose source movie is the first one,
// plays to `firstFrame` (the state inputs 0..firstFrame-1 produced), then plays the second
// movie's inputs from `secondFrame` to its end as this script's own diff (Apply), so frame
// secondFrame + k of the second movie is frame firstFrame + k of the result, and exports
// the whole as `out` (ExportM64: the source movie up to the diff, then the diff). Every
// frame of the result was advanced by the game (hard rule 1), so what is written is what
// it played. This is how a movie for one game version is made from a movie that reaches a
// level on that version and a movie that does the work inside it on another: a level's
// physics is the same on JP and US, the way there is not (docs/libsm64.md). The two frames
// are the movies' frames of the same level transition, which LevelTransitions finds;
// m64splice is the tool that does both.
class SpliceMovie : public TopLevelScript<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		int64_t frames = 0; // inputs in the result
		bool exported = false;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	SpliceMovie(int64_t firstFrame, const M64& second, int64_t secondFrame, std::filesystem::path out) :
		_firstFrame(firstFrame), _second(second), _secondFrame(secondFrame), _out(std::move(out))
	{
	}

	bool validation() override { return _firstFrame >= 0 && _secondFrame >= 0; }

	bool execution() override
	{
		// The second movie from its frame, re-based onto ours. Apply plays a diff at the diff's
		// own frames and GetInputs reads only this script's movie, so the re-basing is the one
		// step done here; a framework helper for it was considered and not wanted (maintainer,
		// 2026-09-13), this being its only use.
		M64Diff diff;
		for (auto it = _second.frames.lower_bound(uint64_t(_secondFrame)); it != _second.frames.end(); ++it)
			diff.frames[it->first - uint64_t(_secondFrame) + uint64_t(_firstFrame)] = it->second;
		if (diff.frames.empty())
			return false;

		LongLoad(_firstFrame);
		Apply(diff);
		CustomStatus.frames = int64_t(GetCurrentFrame());
		CustomStatus.exported = ExportM64(_out);
		return CustomStatus.exported;
	}

	bool assertion() override { return CustomStatus.exported; }

private:
	int64_t _firstFrame;
	const M64& _second;
	int64_t _secondFrame;
	std::filesystem::path _out;
};
