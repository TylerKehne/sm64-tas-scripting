#include <benchmark/benchmark.h>
#include "alloc_counter.hpp"
#include <tasfw/Script.hpp>

#include <tasfw/testing/FakeResource.hpp>

// Framework overhead per operation, measured on a resource whose frame advance is ~free.
// These numbers are what the script hierarchy costs on top of the game itself.
//
// Every benchmark here uses a fixed iteration count. These operations allocate, and the
// heap state entering each benchmark then depends on exactly how many iterations the
// earlier ones ran; with Google Benchmark's adaptive counts that produced bimodal results
// between runs on MSVC. Fixed counts make the sequence deterministic.

// Root script that exposes the protected Script API to a benchmark body.
template <class Body, class TTracker = DefaultStateTracker<FakeResource>>
class BenchRoot : public TopLevelScript<FakeResource, TTracker>
{
public:
	using Base = TopLevelScript<FakeResource, TTracker>;
	using Base::AdvanceFrameWrite;
	using Base::Execute;
	using Base::ExecuteAdhoc;
	using Base::GetCurrentFrame;
	using Base::GetInputs;
	using Base::Load;
	using Base::LongLoad;
	using Base::Modify;
	using Base::ModifyAdhoc;

	// Save and AdvanceFrameRead also have private overloads in Script. A using-declaration
	// names every overload and [namespace.udecl] requires all of them to be accessible;
	// Clang enforces that, MSVC does not. Forward explicitly instead.
	void Save() { Base::Save(); }
	void AdvanceFrameRead() { Base::AdvanceFrameRead(); }

	BenchRoot(benchmark::State& state, Body body) : _state(state), _body(body) {}

	bool validation() override { return true; }
	bool execution() override
	{
		// Allocations are counted around the whole body: the timed loop plus whatever the
		// body sets up before it, which the fixed iteration counts amortise to nothing.
		uint64_t allocs0 = tasfw_perf::AllocCount();
		_body(*this, _state);
		tasfw_perf::ReportAllocs(_state, allocs0);
		return true;
	}
	bool assertion() override { return true; }

private:
	benchmark::State& _state;
	Body _body;
};

template <class TTracker = DefaultStateTracker<FakeResource>, class Body>
static void RunRoot(benchmark::State& state, Body body)
{
	FakeResource resource;
	M64 m64;
	TopLevelScriptBuilder<BenchRoot<Body, TTracker>>::Build(m64).ImportResource(&resource).Run(state, body);
	benchmark::DoNotOptimize(resource.checksum());
}

static const Inputs SomeInputs(0, 20, 20);

// --- Per-frame operations -----------------------------------------------------------------

static void BM_Script_AdvanceFrameWrite(benchmark::State& state)
{
	RunRoot(state, [](auto& s, benchmark::State& st)
		{
			for (auto _ : st)
				s.AdvanceFrameWrite(SomeInputs);
		});
}
BENCHMARK(BM_Script_AdvanceFrameWrite)->Iterations(200000);

static void BM_Script_AdvanceFrameRead(benchmark::State& state)
{
	RunRoot(state, [](auto& s, benchmark::State& st)
		{
			for (auto _ : st)
				s.AdvanceFrameRead();
		});
}
BENCHMARK(BM_Script_AdvanceFrameRead)->Iterations(200000);

static void BM_Script_AdvanceFrameWrite_Save(benchmark::State& state)
{
	RunRoot(state, [](auto& s, benchmark::State& st)
		{
			for (auto _ : st)
			{
				s.AdvanceFrameWrite(SomeInputs);
				s.Save();
			}
		});
}
BENCHMARK(BM_Script_AdvanceFrameWrite_Save)->Iterations(100000);

