#include <doctest/doctest.h>

#include "script_fixtures.hpp"

#include <cstdint>

// Savestates on the mock resource (script_fixtures.hpp): loads restore exact state and
// replays are bit-identical, state is a pure function of the start save and the resolved
// inputs, rollback and ad-hoc writes invalidate what they must, and a child's saves reach
// the parent only when the inputs they were made with survive.

using namespace tasfw::tests;

TEST_CASE("Load restores exact state and replays are bit-identical")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [](auto& s)
		{
			for (int i = 0; i < 5; i++)
				s.AdvanceFrameWrite(In(i));
			uint64_t at5 = s.resource->checksum();
			s.Save();

			for (int i = 5; i < 10; i++)
				s.AdvanceFrameWrite(In(i));
			uint64_t at10 = s.resource->checksum();

			s.Load(5);
			CHECK(s.GetCurrentFrame() == 5);
			CHECK(s.resource->checksum() == at5);

			s.Load(10); // replays frames 5..9 from the diff
			CHECK(s.GetCurrentFrame() == 10);
			CHECK(s.resource->checksum() == at10);

			s.Load(0); // the start save
			CHECK(s.GetCurrentFrame() == 0);
			s.Load(10); // full replay from the start save
			CHECK(s.resource->checksum() == at10);

			s.LongLoad(3);
			CHECK(s.GetCurrentFrame() == 3);
			s.LongLoad(10);
			CHECK(s.resource->checksum() == at10);
		});
}

TEST_CASE("State is a pure function of the start save and the resolved inputs across the hierarchy")
{
	MockResource resource;
	M64 m64;
	for (int i = 0; i < 20; i++)
		m64.frames[i] = In(500 + i);

	RunRoot(resource, m64, [](auto& s)
		{
			// Root overrides frames 0..4, a child overrides 5..6, frames 7..9 come from the movie.
			for (int i = 0; i < 5; i++)
				s.AdvanceFrameWrite(In(i));
			s.template Modify<WriteFrames>(2, 30);
			for (int i = 0; i < 3; i++)
				s.AdvanceFrameRead();
			CHECK(s.GetCurrentFrame() == 10);
			uint64_t at10 = s.resource->checksum();

			CHECK(s.GetInputs(4) == In(4));
			CHECK(s.GetInputs(5) == In(30));
			CHECK(s.GetInputs(6) == In(31));
			CHECK(s.GetInputs(7) == In(507));

			s.Load(0);
			s.Load(10);
			CHECK(s.resource->checksum() == at10);
		});
}

TEST_CASE("Rollback erases the diff from the target frame and lands there")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [](auto& s)
		{
			for (int i = 0; i < 10; i++)
				s.AdvanceFrameWrite(In(i));
			uint64_t at5 = 0;
			s.Load(5);
			at5 = s.resource->checksum();
			s.Load(10);

			s.Rollback(5);
			CHECK(s.GetCurrentFrame() == 5);
			CHECK(s.resource->checksum() == at5);
			CHECK(s.GetDiff().frames.size() == 5);
			CHECK(s.GetInputs(7) == Inputs(0, 0, 0));
		});
}

TEST_CASE("Saves made by a child survive being handed to the parent on Modify")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&resource](auto& s)
		{
			s.AdvanceFrameWrite(In(0));
			auto status = s.template Modify<SaveInTheMiddle>();
			REQUIRE(status.asserted);
			REQUIRE(status.savedFrame == 3);
			CHECK(s.GetCurrentFrame() == 5);

			// Loading the child's save frame from the parent is one load and no replay: the
			// handle was moved, not copied and then released by the child's bank.
			uint64_t loads = resource.work.loads;
			uint64_t advances = resource.work.frameAdvances;
			s.Load(3);
			CHECK(s.GetCurrentFrame() == 3);
			CHECK(resource.work.loads == loads + 1);
			CHECK(resource.work.frameAdvances == advances);
		});
}

