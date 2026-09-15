#pragma once

#include <LibSm64.hpp>
#include <tasfw/Script.hpp>

#include <cstdint>
#include <vector>

// Where a movie is at every frame: plays the source movie from power-on to `frame` and
// records each frame at which the level or the area changes, with the course. This is how
// the frame a movie enters a level is found (the frame the tests, dllcheck and a stage's
// startFrame are given, and the frame two movies for different game versions are spliced
// at, docs/libsm64.md "US"). Reads through ReadState() like every script, the three
// symbols resolved once before the loop. Not a hot path: `dllcheck --levels` is the only
// caller. A transition's frame is the first frame whose state shows the new level: the
// state at frame f is what inputs 0..f-1 produced, so a movie spliced at f keeps inputs
// 0..f-1 of the first movie.
class LevelTransitions : public TopLevelScript<LibSm64>
{
public:
	struct Transition
	{
		int64_t frame;
		int16_t level;  // gCurrLevelNum (the decomp's LevelNum: 6 castle, 16 castle grounds, 19 BitFS)
		int16_t area;   // gCurrAreaIndex
		int16_t course; // gCurrCourseNum (17 for BitFS)
	};

	class CustomScriptStatus
	{
	public:
		std::vector<Transition> transitions;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	LevelTransitions(int64_t frame) : _frame(frame) {}

	bool validation() override { return true; }
	bool execution() override;
	bool assertion() override { return true; }

	// The decomp's name for a LevelNum (levels/level_defines.h order), "?" if out of range.
	static const char* LevelName(int16_t level);

private:
	int64_t _frame;
};
