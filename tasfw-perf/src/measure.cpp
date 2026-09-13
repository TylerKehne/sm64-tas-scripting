#include "measure.hpp"

#include "alloc_counter.hpp"

#if defined(_WIN32)
	#define NOMINMAX
	#include <windows.h>
#elif defined(__linux__)
	#include <cstring>
	#include <linux/perf_event.h>
	#include <sys/syscall.h>
	#include <unistd.h>
#endif

namespace
{
#if defined(_WIN32)
	// User plus kernel cycles of the calling thread; a deliberate page fault in the dirty
	// save mode is real cost and counts.
	bool ThreadCycles(uint64_t& cycles)
	{
		ULONG64 value = 0;
		if (!QueryThreadCycleTime(GetCurrentThread(), &value))
			return false;
		cycles = value;
		return true;
	}
#elif defined(__linux__)
	// One hardware cycle counter per thread, opened on first use. The kernel refuses it in
	// most containers and under a strict perf_event_paranoid (Ubuntu ships 4); the
	// benchmarks then carry no "cycles" counter, and nothing else changes.
	bool ThreadCycles(uint64_t& cycles)
	{
		thread_local int fd = -2;   // -2: not opened yet; -1: refused
		if (fd == -2)
		{
			perf_event_attr attr;
			std::memset(&attr, 0, sizeof attr);
			attr.type = PERF_TYPE_HARDWARE;
			attr.size = sizeof attr;
			attr.config = PERF_COUNT_HW_CPU_CYCLES;
			attr.exclude_hv = 1;
			fd = int(syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
			if (fd < 0)
				fd = -1;
		}
		if (fd < 0)
			return false;
		uint64_t value = 0;
		if (read(fd, &value, sizeof value) != ssize_t(sizeof value))
			return false;
		cycles = value;
		return true;
	}
#else
	bool ThreadCycles(uint64_t&)
	{
		return false;
	}
#endif
}

namespace tasfw_perf
{
	Measurement BeginMeasure()
	{
		Measurement m;
		m.allocs = AllocCount();
		m.hasCycles = ThreadCycles(m.cycles);
		return m;
	}

	void EndMeasure(benchmark::State& state, const Measurement& before)
	{
		uint64_t cycles = 0;
		bool hasCycles = before.hasCycles && ThreadCycles(cycles);
		uint64_t allocs = AllocCount();
		double iterations = state.iterations() > 0 ? double(state.iterations()) : 1.0;
		state.counters["allocs"] = benchmark::Counter(double(allocs - before.allocs) / iterations);
		if (hasCycles)
			state.counters["cycles"] = benchmark::Counter(double(cycles - before.cycles) / iterations);
	}
}
