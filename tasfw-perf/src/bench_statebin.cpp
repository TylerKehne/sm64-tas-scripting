#include <benchmark/benchmark.h>
#include "measure.hpp"
#include <BinaryStateBin.hpp>
#include <cstdint>

// Packing a state bin the way a scattershot GetStateBin() does: a handful of quantised
// floats and small integers into 16 bytes.
static void BM_BinaryStateBin_Pack(benchmark::State& state)
{
	uint32_t i = 0;
	tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
	for (auto _ : state)
	{
		i++;
		float x = float(i % 2000) * 0.001f - 1.0f;
		float z = float((i * 7) % 2000) * 0.001f - 1.0f;
		float speed = float(i % 6400) * 0.01f;
		int16_t angle = int16_t(i * 37);

		BinaryStateBin<16> bin;
		uint8_t cursor = 0;
		bin.AddRegionBitsByNRegions<float>(cursor, 12, x, -1.0f, 1.0f, 4000);
		bin.AddRegionBitsByNRegions<float>(cursor, 12, z, -1.0f, 1.0f, 4000);
		bin.AddRegionBitsByRegionSize<float>(cursor, 12, speed, 0.0f, 64.0f, 0.05f);
		bin.AddRegionBitsByRegionSize<int>(cursor, 12, int(angle), -32768, 32767, 16);
		bin.AddValueBits(cursor, 4, i % 7);
		bin.AddValueBits(cursor, 8, i % 200);
		benchmark::DoNotOptimize(bin);
	}
	state.SetItemsProcessed(state.iterations());
	tasfw_perf::EndMeasure(state, m0);
}
BENCHMARK(BM_BinaryStateBin_Pack);

// Bin equality is what UpsertBlock and ValidateBaseBlock do on every hit. A single compare is
// sub-nanosecond, so 64 are batched per iteration to keep the timing above noise.
static void BM_BinaryStateBin_Equals64(benchmark::State& state)
{
	constexpr int n = 64;
	BinaryStateBin<16> bins[n];
	for (int i = 0; i < n; i++)
	{
		uint8_t cursor = 0;
		bins[i].AddValueBits(cursor, 16, uint64_t(0xABCD));
		bins[i].bytes[15] = uint8_t(i & 1);
	}

	tasfw_perf::Measurement m0 = tasfw_perf::BeginMeasure();
	for (auto _ : state)
	{
		int equal = 0;
		for (int i = 1; i < n; i++)
			equal += (bins[i] == bins[i - 1]);
		benchmark::DoNotOptimize(equal);
	}
	state.SetItemsProcessed(state.iterations() * (n - 1));
	tasfw_perf::EndMeasure(state, m0);
}
BENCHMARK(BM_BinaryStateBin_Equals64);
