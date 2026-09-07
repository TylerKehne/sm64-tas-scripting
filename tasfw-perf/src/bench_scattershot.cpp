#include <benchmark/benchmark.h>
#include <Scattershot.hpp>
#include <random>
#include <vector>

#include <tasfw/testing/FakeResource.hpp>
#include "PerfAccess.hpp"

// Scattershot's block table and hashing, exercised without threads, resources or scripts.

using Bin = BinaryStateBin<16>;
using SS = Scattershot<Bin, FakeResource, DefaultStateTracker<FakeResource>, DefaultState>;
using Solution = ScattershotSolution<DefaultState>;

static const std::vector<Solution> NoInputSolutions;

static Configuration MakeConfig(int maxBlocks)
{
	Configuration cfg {};
	cfg.MaxBlocks = maxBlocks;
	cfg.MaxSolutions = 100;
	cfg.FitnessTieGoesToNewBlock = false;
	cfg.CsvSamplePeriod = 0;
	cfg.TotalThreads = 1;
	return cfg;
}

static std::vector<Bin> RandomBins(std::size_t n, uint64_t seed)
{
	std::mt19937_64 rng(seed);
	std::vector<Bin> bins(n);
	for (auto& bin : bins)
	{
		for (auto& byte : bin.bytes)
			byte = uint8_t(rng());
	}
	return bins;
}

static void BM_Scattershot_GetHash(benchmark::State& state)
{
	Configuration cfg = MakeConfig(16);
	SS scattershot(cfg, NoInputSolutions);
	auto bins = RandomBins(1024, 1);
	bool ignoreFiller = state.range(0) != 0;

	std::size_t i = 0;
	for (auto _ : state)
	{
		benchmark::DoNotOptimize(PerfAccess::GetHash(scattershot, bins[i++ & 1023], ignoreFiller));
	}
}
BENCHMARK(BM_Scattershot_GetHash)->Arg(0)->Arg(1);

// Insert N distinct bins into a fresh table. Reports per-insert time via items processed.
static void BM_Scattershot_UpsertBlock_Novel(benchmark::State& state)
{
	const int n = int(state.range(0));
	Configuration cfg = MakeConfig(n + 16);
	auto bins = RandomBins(n, 2);

	for (auto _ : state)
	{
		state.PauseTiming();
		SS scattershot(cfg, NoInputSolutions);
		state.ResumeTiming();

		for (int i = 0; i < n; i++)
			PerfAccess::UpsertBlock(scattershot, bins[i], false, Solution(), 0.0f, nullptr, uint8_t(1), uint64_t(i), uint16_t(0));

		benchmark::DoNotOptimize(PerfAccess::BlockCount(scattershot));
	}
	state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_Scattershot_UpsertBlock_Novel)->Arg(1000)->Arg(50000)->Unit(benchmark::kMillisecond);

// Same bin, same fitness: the "redundant script" path, which is the most common outcome.
static void BM_Scattershot_UpsertBlock_Redundant(benchmark::State& state)
{
	Configuration cfg = MakeConfig(1024);
	SS scattershot(cfg, NoInputSolutions);
	auto bins = RandomBins(64, 3);
	for (int i = 0; i < 64; i++)
		PerfAccess::UpsertBlock(scattershot, bins[i], false, Solution(), 1.0f, nullptr, uint8_t(1), uint64_t(i), uint16_t(0));

	std::size_t i = 0;
	for (auto _ : state)
	{
		std::size_t index = i++;
		benchmark::DoNotOptimize(PerfAccess::UpsertBlock(scattershot, bins[index & 63], false, Solution(), 1.0f, nullptr, uint8_t(1), uint64_t(index), uint16_t(0)));
	}
}
BENCHMARK(BM_Scattershot_UpsertBlock_Redundant);

// Same bin, strictly improving fitness: replaces the tail segment every time.
static void BM_Scattershot_UpsertBlock_Improve(benchmark::State& state)
{
	Configuration cfg = MakeConfig(1024);
	SS scattershot(cfg, NoInputSolutions);
	auto bins = RandomBins(1, 4);
	float fitness = 0.0f;

	for (auto _ : state)
	{
		fitness += 1.0f;
		benchmark::DoNotOptimize(PerfAccess::UpsertBlock(scattershot, bins[0], false, Solution(), fitness, nullptr, uint8_t(1), uint64_t(fitness), uint16_t(0)));
	}
}
BENCHMARK(BM_Scattershot_UpsertBlock_Improve);
