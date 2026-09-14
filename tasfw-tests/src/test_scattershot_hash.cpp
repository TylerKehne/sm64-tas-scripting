#include <doctest/doctest.h>
#include <Scattershot.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <tasfw/testing/MockResource.hpp>
#include <tasfw/testing/PerfAccess.hpp>

// The search's hashes must be the same function on every platform: the block table's
// state-bin hash decides which states are the same block, and the RNG chain (GetRng,
// GetTempRng) is that hash applied to its previous value, so a hash that differed between
// standard libraries made the same seed take a different search path on Linux than on
// Windows (docs/compilers.md). The values below are what MSVC's STL computed before the
// framework took the function over (2026-09-13); they pin every Windows search ever recorded.

namespace
{
	using Bin = BinaryStateBin<16>;
	using SS = Scattershot<Bin, MockResource, DefaultStateTracker<MockResource>, DefaultState>;
	using Solution = ScattershotSolution<DefaultState>;

	const std::vector<Solution> NoInputSolutions;

	Configuration MakeConfig()
	{
		Configuration cfg {};
		cfg.MaxBlocks = 16;
		cfg.MaxSolutions = 1;
		cfg.FitnessTieGoesToNewBlock = false;
		cfg.CsvSamplePeriod = 0;
		cfg.TotalThreads = 1;
		return cfg;
	}
}

TEST_CASE("HashByte is FNV-1a over the byte, the function MSVC's std::hash<std::byte> computes")
{
	CHECK(HashByte(std::byte { 0 }) == 12638153115695167455ull);
	CHECK(HashByte(std::byte { 1 }) == 12638152016183539244ull);
	CHECK(HashByte(std::byte { 42 }) == 12638128926439346813ull);
	CHECK(HashByte(std::byte { 255 }) == 12638352127299873646ull);
}

TEST_CASE("The state-bin hash and the RNG chain are pinned to their Windows values")
{
	Configuration cfg = MakeConfig();
	SS scattershot(cfg, NoInputSolutions);

	Bin bin {};
	for (int i = 0; i < 16; i++)
		bin.bytes[i] = uint8_t(i);
	CHECK(PerfAccess::GetHash(scattershot, bin, true) == 14738338891769348960ull);

	// GetRng and GetTempRng advance by hashing the previous 64-bit value, all eight bytes,
	// in memory order; the chain from the deterministic seed the Tier D workloads use.
	uint64_t rng = 3;
	rng = PerfAccess::GetHash(scattershot, rng, true);
	CHECK(rng == 5287444456481613715ull);
	rng = PerfAccess::GetHash(scattershot, rng, true);
	CHECK(rng == 4508882137686409130ull);
	rng = PerfAccess::GetHash(scattershot, rng, true);
	CHECK(rng == 9262616032610819667ull);
}
