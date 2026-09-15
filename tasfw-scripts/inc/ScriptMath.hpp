#pragma once
#include <sm64/Types.hpp>

// Arithmetic the BitFS scripts share. Static members, the way the framework keeps its helpers
// (AGENTS.md, "Conventions"); they lived on Script until ROADMAP 3.2 took them off its surface.
class ScriptMath
{
public:
	// -1, 0 or 1.
	template <typename T>
	static int Sign(T value)
	{
		return (T(0) < value) - (value < T(0));
	}

	static void CopyVec3f(Vec3f dest, Vec3f source)
	{
		dest[0] = source[0];
		dest[1] = source[1];
		dest[2] = source[2];
	}
};
