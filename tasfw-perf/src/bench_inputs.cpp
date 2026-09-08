#include <benchmark/benchmark.h>
#include "alloc_counter.hpp"
#include <tasfw/Inputs.hpp>
#include <cstdint>

// Joystick lookups. These run once per random input in scattershot and once per frame in
// most movement scripts. The static tables are built before main(), so only lookups are timed.

static void BM_Inputs_GetClosestInputByYawHau(benchmark::State& state)
{
	uint32_t i = 0;
	uint64_t allocs0 = tasfw_perf::AllocCount();
	for (auto _ : state)
	{
		i++;
		int16_t intendedYaw = int16_t(i * 4099);
		int16_t cameraYaw = int16_t(i * 733);
		benchmark::DoNotOptimize(Inputs::GetClosestInputByYawHau(intendedYaw, 32.0f, cameraYaw));
	}
	tasfw_perf::ReportAllocs(state, allocs0);
}
BENCHMARK(BM_Inputs_GetClosestInputByYawHau);

static void BM_Inputs_GetClosestInputByYawHau_PartialMag(benchmark::State& state)
{
	uint32_t i = 0;
	uint64_t allocs0 = tasfw_perf::AllocCount();
	for (auto _ : state)
	{
		i++;
		int16_t intendedYaw = int16_t(i * 4099);
		int16_t cameraYaw = int16_t(i * 733);
		float mag = float(i % 32) + 0.5f;
		benchmark::DoNotOptimize(Inputs::GetClosestInputByYawHau(intendedYaw, mag, cameraYaw));
	}
	tasfw_perf::ReportAllocs(state, allocs0);
}
BENCHMARK(BM_Inputs_GetClosestInputByYawHau_PartialMag);

static void BM_Inputs_GetClosestInputByYawExact(benchmark::State& state)
{
	uint32_t i = 0;
	uint64_t allocs0 = tasfw_perf::AllocCount();
	for (auto _ : state)
	{
		i++;
		int16_t intendedYaw = int16_t(i * 4099);
		int16_t cameraYaw = int16_t(i * 733);
		benchmark::DoNotOptimize(Inputs::GetClosestInputByYawExact(intendedYaw, 32.0f, cameraYaw));
	}
	tasfw_perf::ReportAllocs(state, allocs0);
}
BENCHMARK(BM_Inputs_GetClosestInputByYawExact);

static void BM_Inputs_GetIntendedYawMagFromInput(benchmark::State& state)
{
	uint32_t i = 0;
	uint64_t allocs0 = tasfw_perf::AllocCount();
	for (auto _ : state)
	{
		i++;
		int8_t x = int8_t(i * 17);
		int8_t y = int8_t(i * 101);
		benchmark::DoNotOptimize(Inputs::GetIntendedYawMagFromInput(x, y, int16_t(i)));
	}
	tasfw_perf::ReportAllocs(state, allocs0);
}
BENCHMARK(BM_Inputs_GetIntendedYawMagFromInput);
