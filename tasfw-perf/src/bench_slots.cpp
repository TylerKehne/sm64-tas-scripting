#include <benchmark/benchmark.h>
#include "measure.hpp"
#include <vector>

#include <tasfw/testing/FakeResource.hpp>

// SlotManager bookkeeping (three std::maps per slot) at different live-slot counts.
// The fake state is 256 bytes, so the copy itself is negligible. Fixed iteration counts keep
// the heap state deterministic across runs (see bench_script.cpp).

static void BM_SlotManager_CreateErase(benchmark::State& state)
{
	const int live = int(state.range(0));
	FakeResource resource;
	auto& slots = resource.slotManager;
	for (int i = 0; i < live; i++)
		slots.CreateSlot();

	tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
	for (auto _ : state)
	{
		int64_t id = slots.CreateSlot();
		slots.EraseSlot(id);
	}
	tasfw_perf::EndMeasure(state, m0);
}
BENCHMARK(BM_SlotManager_CreateErase)->Arg(100)->Arg(1000)->Arg(10000)->Iterations(1000000);

static void BM_SlotManager_LoadSlot(benchmark::State& state)
{
	const int live = int(state.range(0));
	FakeResource resource;
	auto& slots = resource.slotManager;
	std::vector<int64_t> ids;
	ids.reserve(live);
	for (int i = 0; i < live; i++)
		ids.push_back(slots.CreateSlot());

	std::size_t i = 0;
	tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
	for (auto _ : state)
	{
		slots.LoadSlot(ids[(i++ * 7919) % live]);
	}
	benchmark::DoNotOptimize(resource.checksum());
	tasfw_perf::EndMeasure(state, m0);
}
BENCHMARK(BM_SlotManager_LoadSlot)->Arg(100)->Arg(1000)->Arg(10000)->Iterations(1000000);

// At the memory cap every CreateSlot evicts the least recently touched slot first.
static void BM_SlotManager_CreateAtCap(benchmark::State& state)
{
	const int live = int(state.range(0));
	FakeResource resource;
	auto& slots = resource.slotManager;
	slots._saveMemLimit = int64_t(live + 1) * int64_t(sizeof(FakeState));
	for (int i = 0; i < live; i++)
		slots.CreateSlot();

	tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
	for (auto _ : state)
	{
		benchmark::DoNotOptimize(slots.CreateSlot());
	}
	tasfw_perf::EndMeasure(state, m0);
}
BENCHMARK(BM_SlotManager_CreateAtCap)->Arg(100)->Arg(1000)->Arg(10000)->Iterations(1000000);

// Resource-level wrappers add rdtsc timing and counters on top of the slot manager.
static void BM_Resource_SaveLoadState(benchmark::State& state)
{
	FakeResource resource;
	tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
	for (auto _ : state)
	{
		int64_t id = resource.SaveState();
		resource.LoadState(id);
		resource.slotManager.EraseSlot(id);
	}
	tasfw_perf::EndMeasure(state, m0);
}
BENCHMARK(BM_Resource_SaveLoadState)->Iterations(1000000);
