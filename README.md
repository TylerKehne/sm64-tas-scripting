# sm64-tas-scripting
A C++23 framework for scripting and brute-forcing Super Mario 64 TAS inputs. The game runs
inside a native x64 DLL (wafel's libsm64); scripts drive it with savestates and frame
advances, and a multithreaded "scattershot" search explores input space on top of that.
The current driver is a squish-cancel setup brute forcer for the BitFS tilting pyramid.

Still very much a work in progress. Start with:

- [AGENTS.md](AGENTS.md): rules and conventions for anyone (human or AI) changing the code.
- [ARCHITECTURE.md](ARCHITECTURE.md): how scripts, savestates and scattershot fit together.
- [ROADMAP.md](ROADMAP.md): what is planned and in what order.
- [docs/libsm64.md](docs/libsm64.md): where the game DLL comes from and what depends on it.
- [docs/decomp.md](docs/decomp.md): what was copied from the decompilation, at which revision, and how the copies and the DLL are checked against each other.
- [docs/performance.md](docs/performance.md): performance is a correctness requirement; how it is measured and gated.
- [docs/performance-changelog.md](docs/performance-changelog.md): the measured history, one delta table per hot-path change.
- [docs/tasing.md](docs/tasing.md): how to TAS with the framework: which tool to reach for, how a goal becomes a script and a run, how a result is checked.

These files are kept current by design: a Claude Code hook in [.claude/](.claude/) reviews
them after every change (see AGENTS.md, "Documentation must match the repository").

# Building instructions
CMake 3.22+ and a C++23 compiler with OpenMP (MSVC 2022, GCC 14 or Clang 18 and newer). The dependencies (nlohmann/json; doctest and
Google Benchmark for the tests and benchmarks) are downloaded by CMake, verified by hash and
cached in `build\downloads`, so later builds work offline; no vcpkg needed.
`python scripts\fetch_deps.py` fills that directory with retries, reading the names, URLs
and hashes from the CMake files; CI restores the directory from its cache and runs it
before configuring, so a run does not fail on the 504 GitHub's downloads return now and
then, and it is how to prepare an offline build.

**Windows (supported path)**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1            # Debug
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Release
```

The script finds the latest Visual Studio (2026 or 2022) with vswhere, imports the x64
developer environment,
uses the cmake/ninja bundled with Visual Studio if none are on PATH, and configures from the
`CMakePresets.json` preset for the compiler and config (`msvc-release`, or `clang-cl-release`
with `-Compiler clang`). Output goes to `build\<Config>\out\bitfs-turn.exe`
(`build\<Config>-clang` for clang-cl). Visual Studio and VS Code pick up the same presets
when you open the folder, and `-CMakeArgs '-DTASFW_WARNINGS_AS_ERRORS=ON'` reproduces the
CI build.

Tests: `powershell -ExecutionPolicy Bypass -File scripts\test.ps1`. Benchmarks:
`scripts\perf.ps1`. Both accept `-Compiler clang` for the clang-cl build.

**Linux** (builds and passes the DLL-free tests in CI with GCC 14 and Clang 18; the game
path passes the smoke test against a Linux libsm64 `.so` on Ubuntu 26.04, which the `.so`
needs for its glibc, see [docs/libsm64.md](docs/libsm64.md); there is no macOS preset)

```bash
cmake --preset gcc-release          # or clang-release; build/Release-gcc, build/Release-clang
cmake --build --preset gcc-release
ctest --preset gcc-release
```

# Runtime inputs
The executable needs files that are not in git (see [docs/libsm64.md](docs/libsm64.md)):

- `res/sm64_jp_0.dll` through `res/sm64_jp_23.dll`, one copy of the libsm64 DLL per thread
  (on Linux `.so` copies and `"dllPattern": "sm64_{version}_{}.so"`). One command makes them from
  a ROM: `python scripts\unlock_libsm64.py --rom <sm64 jp>.z64 --out res --copies 24`
  (docs/libsm64.md); never commit a ROM or an unlocked binary.
- The source movie `config.json` names is committed under `movies/` (`bitfs-pyramid-jp.m64`,
  which CI, the tests and the perf suite use too), beside `bitfs-osc-final-jp.m64`, the
  oscillations done by hand to frame 3604, which nothing runs today (it was the source of a
  final-oscillation experiment), and two US movies: `1keyU.m64`, a whole 1-key run,
  and `bitfs-pyramid-us.m64`, the JP movie's BitFS part on the US way there, which the tests
  run on the US game (`unlock_libsm64.py --version us` makes `res/sm64_us_0.dll`) when it is
  there (docs/libsm64.md, "A movie for the US game").

# Running the pipeline

```
bitfs-turn [--config <file>] [--stage <name>] [--list] [--dry-run]
```

`bitfs-turn.exe` runs the BitFS squish-cancel pipeline described by its config: every
stage in order, or one stage with `--stage <name>`. Without `--config` it reads the
`config.json` next to the executable, which the build writes from
[tasfw-bruteforcers/bitfs-turnaround/config.json](tasfw-bruteforcers/bitfs-turnaround/config.json)
whenever the executable target builds, so a Visual Studio play button, which builds only
that target, keeps the copy current too.
`--list` prints the stage types and the configured stages; `--dry-run` resolves every path,
checks the files exist, loads one DLL, runs the `VerifyLayout` script to the first stage's
start frame and prints its report (struct layout, the hardcoded object slots), exiting 1 on
any `FAIL`; a real run makes the same check before its first stage. Both are safe. `--test`
runs the executable's own optional tests on the config's game (the `are-fix` stage solving
within its tolerance without an A press, with the fixer's float model reproducing the game's
resting normal; the least errors a target admits, no game needed; the `dr` stage's leg
from its hand-over, and the first
oscillation coming full from the fixer's hand-over in every corner, the config's target normal
mirrored into each, one thread, deterministic) and hands everything after it to doctest;
neither CI nor the framework's suite runs them. A full
run is not a smoke test: 16 threads, hours, thousands of exported `.m64` files, and the
viewer's window with a tab per stage and per `dr` pass ("The viewer").

Each stage writes its solutions to `<outputDirectory>/solutions/<stage>.json` (input diffs
plus named metrics). A stage run alone reads its input from the file its input stage wrote
last time, so a long pipeline can be advanced one stage at a time, and a search that needs
more shots continues as a new stage entry of the same type with `input` set to the earlier
one: its solutions become the new run's root blocks. The blocks themselves are never saved
(ROADMAP 4.2), and solutions are written when a stage finishes, so an interrupted stage
writes nothing (a cancellation that exports what was found is a Phase 5 item). Stages with
`"export": true` also write one movie per solution under `<outputDirectory>/m64/<stage>/`.
Scattershot CSVs and the `error.m64` dump go to `<outputDirectory>` as well, and a stage
with a `visualize` block opens the viewer on its CSV as it is written ("The viewer"). The stage
summary prints wall time and the frame advances, saves and loads summed over threads, which
are the fixed-workload numbers a performance change has to report (AGENTS.md, hard rule 8),
then the slot manager's line: the most savestates and bytes any thread held at once, and the
evictions and pool reuses (what the per-thread memory cap is up against; ROADMAP 3.5), and
where the threads' CPU time went: the resource's own advance, save and load as shares of
the process CPU time over the stage, with the cost of each, and the share outside the
resource, which is the framework, the scripts and the search (docs/performance.md, "Tier D").

# Configuration

One JSON file. Relative paths resolve against the file's own directory, unless the file has
`"baseDirectory"` (the build sets it in the copy next to the executable). Keys starting with
`_` are comments; any other unknown key is an error, with the location in the message.

```json
{
	"resources": { "dllDirectory": "../../res", "dllPattern": "sm64_{version}_{}.dll", "threads": 16, "saveMode": "fixed" },
	"m64": "../../res/source.m64",
	"outputDirectory": "../../analysis",
	"visualizer": "../../analysis/visualizer.py",
	"visualize": { "view": { "x": -715, "y": -1945, "width": 900, "height": 900 } },
	"scattershot": { "maxShots": 3000, "maxSolutions": 100, "seed": 6, "deterministic": false, "...": "any Configuration field" },
	"stages": [
		{
			"name": "are-fix", "type": "are-fixer", "startFrame": 3250,
			"args": { "targetNx": -0.1792, "targetNz": 0.3929998875, "tolerance": 0, "quadrant": 4, "minXzSum": 0.5 },
			"export": true
		},
		{
			"name": "dr", "type": "dr-oscillations", "startFrame": 3250, "input": "are-fix",
			"scattershot": { "maxSolutions": 100, "csvSamplePeriod": 10 },
			"args": { "equilibriumFrame": "input:handoverFrame", "quadrant": 4, "platform": 84,
			          "targetNx": -0.1792, "targetNz": 0.3929998875, "maxOscillations": 5,
			          "normalSpecs": { "startXzSum": 0.6, "minXzSum": 0.69, "minAxis": 0.05, "...": "see config.json" } },
			"select": { "sortBy": ["fSpd"], "take": 10 },
			"visualize": { "filters": [{ "column": "MarioFSpd", "max": 8 }] },
			"export": true
		}
	]
}
```

- `resources`: the DLL directory and file pattern (`{version}` becomes the game the movie's
  header names, `jp` or `us`, so the DLLs follow the movie; `{}` the thread index, one copy
  per thread), the thread count, the save mode (`saveMode`: `full`, `fixed` or `dirty`;
  `dirty` when absent, `fixed` in the committed config because it measures faster for this
  search; docs/libsm64.md, "Savestates"), `costModel` (default
  true; false disables the replay-versus-load cost model so a run is timing-independent,
  for diagnosis), and `savestateBudgetMB` (default 8192), the process budget for savestate
  memory: a resource subtracts its limit from the balance when it is created, whether it
  ever uses it or not, or fails if the balance is too low. The pipeline gives each thread's
  game resource an equal share of the budget less the 16 MB a script's `PyramidUpdate`
  takes per thread. The default is what one thread alone was allowed before the budget
  existed; the BitFS stages hold at most 27 savestates per thread (38 MB of `fixed`
  slices), and the stage summary's slot line shows the high-water mark and any eviction.
- `scattershot`: defaults for every stage, in the field names of `Configuration`
  (`pelletMaxScripts`, `pelletMaxFrameDistance`, `maxBlocks`, `maxShots`, `pelletsPerShot`,
  `shotsPerUpdate`, `startFromRootEveryNShots`, `maxConsecutiveFailedPellets`,
  `maxSolutions`, `seed`, `fitnessTieGoesToNewBlock`, `deterministic`, `csvSamplePeriod`).
  A stage's own `scattershot` object overrides them.
- `visualizer`: the viewer's path (`analysis/visualizer.py`), which a stage's `visualize`
  needs ("The viewer"). `visualize` at the top level holds defaults for every stage's
  block, overridden key by key by the stage's own; it enables nothing on its own. The
  committed one fixes a 900-by-900 window on the pyramid and names `MarioAction`
  categorical; the `dr` stage's own list adds `Oscillation`, `Crossing` and `Phase`.
- A stage has a unique `name`, a `type` (`bitfs-turn --list`), a `startFrame`, optionally its
  own `m64`, an `input` (the name of an earlier stage), `args` for the stage type, a `select`
  applied to its output, `export`, and a `visualize` block that opens the viewer on the
  stage's CSV: `title` (the stage's name unless given), `interpreter` (`pythonw` on Windows,
  `python3` elsewhere), the plot's columns `x`, `y`, `angle` and `speed` (`MarioZ`,
  `MarioX`, `MarioFYaw`, `MarioFSpd`), its bin sizes `binX`, `binY`, `binAngle` and
  `binSpeed` (0.1, 0.1, 16, 0.1), `filters` (`[{ "column", "min", "max" }]`, either bound
  optional), `categorical` (a list of column names: the columns the viewer's filter panel
  offers by value, a checkbox each; every other column is a range; a name the CSV lacks is
  an error in the viewer, as a filter's is, so a stage whose CSV lacks a column named at
  the top level names its own list), `view` (`{ "x", "y", "width", "height" }`: a fixed window of the plot's units
  centered on x, y, drawn square to its units and never rescaled; without one the plot
  follows its data) and `sampledOnly` (true). An empty block takes every default; only a
  stage with a `csvSamplePeriod` has a CSV to show.
- Numeric `args` accept a number or `"input:<metric>"`, which takes that metric from the
  first solution of the input stage (for example the equilibrium frame the tilt search found).
  The argument names of each type are checked in `Stages.cpp`; an unknown one is an error.
  Comments are allowed anywhere in the file (`//` to the end of the line, or `/* */`;
  the loader ignores them, and the build's copy keeps them), and a key starting with `_`
  is ignored too: the committed config carries a `_comment` per stage and a `//` comment
  on each parameter saying briefly what it controls.
- The `dr-oscillations` stage names the setup it works toward (ROADMAP 4.6): `platform`,
  the tilting pyramid's object slot (84, the one the movie's dive lands on, or 83;
  `BitFsObjects.hpp`, verified at start-up), `quadrant`, the corner as the signs of the
  normal's x and z there (1: +x +z, 2: +x -z, 3: -x -z, 4: -x +z), and in `normalSpecs`
  the tilt (`|nX| + |nZ|`) from which the oscillation's regime holds, `startXzSum` (once
  the tilt has been there it may not fall below it, and it may never fall more than 0.05
  below the rest's own, and below it, once it has begun to climb, it may sit at most one
  shift, 0.02, under its highest yet; `minXzSum` by default; the first pass runs the swing's first leg
  from the rest, which keeps the rest's own tilt, and the swings' ends build it up), the
  old lineup's handover, `handoverXzSum` (0, none; above the rest's own tilt the first pass
  is the old lineup instead, random sticks with the leg and the corner run mixed in,
  running the tilt up to it and handing over there with speed and leads, which is rare, 3
  in 10,000 shots from the 0.573 rest at 0.69, but whose oscillation then comes easily;
  from a low rest, the fixer's `minXzSum` 0.3, the lineup to 0.5 or 0.6 is quick, 100 in
  about 40 shots, and the oscillation follows it under a 0.67 floor when the stage's
  `maxSolutions` lets the lineup pass keep hundreds of lineups rather than the first
  hundred, whose roots are alike; ROADMAP 4.6), the tilt the last pass's
  solutions must have, `minXzSum`, and how far
  into the corner's
  quadrant each axis of the normal must stay, `minAxis` (0.05 in the committed config: the
  fixer's lattice steps reversibly down to its `farNormal` past the origin, and a floor at
  its `minNormal`, 0.13, leaves the first swing too few frames; without a floor the search
  roams the platform instead of swinging; lowering it to 0.02 or 0 changes nothing, since
  what ends the first swing's slow populations is the lava under the sinking +z end, a
  race the search wins about one time in two, which is where the stage's runs still part,
  ROADMAP 4.6); the ceiling the tilt of a solution carried into a next pass may not
  exceed, `maxXzSum` (2 in the committed config, none: past the lava's limit a root
  cannot be continued, a solution that is not one by the maintainer's rule, and roots at
  0.753 gave the next pass nothing where 0.71 gave 100 to 583, but a ceiling of 0.733
  starved the fourth and fifth passes instead, since the sum climbs about 0.02 an
  oscillation, 1 run in 3 through against about 17 in 20 without; the last pass's
  solutions are exempt either way, being the next stage's to judge); and the least height
  of Mario over the lava a state may have, `minLavaClearance` (0 in the committed config,
  none: lower, on a sinking end, the surface is under him before the swing can leave it,
  which is how the first swing's slow populations die, but at 20 the first oscillation
  came 7 runs in 14, no more often than without, so the rule prunes drowning states
  without steering the search to the lineage that leaves); and how far the first swing
  may fall back from the leg's end along the way to its target before the state is
  refused, `maxRetreat` (absent in the committed config, no limit): the leg ends on the
  sinking end, downhill is toward it, and the search shoots every block alike, so a
  population that runs on there fills the table, drowns, and starves the lineage that
  turned, and the rule refuses those states from their first frame; measured on 16
  sequential runs a setting it does not move the first oscillation (8 full and 3 thin
  of 16 without, 8 and 1 at 90; 60 cuts the turnaround's own overshoot of up to 90
  units, 3 of 16), so the table it starves the search of is not what the lineage that
  leaves was missing; the forward speed at which the leg from the rest ends and the first
  swing begins, `legExitSpeed` (17.5 in the committed config; 16, the game's turnaround
  threshold, by default): the census of 2026-10-04 inside the first-oscillation pass found
  that the swing crosses only when it reverses within a frame or two of the leg's end, an
  uphill frame that costs about a unit of speed and then the turnaround the game refuses
  under 16, before the lead-steered run carries Mario out to the +x side where no crossing
  comes before the sinking end is under him, so a leg handed over at 16 cannot, and the
  first swing draws the uphill turn and the turnaround as often as the run, where the
  later swings keep the run's weight (three times in four was measured no better); the
  first swing may begin its turnaround from any frame, where every later swing turns
  only from a frame the slope took speed on, since from the rest's equilibrium the chord
  ahead is downhill and no such frame comes on it (with the rule, the lineages left the
  chord to find one and drowned; free of it, 16 plain runs of 16 found the first
  oscillation full against 6, 2026-10-04); and
  below the regime the search's block bin holds the swing's two leads, its speed and the
  tilt's progress beside Mario's cell, so lineages whose crossing differs by a frame are
  kept apart instead of the fastest owning the cell (2026-10-04: with it 16 plain runs of
  16 found the first oscillation, 13 full, against 15 and 9).
  `legMinClearance`, the least height of Mario over the lava at the leg's end for the leg
  to count as a solution (0, none, in the committed config), is a diagnostic: at 60 three
  deterministic seeds of four found no first oscillation at all where all four do without
  it, so the leg's height is not the lever (a return after the turnaround that ran straight at
  the chord's far end instead of the downhill angle was tried on top and is worse, five
  seeds of six finding no first oscillation, so the return keeps the downhill run;
  ROADMAP 4.6); `minFirstCrossingSpeed` asks a forward speed of
  Mario at the first crossing (the "gain speed each crossing" rule ratchets up from
  whatever it is; 0 asks none): 17 in the committed config, since under the turnaround's
  16 the swing cannot reverse for the next oscillation, and of 58 logged runs every
  hand-over whose fastest root was under 16.1 died and every one over 17.5 continued; the
  later hand-overs part on the tilt sum instead, with a ceiling near 0.73 (ROADMAP 4.6).
  Every swing, until its turnaround, is steered by the leads rather than
  the downhill angle (`LeadRun_1f`: of a fan of sticks around the face yaw, a quarter turn
  either side, the one that leaves the thinner lead largest among those that keep both
  axes stepping; the first swing is free to reverse at any speed, without the turnaround
  action's 16), since the downhill-angle moves run the axis a swing steps outward dry and
  the swing dies at its turn, from the rest and in every oscillation after (ROADMAP 4.6).
  The stage conserves the
  fixer's adjusted remainder error exactly from the hand-over frame on, which needs set
  values whose 0.01 steps round back exactly across the tilts the oscillations use: the
  `are-fixer` stage sets only such, its `minNormal` and `maxNormal` (0.13 and 0.61)
  naming that range in magnitude on each axis and `farNormal` (0.02) how far past the
  origin into the adjacent corner the final oscillation takes an axis; when a target lies
  past the origin the walk reaches it whatever `farNormal` says (every value that survives
  the oscillation's side crosses the origin intact; the far side's own binades stop x at
  0.24 and z at 0.02 for the config's targets). Not every error survives those crossings,
  and which do is the target float's own residue: from the setup's former x target, -0.17944f,
  only the errors 1, -3, 5, ... ULPs step reversibly (every fourth), from its z target the
  even ones, so `tolerance` 0, an exact match, cannot be held on x there (one ULP more
  negative it could). The fixer's validation settles this before any frame, in the game's
  own arithmetic: the least error each axis admits (`LeastError`); none within the
  tolerance, and the stage prints why with the least errors. The target is never replaced by
  a neighbouring float that would hold: a target the arithmetic cannot hold, or one the
  search does not set within the tolerance, is a refusal (the maintainer, 2026-10-06), and
  the choice of target stays the config's. The fixer then sets the two
  axes one at a time, since an axis's error is the goal's float on the last frame the
  pyramid's normal snapped to it and the goal is a function of Mario's position alone: x
  by a rest, the rollout's landing swept for one whose rest puts the x goal on the right
  float (a curve of positions near the rest asked for; the settle after a landing rounds
  rests onto a staircase coarser than the floats and about one rest in five near the curve
  lies on it, a landing left idle failing being tried again with a walk after it, a frame
  or two of stick before the settle, `Walks`), the constant stick aimed at the rest asked
  for by a Newton on the rollout and its settle with the stick whose effect is nearest the
  aim among the distinct sticks (the yaw lookups return one ray's few magnitudes); then z
  by the turn out of a short run from that rest along x, x stepping and z snapping until
  Mario turns toward the chord the oscillation's first leg runs, the frame z last snaps on
  setting it from his position the frame before, which the run's last frames' sticks
  place: each stick's effect measured end to end, every combination's error predicted by
  the goal's slope and computed exactly on the predicted floats, the nearest played and
  re-ranked by the median offset of the plays' actual errors (the carry's float noise,
  some hundred ULPs) until one sets z, one more run frame flipping x's step parity when
  the parities differ. The hand-over, the last frame z snapped on, is where the
  oscillation stage starts (`handoverFrame`), Mario running with both axes stepping in
  the leg's directions (z the steeper axis, so the leg runs x toward the corner and z
  away): the config's targets hand over at frame 3336 in about 93,000 frame advances and 2
  s, and of the 24 survivor pairs nearest them 24 hand over (ROADMAP 4.8, which also records what
  was measured and dropped on the way: the lattice of positions a rest had to hit exactly,
  and the dive slide to a stop, chaotic at the float scale). The hand-over is within 20
  units of the rest asked for, the corner's diagonal unless `restX`/`restZ` name a point,
  since the swings after the first do not
  cross from further off (measured 2026-10-05: through at its usual rate to about 20
  units, slipping near 30, mostly failing at 40). The hand-over's normal is in the corner `quadrant` names (the signs of
  its x and z there, as the oscillation stage counts them; the target's own corner when
  absent, and the error matched is the target's wherever the hand-over lies), and its tilt
  cross from a rest further off (measured 2026-10-05: through at its usual rate to about 20
  units, slipping near 30, mostly failing at 40), and validation refuses when none holds the
  error there: -0.1792 and 0.393 have one such position, 12.8 units off, which no landing
  reaches (the run ends unsolved in 5 s), where 0.3929998875 for z, four ULPs off, has one
  the fixer lands at in about 77,000 frame advances; ±100 takes the first cell. The rest's normal is in the corner `quadrant` names (the signs of
  its x and z there, as the oscillation stage counts them; the target's own corner when
  absent, and the error matched is the target's wherever the rest lies), and its tilt
  at least the fixer's `minXzSum` (stated, never derived: 0.553 in the config, the target
  normal's own sum less 0.02), so the oscillation starts near its regime; from idle a stick
  gives 8 speed and the first frame must step both axes toward the chord's end, which the
  slope's own push allows only on the downhill side of the chord's perpendicular and for
  a few degrees past it, so a rest off the corner's diagonal has a first frame toward one
  end only; and from any rest at a tilt of 0.65 or more the first oscillation is not found
  at all, since a crossing has to come before the normal's steepness reaches the lava's
  limit (ROADMAP 4.6 has the numbers). The way to
  the rest is the fixer's own (the maintainer, 2026-09-23: the dive is never a limit, and
  the movement is not the config's to spell out): it asks for the corner's diagonal at the
  radius the tilt floor needs (`restX`, `restZ` override that), plays every way onto the
  platform once with a straight rollout to its rest (from the run before the movie's dive:
  the run's length, the dive's yaw, the movie's or up to four steps of 1024 to either
  side, and its air stick, back, neutral or at the yaw, about 280, 340 and 500 units of
  dive; from a dive slide, the slide frames only, 0 to 3), and runs the landing search at
  each position with the three ways resting nearest it, bringing the rest to the position
  through the rest's measured response to the landing, keeping the first that solves. The
  positions nearest the rest asked for come first because that is what the oscillation
  wants: at tilt 0.6 a rest on the diagonal crosses (100 first oscillations in 3,000 shots)
  and one 20 units to either side does not (ROADMAP 4.6). From the movie's own dive
  slide (frame 3269) the rollout reaches tilts up to about 0.6 only; from the run before
  the dive (3250, the committed start) 0.67 and beyond. The final oscillation runs from the oscillation's corner along an
  edge to the adjacent corner, where the squish cancel is: `uscz` along a z edge, so the
  normal's x crosses the origin, `uscx` along an x edge, so z does; the committed setup
  oscillates in corner 4 and runs toward corner 1 along the +z edge, its target's x
  (-0.179) still on the oscillation's side. Between its passes the stage carries the
  `keepTop` (10) fastest solutions into the next, and after the first oscillation it commits
  to the direction most of those took, dropping the other direction's solutions (which
  would need one oscillation more); a run's solutions arrive in block order, so the first
  of them says nothing, and committing to its direction was what made runs fail one time in
  two (2026-10-03: one to three slow solutions of the minority direction carried, the next
  pass dead on them). A pass that yields fewer than `keepTop` solutions (the last pass,
  none) runs again from the same roots under a seed derived from the run's, up to
  `passRetries` times (0 in the committed config, none), its solutions pooled: with 3,
  the first oscillation, which comes about one run in two, ended no run in 16 (13 through
  against 11 without; the three failures were the last pass at the tilt ceiling), but a
  retry re-rolls that lottery rather than removing it, so the committed config asks none
  and the knob is a diagnostic; the cause is ROADMAP 4.6's open item. After each pass the stage prints what it found and the
  roots it carries, their speed, tilt and oscillation, and each retry, which is what to
  read when a run stalls.

