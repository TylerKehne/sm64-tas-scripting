#pragma once
#include <cstdint>

// CPU cycles consumed so far by every thread of this process, when the platform counts
// them: QueryProcessCycleTime on Windows; on Linux a perf_event opened before main runs, so
// that every thread created afterwards inherits it, where the kernel permits one. Returns
// false when it cannot. bitfs-turn prints the difference per stage next to the wall time:
// cycles do not count time the threads spend descheduled, and a lower clock changes them
// only through memory stalls, which is what makes them worth printing next to the clock
// (docs/performance.md, Tier D). Spin-waits at barriers do count.
bool ProcessCycles(uint64_t& cycles);

// CPU time consumed so far by every thread of this process, user and kernel, in seconds
// (GetProcessTimes on Windows, CLOCK_PROCESS_CPUTIME_ID on Linux). Returns false when the
// platform cannot say. bitfs-turn divides the resource's own advance, save and load time by
// the difference per stage, so the summary says what share of the threads' time the
// framework, the scripts and the search took (docs/performance.md, "Tier D").
bool ProcessCpuSeconds(double& seconds);
