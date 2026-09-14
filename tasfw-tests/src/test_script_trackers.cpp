#include <doctest/doctest.h>

#include "script_fixtures.hpp"

#include <cstdint>
#include <stdexcept>

// State trackers on the mock resource (script_fixtures.hpp): per-frame state computed on
// demand and recursively, in a sandbox that leaves the cursor where it was, typed by the
// tracker the root installs, and following the diff through Modify and Execute.

using namespace tasfw::tests;

TEST_CASE("State trackers compute per-frame state, recursively, without moving the cursor")
{
	MockResource resource;
	M64 m64;
	RunRoot<RecursiveTracker>(resource, m64, [](auto& s)
		{
			for (int i = 0; i < 10; i++)
				s.AdvanceFrameWrite(In(i));

			auto at10 = s.template GetTrackedState<RecursiveTracker>(10);
			CHECK(at10.initialized);
			CHECK(at10.sum == 55); // 0 + 1 + ... + 10

			auto at4 = s.template GetTrackedState<RecursiveTracker>(4);
			CHECK(at4.sum == 10);
			CHECK(s.GetCurrentFrame() == 10);

			// Rewriting frame 6 invalidates tracked states after it; they are recomputed.
			s.Load(6);
			s.AdvanceFrameWrite(In(60));
			for (int i = 7; i < 10; i++)
				s.AdvanceFrameWrite(In(i));
			CHECK(s.template GetTrackedState<RecursiveTracker>(10).sum == 55);
		});
}

TEST_CASE("A tracked state ahead of the cursor is computed in a sandbox and the cursor stays")
{
	MockResource resource;
	M64 m64;
	for (int i = 0; i < 20; i++)
		m64.frames[i] = In(100 + i);

	RunRoot<RecursiveTracker>(resource, m64, [&resource](auto& s)
		{
			for (int i = 0; i < 5; i++)
				s.AdvanceFrameRead();
			CHECK(s.GetCurrentFrame() == 5);
			uint64_t checksum = resource.checksum();

			// Frame 12 is seven frames ahead: the tracker advances there in its own sandbox
			// and is reverted; the requesting script does not move and writes nothing.
			const auto& at12 = s.template GetTrackedState<RecursiveTracker>(12);
			CHECK(at12.sum == 78); // 0 + 1 + ... + 12
			CHECK(s.GetCurrentFrame() == 5);
			CHECK(resource.checksum() == checksum);
			CHECK(s.IsDiffEmpty());

			// The frames computed on the way are cached: no further frame advances.
			uint64_t advances = resource.work.frameAdvances;
			CHECK(s.template GetTrackedState<RecursiveTracker>(12).sum == 78);
			CHECK(s.template GetTrackedState<RecursiveTracker>(9).sum == 45);
			CHECK(resource.work.frameAdvances == advances);
		});
}

TEST_CASE("Asking for a tracker type the root does not install throws instead of miscasting")
{
	MockResource resource;
	M64 m64;
	RunRoot<RecursiveTracker>(resource, m64, [](auto& s)
		{
			s.AdvanceFrameWrite(In(0));
			CHECK_THROWS_AS(s.template GetTrackedState<OtherTracker>(1), std::runtime_error);
			CHECK_THROWS_AS(s.template TrackedStateExists<OtherTracker>(1), std::runtime_error);
			// The right type still works afterwards.
			CHECK(s.template GetTrackedState<RecursiveTracker>(1).sum == 1);
		});

	// A root without a tracker rejects every tracker type.
	RunRoot(resource, m64, [](auto& s)
		{
			CHECK_THROWS_AS(s.template GetTrackedState<OtherTracker>(0), std::runtime_error);
		});
}

TEST_CASE("A tracker that does not assert leaves a default state that is not recomputed")
{
	MockResource resource;
	M64 m64;
	RunRoot<EvenFramesTracker>(resource, m64, [&resource](auto& s)
		{
			for (int i = 0; i < 4; i++)
				s.AdvanceFrameWrite(In(i));

			CHECK(s.template GetTrackedState<EvenFramesTracker>(2).initialized);
			CHECK(s.template GetTrackedState<EvenFramesTracker>(2).frame == 2);
			CHECK_FALSE(s.template GetTrackedState<EvenFramesTracker>(3).initialized);
			CHECK(s.template TrackedStateExists<EvenFramesTracker>(3)); // stored, as a default

			uint64_t advances = resource.work.frameAdvances;
			CHECK_FALSE(s.template GetTrackedState<EvenFramesTracker>(3).initialized);
			CHECK(resource.work.frameAdvances == advances); // served from the table
		});
}

TEST_CASE("Tracked states follow the diff: kept by Modify, dropped by Execute")
{
	MockResource resource;
	M64 m64;
	RunRoot<RecursiveTracker>(resource, m64, [](auto& s)
		{
			for (int i = 0; i < 3; i++)
				s.AdvanceFrameWrite(In(i));

			// Execute: the child's frames are reverted, and so are their states.
			s.template Execute<WriteFrames>(4, 10);
			CHECK(s.GetCurrentFrame() == 3);
			CHECK_FALSE(s.template TrackedStateExists<RecursiveTracker>(6));

			// Modify: the frames persist and their states are handed to the parent.
			s.template Modify<WriteFrames>(4, 20);
			CHECK(s.GetCurrentFrame() == 7);
			CHECK(s.template TrackedStateExists<RecursiveTracker>(6));
			CHECK(s.template GetTrackedState<RecursiveTracker>(7).sum == 28); // 0 + ... + 7

			// A reference into the table stays valid until a write invalidates that frame.
			const auto& at5 = s.template GetTrackedState<RecursiveTracker>(5);
			CHECK(at5.sum == 15);
			CHECK(s.template GetTrackedState<RecursiveTracker>(7).sum == 28); // unrelated lookup
			CHECK(at5.sum == 15);
		});
}
