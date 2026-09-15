#pragma once
#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#else
// Provides __rdtsc outside MSVC
#include <x86intrin.h>
#endif

#ifndef RESOURCEWORK_H
#define RESOURCEWORK_H

// What a resource has done since it was constructed and what it cost, in rdtsc cycles, the
// framework's one unit for every duration (ROADMAP 3.6). Script snapshots the cycle fields
// around a child to fill its status, the perf suite and bitfs-turn read a snapshot per
// workload and per stage, and the cost model (shouldSave / shouldLoad) reads the averages.
struct ResourceWork
{
	uint64_t frameAdvances = 0;
	uint64_t saves = 0;
	uint64_t loads = 0;
	uint64_t advanceCycles = 0;
	uint64_t saveCycles = 0;
	uint64_t loadCycles = 0;
	// The slot manager: the most slots and bytes live at once, saves that recycled a pooled
	// state instead of allocating, and slots evicted to stay under the memory limit.
	uint64_t slotsLiveMax = 0;
	uint64_t slotBytesMax = 0;
	uint64_t poolReuses = 0;
	uint64_t evictions = 0;

	// The work between an earlier snapshot and this one. The two maxima are not differences;
	// this snapshot's values are kept.
	ResourceWork operator-(const ResourceWork& earlier) const
	{
		ResourceWork d = *this;
		d.frameAdvances -= earlier.frameAdvances;
		d.saves -= earlier.saves;
		d.loads -= earlier.loads;
		d.advanceCycles -= earlier.advanceCycles;
		d.saveCycles -= earlier.saveCycles;
		d.loadCycles -= earlier.loadCycles;
		d.poolReuses -= earlier.poolReuses;
		d.evictions -= earlier.evictions;
		return d;
	}
};

// The framework's clock: rdtsc cycles. Every duration in ResourceWork, BaseScriptStatus and
// the scattershot summary is a difference of two of these (ROADMAP 3.6); divide by the
// machine's TSC rate for seconds.
static inline uint64_t get_time()
{
	return __rdtsc();
}

#endif
