# Performance change log

Every hot-path change records its delta table here, newest first; the policy, the suite and
how to run it are in [performance.md](performance.md). The first measurements (2026-09-07),
which everything since is compared against, are at the bottom.

## 2026-09-15: the input walk's front-end cost (ROADMAP 3.19)

Two measured optimizations behind unchanged interfaces, found while root-causing the 3.2
rows below. `LevelStack::Grow()` is `TAS_FW_NOINLINE`: MSVC 19.51 had inlined it, vector
growth and allocation included, into `operator[]` and then inlined the accessor nowhere
(docs/compilers.md); with the keyword the accessor is 23 instructions and inlines into every
caller again, so the walk and `AdvanceFrameWrite` make no accessor calls (their only
`Grow` references are the cold branches). And the walk writes into the caller's object:
`GetInputsMetadata(frame, metadata)` is the recursion, each level adjusting the parent's
answer in place, and `GetInputsMetadata(frame)` is that object built in the caller's return
slot, where returning temporaries on some paths and a named local on another had made
MSVC copy the 40 bytes at the end of every level. No behavior changes; the engine tests
pin the walk's answers.

MSVC 19.51, Release, against master built with the same toolset (`-Reference`) and the
committed 19.44 baseline; every Tier C and D count identical (deterministic Tier D: 52
solutions, 111,860 blocks, 524,380 scripts on both), allocations identical, 0 regressions,
15 rows faster:

| | 19.44 baseline | master, 19.51 | this change | vs master | vs 19.44 |
|---|---|---|---|---|---|
| AdvanceFrameWrite | 49.4 ns | 66.3 ns | 52.2 ns | -21.3% | +5.6% |
| AdvanceFrameRead | 85.9 ns | 100.8 ns | 92.2 ns | -8.5% | +7.3% |
| Write_RewindOne | 127.3 ns | 144.4 ns | 124.9 ns | -13.5% | -1.9% |
| ExecuteAdhoc_Empty | 25.5 ns | 32.4 ns | 24.3 ns | -24.9% | -4.7% |
| ModifyAdhoc_OneFrame | 125.2 ns | 163.0 ns | 122.6 ns | -24.8% | -2.1% |
| Execute_ChildOneFrame | 815.0 ns | 717.8 ns | 604.5 ns | -15.8% | -25.8% |
| Modify_ChildOneFrame | 708.5 ns | 639.8 ns | 564.6 ns | -11.8% | -20.3% |
| GetInputs_Uncached_Depth/1 | 44.3 ns | 50.1 ns | 45.7 ns | -8.7% | +3.3% |
| GetInputs_Uncached_Depth/4 | 73.2 ns | 85.5 ns | 70.3 ns | -17.8% | -3.9% |
| GetInputs_Uncached_Depth/16 | 200.5 ns | 242.9 ns | 163.5 ns | -32.7% | -18.5% |
| LongLoad_RewindToRoot_Depth/16 | 208.7 ns | 227.1 ns | 193.9 ns | -14.6% | -7.1% |
| AdvanceFrameWrite_TrivialTracker | 426.1 ns | 530.1 ns | 435.0 ns | -17.9% | +2.1% |
| AdvanceFrameWrite_RecursiveTracker | 584.1 ns | 701.6 ns | 580.9 ns | -17.2% | -0.6% |
| Framework_PyramidOscillation | 663.5 ms | 675.7 ms | 675.1 ms | -0.1% | +1.8% |
| Framework_TrackerSweep | 6.9 ms | 7.1 ms | 6.9 ms | -2.7% | +0.9% |
| TierD_Deterministic | 95.1 s | 94.6 s | 93.8 s | -0.8% | -1.4% |
| TierD_Throughput | 67.9 s | 58.9 s | 57.9 s | -1.7% | -14.7% |

The Script family is where the framework's own bookkeeping shows; the game workloads are
bounded by the DLL and read within noise. What 19.51 still reads over 19.44 on the four
rows above is the toolset's, the change closed the rest. clang-cl 22, against master built
with it: `GetInputs_Uncached_Depth/16` 181.9 to 148.7 ns (-18.3%, the return copy), every
other Script row within 5%, 0 regressions; clang's inliner never had the accessor problem.

The placement sensitivity of the 3.2 entry below went with the cause: the same never-run
benchmark appended to this code moves the uncached rows -1.2%, -0.1% and -5.9% against
the code's own binary (master's moved +28.4%, +19.4% and +14.6%), so those rows need no
rule of their own. The baselines and references of both compilers were then re-saved from
this code on the new toolset (ROADMAP 3.18).

## 2026-09-15: ScriptFriend retired, and the walk the root keeps (ROADMAP 3.2)

The first change measured on Visual Studio 2026's toolset (MSVC 19.51, clang-cl 22;
`scripts\build.ps1` takes the latest install). The committed baselines and the reference
under `perf\reference\tyler-desktop` are 19.44's, so this table's reference is master built
with 19.51 in a worktree and passed with `-Reference` (`perf\reference\tyler-desktop-master-19.51`,
gitignored), interleaved as usual. The "vs base" column then reads the toolchain, not the
change: against 19.44's baseline, 19.51 runs most of the Script family 14 to 35% slower,
the child-script rows 9 to 12% faster and the throughput Tier D 12% faster. Re-saving the
baselines on 19.51 is the maintainer's call.

The change: `Script` befriends `TopLevelScript` and the accessor class is gone; a tracker's
own frames skip the root's `TrackState` at the call site instead of being asked;
`startSaveHandle` and the root's `_m64` are private (docs/compilers.md, ROADMAP 3.2). Every
Tier C and D count identical on both binaries (deterministic Tier D: 52 solutions, 111,860
blocks, 524,380 scripts), allocations identical, Tier D and the framework workloads within
noise. Release, MSVC 19.51, min of nine:

| | reference (master, 19.51) | current | vs ref |
|---|---|---|---|
| AdvanceFrameWrite | 63.0 ns | 63.2 ns | +0.4% |
| AdvanceFrameRead | 99.1 ns | 97.5 ns | -1.6% |
| AdvanceFrameWrite_TrivialTracker | 531.9 ns | 506.5 ns | -4.8% |
| AdvanceFrameWrite_RecursiveTracker | 668.5 ns | 660.2 ns | -1.2% |
| GetInputs_Uncached_Depth/1 | 49.9 ns | 54.9 ns | +10.0%; re-run 51.5 / 54.9 ns, +6.6% |
| GetInputs_Uncached_Depth/4 | 83.2 ns | 90.6 ns | +8.9%; re-run +8.8% |
| GetInputs_Uncached_Depth/16 | 244.9 ns | 253.7 ns | +3.6%; re-run +5.6% |
| LongLoad_RewindToRoot_Depth/16 | 227.8 ns | 221.8 ns | -2.6% |
| Framework_PyramidOscillation | 680.9 ms | 687.8 ms | +1.0% |
| Framework_TrackerSweep | 6.9 ms | 7.0 ms | +1.5% |
| TierD_Deterministic | 94.7 s | 95.1 s | +0.4% |
| TierD_Throughput | 59.0 s | 59.6 s | +1.0% |

The uncached `GetInputs` rows read 5 to 9 ns over the reference. Root cause, established
on symbolized Release-codegen builds of master and the branch (the `/Zi /O2 /Ob2` recipe,
which reproduced the gap: 52.1 against 54.8 ns at depth 1):

- The code that runs is the same. `dumpbin /disasm` of `Script::GetInputsMetadata`, the
  root's override, `GetInputsMetadataAndCache` and the container accessors is
  instruction-identical between the two binaries (addresses aside). Only
  `DepthScript::execution`, the benchmark fixture, differs, by the four instructions of the
  inlined tracker check in its `LongLoad` path, which moved every function after it: the
  walk sits at a different 64-byte-line phase in each binary (start mod 64: 16 against 32)
  and the timed loop's head at mod 16 = 0 against 6.
- The hardware counters say what changed. Timer samples every 0.12 ms carrying the CPU's
  cumulative counters, the loop's intervals told from the fixture's by the sampled PC,
  depth 1, 8,000,000 iterations, per iteration:

  | | master | branch |
  |---|---|---|
  | instructions retired | 911 | 936 |
  | retired branch mispredicts | 0.027 | 0.043 |
  | cache misses, LLC misses | 1.42 | 1.44 |
  | unhalted core cycles | 203 | 230 |
  | IPC | 4.48 | 4.07 |

  Same work, the same predictions and the same misses to within noise, 27 more cycles per
  lookup: instruction delivery. The 0.12 ms heat map of the walk puts the extra samples on
  the instructions after its call returns and taken branches (+368 on the `test` after the
  first `LevelStack::operator[]` return, +178 on the `sar` that follows), with master's
  surplus on other branch targets of the same code; a redistribution over identical
  instructions, not a stalled one. xperf exposes no front-end event on this machine (the
  PMU cannot drive sampling interrupts here at all), so which structure is charged, uop
  cache lines or fetch alignment, stays unnamed.
- Placement alone reproduces it: master's own source with one benchmark appended to the
  M64 family, never run under the Script filter, read against master's reference binary
  +28.4% at depth 1, +19.4% at depth 4 and +14.6% at depth 16 (64.8, 102.9 and 279.5 ns
  against 50.5, 86.2 and 243.8), every other Script row within 4%.

Why this loop is so sensitive, from the same profile, both binaries alike: the walk is
front-end bound at IPC 4.5, and on MSVC 19.51 `LevelStack::operator[]` is not inlined into
it (a call per container per level, twelve per lookup at depth 1, 22% of the loop's
samples; LevelStack.hpp's note says 19.44 inlined it, and the 19.44 baseline's Script rows
are 14 to 35% faster), and the walk returns its 40-byte result by copying a local it has
just assembled (34% of its samples on the `vmovsd` after that copy). Both are ROADMAP 3.19.
On this toolset these rows move by more than the gate with code that did not change
(performance.md, noise control; ROADMAP 3.18).

Tried and dropped: one walk for every script, `Script::GetInputsMetadata` ending in a
private virtual the root overrides for the movie (`GetM64Inputs`, the `GetM64Metadata`
shape), so that the root's copy of the walk goes. Three forms, Script family only, each
against the same reference:

| form | Depth/1 | Depth/4 | Depth/16 | AdvanceFrameWrite |
|---|---|---|---|---|
| the fallback returns an object the walk then adjusts (owner, level, inputs) | +30.2% (+15 ns) | +18.6% | +7.3% | +8.9% |
| the same, the walk `__declspec(noinline)` | +33.1% | +24.2% | +7.8% | +10.3% |
| the fallback builds the answer in place, the walk non-virtual | +13.7% (+7 ns) | +16.1% | +13.7% | -2.6% |
| the same, the walk virtual with no override | +36.0% | +25.0% | +10.5% | +0.5% |
| the root's own walk kept, reached through the friend (as landed) | -1.1% | +3.3% | -0.4% | +1.2% |

Not explained (inlining is ruled out by the second row, and a virtual per hop was the shape
before too) and not pursued: the real workloads never moved, but the row is gated and the
duplicate walk costs nothing at run time. The root keeps its walk; the comment on
`TopLevelScript::GetInputsMetadata` says why.

## 2026-09-14: the ticket wait blocks past a bounded spin (ROADMAP 3.15)

Designed under hard rule 10 and agreed the same evening. `WaitForTurn` spun with
`_mm_pause` and a yield every 1,024 spins; on clang-cl's libomp that burned a core per
waiting thread where the barriers it replaced had slept (the clang-cl suite's deterministic
Tier D: wall -40% against the pre-branch binaries, process cycles +57%). It now spins for
`SpinBudget` (4,096) pauses, about a turn of typical length, and then waits on the turn
counter (`std::atomic::wait`), which `PassTurn` notifies after its store; no new state, the
same order of turns, so the counts are unchanged. Deterministic Tier D, Tier D alone, each
compiler's Release built by the perf script against its interleaved pre-branch reference:

| | Reference | Spin (before) | Spin, then wait |
|---|---|---|---|
| MSVC wall | 130.4 s | 92.3 s | 93.8 s |
| MSVC cycles against the reference | | -29% | -59% |
| MSVC CPU time, outside the resource | | 725 s, 55.8% | 426 s, 24.0% |
| clang-cl wall | 144.9 s | 89.9 s | 93.9 s |
| clang-cl cycles against the reference | | +57% | -5% |
| clang-cl CPU time, outside the resource | | | 430 s, 24.6% |
| throughput wall (no queue), MSVC / clang-cl | 110.3 / 105.8 s | 89.1 / 89.7 s | 91.6 / 87.4 s |

A budget of 16,384 pauses was measured too, on clang-cl: wall 92.4 s, cycles +14% against
the reference, 524 s of CPU with 38.8% outside the resource, so 1.5 s of wall for 22% more
CPU; not taken. Counts on both compilers: 52 solutions, 111,860 blocks, 524,380 scripts,
17,800,136 frame advances, 608 saves, 1,045,094 loads, the ticket queue's to the number.

## 2026-09-14: `M64::save` walks its frames once (ROADMAP 3.16)

An xperf profile of `BM_M64_Save_10k` on clang-cl builds with symbols (Release codegen),
current tree against the FrameMap commit's parent: 34.7% of the current build's samples in
`FrameMap<unsigned long long, Inputs>::operator[]`, a call per lookup that clang-cl does not
inline into the save loop (`contains` and three `frames[i]` per frame), against 0% in the
parent, whose `std::map` lookups were inlined into `M64::save` (40% of its samples there);
MSVC inlines both, which is why its row had gained. The loop now walks the sorted frames
once, a zero input for every frame not in it, behind the unchanged `save` (the round-trip,
gap-filling and libsm64 identity tests check the file):

| Row | Reference (5238d9b) | This change |
|---|---|---|
| BM_M64_Save_10k, clang-cl | 1.4 ms | 0.8 ms |
| BM_M64_Save_10k, MSVC | 2.1 ms | 0.8 ms |
| BM_M64_Load_10k, both | unchanged from the branch (0.6 ms) | |

