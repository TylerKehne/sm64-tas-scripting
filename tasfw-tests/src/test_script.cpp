#include <doctest/doctest.h>
#include <tasfw/Script.hpp>
#include <tasfw/testing/FakeResource.hpp>

#include <cstdint>

// The script engine's invariants, on the deterministic fake resource:
//   * game state is a pure function of (start save, resolved inputs);
//   * Execute reverts, Modify persists, ad-hoc scripts sandbox the same way;
//   * loads restore exact state and replays are bit-identical;
//   * inputs resolve through the hierarchy and fall back to the movie.
// FakeResource::checksum() folds every applied input into a rolling hash, so "same
// checksum" means "same inputs were applied in the same order".

namespace
{
	// Root script that exposes the protected Script API to a test body.
	template <class Body, class TTracker = DefaultStateTracker<FakeResource>>
	class TestRoot : public TopLevelScript<FakeResource, TTracker>
	{
	public:
		using Base = TopLevelScript<FakeResource, TTracker>;
		using Base::AdvanceFrameWrite;
		using Base::Execute;
		using Base::ExecuteAdhoc;
		using Base::GetCurrentFrame;
		using Base::GetDiff;
		using Base::GetInputs;
		using Base::GetTrackedState;
		using Base::IsDiffEmpty;
		using Base::Load;
		using Base::LongLoad;
		using Base::Modify;
		using Base::ModifyAdhoc;
		using Base::Rollback;
		using Base::Test;
		using Base::TrackedStateExists;

		// Overloaded with private variants in Script; forward instead of using-declaring.
		void Save() { Base::Save(); }
		void AdvanceFrameRead() { Base::AdvanceFrameRead(); }

		explicit TestRoot(Body body) : _body(body) {}

		bool validation() override { return true; }
		bool execution() override
		{
			_body(*this);
			return true;
		}
		bool assertion() override { return true; }

	private:
		Body _body;
	};

	template <class TTracker = DefaultStateTracker<FakeResource>, class Body>
	void RunRoot(FakeResource& resource, M64& m64, Body body)
	{
		TopLevelScriptBuilder<TestRoot<Body, TTracker>>::Build(m64).ImportResource(&resource).Run(body);
	}

	Inputs In(int i)
	{
		return Inputs(uint16_t(i * 7 + 1), int8_t(i * 5), int8_t(-3 * i));
	}

	class WriteFrames : public Script<FakeResource>
	{
	public:
		class CustomScriptStatus
		{
		public:
			int64_t lastFrame = -1;
		};
		CustomScriptStatus CustomStatus {};

		WriteFrames(int count, int seed) : _count(count), _seed(seed) {}

		bool validation() override { return true; }
		bool execution() override
		{
			for (int i = 0; i < _count; i++)
				AdvanceFrameWrite(In(_seed + i));
			CustomStatus.lastFrame = GetCurrentFrame() - 1;
			return true;
		}
		bool assertion() override { return true; }

	private:
		int _count;
		int _seed;
	};

	class FailingScript : public Script<FakeResource>
	{
	public:
		class CustomScriptStatus {};
		CustomScriptStatus CustomStatus {};

		bool validation() override { return true; }
		bool execution() override
		{
			AdvanceFrameWrite(In(99));
			return true;
		}
		bool assertion() override { return false; } // never accepted
	};

	class RecursiveTracker : public Script<FakeResource>
	{
	public:
		class CustomScriptStatus
		{
		public:
			bool initialized = false;
			uint64_t sum = 0;
		};
		CustomScriptStatus CustomStatus {};

		bool validation() override { return true; }
		bool execution() override
		{
			int64_t frame = GetCurrentFrame();
			CustomStatus.sum = uint64_t(frame);
			if (frame > 0)
				CustomStatus.sum += GetTrackedState<RecursiveTracker>(frame - 1).sum;
			CustomStatus.initialized = true;
			return true;
		}
		bool assertion() override { return CustomStatus.initialized; }
	};