// Write one frame, rewind to the save just before it. The elementary "try and undo" step.
static void BM_Script_Write_RewindOne(benchmark::State& state)
{
	RunRoot(state, [](auto& s, benchmark::State& st)
		{
			for (int i = 0; i < 10; i++)
				s.AdvanceFrameWrite(SomeInputs);
			s.Save();
			int64_t saved = s.GetCurrentFrame();

			for (auto _ : st)
			{
				s.AdvanceFrameWrite(SomeInputs);
				s.Load(saved);
			}
		});
}
BENCHMARK(BM_Script_Write_RewindOne)->Iterations(1000000);

// --- Ad-hoc scripts -----------------------------------------------------------------------

static void BM_Script_ExecuteAdhoc_Empty(benchmark::State& state)
{
	RunRoot(state, [](auto& s, benchmark::State& st)
		{
			for (auto _ : st)
				s.ExecuteAdhoc([]() { return true; });
		});
}
BENCHMARK(BM_Script_ExecuteAdhoc_Empty)->Iterations(500000);

// The scattershot pellet pattern: sandbox, write a frame, revert to the parent's save.
static void BM_Script_ExecuteAdhoc_OneFrame(benchmark::State& state)
{
	RunRoot(state, [](auto& s, benchmark::State& st)
		{
			for (int i = 0; i < 10; i++)
				s.AdvanceFrameWrite(SomeInputs);
			s.Save();

			for (auto _ : st)
			{
				s.ExecuteAdhoc([&]()
					{
						s.AdvanceFrameWrite(SomeInputs);
						return true;
					});
			}
		});
}
BENCHMARK(BM_Script_ExecuteAdhoc_OneFrame)->Iterations(300000);

static void BM_Script_ModifyAdhoc_OneFrame(benchmark::State& state)
{
	RunRoot(state, [](auto& s, benchmark::State& st)
		{
			for (auto _ : st)
			{
				s.ModifyAdhoc([&]()
					{
						s.AdvanceFrameWrite(SomeInputs);
						return true;
					});
			}
		});
}
BENCHMARK(BM_Script_ModifyAdhoc_OneFrame)->Iterations(100000);

// --- Child scripts ------------------------------------------------------------------------

class EmptyScript : public Script<FakeResource>
{
public:
	class CustomScriptStatus {};
	CustomScriptStatus CustomStatus {};

	bool validation() override { return true; }
	bool execution() override { return true; }
	bool assertion() override { return true; }
};

class OneFrameScript : public Script<FakeResource>
{
public:
	class CustomScriptStatus {};
	CustomScriptStatus CustomStatus {};

	bool validation() override { return true; }
	bool execution() override
	{
		AdvanceFrameWrite(SomeInputs);
		return true;
	}
	bool assertion() override { return true; }
};

static void BM_Script_Execute_ChildEmpty(benchmark::State& state)
{
	RunRoot(state, [](auto& s, benchmark::State& st)
		{
			for (auto _ : st)
				benchmark::DoNotOptimize(s.template Execute<EmptyScript>());
		});
}
BENCHMARK(BM_Script_Execute_ChildEmpty)->Iterations(200000);

static void BM_Script_Execute_ChildOneFrame(benchmark::State& state)
{
	RunRoot(state, [](auto& s, benchmark::State& st)
		{
			for (int i = 0; i < 10; i++)
				s.AdvanceFrameWrite(SomeInputs);
			s.Save();

			for (auto _ : st)
				benchmark::DoNotOptimize(s.template Execute<OneFrameScript>());
		});
}
BENCHMARK(BM_Script_Execute_ChildOneFrame)->Iterations(150000);

static void BM_Script_Modify_ChildOneFrame(benchmark::State& state)
{
	RunRoot(state, [](auto& s, benchmark::State& st)
		{
			for (auto _ : st)
				benchmark::DoNotOptimize(s.template Modify<OneFrameScript>());
		});
}
BENCHMARK(BM_Script_Modify_ChildOneFrame)->Iterations(100000);

// --- Hierarchy depth ----------------------------------------------------------------------

// Nests itself `depth` levels, each level writing two frames, then runs the timed loop at
// the leaf so lookups have to walk the whole ancestor chain.
class DepthScript : public Script<FakeResource>
{
public:
	enum class Mode
	{
		GetInputsUncached,
		LongLoadRewindToRoot
	};

