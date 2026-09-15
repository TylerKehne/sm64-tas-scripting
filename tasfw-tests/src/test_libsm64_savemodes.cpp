#include <doctest/doctest.h>

#include "libsm64_env.hpp"
#include <tasfw/testing/PerfAccess.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// The resource's save modes, against the real game DLL (libsm64_env.hpp says when these run
// and what they play). Every mode must bring the game's bytes of .data and .bss back byte
// for byte from a load and leave the C runtime's bytes at the sections' edges alone
// (LibSm64GameBytes), and the dirty mode's copy-on-write baselines must keep every live
// state exact across runs. The subject is the resource itself, so these tests drive it
// directly, outside any script (AGENTS.md, hard rule 9).

using namespace tasfw::tests;

namespace
{
	// Every save mode must bring the whole .data and .bss back byte for byte from a load, for
	// states saved before and after other saves and for pages first written after the save. In
	// Dirty mode this crosses the baseline the resource took at the first slot of the run and
	// restores pages from the baseline snapshot.
	struct SaveModeResults
	{
		size_t s0Diff = 0, s1Diff = 0, s2Diff = 0, replayDiff = 0; // bytes of .data+.bss that differ after the load
		int baselineAtS1 = -1;
		size_t pagesInS0 = 0, pagesInS1 = 0;
		uint64_t faults = 0;
	};

