#include <doctest/doctest.h>
#include <Scattershot.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <tasfw/testing/MockResource.hpp>
#include <tasfw/testing/PerfAccess.hpp>

// A scattershot on the mock resource (ROADMAP 3.13): the search machinery without the game,
// in milliseconds and deterministically. Pinned here: a seed reproduces its search (the same
// shots, scripts, blocks and solutions, and the same resource work), single-threaded and on
// two threads in deterministic mode; a tight savestate limit changes replays and nothing
// else; and a state bin that is not a function of state is caught by the base-block
// validation of ROADMAP 4.5.

namespace fs = std::filesystem;

namespace
{
	using Bin = BinaryStateBin<4>;
	using SS = Scattershot<Bin, MockResource, DefaultStateTracker<MockResource>, DefaultState>;
	using Thread = ScattershotThread<Bin, MockResource, DefaultStateTracker<MockResource>, DefaultState>;
	using Solution = ScattershotSolution<DefaultState>;

	// The run's totals, copied out by every thread when it finishes (assertion runs after
	// execution), so the last thread to finish leaves the final values.
	struct Counts
	{
		uint64_t shots = 0;
		uint64_t scripts = 0;
		uint64_t blocks = 0;
		uint64_t validationFailures = 0;
	};

	class MockShot : public Thread
	{
	public:
		// `binDependsOnHistory` makes GetStateBin count its own calls, a bin that is not a
		// function of state: what hard rule 3 forbids and the base-block validation catches.
		MockShot(SS& scattershot, Counts& counts, bool binDependsOnHistory)
			: Thread(scattershot), _scattershot(&scattershot), _counts(&counts), _binDependsOnHistory(binDependsOnHistory) {}

		bool assertion() override
		{
			#pragma omp critical (mockshotcounts)
			{
				_counts->shots = PerfAccess::TotalShots(*_scattershot);
				_counts->scripts = PerfAccess::ScriptCount(*_scattershot);
				_counts->blocks = PerfAccess::BlockCount(*_scattershot);
				_counts->validationFailures = PerfAccess::ValidationFailures(*_scattershot);
			}
			return true;
		}

		void SelectMovementOptions() override { AddMovementOption(MovementOption::RANDOM_YAW); }

		// One frame of a stick from the pellet's RNG. Buttons stay zero: ValidateCourseAndArea
		// reads two symbols through addr, which the mock answers with its pad, so a button
		// change would read as a level change.
		bool ApplyMovement() override
		{
			uint64_t r = GetTempRng();
			AdvanceFrameWrite(Inputs(0, int8_t(int(r % 121) - 60), int8_t(int((r >> 8) % 121) - 60)));
			return true;
		}

		Bin GetStateBin() override
		{
			Bin bin;
			uint8_t cursor = 0;
			bin.AddValueBits(cursor, 16, uint64_t(GetCurrentFrame() - uint64_t(config.StartFrame)));
			bin.AddValueBits(cursor, 8, resource->checksum() >> 56);
			if (_binDependsOnHistory && GetCurrentFrame() > uint64_t(config.StartFrame))
				bin.AddValueBits(cursor, 8, uint64_t(++_calls & 0xFF));
			return bin;
		}

		bool ValidateState() override { return true; }
		float GetStateFitness() override { return float(resource->checksum() % 1000); }
		bool IsSolution() override { return GetCurrentFrame() >= uint64_t(config.StartFrame) + 6; }

	private:
		SS* _scattershot;
		Counts* _counts;
		bool _binDependsOnHistory;
		uint32_t _calls = 0;
	};

	struct Run
	{
		Counts counts;
		std::vector<Solution> solutions;
		uint64_t frameAdvances = 0;
		uint64_t saves = 0;
		uint64_t loads = 0;
		uint64_t evictions = 0;
	};

	// A movie of four neutral-ish frames before the start frame, written once per process,
	// and the directory the diagnostics' error.m64 goes to.
	const fs::path& ScratchDirectory()
	{
		static fs::path dir = [] {
			fs::path d = fs::temp_directory_path() / "tasfw-tests-scattershot-mock";
			fs::create_directories(d);
			M64 movie(d / "start.m64");
			for (uint64_t i = 0; i < 4; i++)
				movie.frames[i] = Inputs(0, int8_t(10 + i), int8_t(-10));
			REQUIRE(movie.save() == 1);
			return d;
		}();
		return dir;
	}

