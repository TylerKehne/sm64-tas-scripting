# Performance change log

Every hot-path change records its delta table here, newest first; the policy, the suite and
how to run it are in [performance.md](performance.md). The first measurements (2026-09-07),
which everything since is compared against, are at the bottom.

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
