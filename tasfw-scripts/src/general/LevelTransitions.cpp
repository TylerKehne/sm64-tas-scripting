#include <LevelTransitions.hpp>

namespace
{
	// enum LevelNum in the decomp's include/level_table.h: LEVEL_NONE, then levels/level_defines.h in order.
	const char* const LevelNames[] = {
		"none", "unknown_1", "unknown_2", "unknown_3", "bbh", "ccm", "castle", "hmc", "ssl", "bob",
		"sl", "wdw", "jrb", "thi", "ttc", "rr", "castle_grounds", "bitdw", "vcutm", "bitfs",
		"sa", "bits", "lll", "ddd", "wf", "ending", "castle_courtyard", "pss", "cotmc", "totwc",
		"bowser_1", "wmotr", "unknown_32", "bowser_2", "bowser_3", "unknown_35", "ttm", "unknown_37", "unknown_38",
	};
}

const char* LevelTransitions::LevelName(int16_t level)
{
	if (level < 0 || size_t(level) >= sizeof(LevelNames) / sizeof(LevelNames[0]))
		return "?";
	return LevelNames[level];
}

bool LevelTransitions::execution()
{
	const int16_t* level = static_cast<const int16_t*>(resource->addr("gCurrLevelNum"));
	const int16_t* area = static_cast<const int16_t*>(resource->addr("gCurrAreaIndex"));
	const int16_t* course = static_cast<const int16_t*>(resource->addr("gCurrCourseNum"));

	int16_t lastLevel = -1;
	int16_t lastArea = -1;
	for (int64_t frame = 0; frame <= _frame; frame++)
	{
		if (*level != lastLevel || *area != lastArea)
		{
			CustomStatus.transitions.push_back(Transition { frame, *level, *area, *course });
			lastLevel = *level;
			lastArea = *area;
		}
		if (frame < _frame)
			AdvanceFrameRead();
	}
	return true;
}
