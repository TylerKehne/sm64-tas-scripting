# TASing with the framework

How to make a TAS with this framework: which tool to reach for, how a goal turns into a
script and a run, how a result is checked, and which of the hard rules bite on the way.
Written for an agent or a person who writes scripts and runs them (AGENTS.md, "Who it is
for"): comfortable with code, not necessarily with C++, and not expected to know the
framework's internals. ARCHITECTURE.md explains how the pieces work; this page says how to
use them. The status of the guidelines is ROADMAP 3.17.

## The rules

The maintainer's rules for TASing with the framework (2026-09-15):

- **Use the framework's methods and idioms for TASing; do not hack around it.** Frames
  advance through `AdvanceFrameWrite` and `Apply`, game memory is read through
  `ReadState`, saves and loads are the framework's, results leave a script through its
  `CustomStatus`. A direct write into game memory, a file edited by hand, a result
  smuggled out through a member, each defeats what the framework guarantees (replay,
  reverts, determinism) and is how the search silently corrupts itself.
- **If something you want to do seems impossible, bring it up.** It may be possible with
  some direction, or it may be a gap the framework should close (hard rule 10 makes that
  a design discussion, not a workaround). Do not conclude "the framework cannot" and route
  around it.
- **Use the existing scripts as a guideline, not a constraint.** They show the idioms and
  they work, but they were written at different times and some of their patterns are
  worse than what the framework now offers. There may be better ways within the
  framework; experiment.
- **The compare family may help even though it is not used much.** `Compare`,
  `ModifyCompare`, their `Dynamic` variants and the ad-hoc forms run a candidate over a
  parameter sequence and keep the best; most of the sweeps in the existing scripts are
  hand-rolled loops that could be one of these.
- **Make the TAS as fast as possible and the script efficient.** Use the framework, the
  game's mechanics from the decompilation, and general algorithmic knowledge for both:
  the fewest frames in the movie, and the fewest frame advances, saves and loads per
  useful frame of output (docs/performance.md). The two trade off, and it is not always
  possible to improve one without hurting the other; which matters more depends on the
  context. Usually, in a TASing context, saving movie frames matters more; for the
  squish-cancel brute forcer, overall performance matters more (the maintainer,
  2026-09-16). Say which one a change serves.
- **Ad-hoc scripts for simple or one-off tasks, script classes for heavy or modular ones**
  that other scripts may want to use (AGENTS.md, "Conventions").
- **Metrics are a powerful feature.** A metric script makes state about the game
  available at any frame, computed once and cached: derived quantities, so a decision
  that rests on more than a raw memory variable is made in one place and the script that
  decides stays small, fast and organized; and raw variables too, when a script wants to
  look at them in the past without rewinding. Looking into the future can be worth it as
  well (the maintainer, 2026-09-16).

## Which tool to reach for

| You want to | Reach for |
|---|---|
| Try a sequence of inputs and keep it only if it worked | `ModifyAdhoc([&] { ...; return ok; })`, checked through `.executed` |
| Look at what would happen without keeping it | `ExecuteAdhoc` (the diff comes back in the status), or `TestAdhoc` (it does not) |
| A move other scripts will reuse, or one with its own validity and result | A script class: `class X : public Script<LibSm64>` with `validation`, `execution`, `assertion` and a `CustomScriptStatus` |
| Run a child script and keep its frames | `Modify<X>(args...)`; to look first and decide later, `Execute<X>` then `Apply(status.m64Diff)`; for a query that advances nothing, `Test<X>` |
| Find the best of a parameter sequence | The compare family: `ModifyCompareAdhoc<TStatus, std::tuple<Ts...>>(generator, candidate, comparator[, terminator])`, and `Dynamic...` when the candidates are "one more frame of the same thing" |
| State about the game at another frame: a raw variable in the past without a rewind, or a derived quantity (phase, oscillation count, distance to a target) | A metric script, read with `GetMetrics(frame)` |
| What the game would compute, without playing it | A resource that simulates the part of the game that matters (`PyramidUpdate`), reached through `ExportSave<PyramidUpdateMem>(pyramid)` into a `TopLevelScript<PyramidUpdate>`; or `simulate_platform_tilt` from `tasfw-core/src/decomp` (see "Simulating the part of the game that matters") |
| Explore a large input space, or find a near-optimal route | A scattershot search, a proven route finder for SM64 in general: a metric script, a `ScattershotThread` subclass and a solution type, run as a stage of `bitfs-turn` |
| Run a script on the game | A stage type in `Stages.cpp` (run by `bitfs-turn --stage`), a doctest case under `libsm64:`, a tool, or a new folder with its own `main.cpp` (see "Running a script") |
| Look at the game rather than play it | `VerifyLayout`, `LevelTransitions`, `MarioTrace` through `dllcheck`; `--objects`, `--trace`, `--levels` (docs/libsm64.md) |

## The anatomy of a script

A script is one class. `<tasfw/Script.hpp>` is the framework include (it pulls in the
root and the builders itself; never include `TopLevelScript.hpp` or
`TopLevelScriptBuilder.hpp` directly, they refuse), plus the resource's header
(`<LibSm64.hpp>`) and the decomp headers it dereferences (`<sm64/Sm64.hpp>` for
`MarioState` and the `ACT_*` constants, `<sm64/Camera.hpp>`, `<sm64/ObjectFields.hpp>` for
the `o*` object fields, `<sm64/Trig.hpp>` for `sins`, `coss`, `atan2s` and the tables).

The shape, modelled on `BrakeToIdle` (tasfw-scripts/inc/General.hpp, the committed
primitive to copy):

```cpp
#include <tasfw/Script.hpp>
#include <LibSm64.hpp>
#include <sm64/Sm64.hpp>
#include <sm64/Camera.hpp>

// Walk toward a yaw for a number of frames. Illustrative: the same shape as BrakeToIdle.
class WalkTowardYaw : public Script<LibSm64>
{
public:
	class CustomScriptStatus
	{
	public:
		float finalSpeed = 0;
	};
	CustomScriptStatus CustomStatus = CustomScriptStatus();

	WalkTowardYaw(int16_t yaw, int frames) : _yaw(yaw), _frames(frames) {}

	bool validation()
	{
		MarioState* marioState = (MarioState*)(ReadState("gMarioStates"));
		return marioState->action == ACT_IDLE || marioState->action == ACT_WALKING;
	}

	bool execution()
	{
		MarioState* marioState = (MarioState*)(ReadState("gMarioStates"));
		Camera* camera = *(Camera**)(ReadState("gCamera"));
		for (int i = 0; i < _frames; i++)
		{
			auto stick = Inputs::GetClosestInputByYawHau(_yaw, 32, camera->yaw);
			AdvanceFrameWrite(Inputs(0, stick.first, stick.second));
			if (marioState->action != ACT_WALKING)
				return false;
		}
		CustomStatus.finalSpeed = marioState->forwardVel;
		return true;
	}

	bool assertion()
	{
		return !IsDiffEmpty() && CustomStatus.finalSpeed > 0;
	}

private:
	int16_t _yaw;
	int _frames;
};
```

**The lifecycle** (hard rule 4). `validation()` says whether the script applies to the
state it was started in; `execution()` produces the input diff; `assertion()` says whether
the result is the one wanted. Validation and assertion run in a sandbox the framework
reverts, so they may advance frames or load to look around, and nothing they do persists;
execution is the only phase whose diff can persist, and only if all three returned true.
A parent reads the outcome from the returned `ScriptStatus<X>`: `validated`, `executed`,
`asserted`, the `m64Diff`, the counts `nFrameAdvances`, `nSaves`, `nLoads`, and the
script's own `CustomScriptStatus` fields, flattened into the same object
(`status.finalSpeed`). Check `asserted`; `executed` alone says the body ran, not that it
succeeded. Fields written during `validation()` reach the parent even when validation
fails (`BitFsScApproach_AttemptDr` uses that to say why).

**Results leave through `CustomStatus`**, never through a member the parent reads or a
global. Keep it plain data with defaults; it is moved into the returned status. Keep it
small in a metric script: it is stored once per frame, and a `std::vector` in it is an
allocation per frame (hard rule 8).

**Reading the game.** `ReadState("symbol")` returns the address of a DLL export, the one way
a script sees memory (hard rule 9); cast it to the copied decomp type. The pointer stays
valid across frame advances and loads, so resolve it once at the top of `execution()` and
read through it as the game moves. Two names look alike and are not: `gMarioStates` is the
array (`MarioState*`), `gMarioState` a pointer variable (`MarioState**`, so `*(MarioState**)`).
`gCamera` and `gMarioObject` are pointer variables too; `gObjectPool` is the array of 240
`Object`s. An unknown name throws; a symbol a build may not export is probed with a
`try`/`catch` (`MarioTrace.cpp`), and `python scripts\dll_symbols.py <dll> -` lists what a DLL
exports. Objects a script addresses by pool slot (the pyramid at `gObjectPool[84]`, the
track platform at 85) are declared with their behavior and home in `BitFsObjects.hpp` and
verified by `VerifyLayout` before the pipeline's first stage; a new hardcoded slot goes
into that list (AGENTS.md, "Known problems"). Writing through a `ReadState` pointer is a
hack, forbidden (hard rule 1; `TryHackedWalkOutOfBounds` is the anti-pattern that remains
in the tree, and it is dead code).

**Writing inputs.** `Inputs(buttons, stick_x, stick_y)`; buttons are the `Buttons` bits
(`A`, `B`, `Z`, `START`, `C_UP`, ...), combined with `|`. The stick is quantised and
camera-relative, so aim it through the helpers: `Inputs::GetClosestInputByYawHau(yaw, mag,
cameraYaw[, bias])` for the nearest input whose intended yaw matches to the HAU (an angle
unit of 16), `GetClosestInputByYawExact` for an exact yaw, and
`GetIntendedYawMagFromInput(stickX, stickY, cameraYaw)` to read back what the game will see.
When an input must satisfy a constraint on what the game reads, round-trip it: draw, convert
to a stick, convert back, and keep only if the read-back value satisfies the constraint
(`Scattershot_BitfsDrRecover::CUpTrick`). `nextafter(0.0f, 1.0f)` is the smallest non-zero
magnitude (`BrakeToIdle`).

**The cursor.** The state at frame *f* is what inputs 0 to *f - 1* produced, and the cursor
is the frame whose inputs come next. `AdvanceFrameWrite(inputs)` writes them into the
script's diff and advances; `AdvanceFrameRead()` advances with the inputs already there
(the parent's, an ancestor's, the movie's, else neutral); `Apply(diff)` loads the diff's
first frame and plays it at the diff's own frame numbers (re-base it first if it came from
another frame, as `SpliceMovie` does); `GetInputs(frame)` reads the resolved inputs;
`GetCurrentFrame()` is the cursor; `ExportM64(path)` writes the source movie up to the cursor
with the diff applied, for the game the movie's header names.

**Saves and loads are automatic.** The framework creates savestates during replays when its
cost model says a save beats the frames it skips, and reverts children with them; a script
never sees a slot. The calls a script does make: `Load(frame)` to jump between candidates
(`BitFsPyramidOscillation_Iteration` loads each start frame it tries), `Rollback(GetCurrentFrame()
- 1)` to un-write the last frame (the double-turnaround workaround in
`BitFsPyramidOscillation_TurnAroundAndRunDownhill`), and `LongLoad(frame)` for a root's first
seek to its start frame (a long replay, uncached, with a save at the end). `Save`,
`OptionalSave`, `RollForward` and `Restore` are escape hatches for cases the automatic
management does not cover; none of the committed scripts needs them, and a script that
does is worth a second look. A load to a frame before the script's initial frame throws.

**Child scripts.** `Modify<X>(args...)` runs `X` and keeps its diff if it asserted, and the
cursor ends after the diff's last frame (by design: the common case is to keep going;
ROADMAP 3.1). `Execute<X>` runs it and reverts, returning the diff in the status for the
parent to `Apply` later or not (`BitFsPyramidOscillation` tries a whole oscillation this
way, then the same one with braking, and applies the faster). `Test<X>` is `Execute` with
the diff dropped from the status, the call for a query script: `GetMinimumDownhillWalkingAngle`
advances no frames at all and is only ever `Test`ed. A child that fails validation or
assertion costs its frame advances and changes nothing.

