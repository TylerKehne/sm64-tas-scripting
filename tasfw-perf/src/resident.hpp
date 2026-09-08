#pragma once
#include <cstdint>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#else
#include <cstdio>
#include <unistd.h>
#endif

// Resident set of this process in bytes: the working set on Windows, the resident pages of
// /proc/self/statm on Linux. A platform-API fork of the kind docs/compilers.md allows. Used
// by the Tier B memory-per-slot rows only; not a hot path.
namespace tasfw_perf
{
	inline uint64_t ResidentBytes()
	{
#if defined(_WIN32)
		PROCESS_MEMORY_COUNTERS counters {};
		counters.cb = sizeof(counters);
		if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
			return 0;
		return uint64_t(counters.WorkingSetSize);
#else
		long pages = 0;
		long resident = 0;
		FILE* statm = std::fopen("/proc/self/statm", "r");
		if (!statm)
			return 0;
		if (std::fscanf(statm, "%ld %ld", &pages, &resident) != 2)
			resident = 0;
		std::fclose(statm);
		return uint64_t(resident) * uint64_t(sysconf(_SC_PAGESIZE));
#endif
	}
}
