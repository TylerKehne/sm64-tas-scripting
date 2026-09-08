# Architecture

How the pieces fit, what the invariants are, and where the sharp edges live. Written from
the code as of 2026-09-07; where behavior is inferred rather than documented it says so.

## Layers

```
bitfs-turnaround/                config-selected pipeline stages (Stages.cpp) + m64 export
        |
tasfw-scripts                    BitFS scripts, state trackers, scattershot stages
        |
tasfw-scattershot (header-only)  Scattershot, ScattershotThread, builders
        |
tasfw-core                       Script / TopLevelScript / Resource / SlotManager / Inputs / M64
        |
tasfw-resources                  LibSm64 (game DLL)   PyramidUpdate (pure C++ stand-in)
        |
res/sm64_jp_N.dll                wafel libsm64: SM64 decomp compiled as a native x64 DLL
```

`tasfw-core` depends on nothing but the standard library and OpenMP. `tasfw-resources` sits
below `tasfw-scripts` in CMake even though it is drawn below core here; the header
`tasfw-core/inc/sm64/` is the shared vocabulary.

## Resource and savestates

A resource is anything that fulfils the basic TASing contract: advance one frame with given
inputs, save the state, load it back, read memory. It need not be the game. With the full
game, rewinding (a load) is slow compared with advancing, and that ratio dictates which
TASing algorithms are viable; a custom state machine that simulates only the part of the
game a search cares about (`PyramidUpdate`) makes both advancing and rewinding much faster
and so changes what is affordable. The savestate policy (`shouldSave`/`shouldLoad`, the slot
manager) is currently one naive policy for every resource; making it pluggable per resource
type is planned (ROADMAP 3.11).

`Resource<TState>` (`tasfw-core/inc/tasfw/Resource.hpp`) is the abstract game: `save`, `load`,
`advance`, `addr(symbol)`, `getCurrentFrame`. It also owns a `SlotManager` and timing counters.