**Ad-hoc scripts.** `ModifyAdhoc([&] { ...; return ok; })` is the unit of "try these frames
and keep them if it worked": the lambda's writes persist if it returns true and are
reverted if it returns false, and `.executed` on the result says which. `ExecuteAdhoc`
reverts either way and returns the diff; `TestAdhoc` reverts and drops it. Each has a
second form, `ExecuteAdhoc<TStatus>([&](TStatus* status) { ...; return ok; })`, that carries
a status object out. Every move helper in the scattershot searches is a `ModifyAdhoc`, and
the one `ExecuteAdhoc` in the tree is a metric script looking fifty frames ahead
(`BitfsDrMetrics::CalculateOscillations`): the frames must not survive, so it is the right
call. Ad-hoc bodies capture what they need; when the logic needs a validity check of its
own, a result type or a second caller, it has outgrown a lambda and becomes a class.

**The compare family** runs a candidate over a parameter sequence and keeps the best
(`Script.compare.hpp`; the roles are concepts in `ScriptCompareHelper.hpp`, so a callable of
the wrong shape leaves no viable overload at the call site rather than an error inside).
The ad-hoc form the scripts use:

```cpp
auto result = ModifyCompareAdhoc<AttemptDrStatus, std::tuple<int8_t, int8_t>>(
	[&](auto iteration, auto& params)               // generator: fill params, false when done
	{
		if (iteration >= 33) return false;
		int16_t yaw = int16_t(marioState->faceAngle[1] + 512 * iteration * rotation);
		params = std::tuple(Inputs::GetClosestInputByYawHau(yaw, 32, camera->yaw));
		return true;
	},
	[&](auto status, int8_t stick_x, int8_t stick_y) // candidate: the tuple arrives unpacked
	{
		AdvanceFrameWrite(Inputs(0, stick_x, stick_y));
		status->relHeight = marioState->pos[1] - marioState->floorHeight;
		return true;
	},
	[&](auto incumbent, auto challenger)             // comparator: return the one to keep
	{
		return challenger->relHeight < incumbent->relHeight ? challenger : incumbent;
	},
	[&](auto candidate) { return candidate->landed; }); // terminator (optional): stop here
```