	class CustomScriptStatus {};
	CustomScriptStatus CustomStatus {};

	DepthScript(int depth, Mode mode, benchmark::State& state, int64_t rootSaveFrame)
		: _depth(depth), _mode(mode), _state(state), _rootSaveFrame(rootSaveFrame) {}

	bool validation() override { return true; }
	bool execution() override
	{
		AdvanceFrameWrite(SomeInputs);
		AdvanceFrameWrite(SomeInputs);

		if (_depth > 0)
		{
			Modify<DepthScript>(_depth - 1, _mode, _state, _rootSaveFrame);
			return true;
		}

		switch (_mode)
		{
		case Mode::GetInputsUncached:
		{
			// Each frame is queried once, so every lookup walks the chain and misses the cache.
			int64_t frame = 0;
			for (auto _ : _state)
				benchmark::DoNotOptimize(GetInputs(frame++));
			break;
		}
		case Mode::LongLoadRewindToRoot:
		{
			int64_t here = GetCurrentFrame();
			for (auto _ : _state)
			{
				LongLoad(_rootSaveFrame);
				LongLoad(here);
			}
			break;
		}
		}
		return true;
	}
	bool assertion() override { return true; }

private:
	int _depth;
	Mode _mode;
	benchmark::State& _state;
	int64_t _rootSaveFrame;
};

static void RunAtDepth(benchmark::State& state, DepthScript::Mode mode)
{
	int depth = int(state.range(0));
	RunRoot(state, [=](auto& s, benchmark::State& st)
		{
			for (int i = 0; i < 10; i++)
				s.AdvanceFrameWrite(SomeInputs);
			s.Save();
			s.template Modify<DepthScript>(depth - 1, mode, st, int64_t(s.GetCurrentFrame()));
		});
}

static void BM_Script_GetInputs_Uncached_Depth(benchmark::State& state)
{
	RunAtDepth(state, DepthScript::Mode::GetInputsUncached);
}
BENCHMARK(BM_Script_GetInputs_Uncached_Depth)->Arg(1)->Arg(4)->Arg(16)->Iterations(200000);

static void BM_Script_LongLoad_RewindToRoot_Depth(benchmark::State& state)
{
	RunAtDepth(state, DepthScript::Mode::LongLoadRewindToRoot);
}
BENCHMARK(BM_Script_LongLoad_RewindToRoot_Depth)->Arg(1)->Arg(4)->Arg(16)->Iterations(50000);

// --- State trackers -----------------------------------------------------------------------

class TrivialTracker : public Script<FakeResource>
{
public:
	class CustomScriptStatus
	{
	public:
		bool initialized = false;
		uint32_t frame = 0;
	};
	CustomScriptStatus CustomStatus {};

	bool validation() override { return true; }
	bool execution() override
	{
		CustomStatus.frame = uint32_t(GetCurrentFrame());
		CustomStatus.initialized = true;
		return true;
	}
	bool assertion() override { return CustomStatus.initialized; }
};

// Mirrors the real trackers: each frame's state depends on the previous frame's state.
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

static void BM_Script_AdvanceFrameWrite_TrivialTracker(benchmark::State& state)
{
	RunRoot<TrivialTracker>(state, [](auto& s, benchmark::State& st)
		{
			for (auto _ : st)
				s.AdvanceFrameWrite(SomeInputs);
		});
}
BENCHMARK(BM_Script_AdvanceFrameWrite_TrivialTracker)->Iterations(100000);

static void BM_Script_AdvanceFrameWrite_RecursiveTracker(benchmark::State& state)
{
	RunRoot<RecursiveTracker>(state, [](auto& s, benchmark::State& st)
		{
			for (auto _ : st)
				s.AdvanceFrameWrite(SomeInputs);
		});
}
BENCHMARK(BM_Script_AdvanceFrameWrite_RecursiveTracker)->Iterations(100000);
