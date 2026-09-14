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
	RunRoot(resource, m64, [](auto& s)
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

			const MockState& state = s.resource->state();
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

	RunRoot(resource, m64, [](auto& s)
		{
			for (int i = 0; i < 10; i++)
			{
				CHECK(s.GetInputs(i) == In(100 + i));
				s.AdvanceFrameRead();
				CHECK(s.resource->state().buttons == In(100 + i).buttons);
			}
			CHECK(s.IsDiffEmpty());
			CHECK(s.GetCurrentFrame() == 10);
		});
}

TEST_CASE("Execute reverts the child's frames; Modify keeps them")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [](auto& s)
		{
			s.AdvanceFrameWrite(In(0));
			s.AdvanceFrameWrite(In(1));
			uint64_t before = s.resource->checksum();

			auto executed = s.template Execute<WriteFrames>(3, 10);
			CHECK(executed.executed);
			CHECK(executed.asserted);
			CHECK(executed.m64Diff.frames.size() == 3);
			CHECK(executed.lastFrame == 4);
			CHECK(s.GetCurrentFrame() == 2);
			CHECK(s.resource->checksum() == before);
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
			CHECK(s.resource->checksum() != before);
		});
}

TEST_CASE("Modify reverts a child whose assertion fails")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [](auto& s)
		{
			s.AdvanceFrameWrite(In(0));
			uint64_t before = s.resource->checksum();

			auto status = s.template Modify<FailingScript>();
			CHECK(status.executed);
			CHECK_FALSE(status.asserted);
			CHECK(s.GetCurrentFrame() == 1);
			CHECK(s.resource->checksum() == before);
			CHECK(s.GetDiff().frames.size() == 1);
		});
}

TEST_CASE("ExecuteAdhoc sandboxes; ModifyAdhoc persists")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [](auto& s)
		{
			s.AdvanceFrameWrite(In(0));
			uint64_t before = s.resource->checksum();

			auto sandboxed = s.ExecuteAdhoc([&]()
				{
					s.AdvanceFrameWrite(In(1));
					s.AdvanceFrameWrite(In(2));
					return true;
				});
			CHECK(sandboxed.executed);
			CHECK(sandboxed.m64Diff.frames.size() == 2);
			CHECK(s.GetCurrentFrame() == 1);
			CHECK(s.resource->checksum() == before);

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