The generator fills a `std::tuple` per iteration and the candidate receives its elements as
arguments, so a generator that computes the stick pair hands the candidate `stick_x,
stick_y` (`BitFsScApproach_AttemptDr.cpp`). The status type is a small class of plain fields
(`AttemptDrStatus` there), `StatusField<X>` when the result is a child script's whole status
(`ModifyCompareAdhoc<StatusField<X>, ...>` with `status->status = Modify<X>(...)` in the
candidate), or `std::tuple<>` when only `.executed` matters. The container forms take a
container of tuples instead of a generator; the `Compare<X>` forms construct a script `X`
from each tuple instead of calling a lambda. Semantics: `Compare...` has `Execute`'s (nothing
persists, the winner's diff comes back in the status), `ModifyCompare...` applies the winner
(and avoids re-running it when it was the last candidate). The terminator is asked about
each candidate before the comparator, so a terminating candidate wins without comparison.
`Dynamic...` variants take a mutator, an ad-hoc lambda run through `ModifyAdhoc` between
candidates, for sequences like "turn around after *k* more frames of running"
(`BitFsPyramidOscillation_TurnThenRunDownhill_AtAngle`: the mutator writes one more frame);
their result is an `AdhocSubstatus`: the winner's status under `.substatus`, the mutations
that led to it as `nMutations`, and only those mutations persist. A comparator may set a captured
flag the generator reads to stop early; the scripts do, and it is clearer than a terminator
when the stop condition is about the pair.