TEST_CASE("A reverted child's saves made after its first written frame never serve the parent")
{
	// ROADMAP 4.5: scattershot's base-block validation failed on about 2% of shots because
	// Revert moved every save of a child whose saves were all desynced into the parent's
	// bank. The mock resource's cost model is off so that no auto-save can mask the stale
	// one by landing on the same frame first.
	MockResource resource;
	resource.useCostModel = false;
	M64 m64;
	for (int i = 0; i < 20; i++)
		m64.frames[i] = In(500 + i);

	RunRoot(resource, m64, [](auto& s)
		{
			for (int i = 0; i < 3; i++)
				s.AdvanceFrameWrite(In(i));
			s.Load(6); // frames 3..5 from the movie
			uint64_t at6 = s.resource->checksum();
			s.Load(3);

			// The child overwrites frames 3 and 4, so its save at 6 depends on inputs that
			// are reverted with it.
			auto status = s.ExecuteAdhoc([&]()
				{
					s.AdvanceFrameWrite(In(40));
					s.AdvanceFrameWrite(In(41));
					s.AdvanceFrameRead();
					s.Save();
					return s.GetCurrentFrame() == 6;
				});
			REQUIRE(status.executed);
			CHECK(s.GetCurrentFrame() == 3);
			CHECK(s.GetInputs(3) == In(503));

			// Go past 6 and come back: a load backwards takes the latest save at or before
			// the target, which must not be the child's.
			s.Load(8);
			s.Load(6);
			CHECK(s.GetCurrentFrame() == 6);
			CHECK(s.resource->checksum() == at6);
		});
}

TEST_CASE("Ad-hoc writes invalidate later saves and caches")
{
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [](auto& s)
		{
			for (int i = 0; i < 6; i++)
				s.AdvanceFrameWrite(In(i));
			s.Save();
			s.Load(3);

			// Overwrite frame 3 with different inputs, then move forward: the save at 6 was
			// made with the old inputs and must not be reused.
			s.AdvanceFrameWrite(In(77));
			s.Load(6);
			CHECK(s.GetCurrentFrame() == 6);
			CHECK(s.GetInputs(3) == In(77));
			CHECK(s.GetInputs(4) == In(4));

			uint64_t direct = s.resource->checksum();
			s.Load(0);
			s.Load(6);
			CHECK(s.resource->checksum() == direct);
		});
}

TEST_CASE("A forward load jumps to a save that lies between the cursor and the target")
{
	// LoadBase and LongLoad compared that save's frame with the target instead of the cursor
	// from 2022-06-14 to 2026-09-13, which the lookup makes impossible, so a forward load
	// always replayed from where it stood (ROADMAP 3.11). The cost model is on: on the mock
	// resource a load is a 256-byte copy and an advance a few operations, so skipping a
	// thousand frames is cheaper than replaying them by a wide margin.
	MockResource resource;
	M64 m64;
	RunRoot(resource, m64, [&resource](auto& s)
		{
			uint64_t at1190 = 0;
			for (int i = 0; i < 1200; i++)
			{
				if (i == 1000)
					s.Save();
				if (i == 1190)
					at1190 = s.resource->checksum();
				s.AdvanceFrameWrite(In(i));
			}
			s.Load(0);

			uint64_t loads = resource.work.loads;
			uint64_t advances = resource.work.frameAdvances;
			s.Load(1190); // the save at 1000 lies between the cursor and the target
			CHECK(s.GetCurrentFrame() == 1190);
			CHECK(s.resource->checksum() == at1190);
			CHECK(resource.work.loads == loads + 1);
			CHECK(resource.work.frameAdvances == advances + 190);

			// LongLoad takes the same jump (and saves where it lands, as it always did).
			s.Load(0);
			loads = resource.work.loads;
			advances = resource.work.frameAdvances;
			s.LongLoad(1190);
			CHECK(s.GetCurrentFrame() == 1190);
			CHECK(s.resource->checksum() == at1190);
			CHECK(resource.work.loads == loads + 1);
			CHECK(resource.work.frameAdvances == advances + 190);
		});
}
