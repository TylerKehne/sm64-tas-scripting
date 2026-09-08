#include <benchmark/benchmark.h>
#include "alloc_counter.hpp"
#include <tasfw/Inputs.hpp>
#include <filesystem>

// Movie I/O. Every scattershot thread loads the source m64 at start, and every solution
// export writes one, so this is not per-frame work, but M64::load reads byte by byte.

static std::filesystem::path TempMovie()
{
	return std::filesystem::temp_directory_path() / "tasfw-perf-10k.m64";
}

static void FillMovie(M64& m64, int frames)
{
	for (int i = 0; i < frames; i++)
		m64.frames[i] = Inputs(uint16_t(i & 0x8000), int8_t(i * 3), int8_t(i * 5));
}

static void BM_M64_Save_10k(benchmark::State& state)
{
	M64 m64(TempMovie());
	FillMovie(m64, 10000);

	uint64_t allocs0 = tasfw_perf::AllocCount();
	for (auto _ : state)
	{
		state.PauseTiming();
		std::filesystem::remove(m64.fileName);
		state.ResumeTiming();
		benchmark::DoNotOptimize(m64.save());
	}
	std::filesystem::remove(m64.fileName);
	tasfw_perf::ReportAllocs(state, allocs0);
}
BENCHMARK(BM_M64_Save_10k)->Unit(benchmark::kMillisecond);

static void BM_M64_Load_10k(benchmark::State& state)
{
	{
		M64 writer(TempMovie());
		FillMovie(writer, 10000);
		std::filesystem::remove(writer.fileName);
		writer.save();
	}

	uint64_t allocs0 = tasfw_perf::AllocCount();
	for (auto _ : state)
	{
		M64 m64(TempMovie());
		benchmark::DoNotOptimize(m64.load());
		benchmark::DoNotOptimize(m64.frames.size());
	}
	std::filesystem::remove(TempMovie());
	tasfw_perf::ReportAllocs(state, allocs0);
}
BENCHMARK(BM_M64_Load_10k)->Unit(benchmark::kMillisecond);