**Metric scripts.** A metric script is a `Script` whose `CustomScriptStatus` describes the
game at one frame; the root records it after every frame advance and load, and
`GetMetrics(frame)` answers for any frame, past or future, by running it there in a reverted
sandbox and caching (ARCHITECTURE.md, "Top-level scripts, builders and metric scripts").
Writing one (`BitfsDrMetrics` is the model):

- Its status is the state at a frame: an `initialized` flag first, then the raw values it
  reads (position, action, speed, the pyramid normal) and what it derives (phase, crossing
  count, distance to a target). Its `execution()` reads the game, then reads its own
  previous frame with `GetMetrics(currentFrame - 1)` and carries running quantities forward
  (a phase state machine, an oscillation count): the recursion is linear because of the
  cache. It may advance frames to look ahead, inside `ExecuteAdhoc`; that multiplies the
  cost of every frame it is evaluated on, so look ahead only when the answer is used.
  `validation()` gates on the frames it applies to; `assertion()` says the state is valid,
  and a frame whose metric script does not assert stores a default-constructed state that
  is never recomputed, which is what the `initialized` flag detects.
- It is named once. A root or a scattershot stage names it as the second template argument
  (`TopLevelScript<LibSm64, BitfsDrMetrics>`, `ScattershotThread<..., LibSm64, BitfsDrMetrics,
  ...>`) and inherits `MetricScript` from it; a child script that reads metrics declares
  `using MetricScript = BitfsDrMetrics;` once; a metric script names nothing and reads its
  own state. `GetMetrics(frame)` then names nothing at the call; `GetMetrics<T>(frame)` asks
  about a named one, and throws if the root's is another type.
- Its constructor arguments come from the builder, `.ConfigureMetricScript(args...)`, and it
  needs a default constructor too.
- `GetMetrics` returns a reference into the root's table. A write at or before that frame
  (`AdvanceFrameWrite`, `Apply`, `Rollback`) invalidates it, so copy it (`auto state =
  GetMetrics(frame);`) when it has to outlive one, and bind a reference only to read at once.

Two uses beyond derived quantities (the maintainer, 2026-09-16): a raw variable at a past
frame, read from the metric instead of by rewinding (every committed metric status carries
Mario's position, action and speed for that reason), and a look into the future, worth it
when the decision is worth the sandboxed run to that frame. Everything a search's state
bin, validation, fitness and solution test need about the game is read from the metric
script; that is what lets a bin describe progress no single frame shows and a fitness use
a look-ahead computed once.

**Simulating the part of the game that matters.** It is often worth simulating the subset
of the game's state machine that the task depends on: a new resource with that behavior,
and scripts run on it, to save on performance and to run scripts that jump around in time
more than the game allows cheaply (the maintainer, 2026-09-16). A resource is a class
deriving `Resource<TState>` over its own state type with `save`, `load`, `advance`,
`setInputs`, `addr`, `getStateSize` and `getCurrentFrame` (`MockResource` in tasfw-testing
is the smallest one; a skeleton generator is a Phase 5 item, ROADMAP.md); a save or a load
of a small state is cheap where the game's costs microseconds (a frame advance tens of
them, a rewind generally more; docs/performance.md, "What costs what"). `PyramidUpdate` is
the example: it reimplements the pyramid's tilt on its own state type, and a script exports
its state into it and runs a `TopLevelScript<PyramidUpdate>` on that:

```cpp
auto m64 = M64(); // must outlive the call; the builder holds a reference
auto angles = TopLevelScriptBuilder<BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle>::Build(m64)
	.ImportSave(ExportSave<PyramidUpdateMem>(pyramid))
	.Run(roughTargetAngle, marioState->faceAngle[1]);
if (angles.validated) ... angles.angleFacing ...
```