The same walk, one write per frame and one write per 4 KB chunk had each read as "no
change" earlier the same day: `perf.ps1 -NoBuild` after `test.ps1`, which builds only the
tests target, measured the previous perf binary every time.

## 2026-09-14: the piped-in inputs handed out in rounds (ROADMAP 3.14)

A determinism fix in `ScattershotThread::Initialize`, behind the unchanged interface: the
input solutions of a piped-in run go to the threads in rounds keyed on the thread id, one
queue call per thread per round, instead of through a shared index in timing order that
left the threads with different call counts (the second pass of the dr stage differed run
to run and could hang). No hot-path change: the hand-out is a few calls per run. Counts of
stages with inputs change once (the dr scratch stage's first pass, 9 solutions, 212,436
blocks and 2,910,119 scripts, is now 148, 211,753 and 2,983,279, and its second pass
1,004 blocks and 15,047 scripts on every run and build); the no-input workloads keep theirs
(CI-sized Tier D: every count identical).

## 2026-09-14: a search's own movement options (C++23, ROADMAP Phase 5)

Designed under hard rule 10 and prototyped on a branch for the maintainer's decision. The
three movement-option calls gain overloads on the search's own nested
`enum class CustomMoves`, taken from the object through a C++23 explicit object
parameter; the selected options go in a second per-thread bit vector, and the weighted
draw of 3.8 is one template over either enum (`DrawOption`), which the `BasicMoves`
overloads now call. Nothing on the hot path changes shape: the `BasicMoves` calls do
what they did through the same helpers, the second vector is cleared per script like the
first (a fill over an empty vector in a search without the enum), and no scenario's
script changed, so every count is expected identical and was checked exactly (MSVC
Release, the ticket-queue commit a9c14a9 against this change):

| Workload | a9c14a9 | This change |
|---|---|---|
| CI-sized Tier D (`perf/tierd-ci.json`: 4 threads, deterministic, 100 shots) | 88,778 scripts, 34,241 blocks, 12 solutions, 2,948,886 advances, 104 saves, 174,384 loads | identical |
| dr scratch stage (8 threads pinned, deterministic, 3,000 shots) | 9 solutions, 212,436 blocks, 2,910,119 scripts | identical |

The names are the maintainer's: the framework's enum `MovementOption` became `BasicMoves`
and the magic name `CustomMoves`, the three calls unchanged, every entry keeping its
spelling. The five searches then moved their moves out of `BasicMoves` into their own `CustomMoves`
(`Scattershot_BitfsDr`, `Scattershot_BitfsDrApproach`, `Scattershot_BitfsDrRecover`,
`BitfsOscFinal`; `TiltTargetShot` only ever added `NO_SCRIPT` and never checked it, so that
line is gone and it declares none), each enum listing its moves in the order they had in
`BasicMoves`. The weighted draw walks a list in enum order, so every draw maps the RNG
to the same choice as before, and `BasicMoves` is the three input groups only. Checked
exactly on the same two workloads (the CI-sized Tier D and the dr scratch stage, whose
search is the DR script): every count identical.

The Linux builds of the same change (GCC 14 and 15, Clang 18 and 21, warnings as errors;
docs/compilers.md) rejected an unused copy of the tracked state in
`TiltTargetShot::SelectRandomInputs`, a leftover that became a warning when that status
turned into arrays (3.7). The copy primed the tracker's cache for a frame nothing read
there and is removed, which changes nothing the search decides: the CI-sized Tier D workload, whose stage is that script, repeats every count above
to the frame advance, save and load (the lookup was a cache hit every time).

The perf suite (MSVC Release, three processes, the reference being the 5238d9b binaries
interleaved, the branch's only reference; the deltas are the branch's and are logged per
change above) shows no regression: 0 over 10%, 24 improvements, 0 allocation increases.
The rows this change could touch:

| Row | Reference (5238d9b) | This branch | Allocs |
|---|---|---|---|
| BM_Scattershot_UpsertBlock_Redundant | 64.5 ns | 27.6 ns | 2 to 0 |
| BM_Scattershot_UpsertBlock_Improve | 92.3 ns | 54.4 ns | 3 to 1 |
| BM_Scattershot_UpsertBlock_Novel/50000 | 6.2 ms | 3.7 ms | 150,005 to 50,003 |
| BM_Scattershot_GetHash/0 | 20.9 ns | 20.6 ns | 0 |
| BM_Script_AdvanceFrameWrite | 145.9 ns | 49.8 ns | 2 to 0 |
| BM_Framework_PyramidOscillation | 680.7 ms | 669.0 ms | 404,555 to 229,042 |
| TierD_Deterministic (wall) | 129.1 s | 91.7 s | |
| TierD_Throughput (wall) | 105.7 s | 87.6 s | |

The one count flag is the deterministic Tier D row against the pre-branch baseline: its
counts (52 solutions, 111,860 blocks, 524,380 scripts, 17,800,136 advances, 608 saves,
1,045,094 loads) are the ticket queue's to the number, so the baseline is what moves at
the merge (`-SaveBaseline`). Tier D alone, rerun on the tree with the searches migrated:
deterministic 92.3 s against the reference's 130.2 s with the same counts, throughput 89.1 s
against 107.5 s. The one efficiency flag, the
DLL's frame advance at 8 threads (97.0 to 91.6 points), is not this change's code and did
not reproduce: the family rerun alone put the row within a point of the reference (94.1 to
93.4) with no flag. 

