#pragma once
#include <cstdint>

// Heap allocations are counted by replacing the global operator new/delete (alloc_counter.cpp)
// and reported per iteration as the "allocs" counter of every benchmark (measure.hpp). Unlike
// timings the count is deterministic, so scripts/perf_compare.py gates it at (almost) zero
// tolerance: a change that adds an allocation to a hot path fails the suite even when the
// clock does not notice. docs/performance.md, "Rules of thumb": no heap allocation per frame.
namespace tasfw_perf
{
	// Number of calls to operator new (all forms) so far in this process.
	uint64_t AllocCount();
}