`ExportSave<PyramidUpdateMem>(pyramid)` reads every pyramid surface out of the DLL, so it is
not free either (one per frame in `BitFsPyramidOscillation_RunDownhill`), and the oracle
script asserts unconditionally: check `validated`. `DetectEdge` (Scattershot_BitfsDrRecover.hpp)
is a second such script, a geometric query on the same resource. The in-process alternative
is the decomp reimplementation in `tasfw-core/src/decomp` (`simulate_platform_tilt`, called by
`GetMinimumDownhillWalkingAngle`), which predicts the next frame's floor angle on the copied
structs. Both reproduce the game's arithmetic and must keep doing so: `PyramidUpdate` is
checked bit-for-bit against the DLL by the drift test (ROADMAP 3.3), `src/decomp` is not
(AGENTS.md, "Known problems"). A new reimplementation needs the same kind of check before a
search trusts it, and docs/decomp.md records what each came from.

**Where the mechanics come from.** The decompilation is the reference for what the game
does: actions and their transitions, speed thresholds (a turnaround needs 16 speed), the
0.01 tilt increment, the 16-unit HAU quantisation of yaw, how the stick maps to intended yaw
and magnitude. The copied headers under `tasfw-core/inc/sm64/` are the vocabulary (the
struct fields, `ACT_*`, the trig tables); docs/decomp.md says which upstream revision each
came from. To see what the game is doing at a frame, use the scripts that look at it:
`dllcheck <dll> <m64> <frame> --trace [frames]` prints Mario, the camera and what a movie
carries between levels; `--objects` lists every active object with behavior, position and
home; `--levels` lists the frames a movie changes level, which is how a start frame is
found (docs/libsm64.md, "Checking a DLL"). The committed movies and their known states are
in AGENTS.md ("Repo map", `movies/`): the JP movie's frame 3330 (idle on the pyramid) is
where the tilt stages, the libsm64 tests and the DLL benchmarks start.

## From a goal to a script and a run

1. **State the goal as a predicate and a cost.** What must be true of the game state at
   the end (an action, a position box, a normal within a window, a phase reached), read
   through `ReadState` or a metric; and what "better" means (fewer frames, more speed, a
   smaller error). The predicate becomes an `assertion()` or `IsSolution()`, the cost a
   comparator or `GetStateFitness()`.
2. **Decompose.** The primitive moves (one frame of a stick, a turnaround, a dive, a brake)
   are ad-hoc lambdas or small script classes; the composite that sequences and searches
   over them is a script class with the parameters it sweeps as constructor arguments and
   a compare call inside; anything derived about the game that more than one decision
   reads is a metric script. `BitFsPyramidOscillation` is the model of a deep composite: five levels,
   each a search over one parameter (start frame, turn angle coarse then fine, extra run
   frames), the innermost running downhill one frame at a time, the parameters threaded
   through one DTO. When the space is too large to sweep, or the goal is a route through
   it rather than one parameter, it is a scattershot search.
3. **Pick the start.** A frame of a committed movie (`dllcheck --levels` and `--trace` to
   find and inspect it), or a previous stage's solutions (`"input"` in `config.json`).
4. **Write it where it belongs.** Reusable scripts go in `tasfw-scripts/inc` with their
   definitions in `tasfw-scripts/src/<group>/` and the file added to the group's static
   library in `tasfw-scripts/CMakeLists.txt` (`tasfw-scripts-general` for primitives; a
   scattershot search links `tasfw-scattershot` too). A script only one stage uses can live
   in the stage's header under `tasfw-bruteforcers/bitfs-turnaround/src/`
   (`TiltTargetShot.hpp`, `BitfsOscFinal.hpp`, `ExportSolutions.hpp`). Match the file's
   formatting (AGENTS.md, "Conventions").
5. **Run it** (below), then **check it** (next section).

### Running a script

