#pragma once

#include <VerifyLayout.hpp>

#include <vector>

// The BitFS objects the scripts read by gObjectPool slot, with what the level script spawns
// there (the decomp's levels/bitfs/script.c; `dllcheck <dll> <m64> <frame> --objects` lists
// the live pool). Two objects run the tilting-pyramid behavior: the one the squish-cancel
// setup happens on is spawned at x = -1945 and sits in slot 84, the other at x = -2866 in
// slot 83; the track platform is slot 85. Scripts keep using the slots (ROADMAP 2.4); the
// pipeline hands this list to VerifyLayout before its first stage, so a run fails at start-up
// with a readable message if a slot ever holds something else.
inline const std::vector<ExpectedObject> BitFsExpectedObjects = {
	{84, "bhvBitfsTiltingInvertedPyramid", -1945.0f, -3225.0f, -715.0f},
	{83, "bhvBitfsTiltingInvertedPyramid", -2866.0f, -3225.0f, -715.0f},
	{85, "bhvPlatformOnTrack", -5744.0f, -3072.0f, 0.0f},
};
