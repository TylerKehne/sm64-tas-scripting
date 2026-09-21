#include <doctest/doctest.h>

#include "script_fixtures.hpp"

#include <cstdint>

// The script lifecycle on the mock resource (script_fixtures.hpp): what AdvanceFrameWrite
// records, where inputs come from, and what Execute, Modify, Test and the ad-hoc runners
// keep or revert.

using namespace tasfw::tests;

TEST_CASE("AdvanceFrameWrite records the diff and applies inputs to the resource")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			for (int i = 0; i < 5; i++)
				s.AdvanceFrameWrite(In(i));

			CHECK(s.GetCurrentFrame() == 5);
			M64Diff diff = s.GetDiff();
			REQUIRE(diff.frames.size() == 5);
			for (int i = 0; i < 5; i++)
				CHECK(diff.frames.at(i) == In(i));

			CHECK(s.GetInputs(3) == In(3));
			CHECK(s.GetInputs(7) == Inputs(0, 0, 0)); // beyond the diff, no movie: neutral

			const MockState& state = resource.state();
			CHECK(state.buttons == In(4).buttons);
			CHECK(state.stickX == In(4).stick_x);
			CHECK(state.stickY == In(4).stick_y);
		});
}

TEST_CASE("Inputs fall back to the movie when no diff covers a frame")
{
	MockResource resource;
	M64 m64;
	for (int i = 0; i < 10; i++)
		m64.frames[i] = In(100 + i);

	RunRoot(resource, m64, [&](auto& s)
		{
			for (int i = 0; i < 10; i++)
			{
				CHECK(s.GetInputs(i) == In(100 + i));
				s.AdvanceFrameRead();
				CHECK(resource.state().buttons == In(100 + i).buttons);
			}
			CHECK(s.IsDiffEmpty());
			CHECK(s.GetCurrentFrame() == 10);
		});
}

TEST_CASE("Execute reverts the child's frames; Modify keeps them")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			s.AdvanceFrameWrite(In(0));
			s.AdvanceFrameWrite(In(1));
			uint64_t before = resource.checksum();

			auto executed = s.template Execute<WriteFrames>(3, 10);
			CHECK(executed.executed);
			CHECK(executed.asserted);
			CHECK(executed.m64Diff.frames.size() == 3);
			CHECK(executed.lastFrame == 4);
			CHECK(s.GetCurrentFrame() == 2);
			CHECK(resource.checksum() == before);
			CHECK(s.GetDiff().frames.size() == 2);

			auto tested = s.template Test<WriteFrames>(3, 10);
			CHECK(tested.executed);
			CHECK(tested.m64Diff.frames.empty()); // Test drops the diff from the status
			CHECK(s.GetCurrentFrame() == 2);

			auto modified = s.template Modify<WriteFrames>(3, 20);
			CHECK(modified.asserted);
			CHECK(s.GetCurrentFrame() == 5);
			CHECK(s.GetDiff().frames.size() == 5);
			CHECK(s.GetInputs(4) == In(22));
			CHECK(resource.checksum() != before);
		});
}

TEST_CASE("Modify reverts a child whose assertion fails")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			s.AdvanceFrameWrite(In(0));
			uint64_t before = resource.checksum();

			auto status = s.template Modify<FailingScript>();
			CHECK(status.executed);
			CHECK_FALSE(status.asserted);
			CHECK(s.GetCurrentFrame() == 1);
			CHECK(resource.checksum() == before);
			CHECK(s.GetDiff().frames.size() == 1);
		});
}

TEST_CASE("ExecuteAdhoc sandboxes; ModifyAdhoc persists")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			s.AdvanceFrameWrite(In(0));
			uint64_t before = resource.checksum();

			auto sandboxed = s.ExecuteAdhoc([&]()
				{
					s.AdvanceFrameWrite(In(1));
					s.AdvanceFrameWrite(In(2));
					return true;
				});
			CHECK(sandboxed.executed);
			CHECK(sandboxed.m64Diff.frames.size() == 2);
			CHECK(s.GetCurrentFrame() == 1);
			CHECK(resource.checksum() == before);

			auto kept = s.ModifyAdhoc([&]()
				{
					s.AdvanceFrameWrite(In(1));
					return true;
				});
			CHECK(kept.executed);
			CHECK(s.GetCurrentFrame() == 2);
			CHECK(s.GetInputs(1) == In(1));

			// A failed ad-hoc body is reverted even by ModifyAdhoc.
			auto rejected = s.ModifyAdhoc([&]()
				{
					s.AdvanceFrameWrite(In(50));
					return false;
				});
			CHECK_FALSE(rejected.executed);
			CHECK(s.GetCurrentFrame() == 2);
		});
}

// The status-carrying forms hand the body a pointer to the status that comes back flattened
// into the result, the way the compare family hands its candidates theirs (docs/tasing.md,
// "Ad-hoc scripts"). Nothing in the tree instantiated them before 2026-09-21, when they
// turned out to pass the object instead. (TestAdhoc's status form is protected, so a script
// cannot call it; not exercised here.)
TEST_CASE("ExecuteAdhoc and ModifyAdhoc carry a status the body fills")
{
	struct Seen
	{
		int frames = 0;
	};

	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&](auto& s)
		{
			auto looked = s.template ExecuteAdhoc<Seen>([&](Seen* seen)
				{
					s.AdvanceFrameWrite(In(0));
					s.AdvanceFrameWrite(In(1));
					seen->frames = 2;
					return true;
				});
			CHECK(looked.executed);
			CHECK(looked.frames == 2);
			CHECK(looked.m64Diff.frames.size() == 2);
			CHECK(s.GetCurrentFrame() == 0);

			auto kept = s.template ModifyAdhoc<Seen>([&](Seen* seen)
				{
					s.AdvanceFrameWrite(In(0));
					seen->frames = 1;
					return true;
				});
			CHECK(kept.executed);
			CHECK(kept.frames == 1);
			CHECK(s.GetCurrentFrame() == 1);

			// A body that returns false is reverted and its status still comes back.
			auto rejected = s.template ModifyAdhoc<Seen>([&](Seen* seen)
				{
					s.AdvanceFrameWrite(In(50));
					seen->frames = 7;
					return false;
				});
			CHECK_FALSE(rejected.executed);
			CHECK(rejected.frames == 7);
			CHECK(s.GetCurrentFrame() == 1);
		});
}