A script runs from a root: a `TopLevelScript<TResource[, TMetricScript]>` started through
`TopLevelScriptBuilder<Root>::Build(m64)` with one of `.Run(args...)` (a default-constructed
resource), `.ConfigureResource(LibSm64Config{...}).Run(...)` (the resource built from a
config: DLL path, country code, save mode), `.ImportResource(&resource).Run(...)` (a resource
the caller owns, reset to its start save on entry, how every tool, test and stage does it),
or `.ImportSave(ExportSave<...>(...)).Run(...)` (from another script's state); with
`.ConfigureMetricScript(args...)` before `Run` when the root names a metric script. The
root's `execution()` seeks with `LongLoad(startFrame)` and runs the script under test as a
child, and its results come back in the returned status, or through a reference the root's
constructor took (the tests do that for large results).

There is no general-purpose script runner. The ways to run a new script on the game:

- **As a stage of `bitfs-turn`**, for anything that is or feeds the pipeline. A stage type
  is one function `SolutionSet Run(StageContext&)` in `Stages.cpp` plus a row in
  `g_stageTypes` (name, description for `--list`, function); it reads its arguments from
  the stage's `args` with `ArgNumber`, `ArgBool`, `ArgString` after `RejectUnknownKeys`,
  builds its run on `context.resources` (one `LibSm64` per thread, already constructed),
  and returns solutions as diffs plus named metrics (`ToSet`, with a `Metrics(...)`
  overload per solution type so a search's numbers reach the JSON and the next stage's
  `"input:<metric>"` and `"select"`). A plain script stage (`pyramid-osc-approach`) runs a
  root on `context.resources[0]` and returns its diff as one solution. Then a stage entry
  in `config.json` (`name`, `type`, `startFrame`, `input`, `args`, `scattershot` overrides,
  `select`, `export`; README.md, "Configuration"), `scripts\build.ps1 -Config Release`, and
  `bitfs-turn --stage <name>`, which reads its input from the file the input stage wrote
  last time. `--list` shows the types, `--dry-run` checks the paths, the DLL and the layout
  without searching. A run without arguments runs every stage, for hours.
- **As a test**, for a script's logic and invariants: on `MockResource` when the game is not
  the point (`script_fixtures.hpp`: `RunRoot(resource, m64, [&](auto& s) { ... })` gives a
  root whose body is the lambda, `resource.checksum()` says whether two states are the same
  inputs applied in the same order, `resource.work` holds exact counts), and on the DLL
  under the `libsm64: ` prefix with `* doctest::skip(!HaveDll())` and `DllConfig(mode)`,
  `TestFrame()`, `Env("TASFW_M64")` from `libsm64_env.hpp` when it is
  (`test_libsm64_pyramid.cpp` is the end-to-end model: build the resource, load the movie,
  `ImportResource`, run, assert on the results). `scripts\test.ps1 -Config Release -Filter
  'libsm64*'` runs the group.
- **As a benchmark**, when the script is a hot path or a fixed workload worth gating
  (docs/performance.md, "Tier C": `bench_framework.cpp` is the model, with the cost model
  off so counts are exact).
- **As a tool**, when it looks at the game rather than plays it (`dllcheck`, `m64splice`).
- **As its own executable**, to test an idea: a new folder beside
  `tasfw-bruteforcers/bitfs-turnaround` with its own `main.cpp` and CMake target, added to
  `tasfw-bruteforcers/CMakeLists.txt` with one `add_subdirectory` line and linking the script
  libraries it needs (the maintainer, 2026-09-16). `bitfs-turnaround/CMakeLists.txt` is the
  model; its paths come from a config file, never from source (hard rule 5).

A script never carries a `main` of its own; the executable that runs it is a separate
target, so the script stays reusable.

### A scattershot search

Scattershot is not only for spaces too large to sweep: it is a proven algorithm, good at
finding near-optimal routes in SM64 in general (the maintainer, 2026-09-16; ARCHITECTURE.md,
"Scattershot"), so it is a fair first choice for a route, not a last resort. A search is
three types and a stage (ARCHITECTURE.md for the vocabulary):

1. **The metric script**, as above, with the fields the bin, validation, fitness and solution
   test read.
2. **The thread**, `class X : public ScattershotThread<BinaryStateBin<16>, LibSm64, XMetrics,
   XSolution>`, whose constructor takes the `Scattershot&` first and its own parameters
   after (they arrive from `.Run<X>(args...)`), and which implements:
   - `SelectMovementOptions()`: one weighted draw per decision from a braced list,
     `AddRandomMovementOption({{BasicMoves::MAX_MAGNITUDE, 4}, {BasicMoves::RANDOM_MAGNITUDE, 1}})`
     for the framework's three input groups (magnitude, yaw, buttons, which `RandomInputs`
     reads) and the same call over the search's own `public enum class CustomMoves`, a
     magic name like `CustomScriptStatus`; weights may depend on state and on
     `GetMetrics(GetCurrentFrame()).phase`, so a phase offers the moves that make sense in it.
   - `ApplyMovement()`: `if (CheckMovementOptions(CustomMoves::PBD)) return Pbd();` down the
     options, each move a `ModifyAdhoc` returning whether it applied, and the fall-through
     one frame of `RandomInputs({{Buttons::B, 1}, {Buttons::A, 0}})`. Its only randomness is
     `GetTempRng()` (hard rule 3: a block is reproduced by replaying this from its seed).
   - `GetStateBin()`: pack what distinguishes states worth exploring separately into the
     bits, `AddValueBits` for small integers (an action mapped to a few bits, a phase, a
     crossing count), `AddRegionBitsByRegionSize` and `AddRegionBitsByNRegions` for
     quantised floats (position, yaw, the normal), the `bitCursor` threaded through every
     call; an out-of-range value throws, which the search treats as an invalid state.
     Resolution is the search's central tuning: too fine and the block table explodes
     (`MaxBlocks` is a hard cap), too coarse and it cannot tell progress apart. The
     committed searches switch layout by phase, finer where it matters.
   - `ValidateState()`: reject what the search must never keep (off the platform, a wrong
     action, a metric gone backwards), and `GetStateFitness()`: what "better" is in this
     phase (speed, frames since a crossing, `-frame` for "sooner"). `IsSolution()` and
     `GetSolutionState()` say what a solution is and what it carries out (a plain struct of
     the numbers the next stage sorts by). `GetCsvLabels`, `GetCsvRow`, `ForceAddToCsv` feed
     the CSV the viewer plots live (`csvSamplePeriod` in the config enables it, a stage's
     `visualize` block opens the viewer on it; README.md, "The viewer").