	// Writes two frames, saves, writes two more.
	class SaveInTheMiddle : public Script<FakeResource>
	{
	public:
		class CustomScriptStatus
		{
		public:
			int64_t savedFrame = -1;
		};
		CustomScriptStatus CustomStatus {};

		bool validation() override { return true; }
		bool execution() override
		{
			AdvanceFrameWrite(In(1));
			AdvanceFrameWrite(In(2));
			Save();
			CustomStatus.savedFrame = GetCurrentFrame();
			AdvanceFrameWrite(In(3));
			AdvanceFrameWrite(In(4));
			return true;
		}
		bool assertion() override { return true; }
	};

	// A tracker type the test roots never install; used to check the type guard.
	class OtherTracker : public Script<FakeResource>
	{
	public:
		class CustomScriptStatus
		{
		public:
			int value = 0;
		};
		CustomScriptStatus CustomStatus {};

		bool validation() override { return true; }
		bool execution() override { return true; }
		bool assertion() override { return true; }
	};

	// Asserts only on even frames, so odd frames have no accepted state.
	class EvenFramesTracker : public Script<FakeResource>
	{
	public:
		class CustomScriptStatus
		{
		public:
			bool initialized = false;
			int64_t frame = -1;
		};
		CustomScriptStatus CustomStatus {};

		bool validation() override { return true; }
		bool execution() override
		{
			CustomStatus.frame = GetCurrentFrame();
			CustomStatus.initialized = true;
			return true;
		}
		bool assertion() override { return CustomStatus.frame % 2 == 0; }
	};
}

TEST_CASE("AdvanceFrameWrite records the diff and applies inputs to the resource")
{
	FakeResource resource;
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

			const FakeState& state = s.resource->state();
			CHECK(state.buttons == In(4).buttons);
			CHECK(state.stickX == In(4).stick_x);
			CHECK(state.stickY == In(4).stick_y);
		});
}

TEST_CASE("Inputs fall back to the movie when no diff covers a frame")
{
	FakeResource resource;
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
	FakeResource resource;
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

TEST_CASE("Saves made by a child survive being handed to the parent on Modify")
{
	FakeResource resource;
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
			uint64_t loads = resource.nLoadStates;
			uint64_t advances = resource.nFrameAdvances;
			s.Load(3);
			CHECK(s.GetCurrentFrame() == 3);
			CHECK(resource.nLoadStates == loads + 1);
			CHECK(resource.nFrameAdvances == advances);
		});
}

TEST_CASE("Modify reverts a child whose assertion fails")
{
	FakeResource resource;
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
	FakeResource resource;
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

TEST_CASE("Load restores exact state and replays are bit-identical")
{
	FakeResource resource;
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
	FakeResource resource;
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
	FakeResource resource;
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

TEST_CASE("Ad-hoc writes invalidate later saves and caches")
{
	FakeResource resource;
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

TEST_CASE("State trackers compute per-frame state, recursively, without moving the cursor")
{
	FakeResource resource;
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

TEST_CASE("Asking for a tracker type the root does not install throws instead of miscasting")
{
	FakeResource resource;
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
	FakeResource resource;
	M64 m64;
	RunRoot<EvenFramesTracker>(resource, m64, [&resource](auto& s)
		{
			for (int i = 0; i < 4; i++)
				s.AdvanceFrameWrite(In(i));

			CHECK(s.template GetTrackedState<EvenFramesTracker>(2).initialized);
			CHECK(s.template GetTrackedState<EvenFramesTracker>(2).frame == 2);
			CHECK_FALSE(s.template GetTrackedState<EvenFramesTracker>(3).initialized);
			CHECK(s.template TrackedStateExists<EvenFramesTracker>(3)); // stored, as a default

			uint64_t advances = resource.nFrameAdvances;
			CHECK_FALSE(s.template GetTrackedState<EvenFramesTracker>(3).initialized);
			CHECK(resource.nFrameAdvances == advances); // served from the table
		});
}

TEST_CASE("Tracked states follow the diff: kept by Modify, dropped by Execute")
{
	FakeResource resource;
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
