#pragma once
#include <tasfw/Script.hpp>
#include <tasfw/testing/MockResource.hpp>

#include <cstdint>

// Fixtures shared by the script engine tests (test_script_*.cpp), which pin its invariants
// on the deterministic mock resource:
//   * game state is a pure function of (start save, resolved inputs);
//   * Execute reverts, Modify persists, ad-hoc scripts sandbox the same way;
//   * loads restore exact state and replays are bit-identical;
//   * inputs resolve through the hierarchy and fall back to the movie;
//   * state trackers compute per-frame state on demand without moving the cursor.
// MockResource::checksum() folds every applied input into a rolling hash, so "same
// checksum" means "same inputs were applied in the same order".
namespace tasfw::tests
{
	// Root script that exposes the protected Script API to a test body.
	template <class Body, class TTracker = DefaultStateTracker<MockResource>>
	class TestRoot : public TopLevelScript<MockResource, TTracker>
	{
	public:
		using Base = TopLevelScript<MockResource, TTracker>;
		using Base::AdvanceFrameWrite;
		using Base::Compare;
		using Base::CompareAdhoc;
		using Base::DynamicCompare;
		using Base::DynamicCompareAdhoc;
		using Base::DynamicModifyCompare;
		using Base::DynamicModifyCompareAdhoc;
		using Base::Execute;
		using Base::ExecuteAdhoc;
		using Base::ExportM64;
		using Base::GetCurrentFrame;
		using Base::GetDiff;
		using Base::GetInputs;
		using Base::GetTrackedState;
		using Base::IsDiffEmpty;
		using Base::Load;
		using Base::LongLoad;
		using Base::Modify;
		using Base::ModifyAdhoc;
		using Base::ModifyCompare;
		using Base::ModifyCompareAdhoc;
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

	template <class TTracker = DefaultStateTracker<MockResource>, class Body>
	void RunRoot(MockResource& resource, M64& m64, Body body)
	{
		TopLevelScriptBuilder<TestRoot<Body, TTracker>>::Build(m64).ImportResource(&resource).Run(body);
	}

	// Distinct inputs for frame i, so a checksum tells which frames were applied.
	inline Inputs In(int i)
	{
		return Inputs(uint16_t(i * 7 + 1), int8_t(i * 5), int8_t(-3 * i));
	}

	class WriteFrames : public Script<MockResource>
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

	class FailingScript : public Script<MockResource>
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

	class RecursiveTracker : public Script<MockResource>
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
	class SaveInTheMiddle : public Script<MockResource>
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
	class OtherTracker : public Script<MockResource>
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
	class EvenFramesTracker : public Script<MockResource>
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