3. **The solution type**, that plain struct, and its `Metrics(...)` overload in `Stages.cpp`.
4. **The stage function**, in this order:

```cpp
Configuration config = context.pipeline.ScattershotConfiguration(context.stage);
auto input = PipeIn<XSolution>(context.input);
auto solutions = X::ConfigureScattershot(config)
	.ImportResourcePerThread([&](auto threadId) { return &context.resources[threadId]; })
	.PipeFrom(input)
	.ConfigureMetricScript(metricArgs...)
	.Run<X>(threadArgs...);
return ToSet(context, solutions);
```

`ImportResourcePerThread` before `ConfigureMetricScript`: the bare builder's own
`ConfigureMetricScript` and `PipeFrom` have never been instantiated and do not compile
(ROADMAP 3.21); every committed stage uses this order. Solutions cross a stage boundary as
diffs only: the next stage's solution data starts default, and its scalar inputs come
through `"input:<metric>"` from the first solution, which `"select"` makes the best. The
search prints its status every `shotsPerUpdate` shots and, at the end, the line the perf
gates parse (`Found N solutions in N shots, N blocks, N scripts (N base-block validation
failures)`), then the stage summary (below). `dr-oscillations` shows a stage that runs the
search in a loop, one pass per oscillation, piping each pass into the next and rewriting
`config.MaxShots` between them.

## Checking a result

**It asserted, and its status says why.** The first check on any script is `asserted` and
the `CustomStatus` fields that explain the outcome; a script whose result cannot be read
from its status is not finished.

**Counts.** Every status carries `nFrameAdvances`, `nSaves` and `nLoads`, the stage summary
prints them summed over threads with the wall time (README.md, "Running the pipeline"),
and the Tier C and D rows gate them exactly (docs/performance.md). They are the measure of
a script's efficiency: how many frame advances per frame of output diff (Tier C's replay
ratio), and whether a change made them go up. Timings drift with the machine; counts do
not, once the cost model is off (`"costModel": false` in `config.json` for a diagnosis run,
`useCostModel = false` in a test or benchmark), so prefer counts when writing a test.

**Reproduction.** Game state is a pure function of the start save and the inputs (hard rule
1), and everything else rests on it:

- A script's result must be the same on a second run from the same state; the mock tests
  pin this with the checksum idiom (`Load(0); Load(f)` reaches the same checksum as the
  first pass through `f`; `test_script_saves.cpp`), the DLL tests by playing the movie
  twice and comparing Mario's state (`test_libsm64_smoke.cpp`).
- A deterministic search (`"deterministic": true`, a `seed`, a thread count) takes the same
  path and reaches the same counts on every run and every platform; `bitfs-turn`'s
  end-of-run line and the stage summary are the numbers to compare (the CI-sized Tier D
  workload, `perf\tierd-ci.json`, is the reference: identical counts on both game builds
  and four compilers).
- **Base-block validation failures must be zero.** A non-zero count means a block's
  recording did not reproduce its state: a non-deterministic `ApplyMovement`, a state bin
  that is not a function of state, a direct memory write, or a framework bug. It is never
  a rate to tolerate (hard rule 1); the failure handler dumps `error.m64` and says whether
  decoding itself is deterministic (ARCHITECTURE.md, "Sharp edges").
- A solution is only ever a diff, and every export replays it through the game from the
  stage's start frame (`ExportSolutions`), so an exported movie is by construction what
  the game does with those inputs.

**Exports.** Each stage writes `<outputDirectory>/solutions/<stage>.json` (frames as
`[frame, buttons, stickX, stickY]` plus the named metrics) and, with `"export": true`, one
`.m64` per solution under `<outputDirectory>/m64/<stage>/`, named by index, pyramid normal
and Mario's speed. Inspect one with `dllcheck <dll> <m64> <frame> --trace`, or run
`MarioTrace` on it; `m64splice` makes the same movie for the other game version. A script's
own `ExportM64(path)` writes the movie up to its cursor.

**Tests.** A script's logic that does not need the game (how it sweeps, what it keeps, what
its status says) is tested on `MockResource`; what it does to Mario is tested on the DLL
under `libsm64:`, with the count assertions the mock allows (`resource.work`). Both
compilers (`scripts\test.ps1` and `-Compiler clang`). Anything touching `tasfw-core`
needs a test in `tasfw-tests` (AGENTS.md, "Verifying a change").

