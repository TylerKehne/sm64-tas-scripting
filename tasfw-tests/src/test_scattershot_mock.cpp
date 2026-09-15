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

		void SelectMovementOptions() override { AddMovementOption(BasicMoves::RANDOM_YAW); }

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
			bin.AddValueBits(cursor, 8, *(uint64_t*)ReadState("checksum") >> 56);
			if (_binDependsOnHistory && GetCurrentFrame() > uint64_t(config.StartFrame))
				bin.AddValueBits(cursor, 8, uint64_t(++_calls & 0xFF));
			return bin;
		}

		bool ValidateState() override { return true; }
		float GetStateFitness() override { return float(*(uint64_t*)ReadState("checksum") % 1000); }
		bool IsSolution() override { return GetCurrentFrame() >= uint64_t(config.StartFrame) + 6; }

	private:
		SS* _scattershot;
		Counts* _counts;
		bool _binDependsOnHistory;
		uint32_t _calls = 0;
	};

	// A script with moves of its own: a public nested `enum class CustomMoves`, the magic name,
	// goes through the same three calls as BasicMoves and is typed to this class
	// (ROADMAP 3.8, Phase 5 "Per-scenario movement options").
	class CustomShot : public MockShot
	{
	public:
		using MockShot::MockShot;

		enum class CustomMoves
		{
			DASH,
			WAIT
		};

		void SelectMovementOptions() override
		{
			AddMovementOption(BasicMoves::RANDOM_YAW);
			AddRandomMovementOption({ { CustomMoves::DASH, 3 }, { CustomMoves::WAIT, 1 } });
		}

		bool ApplyMovement() override
		{
			uint64_t r = GetTempRng();
			if (CheckMovementOptions(CustomMoves::DASH))
				AdvanceFrameWrite(Inputs(0, int8_t(int(r % 121) - 60), int8_t(int((r >> 8) % 121) - 60)));
			else
				AdvanceFrameWrite(Inputs(0, int8_t(int(r % 11) - 5), int8_t(int((r >> 8) % 11) - 5)));
			return true;
		}
	};

	enum class ForeignOption
	{
		Other
	};

	// The calls are typed to the script's own enum: another script's enum, or an enum of a
	// script that has none, is a compile error rather than a silent mismatch. Checked from
	// a class derived from the script, where scripts make the calls (they are protected;
	// a requires-expression outside a template is also ill-formed when its call is).
	template <class TShot>
	struct Probe : TShot
	{
		template <class TOption>
		static constexpr bool Checks = requires(Probe& s, TOption option) { s.CheckMovementOptions(option); };
		template <class TOption>
		static constexpr bool Draws = requires(Probe& s, TOption option) { s.AddRandomMovementOption({ { option, 1.0 } }); };
	};

	static_assert(Probe<CustomShot>::Checks<CustomShot::CustomMoves>);
	static_assert(Probe<CustomShot>::Checks<BasicMoves>);
	static_assert(!Probe<CustomShot>::Checks<ForeignOption>);
	static_assert(!Probe<MockShot>::Checks<CustomShot::CustomMoves>);
	static_assert(Probe<CustomShot>::Draws<CustomShot::CustomMoves>);
	static_assert(!Probe<CustomShot>::Draws<ForeignOption>);

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

	template <class TShot = MockShot>
	Run RunSearch(int threads, int seed, bool deterministic, long long maxShots = 24, bool binDependsOnHistory = false, int64_t slotLimitBytes = 0,
		const std::vector<Solution>* inputs = nullptr)
	{
		Configuration cfg = MakeConfig(threads, seed, deterministic, maxShots);
		std::vector<MockResource> resources(static_cast<size_t>(threads));
		for (MockResource& resource : resources)
		{
			resource.useCostModel = false; // no timing-dependent saves: every count is exact (as Tier C runs)
			if (slotLimitBytes > 0)
				PerfAccess::SetSlotLimit(resource, slotLimitBytes);
		}

		Run run;
		auto builder = inputs ? TShot::ConfigureScattershot(cfg).PipeFrom(*inputs) : TShot::ConfigureScattershot(cfg);
		run.solutions = builder
			.ImportResourcePerThread([&](int threadId) { return &resources[size_t(threadId)]; })
			.template Run<TShot>(run.counts, binDependsOnHistory); // `template`: dependent object (docs/compilers.md)
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

// The deterministic queue serves every thread's k-th call in thread order (ROADMAP 3.8);
// with more threads than two and enough shots for them to finish at different times, the
// retirements come in a timing-dependent order that must not reach the search.
TEST_CASE("Four threads in deterministic mode reproduce their search, over several shots each")
{
	Run first = RunSearch(4, 3, true, 96);
	Run again = RunSearch(4, 3, true, 96);
	REQUIRE(first.counts.shots > 48);
	REQUIRE(first.counts.scripts > 0);
	CheckSameSearch(first, again);
	CHECK(first.frameAdvances == again.frameAdvances);
	CHECK(first.loads == again.loads);

	Run three = RunSearch(3, 3, true, 96);
	Run threeAgain = RunSearch(3, 3, true, 96);
	CheckSameSearch(three, threeAgain);
	CHECK(three.frameAdvances == threeAgain.frameAdvances);
}

TEST_CASE("A script's own CustomMoves enum goes through the same calls and its search reproduces")
{
	Run first = RunSearch<CustomShot>(1, 3, true);
	Run again = RunSearch<CustomShot>(1, 3, true);
	REQUIRE(first.counts.scripts > 0);
	CheckSameSearch(first, again);
	CHECK(first.frameAdvances == again.frameAdvances);

	// The custom draw changes the search: the same seed without it is a different search.
	Run plain = RunSearch<MockShot>(1, 3, true);
	CHECK((plain.counts.scripts != first.counts.scripts || plain.counts.blocks != first.counts.blocks
		|| !SameSolutions(plain.solutions, first.solutions)));

	Run four = RunSearch<CustomShot>(4, 3, true, 96);
	Run fourAgain = RunSearch<CustomShot>(4, 3, true, 96);
	CheckSameSearch(four, fourAgain);
	CHECK(four.frameAdvances == fourAgain.frameAdvances);
}

TEST_CASE("Piped-in solutions reproduce in deterministic mode, on three threads with two inputs")
{
	// The inputs are solutions of a single-thread search. They are handed out in rounds every
	// thread takes part in, so every thread makes the same number of queue calls and the
	// tickets pair up thread by thread (ROADMAP 3.14); with two inputs on three threads one
	// thread has none in the only round, with three on two threads the second round is short.
	Run source = RunSearch(1, 3, true, 96);
	REQUIRE(source.solutions.size() >= 3);
	std::vector<Solution> two(source.solutions.begin(), source.solutions.begin() + 2);
	std::vector<Solution> three(source.solutions.begin(), source.solutions.begin() + 3);

	Run first = RunSearch(3, 5, true, 24, false, 0, &two);
	Run again = RunSearch(3, 5, true, 24, false, 0, &two);
	REQUIRE(first.counts.scripts > 0);
	CheckSameSearch(first, again);
	CHECK(first.frameAdvances == again.frameAdvances);

	// The inputs change the search: the same seed without them is a different search.
	Run plain = RunSearch(3, 5, true);
	CHECK((plain.counts.blocks != first.counts.blocks || plain.counts.scripts != first.counts.scripts
		|| !SameSolutions(plain.solutions, first.solutions)));

	Run more = RunSearch(2, 5, true, 24, false, 0, &three);
	Run moreAgain = RunSearch(2, 5, true, 24, false, 0, &three);
	CheckSameSearch(more, moreAgain);
	CHECK(more.frameAdvances == moreAgain.frameAdvances);
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
