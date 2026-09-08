#pragma once
#include <cstdint>
#include <benchmark/benchmark.h>

// Heap allocations are counted by replacing the global operator new/delete (alloc_counter.cpp)
// and reported per iteration as the "allocs" counter of every benchmark. Unlike timings the
// count is deterministic, so scripts/perf_compare.py gates it at (almost) zero tolerance:
// a change that adds an allocation to a hot path fails the suite even when the clock does
// not notice. docs/performance.md, "Rules of thumb": no heap allocation per frame.
namespace tasfw_perf
{
	// Number of calls to operator new (all forms) so far in this process.
	uint64_t AllocCount();

	// Attach "allocs" (allocations per iteration) to the state. Call with the count taken
	// just before the timed loop, after it has finished. One-time set-up inside the measured
	// region is amortised over the (fixed) iteration count and stays below the gate.
	inline void ReportAllocs(benchmark::State& state, uint64_t before)
	{
		double iterations = state.iterations() > 0 ? double(state.iterations()) : 1.0;
		state.counters["allocs"] = benchmark::Counter(double(AllocCount() - before) / iterations);
	}
}
