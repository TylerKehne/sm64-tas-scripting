#pragma once
#include <cstdint>
#include <benchmark/benchmark.h>

// What every benchmark reports besides Google Benchmark's wall and CPU time: heap
// allocations per iteration (alloc_counter.hpp) and CPU cycles per iteration, both taken
// around the timed loop. Cycles are the thread's own cycle count (QueryThreadCycleTime on
// Windows; on Linux a perf_event, where the kernel permits one), so time the thread spends
// descheduled does not count and a lower clock changes the number only through memory
// stalls. They are reported next to wall time, not gated (docs/performance.md, "Noise
// control"). Both counters span PauseTiming/ResumeTiming sections, unlike the clock.
namespace tasfw_perf
{
	struct Measurement
	{
		uint64_t allocs = 0;
		uint64_t cycles = 0;
		bool hasCycles = false;
	};

	// Take just before the timed loop.
	Measurement BeginMeasure();

	// Attach "allocs" and, when the platform counts them, "cycles" (both per iteration) to
	// the state; call right after the loop. One-time set-up inside the measured region is
	// amortised over the (fixed) iteration count and stays below the allocation gate.
	void EndMeasure(benchmark::State& state, const Measurement& before);
}