The clang-cl suite (same shape, the `tyler-desktop-clang` reference) passed the same
gates with one time flag that reproduced on two family reruns: `BM_M64_Save_10k`, 1.4 to
1.7 ms (MSVC's build of the same row is 16% faster than its reference). Bisected over the
branch's clang-cl builds run as the reference against this tree: the FrameMap commit
0fb3aaf has the slow row (1.8 ms), its parent 54e52e7 the fast one (1.4 ms). Resolved as
ROADMAP 3.16, below. (The entry first said that rewrites of the frame loop changed nothing;
those were measured on a stale perf binary, see 3.16.) Its
efficiency flag (frame advance at 2 threads) did not reproduce on the family rerun, and
the 16-thread `SaveErase` row, a save path nothing here touches, read +0.9% in the suite
and +14.9% on that rerun, the 16-thread noise seen all day. The deterministic row's counts
are the ticket queue's on this compiler too; its process cycles rose 57% against the
pre-branch binaries while wall time fell 40%, the ticket wait spinning where libomp's
barriers slept (ROADMAP 3.15). Tier D alone on the migrated tree, clang-cl: deterministic
89.9 s against the reference's 144.5 s with the same counts, throughput 89.7 s against
104.0 s.

## 2026-09-14: the deterministic queue as a ticket (ROADMAP 3.8)

Designed under hard rule 10, prototyped on a branch and accepted by the maintainer on the
numbers below, with the implementation in `ScattershotThread.t.hpp` and a four- and
three-thread reproduction added to the mock test. Deterministic mode
ran every script's upsert through `QueueThreadById`, a barrier and then one barrier per
thread, so every thread waited for the slowest at every round: 58% of the deterministic
Tier D run's CPU was that spin-wait (the 3.8 profile). The prototype replaces the barriers
with a ticket: each thread numbers its calls, call k of thread i is ticket k·N+i, one
shared turn serves tickets in order, and a thread waits only for its own turn, so its next
script runs while others are still on the previous round. Base-block selection and the
end-of-shot counts take tickets too, since they read the block table and the solution
count, and a thread retires from the queue under its turn when its shots are done, which
replaces the barrier loop `MultiThread` ran at the exit. The upserts keep the barriers'
total order; what changes is the table a thread sees when it selects a block, the one at
its ticket rather than the one after a lockstep round, so the deterministic workloads
follow a different path and their counts change once.

Measured on the deterministic Tier D workload (8 threads pinned, cost model off, MSVC
Release, the same hour as the barrier version's 112.1 s and 867 s of CPU):

| | Barriers | Ticket, run 1 | Ticket, run 2 |
|---|---|---|---|
| Wall | 112.1 s | 92.1 s | 91.9 s |
| CPU time, outside the resource | 867 s, 62.8% | 725 s, 55.8% | 725 s, 55.9% |
| Solutions, blocks, scripts | 55; 109,958; 520,052 | 52; 111,860; 524,380 | the same |
| Frame advances, saves, loads | 18,014,927; 608; 1,038,084 | 17,800,136; 608; 1,045,094 | the same |

Two runs agree to the last count, which is the property the mode exists for; the mock
test's two-, three- and four-thread reproductions pass; the throughput workload is
untouched (not deterministic). The deterministic counts changed once with it:
`perf/baselines/tierd-ci.json` was regenerated from the CI-sized workload through
`perf_compare.py tierd` (the new counts are in docs/performance.md, "Tier D"), and the
perf baselines' `TierD_Deterministic` row reads as a count change until the next
`-SaveBaseline`. The suite (`perf.ps1`, MSVC Release, reference 5238d9b interleaved):
0 time regressions on 74 rows, 24 rows faster, allocations identical, `TierD_Deterministic`
130.1 -> 91.7 s against the reference (-29.5%) with exactly the count change above flagged,
`TierD_Throughput` 70.9 -> 60.9 s; the CI-sized workload reads the same counts in `fixed`
and `dirty` mode (2,948,886 frame advances, 12 solutions) and on two builds. Not changed by it:
deterministic mode with piped-in inputs (open, above), and the CSV export, whose sampling
and row order were never in the queue in either scheme (`AddCsvRow` runs outside it, under
its own critical sections, in arrival order); the solutions a stage exports come from the
upserts and are in the queue in both.

## 2026-09-14: the movement-option weights as lists, the options as a bit mask (ROADMAP 3.8)

`AddRandomMovementOption` took a `std::map<MovementOption, double>` by value and every one
of its 33 call sites passed a braced list of three or four pairs, so every call built a map
(a node per entry) and freed it; `RandomInputs` took its button probabilities the same way;
and `movementOptions` was an `unordered_set` reassigned per script. On the `dr` stage, whose
scripts are one frame each, that was 4.1% of the CPU. Both take an
`std::initializer_list<std::pair<..., double>>` now, which the same braced lists initialize
with nothing allocated, walked in key order after a four-element insertion sort with a
duplicate key keeping its first weight, which is the order and the meaning the map had, so
the draw consumes the RNG exactly as before; the options are one bit each in a
`std::vector<bool>` that grows to the largest option a script on the thread ever selects
(a handful of times in a run) and is cleared in place per script, so no size is assumed of
an enum that grows with every scenario and no script allocates for it. A single weighted
list is walked from a stack array of 64 entries, a bound on one draw's candidates, not on
the enum. Call sites unchanged. That the search is the same was checked on the `dr` stage in
deterministic mode (8 threads pinned, 3,000 first shots from one tilt-target solution): the
first pass, 3,172,883 scripts with a weighted draw in each, reached the same 62 solutions
and 226,173 blocks before and after, and on a second run after. Wall 28.6 -> 24.5 s (-14%),
CPU 223 -> 195 s, outside the resource 68.3 -> 64.3% (the mode's barrier wait included).
The suite (`perf.ps1`, MSVC Release, reference 5238d9b interleaved), run twice: 0
regressions on 74 rows, 23 rows faster, counts and allocations identical to the previous
change's; the tilt-target workloads use no weighted option, and read `TierD_Deterministic`
112.1 s and `TierD_Throughput` 57.2 s against the reference's 129.3 and 68.7 s, the day's
drift. The first pass had flagged the 16-thread `LibSm64Scaling_SaveErase` row at +21%
against the reference and read `Script_Execute_ChildOneFrame` at 826 ns; the second read
them at +6% and 619 ns, a noisy unpinned row and the heap-layout flip performance.md
describes, neither reached by this change.

Found on the way, not fixed: the stage's second pass, which starts from the 62 solutions
piped in from the first, does not reproduce between two runs of one binary in deterministic
mode (8,334 and 7,788 scripts). `Initialize` hands the input solutions out one per thread
per iteration and each thread makes one queue call per iteration, so with a solution count
that is not a multiple of the thread count the threads leave the loop after different
numbers of calls and the barriers pair up across the boundary in timing order. No committed
stage runs deterministic with inputs (`tilt-x` has none); ROADMAP 3.8 carries it.

## 2026-09-14: the Tier D rows carry the share outside the resource (ROADMAP 3.8)

`perf_compare.py tierd` reads the stage summary's `CPU time` line into the row as
`overheadPct`, the share of the process CPU time outside the resource, and the compare
gates it as it gates Tier C's: an increase over `--overhead-tolerance` points (2) against
the anchor is a regression. Reported once the 3.8 profile made the number the one to watch;
gated now that its spread is known: on 2026-09-14, runs of one binary read 24.0 and 24.3%
(throughput, morning), 19.1 and 19.2% (after the FrameMap), 68.9 and 68.8% (deterministic),
while the day's three changes moved it 24.0 -> 22.7 -> 19.2 -> 10.7%. Baselines saved before
this lack the counter, so the compare reports it without a gate until they are re-saved.

## 2026-09-14: where the `dr-oscillations` stage's CPU time goes (ROADMAP 3.8)

The two suspects on the hotspot list that no suite workload runs, the `PyramidUpdateMem`
import and `CalculateOscillations`, live in `StateTracker_BitfsDr`, the `dr` stage's
tracker. No code changed. The stage was profiled the way the Tier D workloads were (xperf,
20 ms, the symbolized build of `bd598ca`, 16 threads unpinned, High performance plan) on a
scratch configuration: the committed `dr` stage fed one tilt-target solution of the
deterministic Tier D run in place of `tilt-range`'s (its equilibrium frame 3347), with
`firstShots` 30,000 instead of 50,000 and the later oscillations cut to 3,000 shots. The
first pass is the sample: 30,000 shots in 31.8 s, 13,870,041 scripts, 304 solutions,
281,650 blocks; the second pass found nothing at oscillation 1 and the stage stopped. The
`CPU time` line: 506 s, advance 64.9% (20.7 us each), save 0.6%, load 16.9% (63.4 us
each, 1,352,165 loads), outside the resource 17.6%.

- **Items 2 and 3 of the list are nothing here.** `CalculateOscillations` 0.01% of the
  samples, `CalculatePhase` 0.02%, `GetMinimumDownhillWalkingAngle` 0.02%, the
  `PyramidUpdateMem` import below one sample in 25,478: the crossing path, with its
  up-to-50-frame lookahead and one import per frame of it, runs at crossings, and the
  search crosses rarely for what it advances. The whole tracker is 2.1% inclusive, 3.3%
  with `ExecuteStateTracker`.
- **The DR scripts are one frame each** (15.9 M frame advances for 13.9 M scripts), so
  the per-script costs weigh more than in tilt-target: `ChooseScriptAndApply` 73% inclusive,
  of which `SelectMovementOptions` 4.1% (2.7% of the CPU in the allocator under it: every
  `AddRandomMovementOption` call takes its `std::map<MovementOption, double>` of weights by
  value, constructed from a braced list per call, a node per option), `movementOptions`
  reassigned as a fresh `std::unordered_set` per script 0.5%, `RandomInputs` 1.3% and
  `GetClosestInputByYawHau` 0.9% exclusive.
- **Loads are second to the game**: 16.5% in `LibSm64::load`'s `memcpy`, one load per ten
  scripts from the `REWIND` movement option and the decode replays, against two per script
  in tilt-target but for scripts thirty times longer.
- **Block decoding is 8.4%** inclusive at 281,650 blocks after 30,000 shots, against 2 to
  3% on the tilt-target workloads; the list's first item (ROADMAP 4.3) is this stage's,
  and grows with the run.
- Heap 6.4%, map code 5.3% (the weight maps, the tracked-state nodes, `GetInputsMetadata`
  1.7%), `UpsertBlock` 0.6%, barriers 0.3% (the stage is not deterministic), the tracker's
  `crossingData` vector copies below the threshold.

## 2026-09-14: the trackers' status objects as arrays (ROADMAP 3.8)

The 3.8 profile's largest item outside the framework: `TiltTargetShotMetrics::CustomScriptStatus`
held thirteen `std::vector`s for three-element values, constructed per tracked frame and
copied whole wherever a `GetTrackedState` result was taken by value, 7.0% of the throughput
run's CPU in the allocator and most of its 8.9% in `std::vector` code. The per-axis values
are `std::array<float, 3>` and `std::array<int, 3>` now, in that tracker, in
`BitfsOscFinalMetrics`, in `StateTracker_BitfsDr` and `Scattershot_BitfsDr`, and in their
solutions (`Stages.cpp`'s metrics writer takes any per-axis range), and the tilt-target
tracker reads its previous states by reference (`const auto&`), which the tracked states'
node container keeps valid across the other frames it may track meanwhile. Stage scripts
only; no framework header changed. MSVC Release, High performance plan, the same hour,
before and after this change alone:

| | Before | After |
|---|---|---|
| Throughput: wall, scripts/s, frame advances/s | 64.7 s, 15.8 k, 532 k | 59.1 s, 17.2 k, 601 k |
| Throughput: CPU outside the resource | 19.1% (0.191 ms per script) | 10.7% (0.098 ms per script) |
| Deterministic: wall, CPU time (counts identical) | 123.1 s, 970 s | 109.5 s, 867 s |

The deterministic run's counts are the baseline's to the last frame advance (18,014,927;
608 saves; 1,038,084 loads; 109,958 blocks; 55 solutions; 520,052 scripts). Over the
day's three changes the throughput run went from 69.8 s and 24.0% outside the resource
to 59.1 s and 10.7%, the deterministic run from 133.7 s to 109.5 s.

The suite (`perf.ps1`, MSVC Release, reference 5238d9b interleaved): 0 regressions on 74
rows, 23 rows faster, counts identical, allocations down where this change reaches:
`Framework_TrackerSweep` 14,548 -> 3,034 allocations for 500 tracked frames (6 per frame,
from 17 after the FrameMap and 29 at the start of the day) at 7.5 -> 7.0 ms;
`TierD_Deterministic` 129.7 -> 109.3 s (-15.7%) and `TierD_Throughput` 67.9 -> 58.0 s
(-14.6%) against the reference, of which this change is -11.2% and -11.9% against the
FrameMap head's result of an hour earlier. In that last compare, without an interleaved
reference, `LibSm64Fixed_Load` read 38.3 -> 42.3 us; the row is a 1.5 MB `memcpy` this
change does not reach, it read 42.0, 38.3 and 42.3 us across the day's three suite runs,
and the reference-interleaved gate has it within noise.

## 2026-09-14: the frame-keyed containers as sorted vectors (ROADMAP 3.7)

`M64Base::frames` (the source movie and every diff) and `Script`'s five per-level
containers (`inputsCache`, `saveCache`, `frameCounter`, `saveBank`, `loadTracker`) are
`FrameMap`s and a `FrameSet` (`tasfw/FrameMap.hpp`): a vector sorted by frame with the
subset of `std::map`'s interface the framework uses and the same meanings, no allocation
until the first entry, storage kept across `clear()`. The 3.8 census had found that every
one of the 11 allocations of an empty child script and both of an empty ad-hoc call were
`std::map` sentinel nodes, which MSVC allocates whenever a map is constructed or
move-constructed, and a status object carries an `M64Diff` map through every sandbox,
`Run` and result. The tracked states stay a `std::map` per owner and level: a tracker
reads its previous states by reference while it may track another frame, which a vector's
reallocation would break, and their node is 0.24% of the run. Design presented under hard
rule 10, prototyped on a branch at the maintainer's request and accepted on these numbers.
`test_framemap.cpp` pins the map meanings kept (ordered by key, insert and emplace do not
overwrite, `operator[]` default-constructs, erase by key, position and range, the ordered
lookups, `std::insert_iterator`, equality by contents).

The suite, MSVC Release, reference 5238d9b interleaved (machine factor 1.00): 0 regressions
on 74 rows, 21 rows faster, every count identical (Tier C frame advances, saves and loads;
Tier D shots, scripts, blocks, solutions, frame advances, saves, loads), allocations down on
every row that touches a map. Against the same day's `hotspots` head (the symbol table in,
this change alone; fastest of three, single-thread rows pinned):

| Row | Before | After | Allocations |
|---|---|---|---|
| `Script_AdvanceFrameWrite` | 151 ns | 50 ns | 2 -> 0 |
| `Script_AdvanceFrameRead` | 253 ns | 86 ns | 1 -> 0 |
| `Script_AdvanceFrameWrite_Save` | 765 ns | 441 ns | 7 -> 3 |
| `Script_Write_RewindOne` (a write into the middle of the diff) | 172 ns | 129 ns | 3 -> 1 |
| `Script_ExecuteAdhoc_Empty` | 73 ns | 25 ns | 2 -> 0 |
| `Script_ModifyAdhoc_OneFrame` | 302 ns | 127 ns | 5 -> 1 |
| `Script_Execute_ChildEmpty` | 424 ns | 197 ns | 11 -> 2 |
| `Script_Execute_ChildOneFrame` / `Modify_ChildOneFrame` | 995 ns / 1.07 us | 626 / 550 ns | 31 -> 14 / 13 |
| `Script_GetInputs_Uncached_Depth` 1 / 4 / 16 | 143 / 171 / 299 ns | 47 / 76 / 208 ns | 1 -> 0 |
| `Script_LongLoad_RewindToRoot_Depth` 1 / 4 / 16 | 204 / 204 / 217 ns | 212 / 210 / 212 ns | unchanged |
| `Script_AdvanceFrameWrite_TrivialTracker` / `RecursiveTracker` | 745 / 952 ns | 452 / 590 ns | 14 -> 3 / 19 -> 6 |
| `M64_Load_10k` / `M64_Save_10k` | 1.1 / 2.1 ms | 0.6 / 2.0 ms | 10,004 -> 27 / 0 |
| `Scattershot_UpsertBlock` novel (50k) / redundant / improve | 6.4 ms / 65 / 93 ns | 3.7 ms / 28 / 55 ns | 150,005 -> 50,005 / 2 -> 0 / 3 -> 1 |
| `Framework_PyramidOscillation` (42,923 frames) | 692 ms | 674 ms | 404,555 -> 229,042 |
| `Framework_DownhillAngle_PyramidUpdate` (1,000 calls) | 2.4 ms | 2.1 ms | 56,018 -> 42,006 |
| `Framework_TrackerSweep` (500 tracked frames) | 7.3 ms | 7.1 ms | 14,548 -> 8,541 |
| `TierD_Deterministic` (counts identical) | 128.4 s | 123.1 s | |
| `TierD_Throughput` | 67.8 s | 65.8 s | |

`UpsertBlock` and the movie's load were not targets: a `ScattershotSolution` carries a diff,
so every block insert constructed one, and the movie's 10,000 frames were 10,000 nodes.
The `LongLoad` rows are the one place the ancestor walk touches every level's containers
without a lookup that the vectors speed up; they read +3 to +4%, under the gate and within
what those rows move between runs. The throughput run's `CPU time` line reads 66.4%
advance, 0.7% save, 13.7% load, 19.2% outside the resource (22.7% after the symbol table,
24.0% before it); the deterministic run's CPU time 970 s against 1,052 s that afternoon.

Also removed in the same change: the 16-thread `LibSm64Scaling_Addr` row added earlier the
same day. Between two runs of one binary it read 48.9 and 59.6 ns, which the 10% time gate
would have called a regression once the row was baselined: a 16 ns lookup's per-thread
time on 16 unpinned threads is the hybrid scheduler's, and the contention the row was
added to show is gone with the loader lock. `LibSm64Fixed_Addr`, pinned and single-thread,
stays (14 to 16 ns across the day's runs). Baselines are not re-saved here: the allocation
decreases are the reviewer's to confirm and re-baseline (performance.md, "Reporting and gating").

## 2026-09-14: `LibSm64::addr` answers from its own table (ROADMAP 3.7)

The scripts ask for `gMarioState`, `gCamera`, `gObjectPool` and the pyramid behavior at the
top of every `validation()`, `execution()` and helper, and `addr()` went to the OS loader
each time. `GetProcAddress` is 61 ns alone, but `LdrGetProcedureAddressForCaller` takes the
loader lock, and the threads of a search queue on it: the 3.8 profile read 1.5% of the
throughput run's CPU in the loader, its lock and `RtlBackoff`. `LibSm64::addr` now resolves
a name once (the loader, then the alias table, as before) and keeps it in a table looked up
by `string_view` (a name is copied when first seen, never after; a resource belongs to one
thread, so no lock). `Resource::addr`'s contract note follows: a script may ask per
execution; one that needs a symbol every frame still caches the pointer. Two Tier B rows
were added to measure it, `LibSm64Fixed_Addr` (one thread, pinned) and `LibSm64Scaling_Addr`
(1 to 16 threads, one DLL copy each, unpinned), cycling through the four names; MSVC
Release, five repetitions, means:

| `addr()` per call | Through the loader | From the table |
|---|---|---|
| 1 thread | 61.1 ns (scaling row 69.1 ns) | 16.6 ns (16.6 ns) |
| 2 threads | 167 ns | 16.2 ns |
| 4 threads | 362 ns | 20.7 ns |
| 8 threads | 995 ns | 24.1 ns |
| 16 threads | 5,628 ns | 45.0 ns |

The throughput Tier D workload (16 threads, cost model on; not deterministic, so its rates
and the `CPU time` line compare, not its counts), MSVC Release, the same hour, High
performance plan, unpinned: 14,485 scripts/s and 493 k frame advances/s with 24.0% of the
CPU outside the resource before; 14,507 scripts/s and 507 k frame advances/s with 22.7%
outside after. The CPU outside the resource per script went from 0.263 to 0.248 ms, the
1.5% the profile had attributed to the loader; the wall time of this workload moves more
than that between runs of the same binary. The deterministic workload's counts cannot
change (no frame advance, save or load is involved).

The suite (`perf.ps1`, MSVC Release, reference 5238d9b interleaved, machine factor 1.00):
0 regressions on 74 rows, allocations and counts identical, efficiency within 5 points.
Rows that moved: `Framework_DownhillAngle_PyramidUpdate` 2.7 -> 2.4 ms (-13%, the only
row past the gate's 10%: the `PyramidUpdateMem` import asked for its symbols on every
call), `Framework_TrackerSweep` 7.8 -> 7.3 ms (-5.8%: `StateTracker_BitfsDr` asks per
tracked frame), `TierD_Deterministic` 132.3 -> 128.4 s (-2.9%; counts identical),
`TierD_Throughput` 68.0 -> 67.8 s (-0.3%). Everything else within noise, the largest
`ModifyAdhoc_OneFrame` +5.0% (288 -> 302 ns, a row that touches nothing changed; the
2026-09-08 note on code layout). The scaling `Addr` row was then cut to 1 and 16 threads:
at 8 unpinned threads its efficiency read 69% in one run and 47% in the next for the same
binary (80% both times at 4), the hybrid scheduler's doing on a 16 ns lookup, which the
efficiency gate would have called a regression once the row was baselined.

## 2026-09-14: where the Tier D CPU time goes (ROADMAP 3.8)

No hot path changed. `bitfs-turn`'s stage summary gained the `CPU time` line (the resource's
advance, save and load as shares of the process CPU time over the stage; performance.md,
"Existing instrumentation"), and both Tier D workloads were sampled with the Windows
Performance Toolkit (`xperf -on PROC_THREAD+LOADER+PROFILE -stackwalk Profile`, 4 ms, every
CPU) on a Release-codegen build with debug information (`RelWithDebInfo` preset with
`/O2 /Ob2 /Zi` and `/debug /OPT:REF /OPT:ICF /INCREMENTAL:NO`; 1,274,368 bytes against
Release's 1,273,856, counts identical, wall 133.2 s against 133.7 s). The dump's stacks were
aggregated by module, by function and by inclusive bucket (a sample counts once per bucket
whose regex matches any frame of its stack). Machine as for the suite: High performance plan,
High priority, the deterministic run pinned to `0x5555`, the throughput run unpinned.

| | Deterministic: 8 threads, cost model off, 600 shots | Throughput: 16 threads, cost model on, 1,200 shots |
|---|---|---|
| Wall, CPU time | 133.7 s, 1,052 s | 72.9 s, 1,151 s |
| Scripts, frame advances, saves, loads | 520,052; 18,014,927; 608; 1,038,084 | 1,031,571; 35,768,962; 68,287; 2,072,761 |
| CPU per script | 2.02 ms | 1.12 ms |
| Frame advance (`advance`, the game) | 26.5%, 15.5 us each, 34.6 per script | 61.7%, 19.9 us each, 34.7 per script |
| Load (`fixed` slices, 1.5 MB) | 4.6%, 46.7 us each, 2.0 per script | 13.3%, 74.0 us each, 2.0 per script |
| Save | 0.0%, 77 us each | 0.6%, 108 us each |
| Outside the resource | 68.9% | 24.3% |
| of which OpenMP barrier spin-wait (`_vcomp::PartialBarrierN::Block` and its `NtDelayExecution` / `SwitchToThread` calls) | 58.5% (55.9% at the per-script `QueueThreadById`, 2.1% in decode, 0.4% at the exit) | 0.2% |
| of which heap allocation and free (`RtlpLowFragHeapAllocFromContext`, `RtlFreeHeap`, ...) | 4.5% | 11.1% |
| of which `std::map` and `std::_Tree` code | 2.1% | 5.0% |
| of which `GetInputsMetadata` (inclusive) | 1.5% | 3.3% |
| of which symbol resolution (`LdrpResolveProcedureAddress`, the loader lock, `RtlBackoff`) | 0.6% | 1.5% |
| `bitfs-turn.exe` code, exclusive | 4.5% | 10.4% |
| Block decode (`DecodeBaseBlockDiffAndApply`, inclusive) | 3.0% | 2.3% |
| `UpsertBlock`, `PrintStatus` | 0.02%, 0 | 0.04%, 0 |

What the two runs say together:

- **The per-script cost outside the resource is the same in both runs**, about 0.26 ms
  (deterministic: 2.02 ms less 1.18 ms of barrier wait, 0.54 ms of game and 0.09 ms of
  loads; throughput: 1.12 ms less 0.69 ms of game, 0.15 ms of loads and 0.01 ms of saves).
  The deterministic run's 69% outside the resource is 58 points of waiting: `QueueThreadById`
  puts a barrier and then one barrier per thread around every script's `UpsertBlock`, and
  every thread waits for the slowest one each time, spinning (vcomp spins through
  `SwitchToThread` and `NtDelayExecution`, so the wait is CPU time and shows in the
  `process cycles` row). The wait is the variance of a script's cost, not the barriers'
  own cost; the gate run's cycles measure waiting, and the throughput run is the one whose
  outside share is work.
- **Replays are the game time.** `AdvanceFrameRead` holds 68% of the throughput run's CPU
  against 3.7% for `AdvanceFrameWrite`: about 95% of the frame advances replay known inputs.
  The replay is the search's evaluation: after each script `TiltTargetShot` runs the game to
  the pyramid's equilibrium through `GetEquilibriumTrackedState` (`Load(frame + 1)` frame by
  frame, up to 200, each tracked by `TiltTargetShotMetrics`), 67% of the CPU inclusive,
  plus the rewinds `ApplyMovement` makes, which replay from the shot's base save (cost model
  off) or the nearest automatic save (on). The later calls of the same lookahead in
  `ValidateState`, `GetStateBin` and `GetStateFitness` find the tracked states cached (1.4%,
  2.8%, 0.6%), so the framework's caching holds across the sandboxes; the cost is the first
  evaluation, about 33 game frames per script, and only fewer or cheaper evaluation frames
  change it (ROADMAP 4.3, and `PyramidUpdate` as the stand-in it was written to be).
  Decoding the base block from the root, the list's first suspect, is 2 to 3%.
- **The tracker's status object is the heap.** Of the 11.1% in the heap on the throughput
  run, 7.0% is `TiltTargetShotMetrics::CustomScriptStatus`: thirteen `std::vector` members
  for three-element arrays, constructed per tracked frame and copied whole wherever a
  `GetTrackedState` result is taken by value (its constructor alone 4.2%, `CheckEquilibrium`
  0.8%, `GetEquilibriumTrackedState` 0.7%, the destructor and `operator=` 0.9%,
  `execution` 0.4%). `std::vector` code is another 8.9% inclusive, mostly the same copies.
  That is the stage script, not the framework. The framework's own allocations are about
  3.5%: `BaseScriptStatus` per sandbox (0.6%), `Script::Run` (0.5%), `GetInputsMetadata`'s
  cache nodes (0.4%), `~Script` (0.3%), `LoadBase`'s save-cache and load-tracker nodes
  (0.3%), `LevelStack::Grow` (0.3%), the tracked-state map nodes (0.5%), which with the
  5.0% of map code is the remainder of ROADMAP 3.7 with a number on it: about 9% of the
  production run.
- **`resource->addr()` per call is 1.5%** on 16 threads, and not only the 62 ns
  `GetProcAddress`: `LdrGetProcedureAddressForCaller` takes the loader lock, so the threads
  contend on it (`RtlEnterCriticalSection`, `RtlAcquireSRWLockShared`, `RtlBackoff` in the
  profile). The scripts resolve `gMarioState`, `gCamera` and the pyramid behavior at the
  top of every `validation()`, `execution()` and helper (performance.md, the non-zero-cost
  list; ROADMAP 3.2's access contract).
- **Loads are memory bandwidth.** The `fixed` load is one `memcpy` of 1.5 MB: 41 us alone,
  46.7 us with 8 threads on the performance cores, 74 us with 16 threads on every core.
  Two loads per script in both runs.
- Not hotspots on this workload: `UpsertBlock` and the `blocks` critical section
  (0.04%), console output under the `print` section (0), `GetHash` (0.01%),
  `M64Diff`/`Inputs` (0.2%), the slot manager (no eviction, 3 or 28 slots live).

The Tier C family under the same profiler (1 ms, one performance-core CPU): the family is
the nested-script pyramid oscillation (96% of its samples; 718 ms, 42,923 frame advances
for 20 output frames, a replay ratio of 2,146), which is 91% game, 74% of it on the replay
path (`AdvanceFrameRead` 74% against `AdvanceFrameWrite` 17%), with the `PyramidUpdateMem`
import at 2.5%, `GetMinimumDownhillWalkingAngle` at 1.6% and the heap at 2.7%; its
`overheadPct` reads 5.3. The other two rows, sampled at 0.12 ms over 40 repetitions: the
downhill-angle call is 3.2 us and 56 allocations, of which the `PyramidUpdateMem`
construction itself (reading and transforming the surfaces out of the DLL state) is about
30%, the stand-in's own physics 5%, and the rest the `TopLevelScript`, `Resource` and
`SlotManager` the call builds and tears down around that one frame, with `Script::Run`'s
sandboxes and their `BaseScriptStatus` (heap 17% of the trace's samples, `Sm64Object::operator=`,
`LoadSurfaces`, `MainFromSave`, the resource and script constructors and destructors the
owners). The tracker sweep is 15.7 us and 29 allocations per tracked frame at 12.4%
overhead: 91% the game frame (14.3 us here, Tier B's number for this point of the movie),
1.4 us in `ExecuteStateTracker`, of which about 0.9 us is allocation (`BaseScriptStatus`,
`LevelStack::Grow`, the tracker's `CustomScriptStatus` vectors, `Script::Run`), the 3.7
remainder again. It never crosses (500 frames, 500 advances; `CalculateOscillations`
0.1%), so the list's third item is not measured by any workload in the suite and needs the
`dr-oscillations` stage.

Method notes for the next investigation: the aggregation script and the trace commands are
not in the repository (a session's scratch); `perf.ps1`'s pinning and priority were
reproduced by hand, and xperf's default dump symbolizes every process in the trace, so a
warm symbol cache (`_NT_SYMCACHE_PATH`) is worth keeping between runs. LTO folds identical
functions under `/OPT:ICF`, so a `std::_Tree` node insert can carry the name of an unrelated
map's instantiation; read those as "map insert".

## 2026-09-14: a saved state's lifecycle has two ends

A state's contents live from `Resource::save` until its slot is erased. A state type that
holds a reference to something outside itself now defines `dispose()`, which
`SlotManager::EraseSlot` calls at that point, resolved at compile time like `LevelStack`'s
`Reset()`, and `LibSm64Mem` uses it to give its dirty baseline its reference back. The
dirty mode's baseline bookkeeping is reference counts kept at save and dispose instead of a
walk over the slot table at each baseline, so `LibSm64` no longer names the slot manager;
neither does `SlotHandle`, which releases and checks a slot through `Resource::DisposeState`
and `HasState`; and a resource's limit is the one argument of `Resource`'s constructor.
`FakeResource` is `MockResource`. Measured, MSVC Release against the reference, the two
families the erase path touches: `SlotManager_CreateErase` at 100, 1,000 and 10,000 live
slots +0.6%, -0.1%, -1.1%, `LoadSlot` and `CreateAtCap` within +2.0% .. -1.5%,
`LibSm64Dirty_SaveErase` 6.9 us both, `SaveFresh` -0.7%, `Load` -0.9%, `FrameAdvance`
-1.0%; allocations and counts unchanged. The dirty-baseline test now checks the reference
counts across a run and its erases.

## 2026-09-14: a process-wide savestate budget (ROADMAP 3.5)

`SlotBudget` is a budget and a balance for the whole process: a resource subtracts its
limit from the balance when it is created, or throws if the balance is too low, and adds
it back when it dies. The pipeline sets the budget from `resources.savestateBudgetMB`
(default 8192) and gives each thread's game resource an equal share less the 16 MB a
script's `PyramidUpdate` takes per thread. One subtraction per resource created, nothing on
the save or load path. Checked with the CI-sized Tier D stage (100 shots, 4 threads,
`fixed`, cost model off): at the default, counts identical to the committed baseline
(2,981,801 frame advances, 104 saves, 184,344 loads, 10 solutions, 36,347 blocks, 93,774
scripts) with the slot line at 3 live per thread and 0 evictions; at 80 MB, which leaves
each thread two saves, the slot line reads 2 live and 4 evictions and every count is still
identical, the evicted save never being loaded again. Nothing this pipeline does comes near
the default: 27 savestates per thread at most (previous entry).

## 2026-09-13: one counter struct, one clock, and the forward jump that never fired (ROADMAP 3.6, 3.11)

`Resource` keeps its counts and cycles in one struct now, `ResourceWork work`, which also
carries the slot manager's high-water marks, pool reuses and evictions; the Tier C
benchmarks and `bitfs-turn` read it instead of copying six fields, and the stage summary
prints the slot line. `ExecuteAdhocBase` times the ad-hoc body with `get_time()` like every
other duration: it was the one place that went through `std::chrono::high_resolution_clock`
and stored milliseconds, and that clock call was the dearer of the two. And `LoadBase` and
`LongLoad` compare a found save's frame with the cursor instead of the target, so a forward
load with a save between the cursor and the target loads it when `shouldLoad` says the
skipped frames cost more than a load. Written wrong on 2022-03-22, corrected in `Load` on
2022-04-09, lost when `Load` became `LoadBase` on 2022-06-14 and copied into `LongLoad` that
August, the branch could not fire until now (ROADMAP 3.11).

MSVC Release, `scripts\perf.ps1` with Tiers A to D against the reference (the baseline
commit's binaries, interleaved; machine factor 0.97), fastest of the repetitions. Every gated
count identical: Tier C scripts, saves, loads and allocations, and Tier D deterministic
18,014,927 frame advances, 608 saves, 1,038,084 loads, 55 solutions, 109,958 blocks,
520,052 scripts. Those workloads run with the cost model off, where the fixed branch stays
dead by construction.

| row | reference | current | delta |
|---|---|---|---|
| `Script_ExecuteAdhoc_Empty` | 85.3 ns | 72.5 ns | -15.0% (the clock; -14.3% under clang-cl) |
| `Script_ExecuteAdhoc_OneFrame` | 275.6 ns | 260.0 ns | -5.7% |
| `Script_Execute_ChildEmpty` | 446.0 ns | 403.7 ns | -9.5% |
| `Script_AdvanceFrameWrite` | 144.1 ns, 151.0 ns | 163.5 ns, 163.0 ns | +13.4%, +8.0% in two runs; see below |
| `Script_AdvanceFrameRead` | 246.1 ns | 237.7 ns | -3.4% |
| `Script_LongLoad_RewindToRoot_Depth/4` | 470.1 ns | 202.9 ns | -56.9%, allocations 1 -> 2 per iteration |
| `Script_LongLoad_RewindToRoot_Depth/16` | 3.58 us | 204.2 ns | -94.3%, allocations 1.02 -> 2.02 |
| Tier B, every `LibSm64Full`, `LibSm64Fixed`, `LibSm64Dirty`, `SlotManager` and `Resource` row | | | within -1.5% .. +0.8% |
| Tier C `PyramidOscillation`, `DownhillAngle_PyramidUpdate`, `TrackerSweep` | 681.5 ms, 2.8 ms, 7.4 ms | 682.8 ms, 2.8 ms, 7.4 ms | +0.2%, -1.1%, +0.2% |
| `TierD_Deterministic` (8 threads, cost model off) | 132.5 s | 131.5 s | -0.7% |
| `TierD_Throughput` (16 threads, cost model on) | 69.3 s | 69.5 s | +0.3% |

Two things the gate flags are not regressions:

- `Script_AdvanceFrameWrite` +13.4%, and +8.0% on a rerun of the family: the current MSVC
  binary is consistently about 12 ns slower on it. Nothing on that path changed but the two
  counter increments inside `FrameAdvance`, which `AdvanceFrameRead` shares and reads faster;
  and clang-cl, same source against its own reference, has the row at +3.2% with
  `AdvanceFrameRead` at +0.5%. MSVC code layout, the sensitivity these rows are on record for.
- The two `LongLoad_RewindToRoot` rows are the fixed branch at work. The benchmark rewinds to
  the root's save and loads forward to where it was; the save it made there on the previous
  iteration now serves that load as a jump instead of a replay through every level, so the
  row measures two loads instead of one load and a replay. The extra allocation per
  iteration is the touch-order node `SlotManager::LoadSlot` inserts for any load. At depth 1
  the cost model declines the jump (two frames of replay are cheaper than a load on the fake
  resource) and the row is unchanged. These two rows and the three faster ones were
  re-baselined on 2026-09-14 (`-SaveBaseline` at commit 5238d9b, MSVC and clang-cl), so
  the committed baselines and the reference binaries are this build's.

What the fixed branch does with the cost model on: the deterministic Tier D stage with
`costModel: true` (8 threads, `fixed` saves, seed 3, 600 shots), reference and current
binaries alternated twice at the suite's priority and placement. The search is the same in
all four runs (55 solutions, 109,958 blocks, 520,052 scripts); the rest is per run:

| | frame advances | saves | loads | wall | process cycles | peak resident |
|---|---|---|---|---|---|---|
| reference, run 1 | 17,797,387 | 41,724 | 1,038,084 | 132.1 s | 3.133e12 | 358 MB |
| current, run 1 | 17,750,784 | 41,071 | 1,041,776 | 131.4 s | 3.119e12 | 321 MB |
| reference, run 2 | 17,796,971 | 41,966 | 1,038,084 | 131.8 s | 3.129e12 | 359 MB |
| current, run 2 | 17,750,204 | 41,309 | 1,041,784 | 131.0 s | 3.110e12 | 325 MB |

The reference loads exactly as often as with the cost model off, in both runs: a dead
branch's signature. The fix makes about 3,700 more loads (+0.4%) and 46,600 fewer frame
advances (-0.3%) per run, about 650 fewer automatic saves since a jumped stretch
accumulates no replay history, and a 0.5% shorter run in wall time and process cycles,
which is what 3,700 fixed-slice loads at 40 us against 46,600 advances at 14 us predicts;
peak resident memory is 35 MB lower in both pairs. On the throughput run (cost model on,
16 threads, not deterministic) the same shift shows as loads +1.6% and advances -0.2%
against the reference at unchanged wall time.

The slot line, printed per stage from now on: the deterministic run holds at most 3
savestates per thread (4 MB), the cost-model-on and throughput runs 25 to 27 (35 to 38 MB),
with zero evictions in every run, so nothing this pipeline does comes near the per-thread
8 GB cap (ROADMAP 3.5).

## 2026-09-13: the compare concepts constrain (ROADMAP 3.10)

The concepts behind the `Compare` family (`ScriptCompareHelper.hpp`, and
`constructible_from_tuple` in `SharedLib.hpp`) are constraints on the call's result type
now, instead of `requires { expr; }` blocks that only asked whether the expression was
well-formed (docs/compilers.md, "A concept-id as a requirement expression is always
satisfied"). Compile-time only: no caller changed, nothing instantiates differently, and
`-Wno-missing-requires` left `add_optimization_flags`. The MSVC Release binaries before and
after were compared function by function through their `.pdata` tables: `dllcheck.exe` and
`m64splice.exe` have a byte-identical `.text`; `bitfs-turn.exe` (2417 functions) and
`tasfw-perf.exe` (1976) have the same functions at the same sizes, 97% and 91% of them at
the same address with the same bytes, and the rest the same instruction stream once call
targets and RIP-relative operands are ignored (six checked by disassembly diff): the old
header emitted a few bytes of static data per instantiation, and their removal moved a
handful of functions and shifted `.rdata` constants by 8 to 16 bytes.

Measured anyway, MSVC Release, `scripts\perf.ps1` with Tiers A to D against the reference
(machine factor 1.01), fastest of the repetitions. Every count identical (Tier C scripts,
Tier D frame advances, saves, loads, blocks and solutions), no allocation change, and the
Tier B and D rows:

| row | reference | current | delta |
|---|---|---|---|
| `LibSm64Fixed_FrameAdvance` | 14.1 us | 14.5 us | +2.3% |
| `LibSm64Fixed_SaveErase` | 41.3 us | 42.8 us | +3.6% |
| `LibSm64Fixed_Load` | 40.7 us | 41.8 us | +2.6% |
| `LibSm64Dirty_SaveErase` | 7.1 us | 7.1 us | -0.1% |
| `LibSm64Dirty_Load` | 6.9 us | 6.9 us | -0.3% |
| `LibSm64Full_SaveErase` | 176.0 us | 181.5 us | +3.1% |
| `LibSm64Full_Load` | 176.9 us | 175.7 us | -0.7% |
| `Framework_PyramidOscillation` | 688.2 ms | 688.4 ms | +0.0% |
| `TierD_Deterministic` | 133.6 s | 137.8 s | +3.1% |
| `TierD_Throughput` | 75.5 s | 74.7 s | -1.1% |

The scaling family's two efficiency flags (`FrameAdvance` at 2 and 4 threads, 95% and 94%
against the reference's 101% and 100%) are the reference binary's own 1-thread row reading
15.3 us in that run against 14.5 us for the current one; the 2- and 4-thread times are
within 1% of the reference.

Three Tier A rows on unchanged code moved, and moved the same way on a re-run of the
`Script` and `SlotManager` families (`-Filter 'BM_Script_|BM_SlotManager_' -NoBuild
-NoTierD`), the layout signature performance.md describes (a tight loop's alignment follows
the functions around it):

| row | reference (run 1 / run 2) | run 1 | run 2 |
|---|---|---|---|
| `Script_AdvanceFrameWrite` | 169.3 / 176.4 ns | 146.2 ns (-13.7%) | 150.5 ns (-14.7%) |
| `Script_AdvanceFrameRead` | 235.4 / 229.5 ns | 251.9 ns (+7.0%) | 253.9 ns (+10.6%) |
| `SlotManager_CreateAtCap/10000` | 220.9 / 230.2 ns | 240.9 ns (+9.0%) | 244.5 ns (+6.2%) |

`Script::AdvanceFrameRead` and `AdvanceFrameWrite` did not change and their instruction
streams are identical in the two `tasfw-perf.exe`; the read row's +10.6% on the second run
is over the gate, so it is recorded here as layout and the `tyler-desktop` baseline
re-saved (`-SaveBaseline`, a third run of the whole suite, calibration 1.03 against the
previous baseline; its `tasfw-perf.exe` and `bitfs-turn.exe` are the reference now).
clang-cl was built and tested, not measured.

## 2026-09-13: a savestate is the game's bytes only (ROADMAP 3.12, hard rule 7)

The DLL's `.data` and `.bss` end in the mingw-w64 runtime's own state, which the Windows
loader writes from every thread of the process (the DLL's TLS callback at each thread's
exit takes a critical section in the last page of `.bss`). A `dirty` or `full` load that
restored that page under an exiting thread was the sixteen-thread hang of the scaling
family (ROADMAP 3.12; docs/libsm64.md, "The game's bytes"). `LibSm64` now copies and
restores only the game's bytes of each section: `full` two ranges instead of two whole
sections (3 KB less), `fixed` its slices cut to the ranges (32 bytes less), `dirty` each
page whole except the four edge pages, which it copies in part. The per-page cost is one
span lookup and a branch on its length (the whole-page case keeps the constant-size
`memcpy`); the fault handler is untouched. Interface unchanged; `LibSm64::gameBytes()` is
read-only, for `dllcheck` and the tests.

Measured, MSVC Release, `scripts\perf.ps1` with Tiers A to D against the reference (the
baseline commit's binaries, interleaved; machine factor 1.02 against the committed
baseline), fastest of the repetitions. The Tier B rows, the `dirty` ones being what the
scaling family and the Linux CI run, and the Tier D workloads:

| row | reference | current | delta |
|---|---|---|---|
| `LibSm64Full_SaveErase` | 178.8 us | 181.0 us | +1.2% |
| `LibSm64Full_Load` | 174.0 us | 180.3 us | +3.6% |
| `LibSm64Full_SaveFresh` | 1.18 ms | 1.19 ms | +0.7% |
| `LibSm64Full_ResidentPerSlot/100`, stateBytes | 7,279,456 | 7,276,192 | -3,264 B |
| `LibSm64Fixed_SaveErase` | 41.4 us | 41.4 us | +0.0% |
| `LibSm64Fixed_Load` | 41.8 us | 42.0 us | +0.5% |
| `LibSm64Fixed_SaveFresh` | 282.7 us | 284.1 us | +0.5% |
| `LibSm64Dirty_SaveErase` | 7.1 us | 7.1 us | -0.4% |
| `LibSm64Dirty_Load` | 7.0 us | 7.0 us | +0.1% |
| `LibSm64Dirty_SaveFresh` | 90.3 us | 91.6 us | +1.5% |
| `LibSm64Scaling_SaveErase`, 16 threads | 7.7 us | 7.7 us | +0.4% (efficiency 92 -> 93%) |
| `LibSm64Scaling_FrameAdvance`, 16 threads | 18.2 us | 18.0 us | -1.3% (efficiency 81 -> 84%) |
| `TierD_Deterministic` | 139.9 s | 139.5 s | -0.3% |
| `TierD_Throughput` | 74.1 s | 75.1 s | +1.3% |

Frame advances unchanged on every row; allocations identical; every exact count (Tier C
and D) identical; no efficiency row moved by more than 5 points. The only row past the
gate's threshold anywhere in the run is an improvement unrelated to this change
(`Script_AdvanceFrameWrite`, -19.6%, the day's reading of a Tier A row). The `full` load's
+3.6% is two `memcpy` calls of ranges 3 KB shorter than before; the reference itself read
+7.3% against the committed baseline on that row, so it is the day, not the change.

## 2026-09-13: scattershot's byte hash becomes the framework's own (ROADMAP 3.4, hard rule 3)

`Scattershot::GetHash` (the block table's state-bin hash) and `ScattershotThread::GetHash`
(the RNG chain behind `GetRng`/`GetTempRng`) mixed `std::hash<std::byte>` per byte, which
MSVC's STL implements as FNV-1a over the byte and libstdc++ as the byte itself, so the same
seed took a different search path on Linux from the first pellet (docs/compilers.md). Both
now call `HashByte` in `Scattershot.hpp`, FNV-1a over the byte, the value MSVC computed all
along; `test_scattershot_hash.cpp` pins it and the chain from seed 3. Interface unchanged.

Measured, MSVC Release, the `^BM_Scattershot` family against the reference (machine factor
1.00), fastest of nine:

| row | reference | current | delta | cycles |
|---|---|---|---|---|
| `Scattershot_GetHash/0` | 21.7 ns | 21.6 ns | -0.3% | -0.7% |
| `Scattershot_GetHash/1` | 10.1 ns | 10.0 ns | -0.9% | -0.8% |
| `Scattershot_UpsertBlock_Novel/1000` | 0.1 ms | 0.1 ms | +0.3% | +0.1% |
| `Scattershot_UpsertBlock_Novel/50000` | 6.2 ms | 6.4 ms | +2.8% | +3.1% |
| `Scattershot_UpsertBlock_Redundant` | 65.3 ns | 65.9 ns | +1.0% | +0.8% |
| `Scattershot_UpsertBlock_Improve` | 93.5 ns | 93.8 ns | +0.3% | +0.2% |

Allocations identical on every row. Exact counts: the CI-sized Tier D workload reads its
committed counts unchanged on the fixed MSVC build (2,981,801 frame advances, 10
solutions), and on Linux, where it read 2,867,262 and 11 before, it now reads the same
2,981,801 and 10 with GCC 15 and Clang 21; `perf/baselines/tierd-ci-linux.json` is gone.
Builds clean with warnings as errors on MSVC, clang-cl, GCC 15 and Clang 21; 57 tests pass.
Not measured: the full Tier D workloads, whose Windows counts cannot change (the hash value
is the same function) and whose times do not depend on this.

## 2026-09-12: relative gate, performance-core pinning, pre-flight checks, CPU cycles (ROADMAP 1.3)

No framework code changed (`tasfw-core`, `tasfw-scattershot`, `tasfw-resources` untouched).
The runner and the harness did, as "Running the suite" now describes: `perf.ps1` runs the
baseline commit's binaries from `perf\reference\` interleaved with the current ones and
`perf_compare.py --reference` gates time and efficiency against them; Tier D is pinned to
the performance cores (`0x5555` deterministic, `0xFFFF` throughput) and the scaling family
to `0xFFFF`; a pre-flight refuses a VM or a busy CPU, notes missing Defender exclusions,
switches the power plan and calibrates; every benchmark reports `cycles`
(`tasfw-perf/src/measure.cpp`: `QueryThreadCycleTime`, a per-thread `perf_event` on Linux
where the kernel allows one), and `bitfs-turn` prints `process cycles` per stage
(`ProcessCycles.cpp`, `QueryProcessCycleTime`, an inherited `perf_event` on Linux). The
harness call sites moved from `AllocCount`/`ReportAllocs` to `BeginMeasure`/`EndMeasure`.
The reason is the drift history in the Tier D section: three days of false regressions
from the machine, not the code.

Measured while setting it up (MSVC Release; reference = the 63b8ee3 binaries that produced
the committed baseline, saved that evening):

- Calibration, `LibSm64Fixed_FrameAdvance` on the reference binary: 14.39 and 14.62 us on
  two launches against 14.81 us in the baseline (machine factor 0.97 and 0.99), under the
  High performance plan.
- `BinaryStateBin_Pack`, reference 114.5 ns against current 115.2 ns (+0.7%; -1.6% against
  the baseline's 117.1 ns), 344 cycles per iteration, a 3.0 GHz effective clock.
- The cooked `Win32_PerfFormattedData` CPU counter is unusable at script start-up: its first
  call reported 13% (the previous interval, which was the script's own start) and the next
  ones 0%, and the first full launch refused itself. The raw idle-time delta over the same
  three seconds reads 3.8 to 4.0% on the idle desktop (Corsair services and Edge); the
  check uses that and gates at 8%.
- Cost: Tier A to C launch twice as many processes (66 instead of 33); Tier D takes about
  ten minutes instead of five. A full run is about 22 minutes.
- The full MSVC run (reference interleaved, 74 rows): machine factor 0.96, the High
  performance plan and the pinning reading 4% faster than the day the baseline was saved.
  Against the reference, 71 rows within +8% / -6%, every exact count identical, allocations
  identical, efficiency within 5 points; Tier D deterministic 134.0 s reference against
  132.7 s current (-1.0%; the committed baseline said 149.1 s, which the old absolute gate
  would have read as an 11% improvement of nothing), throughput 70.6 against 71.8 s
  (+1.7%). Over the gate: `Script_AdvanceFrameWrite` +12.1%, `Script_GetInputs_Uncached_Depth/1`
  +16.6% and `/4` +16.5%, the three layout-sensitive rows the noise-control notes already
  name, each about 19 ns, on framework code that did not change; the reference build is the
  perf binary without the harness addition, so the attribution procedure above has already
  run.
- The full clang-cl run: machine factor 0.96 as well; counts, allocations and efficiency
  as MSVC's; Tier D deterministic 144.9 s reference against 150.0 s current (+3.5%),
  throughput 70.1 against 68.6 s (-2.1%). Over the gate, different rows from MSVC's, which
  is the layout signature the noise-control notes describe: `Inputs_GetClosestInputByYawHau_PartialMag`
  +15.2%, `Inputs_GetClosestInputByYawExact` +12.6%, `Script_GetInputs_Uncached_Depth/1`
  +15.3% (`GetClosestInputByYawHau` +9.9%, `/4` +9.3%, `/16` +8.6% under it); the same
  rows on MSVC moved -2.5% to +0.3%. `Inputs.cpp` and the framework are byte-identical
  between the two binaries. Baselines and reference binaries for both compilers re-saved
  from these runs; the next change's compare will carry the `cycles` column on every row.
- Linux: GCC 13 and Clang 17 (the CI container) and GCC 15.2 and Clang 21.1 (26.04) build
  the new files with warnings as errors and pass the DLL-free tests; in the containers the
  kernel refuses the `perf_event`, so the rows carry `allocs` and no `cycles`, as intended.
- The first full run stalled for 45 minutes on the last scaling launch: the family had been
  pinned to the performance cores' 16 logical CPUs (`0xFFFF`) for this change, and one
  thread of the 16-thread `FrameAdvance` row died with `STATUS_RESOURCE_NOT_OWNED` while
  the rest waited at the end barrier with 5 s of CPU between them. Ten launches per
  configuration afterwards (`scripts\perf_scaling_hang.ps1`): current pinned 9 ok 1 hung,
  reference pinned 9 ok 1 hung, current unpinned 10 ok, reference unpinned 10 ok. The
  pinning was the trigger, the bug is older than this change (ROADMAP 3.12). The family
  and the throughput run are unpinned as before; only the deterministic Tier D run is
  pinned, one thread per core.

## 2026-09-12: `dirty` re-baseline measured and dropped; copy-on-write baselines; benchmark rows re-anchored; baselines re-saved (ROADMAP 2.3)

The ROADMAP 2.3 follow-up, tried and measured: `LibSm64` in `dirty` mode took a new
baseline at a save once the loads under the current one had written back four times the
size of both sections, which in the scattershot is the base save opening each shot after
the previous shot's ~1,700 loads. To make baselines cheap enough for that, a baseline now
holds copy-on-write pages the fault handler fills in before a page's first write instead
of a whole-section snapshot (no copy when a baseline is taken, memory only for the pages
written since it began, kept while a live slot's state names it); that part stays. The
re-baseline rule does not: interleaved runs of the pre-change build (A), the build with the
rule (B) and the same build in `fixed` (F), each triple back to back on the idle machine,
MSVC Release, VM stopped:

| Workload | A `dirty` before | B `dirty` with the rule | F `fixed` |
|---|---|---|---|
| Tier D deterministic, 8 threads, cost model off | 152.6 s | 149.7 s (79 baselines per thread) | 133.8 s |
| Tier D throughput, 16 threads, cost model on | 86.4 s | 87.3 s (977 baselines per thread) | 72.6 s |

Counts identical on the deterministic workload. The set regrows to 526 pages within a shot
whatever the baseline (pellets die, the level reloads), so a shot's loads restore as much
as before; with the cost model saving every ~100 loads the rule fired at nearly every save,
7.8 million first-write faults in the run, and the baseline's cost inside `save` made the
cost model save less. Final code (copy-on-write baselines, taken at the first save of a
run only), the same triple:

| Workload | A `dirty` before | C `dirty` final | F `fixed` |
|---|---|---|---|
| Tier D deterministic, 8 threads, cost model off | 173.0 s | 161.7 s | 147.2 s |
| Tier D throughput, 16 threads, cost model on | 105.7 s | 106.0 s | 84.8 s |

Counts identical on the deterministic workload (55 / 18,014,927 / 608 / 1,038,084). The
copy-on-write baselines change no per-save or per-load work: `dllcheck` on the pinned DLL
reads 7.2 us per dirty save and 7.0 us per load 60 frames into the run, leak scan zero
bytes, as before. The machine drifted between triples with nothing else running (`fixed`
133.8 s in one triple, 147.2 s in the next), so only the columns of one triple compare
with each other; across the four triples `dirty` came out 10 to 12% behind `fixed` on the
deterministic workload and 19 to 25% behind on the 16-thread one.

The `BM_LibSm64*` families now anchor 60 frames after the run's first slot (the `dirty`
save and load rows read 7.1 and 7.0 us instead of the 0.1 us of an empty set) with the
frame-advance row registered last in each family, so the save and load rows measure the
warm-up set (3,000 idle frames from a moving Mario can end in a death, which reloads the
level and quadruples the set). Both committed baselines (`perf/baselines/tyler-desktop.json`
and `-clang.json`) were re-saved from the final code, so the `Light` rows are gone and the
`Fixed` and `Dirty` rows are gated from here on: `dirty` save and erase 7.1 us, load 7.0 us
(clang 7.2 / 7.1), thread scaling 7.2 us at one thread to 9.8 us at sixteen (clang 7.6 to
10.3), Tier D 149.1 s and 83.5 s (clang 151.5 s and 85.7 s) as the machine ran that hour,
about 10% slower than its fast readings earlier in the day.

Measurement note: the same workloads launched from a detached, hidden PowerShell host read
20 to 45% slower (deterministic `fixed` 162 s, `dirty` 203 s) than when launched from the
agent's tool or a console, with nothing else running and the CPU at full clock. Launch
measurements from a console.

## 2026-09-12: three save modes in `LibSm64`; `dirty` measured against `fixed` (ROADMAP 2.3)

`LibSm64Config::saveMode` replaces the `lightweight` flag: `full` (both sections), `fixed`
(the hand-tuned slices, unchanged) and `dirty` (write-protected pages, copy what the game
wrote since a baseline the resource takes at the first save of a run; docs/libsm64.md,
"Savestates"). The Tier B families are now `^BM_LibSm64Full`, `^BM_LibSm64Fixed` and
`^BM_LibSm64Dirty`; the baselines still carry the old `Light` rows, which read `MISSING`,
and the `Fixed` and `Dirty` rows read `NEW` until the baseline is re-saved after review.
The scaling family and the Tier C and D workloads ran in `dirty`. MSVC Release, VM stopped.

| Tier | Result |
|---|---|
| A time rows | 0 regressions; `Script_GetInputs_Uncached_Depth/1` and `/4` -15% (the layout-sensitive rows on record) |
| B, `Fixed` family (new rows) | save and erase 43.0 us, load 43.5 us, fresh save 277 us, frame advance 14.3 us, 28.3 ms / 285 ms resident for 100 / 1,000 slots: the old `Light` numbers |
| B, `Dirty` family (new rows) | as written, the benchmark anchors right after the first slot, so its saves and loads had nothing to copy (0.1 us); the meaningful number is `dllcheck`'s, measured 60 frames into the run: 7.3 us save, 7.0 us load for 122 pages |
| B, `Full` family | unchanged (175 to 185 us) |
| B, thread scaling | frame advance flat; save and erase 6.8 / 6.8 / 7.0 / 6.9 / 9.6 us at 1 / 2 / 4 / 8 / 16 threads against 40 to 62 us (-83%), with about 120 dirty pages per state |
| allocations, exact counts | 0 / 0 regressions |
| C (oscillation, downhill, sweep) | -6.7%, -5.1%, -2.7%, counts identical |
| D deterministic, 8 threads, cost model off | counts identical (55 / 109,958 / 520,052 / 0). Paired runs of the same build: `fixed` 134.9 s, `dirty` 139.8 s (+3.6%); the suite read 141.5 s against the 139.6 s baseline (+1.4%). Each thread's dirty set reaches 525 pages (2.1 MB) because pellets die and the level reloads, so every load restores more than the 1.5 MB slices |
| D throughput, 16 threads, cost model on | `dirty` 87.5 s against the 74.9 s baseline (+17%) and a paired `fixed` run at 71.9 s (+22%); the cost model, measuring dearer saves, made 30,568 automatic saves instead of 69,786 and replayed 3% more frames. Not count-gated (non-deterministic) |

Conclusion for this workload: `dirty` wins by 5 to 6x per save or load while the set is
small, loses once the search's set has grown to 2.1 MB, and the 16-thread run pays most.
Decision (maintainer, 2026-09-12): the committed pipeline config and both Tier D workloads
select `fixed`, the faster mode for the BitFS search as it stands, so the gated Tier D rows
keep measuring what the pipeline runs; `dirty` stays the code default for tests, tools,
other builds and Linux. With `fixed` in the committed configs the suite reads 135.1 s
(-3.2%) and 74.2 s (-0.9%) against the baselines, counts identical, zero regressions. A
resource-internal re-baseline when many loads have hit a large set would recover the gap
(ROADMAP 2.3); it is not implemented.

A first suite run read the deterministic row at 221.8 s (+59%); it did not reproduce in
three later runs (139.8, 141.5 s) and is treated as interference, like the 156 s reading
on 2026-09-08.

Same day, after the maintainer's review of how scripts and the framework relate (AGENTS.md
hard rules 9 and 10): the layout check left `LibSm64` and the framework. It is now the
`VerifyLayout` script in tasfw-scripts, reading through `resource->addr()`; the pipeline
runs it once on one resource before its first stage instead of once per thread through a
`Resource` virtual, the dry run and `dllcheck` print its report, and the fixed-slice
coverage report moved into `dllcheck`. `Resource::verifyLayout` is gone, and the
`PlayMovie` helper that had briefly been added to `Resource.hpp` with it. Sixteen start-up
checks became one; nothing on the search path changed. Re-measured in `fixed` after the
machine had idled: deterministic 134.5 s (baseline 139.6 s, -3.7%), throughput 69.5 s
(baseline 74.9 s, -7.2%), counts identical, zero regressions. An earlier suite run that
started three minutes after a two-compiler build-and-test pass read 192.8 s and 89.1 s with
the same phase split as the fast runs (load 6%, frame advance 25%, other 67%), so the whole
process was slowed uniformly; reruns on the idle machine came back at the numbers above.
Third such reading today: do not take Tier D right after a large build.

## 2026-09-08: hardcoded object slots verified at start-up (ROADMAP 2.4)

Nothing on a per-frame path changed. `LibSm64::objectCheckReport` runs inside
`layoutCheckReport`, so once per scattershot thread from `verifyLayout` (three `addr`
lookups and a handful of compares per declared slot), and `bitfs-turn --dry-run` plays to
the first stage's frame once. MSVC Release against the committed baselines, Docker Desktop's
VM stopped:

| Tier | Result |
|---|---|
| A and B time rows | 0 regressions over 10%; 4 rows faster (`LibSm64Scaling_SaveErase` at 1, 2 and 4 threads, -13% to -17%; `Resource_SaveLoadState` -56%, the layout-sensitive row already on record). The untouched DLL rows drifted +1% to +5% (`FrameAdvance` 13.7 -> 14.4 us). |
| allocations, exact counts | 0 / 0 regressions |
| scaling efficiency | 1 flag: `SaveErase` at 8 threads fell more than 5 points below the baseline's efficiency because its 1-thread row ran 13% faster than baseline while the 8-thread row ran 6% faster. Every absolute time in the family improved; the gate reads a ratio, and a faster single thread lowers it. |
| C (oscillation, downhill, sweep) | +5.2%, -1.2%, +4.2% |
| D exact counts | identical: 55 solutions, 109,958 blocks, 520,052 scripts, 0 validation failures |
| D wall | 142.3 s (+1.9%) and 75.6 s (+0.9%); the machine was back at baseline pace for this run |

## 2026-09-08: renamed-symbol fallback in `LibSm64::addr`; the Linux `.so` measured (ROADMAP 2.1, 3.4)

`LibSm64::addr` now resolves a symbol with the new non-throwing `SharedLib::tryGet` and,
only when that fails, looks the name up in `LibSm64SymbolAliases` for the decomp's renamed
spelling (docs/libsm64.md, "Renamed symbols"); the lightweight slice-coverage checks in
`layoutCheckReport` are skipped where lightweight saves do not exist (Linux). On the pinned
DLL every symbol the search asks for resolves on the first lookup, so the per-call work is
the same single `GetProcAddress` as before plus a null test; nothing per frame changed.
MSVC Release against the committed baselines, Docker Desktop's VM stopped:

| Tier | Result |
|---|---|
| A and B (time rows) | 0 regressions over 10%; 3 rows faster (`Script_GetInputs_Uncached_Depth/1` -10.9%, `/4` -12.2%, `Resource_SaveLoadState` -56.3%, the layout-sensitive rows noise control already lists). Everything else within +1% to +6%, the DLL's own `FrameAdvance` rows included (13.7 -> 14.3 us), so the day's drift, not the change. |
| allocations, exact counts, scaling efficiency | 0 / 0 / 0 regressions |
| C (oscillation, downhill, sweep) | +2.2%, -7.2%, -1.0% |
| D exact counts | identical: 55 solutions, 109,958 blocks, 520,052 scripts, 0 validation failures |
| D wall | see below |

The Tier D wall rows tripped the gate and took an hour to run down. The suite read
155.5 s / 85.7 s (deterministic / throughput; +11% / +14%), two `-TierDOnly` reruns 153.0 /
86.2 and 154.0 / 86.9, and a stash A/B of the pre-change tree in between read 141.9 / 71.1,
which looked like a real regression with identical counts. Bisecting said otherwise: with
only the `addr` body reverted the run read 155.1 / 84.2, and with `LibSm64.hpp`/`.cpp` at
HEAD and only the unused `tryGet` added it read 165.8 / 90.5, a binary that cannot be
slower for any reason. Four interleaved runs of the deterministic workload then read
pre 159.0, post 162.6, pre 160.7, post 158.1 s: pre and post agree within 0.3% and the
machine, with no VM and no other process above 1% CPU, had simply drifted from 140 s to
160 s on the same workload over the afternoon (commit charge was 33.6 of 36.3 GB). The
change costs nothing measurable. Two things learned for the procedure: a single A/B on a
drifting machine can point the wrong way, so a Tier D wall regression with identical counts
needs interleaved pre/post runs before it counts (Tier D section); and a suite run right
after long builds and container work is a poor sample.

Linux, first numbers (`dllcheck` on bitfs-sbb's JP `.so` at frame 3330, Ubuntu 26.04
container on this machine, so not comparable with the Windows rows): frame advance 21.0 us
(GCC 15) / 22.4 us (Clang 21), dirty-page save 44.9 / 46.4 us, load 44.1 / 41.6 us, `dlsym`
33 / 37 ns. Windows `dllcheck` on bitfs-sbb's 2026 JP DLL: 8.5 us per frame, 41.0 us
lightweight save, 41.8 us load, 583 ns `GetProcAddress`, all layout checks passing.

## 2026-09-08: warning levels raised on every compiler (ROADMAP 1.7)

About 300 edits across the tree so that MSVC at `/W3`, clang-cl at `/W4` and GCC and Clang
at `-Wall -Wextra` build clean with warnings as errors: explicit casts of conversions that
were already happening (`float(a * b - c * d)` keeps the integer arithmetic and the
result), deleted dead locals and fields (several were `resource->addr()` lookups per call,
so a little less work), unnamed unused parameters, `int64_t` for the tracked-state hooks'
ad-hoc levels and `BitFsPyramidOscillation_Iteration`'s frames, a virtual destructor on
`Resource`, `snprintf` for `sprintf`. Exact counts identical everywhere: the deterministic
Tier D run gives 55 solutions, 109,958 blocks, 520,052 scripts and 0 validation failures on
both compilers, and the drift test still matches the DLL bit for bit.

Against the committed baselines (Tier A and B fastest of nine; Tier C exact; Tier D on the
`perf/tierd-*.json` workloads):

| Compiler | Tier A/B time rows over 10% | Allocation / count / efficiency regressions | Tier C (oscillation, downhill, sweep) | Tier D deterministic / throughput |
|---|---|---|---|---|
| MSVC | 0 (`M64_Load_10k` -11%, its layout flip back to 1.1 ms; re-saved) | 0 / 0 / 0 | +5.4%, -1.3%, +3.6% | 139.6 -> 140.8 s, 74.9 -> 73.3 s |
| clang-cl | 3, `Script_GetInputs_Uncached_Depth/1`, `/4`, `/16` (+17%, +12%, +14%) | 0 / 0 / 0 | +4.2%, +4.3%, +2.5% | 145.0 -> 145.9 s, 73.2 -> 75.5 s |

Two things were run down. The first full MSVC run read the deterministic Tier D at 156 s
(+11.7%); Docker Desktop's VM, started for the Linux checks, was running at the time, and
with it stopped the run reads 140.8 s. Do not measure with the VM up. Second, an A/B against
the pre-change tree (stash, rebuild the perf binary, measure, restore) on the rows that
moved: the MSVC oscillation row reads 772.8 ms before and 777.4 ms after (+0.6%; the rest of
its +5% against the baseline is the day's drift, present before the change), and clang-cl's
uncached `GetInputs` rows read 178 / 212 / 304 ns before and 201 / 226 / 334 ns after. That
path changed only in that `GetAdhocLevel` returns `int64_t` (one sign extension fewer) and
`Script::_initialFrame` widened into what was padding, and on MSVC the same rows moved the
other way (-10%); it is the code-layout sensitivity of these rows that noise control
already describes, so they were re-saved. The search itself did not move on either compiler.

## 2026-09-08: Tier B thread scaling and memory per slot (ROADMAP 1.3); json 3.12

No framework code changed. The new Tier B rows on both compilers (per-thread times;
efficiency relative to one thread, from each run's own rows):

| Row | 1 thread | 2 | 4 | 8 | 16 |
|---|---|---|---|---|---|
| `Scaling_FrameAdvance`, MSVC | 14.2 us | 99.7% | 98.6% | 96.1% | 80.3% |
| `Scaling_FrameAdvance`, clang-cl | 14.4 us | 99.9% | 99.9% | 98.6% | 82.8% |
| `Scaling_SaveErase` (lightweight), MSVC | 40.4 us | 97.9% | 98.6% | 92.9% | 65.0% |
| `Scaling_SaveErase` (lightweight), clang-cl | 40.8 us | 102% | 102% | 94.5% | 66.7% |

Efficiency moved up to 4 points between the family run and the full run that followed it
on the same binary (8-thread saves 92.9 to 89.0 on MSVC), which is why the gate is 5 points
and stops at 8 threads.

| Row | Time | `stateBytes` | `rssPerSlot` |
|---|---|---|---|
| `Full_ResidentPerSlot/100` | 121 ms | 7,279,456 | 7,282,688 |
| `Light_ResidentPerSlot/100` | 27.3 ms | 1,500,000 | 1,498,317 |
| `Light_ResidentPerSlot/1000` | 279 ms | 1,500,000 | 1,502,552 |

The full runs against the committed baselines: Tier C within 5% and Tier D within 2% on
both compilers with identical counts (55 solutions, 109,958 blocks, 520,052 scripts, 0
validation failures); no allocation, count or efficiency regression. Six Tier A and B time
rows were over the gate on unchanged code and were re-saved, each after a re-run and, where
the number was large, a perf binary built without the new benchmark code to attribute it:

| Row | Baseline | Now | Cause |
|---|---|---|---|
| `LibSm64Light_SaveErase`, `_Load` (both compilers) | 37 us | 41 to 42 us | the 1.5 MB copy's second state, every run today but one (noise control) |
| `Resource_SaveLoadState`, MSVC | 149 ns | 340 ns | the perf binary's own layout: 150 ns when built without the new benchmarks |
| `Scattershot_UpsertBlock_Improve`, clang-cl | 91.5 ns | 128 ns | same: 93.7 ns without them |
| `Script_GetInputs_Uncached_Depth/1`, `/4`, MSVC | 125 / 158 ns | 140 / 176 ns | layout from earlier today (138 / 172 ns without the new benchmarks too); the rows noise control already names |
| `SlotManager_LoadSlot/100`, clang-cl | 110 ns | 89 ns | layout the other way (90.5 ns without them) |

`M64_Load_10k` on MSVC, re-saved at 1.2 ms earlier in the day, read 1.1 ms again (-5.7%):
the same layout sensitivity, inside the gate this time.

## 2026-09-08: `tasfw-scripts-scattershot-bitfs-dr` gets the shared optimization flags (ROADMAP 1.5)

The library holding `Scattershot_BitfsDr.cpp` and `StateTracker_BitfsDr.cpp` was the one
first-party target never passed to `add_optimization_flags`, so it built without LTO (and,
on GCC, without the `-Wno-missing-requires` every sibling has, which is how the omission
surfaced: the only warnings left in the GCC job). No source under measurement changed; the
same change fixed the last MSVC warnings (an `int` literal for a float in
`StateTracker_BitfsDr.cpp`) and moved the test helpers' `getenv` into `tasfw-testing`.

Against the committed baselines (Tier A and B fastest of nine, Tier C exact counts, Tier D
on the two `perf/tierd-*.json` workloads):

| Compiler | Time rows over 10% | Allocation / count regressions | Tier C (oscillation, downhill, tracker sweep) | Tier D |
|---|---|---|---|---|
| MSVC | 3 on the first run, see below | 0 / 0 | +2.4%, -0.6%, +1.0% | deterministic 139.6 -> 140.7 s, 141.0 s on re-run, identical counts (55 solutions, 109,958 blocks, 520,052 scripts, 0 validation failures); throughput 74.9 -> 77.3 s, 73.5 s on re-run |
| clang-cl | 1 (`LibSm64Light_SaveErase` +13%) | 0 / 0 | +0.7%, -1.1%, -0.5% | deterministic 145.0 -> 145.9 s, identical counts; throughput 73.2 -> 70.7 s |

The MSVC rows over the gate: `Resource_SaveLoadState` read 217.9 ns, its known bimodal
value, and 148.4 ns on a re-run of its family; `LibSm64Light_SaveErase` +15.6%, then +0.3%
on re-run; both are the placement-sensitive rows described under noise control.
`M64_Load_10k` stayed at 1.2 ms against 1.1 ms across three runs (+13.0%, +11.7%, +13.8%)
and returned to 1.1 ms (+2.1%) when the `add_optimization_flags` line alone was removed and
the perf binary relinked; clang-cl's row moved +3.6%. m64 loading calls nothing in that
library, so this is link-time code layout on MSVC, and the `BM_M64` family rows in
`perf/baselines/tyler-desktop.json` were re-saved from the run with the change; every other
family keeps its baseline rows.

## 2026-09-08: Tier C and Tier D land (ROADMAP 1.3); first numbers

No code under measurement changed; these are the first baselines for the new rows, on the
pinned DLL and movie at frame 3330, MSVC and clang-cl agreeing on every count.

Tier C (`^BM_Framework`, cost model off, per iteration):

| Workload | Wall | Frame advances | Saves | Loads | Allocs | Overhead |
|---|---|---|---|---|---|---|
| `PyramidOscillation` (quadrant 4, does not assert from here; 20 output frames) | 750 ms | 42,923 | 0 | 1,389 | 404.6 k | 5% |
| `DownhillAngle_PyramidUpdate` (1,000 calls) | 3.0 ms MSVC, 2.4 ms clang-cl | 1,000 | 0 | 1,000 | 56.0 k | n/a |
| `TrackerSweep` (500 frames) | 7.7 ms | 500 | 0 | 1 | 14.5 k | 14.5% |

Read the allocation column: about 9 heap allocations per frame advanced in the nested-script
oscillation, 56 per downhill-angle call (the `PyramidUpdateMem` import), 29 per tracked
frame. That is the remainder of ROADMAP 3.7 and the input to 3.8.

Tier D (`bitfs-turn`, `tilt-target` from frame 3330, 100 pellets per shot):

| Run | Wall | Counts |
|---|---|---|
| Deterministic, 600 shots, 8 threads, cost model off | 140 s | 520,052 scripts, 109,958 blocks, 55 solutions, 18,014,927 frame advances, 608 saves, 1,038,084 loads, 0 validation failures; 121 MB peak resident |
| Throughput, 1,200 shots, 16 threads, cost model on | 74 s | 16.1 shots/s, 13.7 k scripts/s, 475 k frame advances/s; 686 MB peak resident |

An 800-shot deterministic run gave the same counts on MSVC and clang-cl to the last frame
advance (24,229,711), which is the reproducibility ROADMAP 4.5 restored.

The gates were checked by mutation before the baselines were committed: one extra
save/advance/load per `LoadBase` call fails Tier C on counts (`frameAdvances 42923 -> 44331`
in the oscillation, `500 -> 501` in the sweep) as well as time and allocations; a 3x
`GetHash` loop fails Tier A at +189% and +273%. One row of the first MSVC baseline run,
`Resource_SaveLoadState`, read 217 ns against 155 ns before on unchanged code and 149 ns on
a re-run of its family: the per-process bimodality described under noise control. The
family's rows in the baseline were taken from the re-run.

## 2026-09-08: `Revert` drops a reverted child's desynced saves (ROADMAP 4.5)

Correctness fix on a per-script path. `Revert` used to move every save of a reverted child
into the parent's bank when none of them was synced; it now moves only saves at or before the
child's first written frame (in practice none, so the bank is simply dropped and its slots
recycled). Less work, fewer live slots, and the parent's bank no longer accumulates a save
per reverted pellet.

Tier A and Tier B, fastest of nine, against the committed baselines:

| Compiler | Rows | Regressions > 10% | Allocation regressions | Notes |
|---|---|---|---|---|
| MSVC | 52 | 0 | 0 | every row within -9.5% .. +3.3% |
| clang-cl | 52 | 0 | 0 | `GetInputs_Uncached_Depth/1` -11% and `LibSm64Light_SaveErase` -13%, the two rows already known to flip with code layout and process placement |

The end-to-end workload (400-shot deterministic `tilt-target` at frame 3330, seed 3, 100
pellets per shot; old `Revert` -> new, back-to-back on the same machine, counts summed over
threads as `bitfs-turn` prints them):

| Run | Validation failures | Wall | Frame advances | Saves | Loads |
|---|---|---|---|---|---|
| 4 threads, lightweight | 10 -> 0 | 147 -> 157 s | 10.88 M -> 11.97 M (+10%) | 26.3 k -> 28.9 k (+10%) | 676 k -> 710 k (+5%) |
| 4 threads, full saves | 0 -> 0 | 214 -> 215 s | 11.70 M -> 12.04 M (+3%) | 2.7 k -> 2.9 k (+9%) | 716 k -> 710 k (-1%) |
| 1 thread, lightweight | 5 to 8 -> 0 | 283 to 291 s -> 329 s | (binary without counters) | | |

Read the counts, not the wall column: the same binary ran the full-save workload in 249 s
and 215 s an hour apart, so wall time swings by up to 15% here. The extra frame advances are
replays that used to stop at a save made with reverted inputs, and the old runs also skipped
the pellets of every failed shot (10 of 400 in the lightweight run). Saves rise because a
pellet's automatic saves are now dropped with the pellet and the next one earns its own.

What the fix buys besides correctness: the lightweight and full-save runs now perform the
identical search (26 solutions and 709,843 loads in both), so the outcome of a deterministic
run no longer depends on which savestates the cost model happened to create. Before, the two
modes found 27 and 38 solutions from the same seed.

## 2026-09-08: recycled savestate buffers (ROADMAP 3.9) and the first Tier B benchmarks

`SlotManager` used to construct a fresh `TState` for every save (`save()` then grew empty
vectors, zero-filling them) and destroy it on erase or eviction. Erased and evicted states
now go to a bounded pool (32 states, pooled memory counted against the slot budget) and the
next `CreateSlot` reuses one, so the save is a single copy into already-sized buffers.

`dllcheck`, MSVC, single thread (before -> after):

| Primitive | Save | Load |
|---|---|---|
| Full (7.3 MB) | 1561 -> 191 us | 197 -> 222 us |
| Lightweight (1.5 MB) | 285 -> 50 us | 42 -> 53 us |

The Tier B family that now gates this (`bench_libsm64.cpp`, fastest of nine, MSVC / clang-cl):

| Benchmark | MSVC | clang-cl | allocs/iter |
|---|---|---|---|
| `LibSm64Full_SaveErase` (recycled) | 189 us | 192 us | 3 |
| `LibSm64Full_SaveFresh` | 1.26 ms | 1.26 ms | 5.25 |
| `LibSm64Full_Load` | 189 us | 189 us | 1 |
| `LibSm64Light_SaveErase` (recycled) | 42 us | 44 us | 3 |
| `LibSm64Light_SaveFresh` | 278 us | 284 us | 5.07 |
| `LibSm64Light_Load` | 44 us | 41 us | 1 |
| `LibSm64*_FrameAdvance` (neutral inputs at frame 3330) | 14.3 us | 14.2 us | 0 |

A recycled save now costs the same as a load in both modes (the 3.9 bar was 2x). The three
allocations per recycled save are the slot's map node and the two access-order map nodes;
the fresh save adds the two buffers (and their growth). Tier A on MSVC: 0 regressions, 10
improvements, among them `SlotManager` create + erase -22 to -56% and create at the cap -15
to -26% (the fake state is 256 bytes, so that is the map churn the pool removes, not the
copy). clang-cl: the same `SlotManager` gains, and four rows the pool does not touch read +12 to
+19% against the previous baseline. Re-run: `LoadSlot` at 100 slots flips between 93 and
104 ns from process to process (the bimodality described under noise control), while
`GetInputs` uncached at depths 1, 4 and 16 is stable at 200 / 226 / 333 ns, against
169 / 202 / 296 in the previous baseline and 204 / 246 / 406 before the 3.7 work. That
loop calls nothing the change touched and MSVC's rows did not move, so this is the
code-layout sensitivity noted under noise control; the baseline was re-saved with the new
values.

## 2026-09-07: allocation-free ad-hoc levels, cheaper child scripts and trackers (ROADMAP 3.7)

Measured first with the new allocation counter: a frame advance with a trivial tracker cost
64 heap allocations on MSVC, an empty child script 53, an empty `ExecuteAdhoc` 9. MSVC's
`std::map` allocates a head node in its constructor, a `Script` holds six such containers
per ad-hoc level, and every pop replaced a level by assigning a fresh `T()`. Changes:
`LevelStack` constructs every level (including 0) on first use and resets popped levels in
place (`clear()`, `BaseScriptStatus::Reset()`); `ExecuteAdhocBase` and `Initialize` no
longer pre-create containers; `Revert`/`ApplyChildDiff` take the child's save bank as a
pointer that is null when the child never saved; tracked-state entries are created on first
insert and looked up with `find()`; the `dynamic_cast` in `GetTrackedState` became a type-tag
compare; statuses are moved out of finished scripts and `GetTrackedState` returns a
reference. While at it, a `SlotHandle` move that copied the slot id (so every save a child
handed to its parent on `Modify` was erased by the child's bank and later replayed) was
fixed and pinned by a test, and `LevelStack::operator[]` was split so MSVC keeps inlining it
(docs/compilers.md).

Before and after are both measured with the counting binary, fastest of nine:

| Benchmark | allocs/iter | MSVC | clang-cl |
|---|---|---|---|
| Frame advance with a trivial tracker | 64 → 14 | 2.18 → 0.83 us (-62%) | 2.41 → 0.96 us (-60%) |
| Frame advance with a recursive tracker | 65 → 19 | 2.36 → 1.08 us (-54%) | 2.72 → 1.26 us (-54%) |
| `Execute<EmptyScript>` | 53 → 11 | 1.48 → 0.67 us (-55%) | 2.37 → 0.43 us (-82%) |
| `Execute` / `Modify<OneFrameScript>` | 62 → 31 | 1.97 / 2.06 → 1.13 / 1.17 us (-43%) | 2.05 / 2.15 → 1.02 / 1.14 us (-50% / -47%) |
| `ExecuteAdhoc`, one frame + revert | 15 → 5 | 571 → 296 ns (-48%) | 566 → 271 ns (-52%) |
| `ModifyAdhoc`, one frame | 15 → 5 | 608 → 328 ns (-46%) | 657 → 358 ns (-46%) |
| `ExecuteAdhoc`, empty lambda | 9 → 2 | 262 → 283 ns (+8%) | 248 → 81 ns (-68%) |
| `AdvanceFrameWrite` / `AdvanceFrameRead` | 2 / 1 | 179 / 259 → 170 / 229 ns | 252 / 354 → 224 / 366 ns |
| Write one frame + `Load` back | 3 | 202 → 176 ns (-13%) | 215 → 171 ns (-21%) |
| `LongLoad` to root and back, depth 1 / 16 | 1 | 243 / 4130 → 209 / 3880 ns | 252 / 4390 → 194 / 2630 ns |
| `GetInputs` uncached, depth 1 / 16 | 1 | 126 / 322 → 138 / 315 ns | 204 / 406 → 169 / 296 ns |

Clang-cl: 16 improvements, 0 regressions. MSVC: 10 improvements; the two `GetInputs` depth
1 and 4 rows read +10% against an unusually fast before-run but are 8% and 4% faster than
the committed baseline, and the empty `ExecuteAdhoc` is the one MSVC case that got slower:
its two remaining allocations are MSVC's `std::map` move constructor (it gives the moved-from
map a new head) and the returned status's own diff, which cost more than the seven it no
longer does. All 616 test assertions unchanged on both compilers. Other families: all
within noise, allocation counts identical.

Two effects of the counting binary itself, visible against the previous baselines and now
baked into the re-saved ones: `SlotManager_CreateErase/100` measures 350 ns in the suite and
215 ns standalone (the per-process heap-layout effect described above), and clang-cl's
`Execute<EmptyScript>` rose from 1.5 to 2.4 us before any core change, consistent with
clang eliding new/delete pairs when `operator new` is the library's and no longer being able
to once a replacement is visible under LTO. Both compilers now measure the un-elided cost,
which is the honest one for code that runs on MSVC as well.

What remains in a tracked frame (0.8 us, 14 allocations): the head nodes MSVC allocates for
the containers the tracker actually touches, one `inputsCache` node per frame, the
`unordered_map` entry for the tracker's own tracked states, and the moved status. A flat or
pooled per-level container would remove most of it (ROADMAP 3.7, remaining).

## 2026-09-07: per-level bookkeeping as a `LevelStack` (ROADMAP 3.7)

`Script` kept six `std::unordered_map<int64_t, ...>` keyed by ad-hoc level, hashed on every
access and allocated on every level push. Levels are a stack, so they are now a
`LevelStack<T>` (`tasfw/LevelStack.hpp`): level 0 inline, higher levels heap-allocated once
and reused after pop. Tier A, MSVC, fastest of nine:

| Benchmark | before | after | delta |
|---|---|---|---|
| `ExecuteAdhoc`, empty lambda | 619 ns | 271 ns | -56% |
| `ExecuteAdhoc`, one frame + revert | 843 ns | 575 ns | -32% |
| `Execute<EmptyScript>` | 2.29 us | 1.56 us | -32% |
| `Execute<OneFrameScript>` | 2.69 us | 2.05 us | -24% |
| Frame advance with a trivial state tracker | 2.83 us | 2.22 us | -22% |
| `AdvanceFrameWrite` / `AdvanceFrameRead` | 216 / 297 ns | 192 / 265 ns | -11% |
| Write one frame + `Load` back | 270 ns | 201 ns | -25% |
| `LongLoad` to root and back, depth 16 | 7.53 us | 4.06 us | -46% |
| `GetInputs` uncached, depth 16 | 482 ns | 328 ns | -32% |

15 improvements, 0 regressions; all 536 test assertions unchanged. What remains in a
tracked frame (about 2.2 us) is constructing the tracker script object (six `std::map`s,
each of which allocates a head node on MSVC's STL), its three lifecycle sandboxes, and
copying its `CustomStatus` (which for the real trackers holds `std::vector`s). Those are the
next targets.

## 2026-09-07: cached symbol pointers in `LibSm64` (ROADMAP 3.7)

`Script::SetInputs` resolved `gControllerPads` three times per frame, `advance()` resolved
`sm64_update` and `getCurrentFrame()` resolved `gGlobalTimer` on every call, all through
`GetProcAddress`. `Resource` gained `setInputs()`; `LibSm64` caches the three pointers at
construction. Measured `GetProcAddress` cost on this DLL is 62 ns (`dllcheck` prints it), so
the frame-advance change is within noise (9.9 vs 9.7 us); hygiene, not a headline. Scripts
that call `addr()` per frame pay that 62 ns per call.

## First measurements (Tier A, 2026-09-07)

32-thread desktop at 3.0 GHz. MSVC 19.44 with `/arch:AVX2 /fp:precise /GL`; clang-cl 19.1 with
`-march=native -ffp-contract=off -flto=thin`. Fastest of nine pinned repetitions. Rounded.
These are the numbers to beat, and the ones that make "zero-cost" concrete.

| Operation | MSVC | clang-cl |
|---|---|---|
| `AdvanceFrameWrite`, root script, no tracker | 216 ns | 297 ns |
| `AdvanceFrameRead` (includes uncached input lookup) | 296 ns | 382 ns |
| `AdvanceFrameWrite` + `Save` | 0.94 us | 1.32 us |
| Write one frame, `Load` back to the previous save | 270 ns | 285 ns |
| `ExecuteAdhoc` with an empty lambda | 619 ns | 433 ns |
| `ExecuteAdhoc` writing one frame, then revert | 843 ns | 846 ns |
| `Execute<EmptyScript>` | 2.29 us | 2.11 us |
| `Execute<OneFrameScript>` | 2.69 us | 2.73 us |
| `AdvanceFrameWrite` with a trivial state tracker | 2.83 us | 3.08 us |
| `AdvanceFrameWrite` with a recursive state tracker | 3.08 us | 3.39 us |
| `GetInputs`, uncached, hierarchy depth 1 / 4 / 16 | 139 / 202 / 482 ns | 226 / 289 / 545 ns |
| `LongLoad` to root save and back, depth 1 / 4 / 16 | 0.36 / 0.88 / 7.5 us | 0.35 / 0.83 / 6.6 us |
| `SlotManager` create + erase at 100 / 10,000 live slots | 196 / 375 ns | 258 / 607 ns |
| `SlotManager` load at 1,000 / 10,000 live slots | 208 / 337 ns | 146 / 246 ns |
| `Scattershot::GetHash` on a 16-byte bin | 21 ns | 13 ns |
| `UpsertBlock` novel / redundant / improved | 104 / 67 / 95 ns | 100 / 64 / 93 ns |
| `Inputs::GetClosestInputByYawHau` (full magnitude) | 58 ns | 57 ns |
| `M64::save` / `M64::load`, 10,000 frames | 2.1 / 1.1 ms | 1.4 / 1.3 ms |

### First Tier B numbers (from `dllcheck`, 2026-09-07)

`dllcheck.exe res\sm64_jp_0.dll movies\bitfs-pyramid-jp.m64 3330 [--lightweight]`,
MSVC build, single thread, idle machine:

| Primitive | Cost |
|---|---|
| Frame advance (`sm64_update` plus `SetInputs`) | 9.5 to 10 us |
| Save, lightweight (1.5 MB) | 285 us |
| Load, lightweight | 42 us |
| Save, full (7.3 MB) | 1.4 ms |
| Load, full | 190 us |

A save cost 5 to 7 times its load in both modes at that point: every `SaveState` allocated and
zero-filled fresh vectors because slots were never reused. That is fixed (ROADMAP 3.9; the
2026-09-08 entry above has the after numbers): a save now costs about what a load does, so a
lightweight save is worth about 5 frame advances and a lightweight load about 5, which is what
the `shouldSave`/`shouldLoad` cost model is trading against.

What the Tier A and B numbers say together:

- The bare per-frame framework cost (216 ns on MSVC then, 170 ns now) is about 2% of a 10 us
  game frame. The hierarchy itself is close to zero-cost.
- A **state tracker cost about 2.6 us per frame** at that point, a quarter of a game frame,
  on every frame of every thread: every tracked frame instantiates a script, runs all three
  lifecycle phases inside ad-hoc sandboxes and reverts. The 3.7 entries above brought it
  to about 0.8 us.
- **Instantiating a child script cost about 2.2 us** even when it did nothing (0.7 us since
  3.7). Scripts that are run per frame (the downhill angle probes) pay this every time.
- `LongLoad` at depth 16 is 20x depth 1; the ancestor walk is linear and not free.
- Slot bookkeeping grows with live slots (three `std::map`s per slot).
- **The two compilers disagree by up to 60% on individual paths, in both directions**, with
  the same MSVC STL headers underneath. MSVC is ahead on the map-heavy slot and per-frame
  bookkeeping; clang is ahead on hashing, m64 writing, and ad-hoc sandbox setup. Any
  "optimization" measured on one compiler alone is suspect.
