#pragma once
#include <cstdlib>
#include <string>

// An environment variable as a string, empty when it is unset. The tests and benchmarks
// read the optional game inputs this way (TASFW_LIBSM64, TASFW_M64, TASFW_FRAME).
// MSVC's CRT marks std::getenv deprecated; clang-cl warns on it at its default level and
// the warning is an error under TASFW_WARNINGS_AS_ERRORS. tasfw-testing defines
// _CRT_SECURE_NO_WARNINGS for its consumers rather than forking to _dupenv_s here
// (docs/compilers.md).
namespace tasfw::testing
{
	inline std::string Env(const char* name)
	{
		const char* value = std::getenv(name);
		return value ? std::string(value) : std::string();
	}
}