**Performance.** A script on a hot path (anything a search runs per shot, a metric script,
a primitive every composite calls) is measured: the suite's delta table for framework
changes, and for a script the stage's counts and wall time on a stated fixed workload
before and after. What the numbers must do depends on the context (the rules above): a
change that costs run time to save movie frames can be right in a TASing context, and one
that costs movie frames to run faster can be right in the brute forcer; report both sides
and say which the change serves. Rules of thumb (docs/performance.md): no allocation per
frame (a `std::vector` in a metric status is one), no I/O under a critical section, nothing
that adds a frame advance without a reason you can state, bound every loop (the framework
will not stop a runaway one: `for (int n = 0; n < 1000; n++)` is the committed habit), let
the cost model choose between a load and a replay (a rewind generally costs more than a
frame advance, and the choice is the framework's), and prefer a `Test`, a metric or a
simulation on a smaller resource to playing the game.

## The hard rules that bite while TASing

The full list is in AGENTS.md; these are the ones a script author meets.

- **Rule 1, purity.** Only inputs change the game. No store through a `ReadState` pointer,
  no file edited by hand (a splice is a script, `SpliceMovie`), and any desync is a root
  cause to find, never a rate.
- **Rule 3, determinism.** In a search, randomness is `GetTempRng()` and nothing else: no
  `rand`, time, thread id, or state that survives across pellets in a member. A state bin
  is a function of the game state and the metrics, nothing else.
- **Rule 4, the lifecycle.** Validation and assertion are sandboxed and may not rely on side
  effects; execution's diff is the only one that persists; results leave through
  `CustomStatus`.
- **Rule 5, no absolute paths.** A script that writes a file takes the path from its
  caller, which takes it from `config.json` (`outputDirectory`) or `Configuration`.
- **Rule 8, performance.** For the framework (`tasfw-core`, `tasfw-scattershot`,
  `tasfw-resources`) counts must not go up and time must not regress. For a script the
  rule is to measure and to choose with the context in mind (the rules above), since movie
  frames and run cost trade off; per-frame allocation and I/O under a critical section are
  bugs anywhere.
- **Rule 9, scripts do not touch the resource.** `ReadState` to read, `AdvanceFrameWrite`
  and `Apply` to change, `ExportSave` to hand a state to another resource; a script never
  holds the resource and never asks it for a slot, a mode or a baseline.
- **Rule 10, framework changes are designed first.** When the framework seems to lack
  something, that is the moment to bring it up, with what the concept is, who calls it and
  what it costs, not the moment to add a method to `Script`.

## Pitfalls

- `gMarioState` is `MarioState**`, `gMarioStates` is `MarioState*`. Mixing them reads garbage
  silently; `VerifyLayout`'s first check exists for this.
- A `GetMetrics` reference dies at the next write at or before its frame; copy it.
- `ModifyAdhoc` reverts when the lambda returns false, and reports `executed == false`.
- Check `asserted`, not `executed`; a metric script that did not assert stores a default
  state, and `BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle` asserts unconditionally
  (check `validated`).
- A comparator returns the pointer to keep (`const AdhocScriptStatus<T>*`), not `bool`; a
  terminator returns `bool`; a generator is `bool(int64_t, std::tuple<...>&)`. A wrong shape
  is "no matching overload" at the call, not an error inside.
- `Dynamic...Compare...` results are under `.substatus`.
- The `M64` a builder is given must outlive the run; a local `auto m64 = M64();` before the
  builder call is the committed idiom for the oracle.
- `BinaryStateBin` throws on an out-of-range value and the pellet is discarded as invalid;
  `MaxBlocks` throws when reached and the run stops.
- A `PyramidUpdate` built inside a script takes its share of the process's savestate budget
  (`resources.savestateBudgetMB`); the pipeline leaves room for one per thread.
- Piped-in solutions are indexed in 16 bits: at most 65534 per stage.
- `std::sqrt(float)`, not `std::sqrtf`: libstdc++ does not declare the `f` names in `std`
  (docs/compilers.md).
- A metric script's constructor runs deep inside a search; one that throws on bad
  arguments (`BitfsDrApproachMetrics` on a quadrant pair) fails there, not at the config.
- The search runs `ValidateState`, `GetStateBin`, `GetStateFitness` and `IsSolution` in a
  reverted sandbox, so they may advance frames to look ahead (`Scattershot_BitfsDrApproach::IsSolution`
  does) and the cursor is unchanged after; but every frame they advance is paid on every
  candidate, and a metric with the look-ahead is cheaper when more than one decision needs it.

Known defects in the committed scripts, not to copy (ROADMAP 4.9): `Scattershot_BitfsDrRecover::IsSolution`
reads its "previous" state from the current frame; `Scattershot_BitfsDrApproach` offers
`CustomMoves::C_UP_TRICK` and never applies it; `CustomMoves::REWIND` is checked by every
search and offered by none; the fine angle sweep in
`BitFsPyramidOscillation_TurnThenRunDownhill` reseeds its uphill half from the coarse midpoint.
The `TurnAround` lambda's missing return in `Scattershot_BitfsDr.cpp` (C4715) is in
AGENTS.md, "Known problems".
