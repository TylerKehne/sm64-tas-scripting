#include "ProcessCycles.hpp"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>

bool ProcessCycles(uint64_t& cycles)
{
	ULONG64 value = 0;
	if (!QueryProcessCycleTime(GetCurrentProcess(), &value))
		return false;
	cycles = value;
	return true;
}

bool ProcessCpuSeconds(double& seconds)
{
	FILETIME creation, exit, kernel, user;
	if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
		return false;
	auto ticks = [](const FILETIME& t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
	seconds = double(ticks(kernel) + ticks(user)) / 1e7; // 100 ns units
	return true;
}

#elif defined(__linux__)
#include <cstring>
#include <ctime>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

bool ProcessCpuSeconds(double& seconds)
{
	timespec t;
	if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t) != 0)
		return false;
	seconds = double(t.tv_sec) + double(t.tv_nsec) / 1e9;
	return true;
}

namespace
{
	// Opened during static initialisation, before any thread exists, so that the OpenMP
	// workers inherit it; a read sums the counter over the process and every inherited
	// thread. The kernel refuses it in most containers and under a strict
	// perf_event_paranoid (Ubuntu ships 4); the stage summary then has no cycles line.
	int OpenCounter()
	{
		perf_event_attr attr;
		std::memset(&attr, 0, sizeof attr);
		attr.type = PERF_TYPE_HARDWARE;
		attr.size = sizeof attr;
		attr.config = PERF_COUNT_HW_CPU_CYCLES;
		attr.inherit = 1;
		attr.exclude_hv = 1;
		return int(syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
	}

	const int g_counter = OpenCounter();
}

bool ProcessCycles(uint64_t& cycles)
{
	if (g_counter < 0)
		return false;
	uint64_t value = 0;
	if (read(g_counter, &value, sizeof value) != ssize_t(sizeof value))
		return false;
	cycles = value;
	return true;
}

#else

bool ProcessCycles(uint64_t&)
{
	return false;
}

bool ProcessCpuSeconds(double&)
{
	return false;
}

#endif