	Configuration MakeConfig(int threads, int seed, bool deterministic, long long maxShots)
	{
		Configuration cfg {};
		cfg.StartFrame = 4;
		cfg.PelletMaxScripts = 3;
		cfg.PelletMaxFrameDistance = 8;
		cfg.MaxBlocks = 4096;
		cfg.TotalThreads = threads;
		cfg.MaxShots = maxShots;
		cfg.PelletsPerShot = 4;
		cfg.ShotsPerUpdate = 1000000; // no progress lines
		cfg.StartFromRootEveryNShots = 8;
		cfg.MaxConsecutiveFailedPellets = 4;
		cfg.MaxSolutions = 1000; // never reached, so every shot runs; zero would record no solution at all
		cfg.Seed = seed;
		cfg.FitnessTieGoesToNewBlock = false;
		cfg.Deterministic = deterministic;
		cfg.CsvSamplePeriod = 0;
		cfg.M64Path = ScratchDirectory() / "start.m64";
		cfg.CsvOutputDirectory = ScratchDirectory().string() + std::string(1, char(fs::path::preferred_separator));
		cfg.SetResourcePaths(std::vector<std::string>(size_t(threads), "mock")); // one resource per thread; the names are not used
		return cfg;
	}

	Run RunSearch(int threads, int seed, bool deterministic, long long maxShots = 24, bool binDependsOnHistory = false, int64_t slotLimitBytes = 0)
	{
		Configuration cfg = MakeConfig(threads, seed, deterministic, maxShots);
		std::vector<MockResource> resources(static_cast<size_t>(threads));
		for (MockResource& resource : resources)
		{
			resource.useCostModel = false; // no timing-dependent saves: every count is exact (as Tier C runs)
			if (slotLimitBytes > 0)
				resource.slotManager._saveMemLimit = slotLimitBytes;
		}

		Run run;
		run.solutions = MockShot::ConfigureScattershot(cfg)
			.ImportResourcePerThread([&](int threadId) { return &resources[size_t(threadId)]; })
			.Run<MockShot>(run.counts, binDependsOnHistory);
		for (const MockResource& resource : resources)
		{
			run.frameAdvances += resource.work.frameAdvances;
			run.saves += resource.work.saves;
			run.loads += resource.work.loads;
			run.evictions += resource.work.evictions;
		}
		return run;
	}

	bool SameSolutions(const std::vector<Solution>& a, const std::vector<Solution>& b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); i++)
			if (a[i].m64Diff.frames != b[i].m64Diff.frames)
				return false;
		return true;
	}

	void CheckSameSearch(const Run& a, const Run& b)
	{
		CHECK(a.counts.shots == b.counts.shots);
		CHECK(a.counts.scripts == b.counts.scripts);
		CHECK(a.counts.blocks == b.counts.blocks);
		CHECK(a.counts.validationFailures == b.counts.validationFailures);
		CHECK(SameSolutions(a.solutions, b.solutions));
	}
}

TEST_CASE("A seed reproduces its search on the mock resource, and another seed does not")
{
	Run first = RunSearch(1, 3, true);
	Run again = RunSearch(1, 3, true);
	REQUIRE(first.counts.shots > 0);
	REQUIRE(first.counts.scripts > 0);
	REQUIRE(first.counts.blocks > 1);
	CHECK(first.counts.validationFailures == 0);
	CheckSameSearch(first, again);
	CHECK(first.frameAdvances == again.frameAdvances);
	CHECK(first.saves == again.saves);
	CHECK(first.loads == again.loads);
	CHECK(first.evictions == 0);

	Run other = RunSearch(1, 4, true);
	CHECK((other.counts.scripts != first.counts.scripts || other.counts.blocks != first.counts.blocks
		|| !SameSolutions(other.solutions, first.solutions)));
}

TEST_CASE("Two threads in deterministic mode reproduce their search too")
{
	Run first = RunSearch(2, 3, true);
	Run again = RunSearch(2, 3, true);
	REQUIRE(first.counts.scripts > 0);
	CheckSameSearch(first, again);
	CHECK(first.frameAdvances == again.frameAdvances);
	CHECK(first.loads == again.loads);
}

TEST_CASE("A tight savestate limit evicts and replays, and changes nothing about the search")
{
	Run roomy = RunSearch(1, 3, true);
	// One slot at a time: the search keeps two alive (the start frame's and the shot's), so
	// the shot's save evicts the other and loads back to the start replay from the start save.
	Run tight = RunSearch(1, 3, true, 24, false, int64_t(2 * sizeof(MockState)) - 1);
	CheckSameSearch(roomy, tight);
	CHECK(tight.evictions > 0);
	CHECK(tight.frameAdvances > roomy.frameAdvances);
	CHECK(tight.loads <= roomy.loads);
}

TEST_CASE("A state bin that is not a function of state fails base-block validation and is counted")
{
	// Every decoded block's bin differs from the one recorded, so every shot from a decoded
	// block fails; shots from the root block, whose bin is honest, pass (ROADMAP 4.5).
	Run broken = RunSearch(1, 3, true, 6, true);
	CHECK(broken.counts.validationFailures > 0);
	CHECK(broken.counts.validationFailures < broken.counts.shots);
	CHECK(fs::exists(ScratchDirectory() / "error.m64")); // the diagnostics' dump of the failing block's inputs
}