The committed config is the pipeline as it stands: the ARE fixer (ROADMAP 4.8), which
sets the adjusted remainder errors the setup needs on the pyramid's normal, one axis at a
time, in two seconds and without a search, from the run before the movie's dive onto the
platform (frame 3250), and hands Mario over running; then
the oscillations, the final oscillation and the dive-recover chain, every stage from 3250, with
a CSV and the viewer on every search stage (every tenth novel block).

# The viewer

`analysis/visualizer.py` plots a run's CSV live: an arrow per state bin at Mario's position,
pointing along his facing yaw with his speed as its length, colored from orange for the
oldest shot to green for the newest (the CSV's `Shot` is each thread's own count, so the
first minute of a run is one shot and all green) and fading with the frame. A stage with a
`visualize` block
starts it when the run's CSV opens: the search writes the plot's parameters and the CSV's
path to `<csv>.visualizer.json` beside the CSV and launches the viewer detached, so nothing
is run by hand. One window holds a tab per run: the first viewer owns a localhost port, a
later launch hands its run to it and exits, and a viewer left open collects the next
pipeline's runs too. A tab closes with the "Close tab" button, Ctrl+W or a middle click on
its header, and the window stays for the next run. A tab whose run configured a `view`
keeps that window on every redraw, square to its units; the Autoscale box applies to the
other tabs. Beside each tab's plot is its filter panel, in the manner of a shop's facets:
a section per CSV column in the CSV's order (`Shot` and `Frame` included). Every column
is a range, a min and a max box, either one empty for an open bound, applied with Enter,
under the least and greatest value seen, except the columns the run names as categorical
(the `visualize` block's `categorical`, written into the parameters file with the rest),
which have a checkbox per value with the number of sampled rows
holding it: checked values pass, and none checked passes every row. The counts and ranges
are over every sampled row of the run, whatever the filter, and grow as the run does; a
categorical column that passes 256 distinct values becomes a range, and its section says
so. The filters in force are listed at the top of the panel, each with
a button that drops it, above a rule that separates them from the sections; the list takes
only the room its lines need, and Clear all drops them all. The panel starts as the run's own
`filters`; every change re-reads the tab's CSV from the start under the new filter, at the
configured bin sizes, since the raw rows are not kept, the same cost an opening tab pays,
and a row that fails the filter is neither binned nor drawn. The filter is the tab's own
for the session and is written nowhere; the parameters file stays the search's. A min
above its max, or an entry that is not a number, is reported in the panel and changes
nothing; the plot's title names the filter in force. The Filters box in the window's bar
shows or hides the panel on every tab (kept in the settings file), and the sash between
panel and plot sets its width. Every cell of the CSV is a number; a row with one that is
not counts as unreadable. The refresh rate is set in the window and kept in
`analysis/visualizer_settings.json` with the port, the segment cap and whether the panels
show, and next to it the
window shows what the viewer costs: its own CPU time over the last 30 seconds as a share
of one core and of the machine, from the process clock, which resolves to about 0.05% of
one core over that window. The viewer reads
only what the CSV gained since its last look, redraws only the selected tab and only when
rows came in, runs at below-normal priority so the brute forcer's threads win any
contested core, stops polling a run that finished (the search rewrites the parameters
file with `finished` and the row count), doubles a tab's bins past the cap, and stretches
its interval so that a redraw is never more than 5% of the wait before it: a big tab
redraws less often, not more expensively.

Setup is none beyond `python` on PATH (3.9 or newer; the unlock script and the doc hook
need it already): on first start the viewer creates `analysis/.venv`, installs
`analysis/requirements.txt` (numpy, matplotlib) into it and re-executes itself from there;
later starts skip the install until the requirements file changes.
A small window says so while it installs, and `analysis/visualizer.log` has the details
when nothing appears (on Windows the viewer is two `pythonw` processes, the venv's launcher
and the interpreter it runs). `TASFW_VISUALIZER_NO_BOOTSTRAP=1` disables the install,
which is how CI runs it. On Linux the window needs `python3-tk`. Headless,
`python analysis/visualizer.py --once --csv <file> [--out <png>] [--rows N] [--filter TERMS]`
renders one PNG, which CI does on `tasfw-tests/data/scattershot_sample.csv`, plain and
under a `--filter` whose drawn and filtered counts it pins; `--filter`
takes a filter as text in place of the parameters file's `filters`: terms such as
`MarioFSpd <= 8`, `3300 <= Frame <= 3400`, `Phase == 5` or `Phase in 1 3 5`, ranges
inclusive, separated by commas or `and`, numbers decimal or hex. With
`TASFW_VISUALIZER_HEADLESS=1` in the environment the viewer tails a run with no window
instead, on the window's schedule with fixed defaults, and writes what it cost beside the
parameters file (`*.visualizer.summary.json`), which the perf suite gates
(docs/performance.md, "Tier D"); CI runs that mode too, on the sample with a finished
parameters file, and checks the summary and the image.

The CSV's columns are `Shot`, `Frame`, `Sampled` (1 for a row the sample period chose, 0 for
one the search forced) and then the stage type's own:

| Stage type | Columns after the first three |
|---|---|
| every type | `MarioX`, `MarioY`, `MarioZ`, `MarioFYaw`, `MarioFSpd`, `MarioAction`, `PlatNormX`, `PlatNormY`, `PlatNormZ` |
| `tilt-target` | plus `MarioYVel` |
| `osc-final` | plus `Phase`, `MarioYVel`, `NormalDistance` |
| `dr-oscillations` | plus `Oscillation`, `Crossing`, `Phase`, `XzSum` (the tilt: the magnitudes of `PlatNormX` and `PlatNormZ` added) |
| `dr-approach`, `dr-recover` | plus `Phase`, `MarioYVel` |

A `visualize` column or filter the stage does not emit is reported in the tab's title and the
log, and that tab stays empty.