	SaveModeResults SaveModeRoundTrip(LibSm64& resource, const M64& m64, int64_t frame)
	{
		SaveModeResults results;
		PlayFrames(resource, m64, frame);

		auto sections = [&]()
		{
			std::vector<uint8_t> all;
			for (const SegVal& seg : resource.segment)
			{
				const uint8_t* begin = static_cast<const uint8_t*>(seg.address);
				all.insert(all.end(), begin, begin + seg.length);
			}
			return all;
		};
		auto play = [&](int frames, int variant)
		{
			for (int i = 0; i < frames; i++)
			{
				double angle = i * (variant == 0 ? 0.37 : 0.61);
				resource.setInputs(Inputs(uint16_t(i % 7 == 0 ? 0x8000 : 0), int8_t(60.0 * std::cos(angle)), int8_t(60.0 * std::sin(angle))));
				resource.FrameAdvance();
			}
		};
		auto differing = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
		{
			size_t n = 0;
			for (size_t i = 0; i < a.size() && i < b.size(); i++)
				n += a[i] != b[i];
			return n + (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
		};
		auto pagesOf = [&](int64_t slot)
		{
			return PerfAccess::SlotState(resource, slot).pages.size() / size_t(pagesize);
		};

		// s0 is the first slot of this run, so in Dirty mode it is where the resource takes its
		// baseline (as it would at LongLoad's slot in a script).
		std::vector<uint8_t> snap0 = sections();
		int64_t s0 = resource.SaveState();
		play(60, 0);
		std::vector<uint8_t> snap1 = sections();
		int64_t s1 = resource.SaveState();
		play(60, 1);
		std::vector<uint8_t> snap2 = sections();
		int64_t s2 = resource.SaveState();

		if (const LibSm64DirtyPages* d = resource.dirtyPages())
		{
			results.baselineAtS1 = d->baseline;
			results.faults = d->faults;
			results.pagesInS0 = pagesOf(s0);
			results.pagesInS1 = pagesOf(s1);
		}

		resource.LoadState(s0);
		results.s0Diff = differing(sections(), snap0);
		resource.LoadState(s2);
		results.s2Diff = differing(sections(), snap2);
		resource.LoadState(s1);
		results.s1Diff = differing(sections(), snap1);
		play(60, 1); // the same inputs from the same state reach the same memory
		results.replayDiff = differing(sections(), snap2);

		resource.DisposeState(s0);
		resource.DisposeState(s1);
		resource.DisposeState(s2);
		return results;
	}
}

TEST_CASE("libsm64: every save mode restores .data and .bss exactly, including pages written after the save"
	* doctest::skip(!HaveDll()))
{
	// Dirty first, then the others in the same test case: on Linux that is where doctest's own
	// SIGSEGV handler (installed per test case) and the resource's have to coexist.
	for (LibSm64SaveMode mode : {LibSm64SaveMode::Dirty, LibSm64SaveMode::Fixed, LibSm64SaveMode::Full})
	{
		std::string modeName = LibSm64::SaveModeName(mode);
		CAPTURE(modeName);
		LibSm64Config config = DllConfig(mode);
		int64_t frame = TestFrame();

		std::unique_ptr<LibSm64> owned;
		try
		{
			owned = std::make_unique<LibSm64>(config);
		}
		catch (const std::runtime_error& e)
		{
			// The fixed slices fit the pinned build only; a build with smaller sections refuses
			// them at construction, which is the intended answer, not a failure of this test.
			REQUIRE(mode == LibSm64SaveMode::Fixed);
			MESSAGE("fixed save mode unavailable on this build: " << std::string(e.what()));
			continue;
		}
		LibSm64& resource = *owned;
		M64 m64(Env("TASFW_M64"));
		REQUIRE(m64.load() == 1);

		SaveModeResults results = SaveModeRoundTrip(resource, m64, frame);
		// Full and Dirty restore everything. Fixed misses a handful of bytes by design: the
		// lava texture-scroll offsets outside its slices, which nothing in the physics reads
		// (docs/libsm64.md, --leak-scan): 4 to 6 bytes after a load, about 20 after a replay
		// through them, since they accumulate. That is the price of its constant cost.
		size_t tolerance = mode == LibSm64SaveMode::Fixed ? 32 : 0;
		CHECK(results.s0Diff <= tolerance);
		CHECK(results.s1Diff <= tolerance);
		CHECK(results.s2Diff <= tolerance);
		CHECK(results.replayDiff <= tolerance);
		if (mode == LibSm64SaveMode::Fixed)
			MESSAGE("fixed slices: bytes not restored after loading s0 / s1 / s2 / replay: " << results.s0Diff << " / " << results.s1Diff
				<< " / " << results.s2Diff << " / " << results.replayDiff);
		if (mode == LibSm64SaveMode::Dirty)
		{
			// Baseline 0 is the start save's; LongLoad's slot at the frame moved it to 1, so the
			// saves of this run copy only what the run wrote (docs/performance.md, "What the game
			// writes"): well under the 1,464 KB the fixed slices copy.
			CHECK(results.baselineAtS1 == 1);
			CHECK(results.faults > 0);
			CHECK(results.pagesInS1 > results.pagesInS0);
			CHECK(results.pagesInS1 > 0);
			CHECK(results.pagesInS1 < 200);
			MESSAGE("dirty pages: " << results.pagesInS0 << " at the first save of the run, " << results.pagesInS1
				<< " after 60 frames; " << results.faults << " first writes recorded in all baselines");
		}
	}
}

TEST_CASE("libsm64: no save mode copies or restores the C runtime's bytes at the sections' edges"
	* doctest::skip(!HaveDll()))
{
	// Both sections end in the state of the runtime the DLL was built with, which the loader
	// touches from every thread of the process (LibSm64GameBytes; ROADMAP 3.12, the hang). A
	// save takes the game's bytes only and a load puts back only those. Observed through the
	// runtime's thread-key critical section in the .bss tail: changing its spin count is a
	// legitimate operation on it (the Windows API for exactly that), and a load that restored
	// the tail would put the old count back. In dirty mode that write is also what makes the
	// load consider the page at all: it is the first write to it since the save's baseline.
	for (LibSm64SaveMode mode : {LibSm64SaveMode::Dirty, LibSm64SaveMode::Fixed, LibSm64SaveMode::Full})
	{
		std::string modeName = LibSm64::SaveModeName(mode);
		CAPTURE(modeName);
		std::unique_ptr<LibSm64> owned;
		try
		{
			owned = std::make_unique<LibSm64>(DllConfig(mode));
		}
		catch (const std::runtime_error& e)
		{
			REQUIRE(mode == LibSm64SaveMode::Fixed);
			MESSAGE("fixed save mode unavailable on this build: " << std::string(e.what()));
			continue;
		}
		LibSm64& resource = *owned;
		const LibSm64GameBytes* game = resource.gameBytes();
#if defined(_WIN32)
		REQUIRE(game != nullptr); // every Windows build the tests run on has an entry
#else
		if (game == nullptr)
		{
			MESSAGE("no LibSm64KnownGameBytes entry for this build: both sections are saved whole, nothing to check");
			continue;
		}
#endif
		CHECK(game->begin[0] > 0);
		CHECK(game->end[0] < resource.segment[0].length);
		CHECK(game->begin[1] > 0);
		CHECK(game->end[1] < resource.segment[1].length);

		M64 m64(Env("TASFW_M64"));
		REQUIRE(m64.load() == 1);
		PlayFrames(resource, m64, TestFrame());
		int64_t slot = resource.SaveState();

		if (mode == LibSm64SaveMode::Full)
		{
			const LibSm64Mem& state = PerfAccess::SlotState(resource, slot);
			CHECK(state.buf1.size() == game->end[0] - game->begin[0]);
			CHECK(state.buf2.size() == game->end[1] - game->begin[1]);
		}

#if defined(_WIN32)
		auto* cs = reinterpret_cast<CRITICAL_SECTION*>(static_cast<uint8_t*>(resource.segment[1].address) + game->threadKeyLock);
		auto spinCount = [cs]()
		{
			ULONG_PTR raw = 0;
			std::memcpy(&raw, &cs->SpinCount, sizeof raw);
			return unsigned(raw & 0x00FFFFFFu); // the high byte holds flags the API keeps
		};
		const unsigned before = spinCount();
		SetCriticalSectionSpinCount(cs, before + 1);
		REQUIRE(spinCount() == before + 1);
		resource.LoadState(slot);
		CHECK(spinCount() == before + 1);
		SetCriticalSectionSpinCount(cs, before);
#endif
		resource.DisposeState(slot);
	}
}

TEST_CASE("libsm64: dirty baselines hold copy-on-write pages, and every live state stays exact across a second run's baseline"
	* doctest::skip(!HaveDll()))
{
	LibSm64Config config = DllConfig(LibSm64SaveMode::Dirty);
	int64_t frame = TestFrame();

	LibSm64 resource(config);
	M64 m64(Env("TASFW_M64"));
	REQUIRE(m64.load() == 1);
	const LibSm64DirtyPages* d = resource.dirtyPages();
	REQUIRE(d != nullptr);

	auto sections = [&]()
	{
		std::vector<uint8_t> all;
		for (const SegVal& seg : resource.segment)
		{
			const uint8_t* begin = static_cast<const uint8_t*>(seg.address);
			all.insert(all.end(), begin, begin + seg.length);
		}
		return all;
	};
	auto play = [&](int frames, int variant)
	{
		for (int i = 0; i < frames; i++)
		{
			double angle = i * (variant == 0 ? 0.37 : 0.61);
			resource.setInputs(Inputs(uint16_t(i % 7 == 0 ? 0x8000 : 0), int8_t(60.0 * std::cos(angle)), int8_t(60.0 * std::sin(angle))));
			resource.FrameAdvance();
		}
	};
	auto differing = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
	{
		size_t n = 0;
		for (size_t i = 0; i < a.size() && i < b.size(); i++)
			n += a[i] != b[i];
		return n + (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
	};
	auto pagesOf = [&](int64_t slot) { return PerfAccess::SlotState(resource, slot).pages.size() / size_t(pagesize); };
	auto isLive = [&](int b) { return d->baselines.at(size_t(b)).pages != nullptr; };

	// What a top-level run's import does: the start save at power-on, under baseline 0, which
	// holds no pages yet because nothing has been written since construction.
	std::vector<uint8_t> powerOn = sections();
	resource.SaveStart(0);
	CHECK(d->baseline == 0);
	CHECK(d->WrittenCount() == 0);

	// Run 1: the first slot takes baseline 1; the pages the replay wrote were copied into
	// baseline 0 by the fault handler before their first write.
	PlayFrames(resource, m64, frame);
	CHECK(d->WrittenCount() > 100); // the replay into the level: 525 pages on the pinned DLL, 368 on the Linux .so
	std::vector<uint8_t> snap0 = sections();
	int64_t s0 = resource.SaveState();
	CHECK(d->baseline == 1);
	CHECK(d->baselines.at(1).refs == 1); // s0's reference; the start save holds baseline 0's
	CHECK(d->baselines.at(0).refs == 1);
	CHECK(d->liveSlots == 1);
	CHECK(pagesOf(s0) == 0);
	CHECK(isLive(0));
	CHECK(isLive(1));
	play(60, 0);
	std::vector<uint8_t> snap1 = sections();
	int64_t s1 = resource.SaveState();
	CHECK(pagesOf(s1) > 0);
	CHECK(pagesOf(s1) < 200);
	for (int i = 0; i < 20; i++)
		resource.LoadState(i % 2 ? s0 : s1);
	resource.LoadState(s0);
	CHECK(differing(sections(), snap0) == 0);
	resource.LoadState(s1);
	CHECK(differing(sections(), snap1) == 0);
	CHECK(d->baseline == 1); // loads never take a baseline

	// Back to power-on through the start save, exactly, with pages from baseline 0 (the only
	// place their power-on content still exists).
	resource.DisposeState(s0);
	resource.DisposeState(s1);
	CHECK(d->baselines.at(1).refs == 0); // both states gave theirs back
	CHECK(d->liveSlots == 0);
	resource.LoadState(-1);
	CHECK(differing(sections(), powerOn) == 0);

	// Run 2: the first slot takes baseline 2, releases baseline 1 (no live state names it)
	// and keeps baseline 0 (the start save's).
	PlayFrames(resource, m64, frame);
	std::vector<uint8_t> snap2 = sections();
	int64_t s2 = resource.SaveState();
	CHECK(d->baseline == 2);
	REQUIRE(d->baselines.size() == 3);
	CHECK(isLive(0));
	CHECK_FALSE(isLive(1));
	CHECK(isLive(2));
	CHECK(pagesOf(s2) == 0);
	CHECK(differing(snap2, snap0) == 0); // the same replay reaches the same memory
	play(60, 1);
	std::vector<uint8_t> snap3 = sections();
	int64_t s3 = resource.SaveState();
	resource.LoadState(s2);
	CHECK(differing(sections(), snap2) == 0);
	resource.LoadState(s3);
	CHECK(differing(sections(), snap3) == 0);
	play(60, 0); // the same inputs from the same state reach the same memory
	std::vector<uint8_t> snap4 = sections();
	resource.LoadState(s3);
	play(60, 0);
	CHECK(differing(sections(), snap4) == 0);
	MESSAGE("dirty baselines: " << d->faults << " first writes in all baselines; " << pagesOf(s3) << " pages in a state 60 frames after the first slot");

	resource.DisposeState(s2);
	resource.DisposeState(s3);
}