- `SlotManager` stores savestates by integer slot id, evicts least-recently-touched slots when
  `_saveMemLimit` (8 GB, set in `LibSm64`'s constructor) would be exceeded, and throws if a
  single save cannot fit. Erased and evicted states go to a bounded pool (32) that the next
  save reuses, so a save into a recycled state is one copy; pooled memory counts toward the
  limit.
- `shouldSave(n)` / `shouldLoad(n)` compare the measured average cost of a save or load
  (rdtsc cycles) against `n` frame advances. Scripts call these to decide whether a
  savestate is worth creating. Every "cost-based" decision in the framework routes here.
  `useCostModel = false` makes both return false, so a run creates no automatic savestates
  and is independent of timing; the pipeline exposes it as `resources.costModel`. It exists
  for diagnosis (it is how ROADMAP 4.5 was bisected) and costs replay time.

`LibSm64` (`tasfw-resources/src/LibSm64.cpp`) loads the DLL with `LoadLibrary`, calls
`sm64_init`, and treats the `.data` and `.bss` sections as the whole game state:

- Full save copies both sections (about 2.4 MB + 4.9 MB).
- **Lightweight mode** copies five hardcoded 100 KB-granular slices (about 1.5 MB). The
  offsets were found empirically for the pinned DLL build and are not derived from symbols.
  The pipeline config's `resources.lightweight` (default true) selects it.
- `advance` calls `sm64_update`. Inputs are written straight into `gControllerPads` by
  `Script::SetInputs` before each advance.
- The Linux branch instead marks the sections read-only and records dirty pages in a
  `SIGSEGV` handler. It has not been built recently.

`PyramidUpdate` is a second `Resource` whose state is a small C++ struct: the pyramid
object, Mario's position, static lava floors, and the pyramid's collision triangles pulled
out of the DLL. `advance` re-implements `bhv_tilting_inverted_pyramid_loop` and floor
finding. It exists so `BitFsPyramidOscillation_GetMinimumDownhillWalkingAngle` can probe
dozens of candidate angles per frame without paying for a full game frame. It is a copy of
decomp logic; the drift test in `tasfw-tests` (`libsm64: PyramidUpdate reproduces ...`)
verifies it matches the DLL's normal bit-for-bit over 240 frames.

Frame ordering that the port depends on: within a game frame the terrain objects (the
pyramid) update **before** the player object, so the pyramid loop reads Mario's object
position and platform pointer as his previous update left them, steps the normal 0.01
toward its goal, and displaces `gMarioState->pos` before Mario's own update runs. Importing
`PyramidUpdateMem` from the DLL at the current frame and advancing it therefore predicts
the next frame's normal exactly; feeding it Mario's post-frame position does not.

## Scripts

`Script<TResource>` (`Script.hpp` / `Script.t.hpp`) is the unit of work. A script is a class
with `validation()`, `execution()`, `assertion()` and a nested `CustomScriptStatus`.

Lifecycle (`Script::Run`):

1. `validation()` runs inside `ExecuteAdhoc`, so any frames it advances are reverted.
2. `execution()` runs inside `ModifyAdhoc`; its input diff is kept if it returns true.
3. `assertion()` runs inside `ExecuteAdhoc` and is reverted.

The parent gets a `ScriptStatus<T>`: the child's `CustomStatus` plus `BaseScriptStatus`
(validated/executed/asserted flags, the `M64Diff`, timing and save/load counts).

Running children:

| Call | Effect on parent state |
|---|---|
| `Execute<T>(args...)` | Child runs, then parent reverts to the frame it was on. Diff returned in status. |
| `Modify<T>(args...)` | If asserted, child's diff is merged into the parent's diff and the cursor moves to the frame after the diff's last frame. Otherwise reverted. |
| `Test<T>(args...)` | `Execute` with the diff removed from the returned status. |
| `ExecuteAdhoc` / `ModifyAdhoc` / `TestAdhoc` | Same three semantics for a lambda returning bool, run on the *same* script object at `_adhocLevel + 1`. |
| `Compare<T>` family | Run `T` for each parameter tuple, keep the best by a comparator, optionally stop early. Lives in `ScriptCompareHelper.hpp`. |

Both forms manage savestates, reverts, the input diff and tracked-state coherence
automatically; the author never touches a slot. The difference is weight and reuse: a
one-off attempt ("try these inputs, keep them if it worked") is better as an ad-hoc lambda,
which captures whatever locals it needs instead of routing results through a status type;
heavier or reusable logic warrants a named script class with its own `CustomScriptStatus`
and lifecycle. The engine is built on the ad-hoc form: `Run` executes validation and
assertion inside `ExecuteAdhoc` and execution inside `ModifyAdhoc`, and every scattershot
pellet is one.

Per script and per ad-hoc level the framework keeps: the input diff (`BaseStatus[level].m64Diff`),
a `saveBank` of savestate handles keyed by frame, a `saveCache` and `inputsCache` that
memoize lookups into ancestors, a `frameCounter` that accumulates replay cost per frame,
and a `loadTracker`. Each is a `LevelStack<T>` (`tasfw/LevelStack.hpp`): levels are pushed
and popped in stack order, every level (including 0) is constructed on first use, and a
popped level is reset in place (`clear()`, or `BaseScriptStatus::Reset()`) and its storage
reused. Entering or leaving an ad-hoc level therefore neither hashes nor allocates, and a
script that never saves never constructs a save bank (MSVC's `std::map` allocates a head
node per construction, which is what made child scripts and trackers expensive).

Input resolution (`GetInputsMetadata`): to find the inputs for frame *f*, walk the current
script's ad-hoc levels from innermost outward, then the parent chain, then the source `M64`,
then default to neutral. Along the way the first level whose diff starts before *f* becomes
the frame's "state owner"; its frame counter is the one charged for replays through *f*.

Loading (`LoadBase`): find the latest usable save at or before the target across levels and
ancestors, never searching past the start of a level's own diff (that would desync). Load it
if the target is in the past. (The branch that would load a save *after* a future target when
`shouldLoad` says so cannot fire, because the lookup only returns saves at or before the
target; `shouldLoad` is effectively unused.) Then `AdvanceFrameRead` to the target, creating
savestates along the way when `shouldSave` says the accumulated frame counter justifies it.
Those automatic saves go into the frame's state owner's bank, and they are the only
timing-dependent decision in the engine: two runs of the same deterministic search differ
only in which savestates exist, never in which states are reached.

Savestate ownership follows input ownership: a save at frame *f* is valid for a level exactly
when none of that level's inputs before *f* have changed since, so writes at a level erase
that level's saves after the written frame, and a level's bank only ever holds saves after the
start of its own diff. `Revert` (after `Execute`) therefore keeps only the child's saves at or
before the first frame the child wrote (in practice none) and drops the rest with the bank,
then loads the original frame, forcing a load if the child changed any frame before the
cursor. Until 2026-09-08 it moved *every* child save into the parent when none was synced;
a later backwards load from a level whose diff started after such a save then restored a
state made with reverted inputs. That was ROADMAP 4.5, and `test_script.cpp` pins it.
`ApplyChildDiff` (after `Modify`) merges the diff and moves saves, then `Load(lastFrame + 1)`.
That last step is why callers in `ScattershotThread` re-`Load` the frame the child actually
stopped on (ROADMAP 3.1).

Other cursor operations: `Load(f)`, `LongLoad(f)` (no caching, always saves at the end),
`Rollback(f)` (erase diff from *f* on, then load), `RollForward(f)` (drop diff before *f*),
`Restore(f)`, `Save()`, `OptionalSave()`.

**Invariant:** every reachable game state must equal "load the start save, then apply the
resolved inputs frame by frame." Anything that writes DLL memory directly breaks replay and
therefore breaks savestate reuse and scattershot decoding.

## Top-level scripts, builders and state trackers

`TopLevelScript<TResource, TStateTracker>` is the root of a hierarchy. It owns the `M64`
pointer and the resource. It is started through `TopLevelScriptBuilder<T>::Build(m64)` with
one of:

- `.Run(args...)` on a default-constructed resource,
- `.ConfigureResource(cfg).Run(...)`,
- `.ImportResource(&res).Run(...)` (resource shared across runs; start save reset on entry),
- `.ImportSave<TState>(frame, stateArgs...).Run(...)` (resource initialised from a state
  object, used to seed `PyramidUpdate` from `LibSm64`).

A script may depend on the game's state in the past or the future of its cursor, not only
the present. The **state tracker** exists to make that state available automatically and
cheaply: a script asks for the state at any frame and never manages the saves, loads,
replays or caching behind the answer. A tracker is a `Script` whose `CustomScriptStatus`
describes the game at one frame (e.g. `StateTracker_BitfsDr`: phase, oscillation count,
crossing history, ARE). The top-level script caches
`trackedStates[script][adhocLevel][frame]` and fills it lazily: after every frame advance or
load, `TrackState` runs the tracker at that frame inside a reverted sandbox, and a request
for a frame ahead of the cursor loads or advances to it in that sandbox and reverts, so the
requesting script's cursor never moves. Because the tracker is a script, it may itself
advance frames to look further ahead (`CalculateOscillations` does).
Trackers may call `GetTrackedState<T>(frame - 1)` to compute recursive metrics; the cache
makes this linear. Entries after a modified frame are erased on `AdvanceFrameWrite`,
`Apply`, `Rollback`; on `Modify` they move from child to parent with the saves.
`GetTrackedState` returns a `const` reference into that table and a finished tracker's
`CustomStatus` is moved into it, not copied. A script's entry in the table is created on its
first tracked frame and dropped when the script's scope ends; the root verifies the
requested tracker type by comparing a per-type tag (`StateTrackerTag`), not with RTTI.

`ConfigureStateTracker(args...)` on any builder supplies constructor arguments for the
tracker; the framework instantiates it through `StateTrackerFactory`.

## Scattershot

Scattershot is a randomized search that explores by coverage of a quantized state space:
one block per distinct state bin, holding the best-fitness path that reached it, extended
by firing random "pellets" from existing blocks. It predates this framework and has proven
a good general-purpose algorithm for SM64, though configuring it (bin resolution, fitness,
movement mix) is the hard part. It was ported here to be pushed further: the original
works on individual per-frame inputs, whereas a pellet in TASFW can apply a whole scripted
move (a `MovementOption` may be a script, not just a random stick), so the search can be
more discerning about which movements it tries. The aim is to find good paths faster and to
keep the state space from exploding, because each move is a meaningful step rather than a
random frame. The state tracker is the other contribution: because a thread's state bin,
fitness and validation can read tracked state, a bin can describe progress that no single
frame shows (oscillations completed, crossing history, phase) and a fitness can use a
look-ahead the tracker computed, so the search ranks and distinguishes states by where they
are going, not only by where they are.

`Scattershot<TState, TResource, TStateTracker, TOutputState>` is the shared search state;
`ScattershotThread<...>` is a `TopLevelScript` that each OpenMP thread runs. A concrete
search subclasses `ScattershotThread` and implements:

- `SelectMovementOptions()`: choose weighted `MovementOption`s using `AddRandomMovementOption`.
- `ApplyMovement()`: turn those options into frames (random inputs or a scripted move).
- `GetStateBin()`: quantise the game state into a `TState` (a `BinaryStateBin<16>` in practice).
- `ValidateState()`, `GetStateFitness()`, `IsSolution()`, `GetSolutionState()`.
- Optional CSV hooks `GetCsvLabels()`, `GetCsvRow()`, `ForceAddToCsv()`.

Vocabulary:

- **Block**: one state bin plus the best fitness seen for it and the segment chain that reaches it.
- **Segment**: (parent, RNG seed, number of scripts, optional piped-diff index). A block is
  reproduced by walking its segment chain from the root and re-running `ChooseScriptAndApply`
  with `SetTempRng(seed)` for each script. Nothing but seeds is stored.
- **Shot**: pick a base block, decode it, verify the state bin matches (`ValidateBaseBlock`;
  a mismatch is counted in `ValidationFailures`, shown in the status line and the end-of-run
  summary, printed with both bins in hex, and dumped as `error.m64`), then fire pellets.
- **Pellet**: up to `PelletMaxScripts` scripts within `PelletMaxFrameDistance` frames; each
  script result is validated, binned, scored and offered to `UpsertBlock`.
- **UpsertBlock**: open-addressed hash table over `BlockIndices` (3x `MaxBlocks`). A new bin
  becomes a block; an existing bin is replaced when fitness improves (or ties, if
  `FitnessTieGoesToNewBlock`). Solutions are recorded with the thread's `GetTotalDiff()` and
  capped at `MaxSolutions`; solved blocks are never chosen as base blocks.
- **Piping**: `PipeFrom(solutions)` seeds the next stage's root blocks with each solution's
  diff. Their segments carry `pipedDiff1Index` so decoding applies the diff instead of scripts.
- **Determinism**: `Configuration::Deterministic` serialises all upserts by thread id with
  barriers (`QueueThreadById`), so a run is reproducible for a given `Seed` and thread count.
- **CSV**: every `CsvSamplePeriod`-th novel block per thread is written as a row; the R script
  in `analysis/` plots them. `CsvRows` is printed so plotting can run mid-search.

Threads: `MultiThread` opens an OpenMP parallel region of `TotalThreads`. Each thread must
have its own `LibSm64` (own DLL file) supplied by `ImportResourcePerThread`. Shared state is
guarded by named `omp critical` sections listed in `CriticalRegions`. The end-of-run
statistics print load/save/advance/overhead percentages; "overhead" is mostly block decoding.

## The BitFS pipeline (`config.json`)

`bitfs-turn` reads a pipeline config (README.md, "Configuration"), constructs one lightweight
`LibSm64` per thread, and runs the configured stages in order, or one of them. A stage is a
named instance of a stage type from `Stages.cpp`, with its own scattershot overrides and
typed arguments; its result is a `SolutionSet` (input diffs plus named metrics per
solution) that is written to `<outputDirectory>/solutions/<stage>.json`, handed to the next
stage in memory, or read back from that file when a stage runs alone. An argument written as
`"input:<metric>"` takes the value from the first input solution, which is how the equilibrium
frame found by the tilt search reaches the oscillation stages. Solutions cross a stage
boundary as diffs only; the new stage's solution data starts out default.

Stage types, in the order the committed config uses them:

1. **osc-final** (`BitfsOscFinal`) from frame 3604 of `test3.m64`: an experiment in progress.
2. **tilt-target** (`TiltTargetShot`), three stages: hit the target normal in X, then in Z with
   the X ARE fixed to what the first found, then constrain to a normal box. The last keeps the
   best by ARE and exports.
3. **dr-oscillations** (`Scattershot_BitfsDr`) once per target oscillation from the
   equilibrium frame, piping solutions forward, committing to the direction the first
   oscillation took, keeping the fastest few between oscillations, and requiring
   increment-frame parity at the end.
4. **osc-final** again from the equilibrium frame, piped from the oscillation solutions.
5. **dr-approach** (`Scattershot_BitfsDrApproach`): dive from the oscillation solutions;
   then **dr-recover** (`Scattershot_BitfsDrRecover`) twice, phase `attempt-dr` to land the
   dive and phase `c-up-trick` after it. This chain was disabled in the old `main.cpp` and
   has never run to completion; it links and runs, nothing more is known.

`pyramid-osc-approach` is the original single-threaded experiment (`BitFsPyramidOscillation`
then `BitFsScApproach` from the start frame) as a stage; it is not in the committed config.
`export` is a stage type that passes its input through, and `"export": true` on any stage
replays each solution on the first resource (`ExportSolutions`) and writes one movie per
solution under `<outputDirectory>/m64/<stage>/`, named by index, pyramid normal and Mario's
speed.

## Coupling to the game binary

Everything below assumes the pinned DLL in `res/` (see `docs/libsm64.md`):

- Struct layouts in `tasfw-core/inc/sm64/Types.hpp`, `ObjectFields.hpp`, `Camera.hpp`,
  `Surface.hpp`; constants in `Sm64.hpp`, `SurfaceTerrains.hpp`; trig tables in `Trig.hpp`.
- Symbols resolved by name through `GetProcAddress`: `gMarioState`, `gMarioStates`,
  `gMarioObject`, `gObjectPool`, `gCamera`, `gControllerPads`, `gGlobalTimer`,
  `gCurrCourseNum`, `gCurrAreaIndex`, `bhvLllTiltingInvertedPyramid`,
  `bhvBitfsTiltingInvertedPyramid`, `sm64_init`, `sm64_update`.
- The pyramid is `gObjectPool[84]` in the BitFS area of the source m64.
- `tasfw-core/src/decomp/` reimplements `mtxf_align_terrain_normal`, object surface loading,
  `find_floor`, `floor_is_slope` and `simulate_platform_tilt` on the copied structs.
  `GetMinimumDownhillWalkingAngle` uses them to predict Mario's floor angle after the next
  tilt without advancing a frame. The DLL stays the reference; this code is the same physics
  `PyramidUpdate` implements on its own surface type, and only `PyramidUpdate` is drift-tested.
- Lightweight save slices (`LibSm64LightweightSlices` in `LibSm64.hpp`).
- `LibSm64::layoutCheckReport()` verifies all of the above relationships at run time; the
  scattershot thread calls `Resource::verifyLayout()` once after loading the start frame,
  and `dllcheck` runs it standalone (docs/libsm64.md).
- `PyramidUpdate` re-implements physics from the decomp.
- The m64 header check expects the JP ROM CRC and country code in `Inputs.hpp`.

The decomp checkout at `C:\repos\sm64` is not part of the build; it is reference material.

## Performance model

The full measurement plan is in [docs/performance.md](docs/performance.md); this is the
mental model behind it.

Cost hierarchy, most to least: frame advance (`sm64_update`, measured at about 10 us),
savestate save/load (`memcpy` of 1.5 MB lightweight or 7.3 MB full: about 50/53 us
lightweight, 190/220 us full, memory-bandwidth bound now that slot buffers are recycled),
block decoding (replay from the root every shot), state trackers (run at every frame advance
and load, and may advance frames themselves), `Script` bookkeeping (map operations per frame
per hierarchy level), synchronization (named critical sections, barriers in deterministic
mode), and `PyramidUpdate` construction (surface copy and transform per `ImportSave`).

The framework's job is to minimize **frames advanced per frame of useful output**. Every
mechanism above exists for that: savestates avoid replays, the cost model in
`Resource::shouldSave` / `shouldLoad` decides when a save is cheaper than replaying, caches
short-circuit ancestor lookups, and `PyramidUpdate` replaces full game frames with a few
hundred floating-point operations where only the platform matters.

Design intent is zero-cost abstraction: resource, tracker and state-bin types are template
parameters constrained by concepts; `if constexpr` compiles state tracking out when the
tracker is `DefaultStateTracker`; LTO is on for every configuration. Where the code falls
short today (scripts resolving symbols by name per execution, virtual per-frame calls on
`Resource`, `shared_ptr` segment chains, one `std::map` node per cached frame in the
bookkeeping) is listed in the performance doc and on the roadmap.

Instrumentation already in the code: rdtsc totals and counts on `Resource`, per-script
durations and counts in `BaseScriptStatus`, and the scattershot end-of-run percentages.
Counts are the metrics to trust; they are deterministic and machine-independent.

## Sharp edges worth knowing

- `Modify` moves the cursor to the end of the child's diff (see above).
- `GetTrackedState` throws if the root is not a `TopLevelScript` with that tracker type.
- `GetTrackedState` returns a reference into the root's table. A write at or before that
  frame (`AdvanceFrameWrite`, `Apply`, `Rollback`) invalidates it; copy the state
  (`auto state = ...`) when it has to survive one.
- `SlotManager` limits are per resource, so aggregate memory scales with thread count.
- `BinaryStateBin` throws on out-of-range values; a state bin that can throw will abort a
  pellet inside an `ExecuteAdhoc`, which is treated as "invalid state", not as a crash.
- `Configuration::MaxBlocks` is a hard cap; hitting it throws "Block cap reached".
- `ValidateBaseBlock` failures mean the encode and the decode of a block saw different
  states: a stale savestate (the ROADMAP 4.5 bug in `Revert`, fixed), a non-deterministic
  `ApplyMovement`, or a direct memory write somewhere upstream. The failure handler
  re-decodes the block and reports whether decoding itself is deterministic; a run with
  `resources.costModel` false removes automatic savestates from the suspect list.
