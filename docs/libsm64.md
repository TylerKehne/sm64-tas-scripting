# libsm64: the game DLL

The framework does not emulate the N64. It loads a native x64 shared library (a `.dll` on
Windows, a `.so` on Linux) that is the SM64 decompilation compiled for the host, with two
extra exports:

- `sm64_init()`: initialise the game (called once from `LibSm64`'s constructor).
- `sm64_update()`: run one frame (called by `LibSm64::advance`).

Every game global (`gMarioState`, `gObjectPool`, behaviors, tables) is also exported by
name, which is how `Resource::addr` resolves symbols. The builds used here have about 9,000
exports.

## Where it comes from

The binaries are **libsm64 builds from wafel's lineage** (https://github.com/branpk/wafel).
They are *not* built from the decomp checkout at `C:\repos\sm64`; that checkout has no DLL
tooling and is only reference material.

Nobody distributes an unlocked build, because the game's assets are inside it. Both known
sources ship the binaries "locked" (Fernet-encrypted with a key derived from the vanilla ROM
by PBKDF2-SHA256) and unlock them on the user's machine:

- **wafel** ships `sm64_<version>.dll.locked` for Windows. The application unlocks them when
  pointed at a ROM, and `wafel/libsm64_lock` (a Rust binary; `cargo build --release` in the
  wafel repo) does it standalone:

  ```
  libsm64_lock --unlock -i sm64_jp.dll.locked -o sm64_jp.dll -r "<path to JP ROM>.z64"
  ```

  Wafel's locked files since 2021-06-15 are libsodium password boxes (`pwbox`, JSON with
  their own salt) that only this locker opens; a built copy is at
  `C:\repos\wafel\target\release\libsm64_lock.exe`, and wafel's git history holds every
  locked file it ever shipped, which is how the builds below were identified. Wafel 0.7.x
  locked with Fernet and a key derived from `wafel-<version>` plus the ROM; bitfs-sbb's
  script reproduces that derivation minus the version prefix, so the two families of
  locked files are not interchangeable.

- **bitfs-sbb** (https://github.com/jgcodes2020/bitfs-sbb, checkout at `C:\repos\bitfs-sbb`)
  ships `libsm64/data/win32/sm64_<version>.dll.locked` **and**
  `libsm64/data/linux/sm64_<version>.so.locked` for `jp`, `us`, `eu` and `sh`, plus
  `libsm64/fernet-lock.py`, a Python port of wafel's key derivation (`pip install
  cryptography`). ROMs go in `libsm64/roms/` (gitignored there) as `sm64-<version>.z64`;
  `.v64` and `.n64` byte orders are converted on the fly:

  ```
  cd C:\repos\bitfs-sbb\libsm64
  python fernet-lock.py unlock-all jp us        # this platform's binaries -> lib\sm64_jp.dll, lib\sm64_us.dll
  python fernet-lock.py unlock data/linux/sm64_jp.so.locked -k roms/sm64-jp.z64 -o lib/sm64_jp.so
  ```

  `unlock-all` picks the `data/<platform>/` directory of the machine it runs on, so the
  second form (explicit input, key and output) is how a Windows machine unlocks the Linux
  `.so`. The EU and Shindou builds needed audio patches and their author does not vouch for
  their accuracy; JP and US are the ones to use.

`scripts\unlock_libsm64.py` does the bitfs-sbb route in one command and is how a fresh
machine populates `res\`: it fetches the locked JP (or US) build for this platform from the
pinned bitfs-sbb commit into `build\downloads\`, checks its sha256, derives the key from a
ROM (or takes it from an environment variable, which is what CI does), unlocks, checks the
unlocked bytes against the sha256 recorded in the script, and writes N per-thread copies.
The derivation is wafel 0.7.1's (PBKDF2-HMAC-SHA256 over the z64 image, no salt, 10,000
iterations, urlsafe base64), standard library; the Fernet step needs `pip install
cryptography`:

```
python scripts\unlock_libsm64.py --rom <sm64 jp>.z64 --out res --copies 24     # res\sm64_jp_0.dll .. sm64_jp_23.dll
python scripts\unlock_libsm64.py --rom <sm64 jp>.z64 --print-key               # the value for the LIBSM64_KEY secret
python scripts\unlock_libsm64.py --key-env LIBSM64_KEY --platform linux --out res   # the .so, key from the environment
```

**Never commit a ROM or an unlocked binary, and do not vendor the locked ones either**; link
to wafel or bitfs-sbb (AGENTS.md hard rule 6). If CI ever needs the game, the unlock key or
the ROM goes in a maintainer-only repository secret so that forks and pull requests from
outside never see it, and the job skips when the secret is absent (ROADMAP 3.4).

## Known builds

Every build below has been run through `dllcheck` and, where a movie exists for it, through
the libsm64 test group (`scripts\test.ps1 -Dll <file> -Filter 'libsm64*'`): the layout
checks, the golden state at frame 3330 of `movies/bitfs-pyramid-jp.m64`, save/load
determinism, and the `PyramidUpdate` drift test (docs/performance.md, ROADMAP 3.3).

| Build | File | Size, MD5 | Status (2026-09-08) |
|---|---|---|---|
| wafel v0.8.1's libsm64, built 2021-06-17 (**pinned**) | `res\sm64_jp_0.dll` .. `_23.dll` | 32,958,049 B, `463a5be6` | The reference: every offset, check and golden value in this repo was taken from it. Provenance established 2026-09-13 by unlocking every `sm64_jp.dll.locked` in wafel's history with wafel's own locker: byte-identical to the file committed 2021-06-20 (`c155b258`) and shipped in v0.8.1 (`19262ea4`); v0.8.0's file is in an older lock format the current locker refuses. The PE header's build time is 2021-06-17 02:39 UTC; the "2022-03-12" this table used to say was a file date. |
| wafel's 2022-08-07 update (`e81ee16b`, after v0.8.5; never in a release), JP | `C:\repos\wafel\libsm64\sm64_jp.dll` | 31,872,634 B, `da7b3620` | Passes every layout check including fixed-slice coverage; identical golden state; drift test max diff 0 (MSVC). `dirty` mode: 125 pages per save, 7 us, leak scan zero bytes. Formerly listed as "wafel 2023-09-07", the checkout's date. |
| bitfs-sbb, JP (= wafel v0.8.5's libsm64, built 2022-06-26, re-locked with Fernet; the "2026-06-30" is the lock date) | `C:\repos\bitfs-sbb\libsm64\lib\sm64_jp.dll` | 33,068,386 B, `625fd921` | Byte-identical to what wafel's `1fa9a832` (v0.8.5) unlocks to. Passes every layout check including fixed-slice coverage; identical golden state; drift test max diff 0 on MSVC and clang-cl. `dllcheck`: 8.5 us per frame; `dirty` mode 123 pages per save, 7 to 8 us, leak scan zero bytes. Tier C exact counts and allocations identical to the pinned build's baseline (2026-09-13), which is what lets CI gate them on this build; its `DownhillAngle_PyramidUpdate` workload reads about 85% slower and `PyramidOscillation` 7% slower, the build's own code. |
| bitfs-sbb, US (= wafel v0.8.5's libsm64, re-locked) | `res\sm64_us_0.dll` (copied from `...\lib\sm64_us.dll`) | 33,508,967 B, `e2efd078` | Byte-identical to what wafel's `1fa9a832` unlocks to with the US ROM. With `movies/bitfs-pyramid-us.m64` at frame 3397 ("A movie for the US game" below): every layout check and the fixed-slice coverage, the same golden state as the JP movie's frame 3330, and the whole libsm64 test group including the drift test with max diff 0 (2026-09-13). `dllcheck`: 7.8 us per frame, `fixed` save and load 49 us. Its `.data` is 2,819,248 B against the JP builds' 2,388,752 and its `.bss` 4,884,240; the hot symbols sit inside the slices. |
| bitfs-sbb, 2026-06-30, JP, Linux | `res/sm64_jp_0.so` (copied from `...\lib\sm64_jp.so`) | 16,733,032 B, `06c58c69` | On Ubuntu 26.04: every layout check, identical golden state, drift test max diff 0 with GCC 15 and Clang 21 (see "Linux"). |
| bitfs-sbb, 2026-06-30, US, Linux | `res/sm64_us_0.so` | 17,168,016 B, `61e27e4f` | Loads; not yet run on the US movie. |

bitfs-sbb's Windows DLLs are wafel v0.8.5's libsm64, re-locked (both unlock to the same
bytes as wafel's own files); only its Linux `.so` builds are jgcodes2020's own, built on
EndeavourOS with GCC 16.1.1 (the `.so`'s `.comment` section says so) from a decomp
revision he did not record. Every build after v0.8.1 comes from a newer decomp than the
pinned DLL, which shows in two ways:
the renamed exports below, and `.data`/`.bss` layouts that differ from the pinned build by
tens of bytes on Windows (the hot symbols still fall inside the fixed slices) and by
hundreds of kilobytes on Linux (where the fixed slices do not fit and `dirty` is the mode; the `.so`'s
`.bss` is 3.6 MB against the DLL's 4.9 MB).

### Renamed symbols

The decomp renamed the tilting-pyramid behaviors after the pinned build:
`bhvBitfsTiltingInvertedPyramid` became `bhvBitFSTiltingInvertedPyramid` and
`bhvLllTiltingInvertedPyramid` became `bhvLLLTiltingInvertedPyramid`. The pinned DLL exports
only the old names, every later build only the new ones, so before 2026-09-08 every BitFS
script and the drift test threw "procedure not found" on anything newer than the pinned DLL.
Scripts keep using the pinned names. `LibSm64::addr` tries the name it is given and only
when that lookup fails consults `LibSm64SymbolAliases` (`LibSm64.hpp`) for the other
spelling, in either direction. The pinned DLL therefore pays nothing; a newer build pays one
extra failed lookup per `addr()` call, which is never per frame (`Resource::addr`). Add a
pair there when the decomp renames something else the framework uses.
`sm64_update_and_render` exists in the pinned and the v0.8.5 builds but not in wafel's
2022-08-07 one; nothing
here calls it.

## What is in `res/` today

| File | Notes |
|---|---|
| `sm64_jp_0.dll` .. `sm64_jp_23.dll` | 24 byte-identical copies of the pinned build. |
| `sm64_us_0.dll` | The US build (bitfs-sbb's), one copy: the US run of the libsm64 tests (`scripts\test.ps1`) and `m64splice`. |
| `sm64_jp_0.so`, `sm64_us_0.so` | bitfs-sbb's Linux builds, for the container runs in "Linux". |
| `comissonPyra2-Fanart_XZ.m64` | An unreferenced JP movie (3,382 frames). The two movies `config.json` names are committed under `movies/`: `bitfs-pyramid-jp.m64` (formerly `comissonPyra2-Fanart_x-Z.m64`; 3,804 frames, 16 KB; the tests, the perf suite and CI use it too) and `bitfs-osc-final-jp.m64` (formerly `test3.m64`; 3,726 frames; the `osc-final-test3` stage, which starts at frame 3604). |
| `bitfs_nut_*.m64` (thousands) | Exported solutions from past runs (new runs export under `analysis/m64/<stage>/`). Safe to delete. |

## Savestates

`LibSm64` treats the DLL's `.data` and `.bss` sections as the whole game state. How a save
represents them is `LibSm64Config::saveMode`, set once at construction (`"saveMode"` under
`"resources"` in `config.json`, `--save-mode` for `dllcheck`, `LibSm64SaveMode` in code) and
invisible to scripts: savestate management stays automatic whichever mode is set. `dirty`
is the default when nothing says otherwise; the committed pipeline config and the Tier D
workloads select `fixed`, which measures faster for the BitFS search (below).

| Mode | A save copies | Pinned DLL, MSVC | When |
|---|---|---|---|
| `full` | both sections whole, 7.3 MB | about 190 us per save or load | the reference; under a debugger (no page faults) |
| `fixed` | five hand-tuned byte ranges (`LibSm64FixedSlices`), 1.5 MB | about 41 to 49 us, constant | when a constant cost matters more than speed; the pinned build only |
| `dirty` (default) | the pages the game wrote since the current baseline: about 122 pages (488 KB) after 60 frames of play, up to 526 pages (2.1 MB) within a scattershot shot | about 7 us at 122 pages; the search's loads restore up to 2.1 MB from scattered pages, so the deterministic Tier D workload runs about 10% slower than in `fixed` and the 16-thread one about 25% slower | any build, Linux, exploration that stays near a settled state |

**Fixed** was found empirically for the pinned build. Construction refuses it, with the
reason, on a build whose sections are smaller than the slices (the Linux `.so`'s `.data` is
330 KB and its `.bss` 3.6 MB, against 2.4 MB and 4.9 MB in the DLL); on a build where they
fit, nothing at run time checks coverage: `dllcheck --save-mode fixed` reports whether every
hot symbol lies inside a range, and `--leak-scan` measures what a load misses; run both
before trusting `fixed` on a new build. On the pinned DLL `--leak-scan` shows a
Fixed load misses 4 to 6 bytes of lava texture scroll that nothing in the physics reads.

**Dirty** write-protects the pages of both sections and catches the first write to each
page in a fault handler (a vectored exception handler on Windows, `SIGSEGV` on Linux). A
baseline holds, for every page written since it began, the page's content as it was when
it began: the handler copies the page into every live baseline that lacks it before the
first write (copy-on-write; storage is one lazily touched buffer per live baseline). A save
copies the pages written since the current baseline began; a load writes them back and
restores every other page written since the state's baseline began from that baseline's
copy, so a load is exact by construction on any build, and taking a baseline copies
nothing. The resource takes a fresh baseline on its own, from what it observes and nothing
else, whenever it is asked to save while it holds no live slots: the start save every
top-level run begins with, and the first slot of a run, which is the save `LongLoad` makes
at the frame exploration starts from. From then on a save copies only what the run itself
writes. A baseline costs a first-write fault, and a page copy per live baseline, for every
page the game touches afterwards (about 120 in BitFS play, 525 during the replay to frame
3330), and the set grows with what the game writes until the next baseline: level loads,
deaths and warps make saves and loads bigger. The BitFS scattershot stage is such a run:
its pellets die and the level reloads, so each thread's set reaches 526 pages (2.1 MB)
within a shot and every load of the shot's base state restores that much from scattered
pages, against the 1.5 MB `fixed` copies from five contiguous ranges that stay
cache-resident; the stage runs about 10% slower than in `fixed` mode on the
deterministic Tier D workload and about 25% slower on the 16-thread one, identical
counts (docs/performance-changelog.md; `bitfs-turn` prints the set size and the number of
baselines per stage). Re-baselining again during a run, at a save once its loads had paid
for one, was measured and dropped: the set regrows within a shot whatever the baseline, and
with the cost model saving often the rule fired about a thousand times per thread. No
result depends on when a baseline is taken. Older states stay loadable: a state names the
baseline it was saved under, every baseline a live slot's state names keeps its pages, the
start save's is always kept, and the rest are released at the next baseline. Debuggers stop
on the deliberate first-write faults unless told not to; use `full` there.

## Checking a DLL: `dllcheck`

```
build\Release\out\dllcheck.exe <libsm64.dll> <movie.m64> <frame> [--save-mode full|fixed|dirty] [--leak-scan [frames]] [--objects] [--dirty-scan [frames]] [--dirty-replay] [--levels] [--trace [frames]]
```

Runs the `VerifyLayout` script (`tasfw-scripts/inc/VerifyLayout.hpp`) to `<frame>` (pick
one inside a level) and prints one `ok:`/`FAIL:` line per check, then replays the movie on
the resource itself and prints the measured cost of a frame advance and of a savestate
save/load. Exit code 0 means every check passed. The pipeline runs the same script once, on
one resource, before its first stage, so a wrong DLL fails at start-up with a readable
message instead of producing garbage searches.

What is checked: `gMarioState` points at `gMarioStates[0]`; Mario's object is a whole slot
of `gObjectPool`; its `behavior` is `bhvMario`; `MarioState::marioObj` points back at it;
`oPosX/Y/Z` and `header.gfx.pos` equal `MarioState::pos` (the game mirrors them every
frame); the floor normal is unit length and the floor's object pointer is null or a slot of
the pool; `gCamera` is set. The script reads through `resource->addr()` only, so it checks
each pointer against the pool or the `gMarioStates` array before following it and stops at
the first such failure, so that a wrong layout ends in a `FAIL` line rather than a crash.
In the `fixed` save mode `dllcheck` adds a second section: whether `gMarioStates`, the
whole object pool, the timer, controller pads, camera, current floor, `gCurrentArea`,
`sSurfacePool` and `gAreas` are inside the slices (`FAIL:` otherwise). The camera globals
(`gLakituState`, `gPlayerCameraState`, `sModeTransition`, ...) and the controller structs
(`gControllers`, `gControllerBits`) are reported as `WARN:` rather than `FAIL:` when
outside the slices; on the pinned DLL all of them are inside. The other modes have nothing
to cover. The save and load costs are measured 60 frames into the run so that a `dirty`
save has the pages a pellet dirties to copy (right at the first slot it would copy none).
The script takes a list of expected objects (`ExpectedObject`; the pipeline passes
`BitFsExpectedObjects`, `dllcheck` an empty list) and verifies each hardcoded `gObjectPool`
slot: active, running the declared behavior and, unless the declaration opts out, at the
declared home. `bitfs-turn --dry-run` prints the report at the first stage's frame; a run
prints it before its first stage and stops on a failure.

`--dirty-scan [frames]` (default 120) and `--dirty-replay` measure what the game writes:
consecutive frames are compared page by page (4 KB), under a fixed input pattern from
`<frame>` or while replaying the movie from power-on to `<frame>`. They print pages and
bytes changed per frame (min / median / max), how the set of touched pages grows at
checkpoints, its total against the full sections and the fixed slices, and every touched
page the fixed slices do not fully contain, as `<section>+<offset>` for `dll_symbols.py`.
These are the numbers behind ROADMAP 2.3 (docs/performance.md, "What the game writes").

`--objects` prints every active object in the pool at `<frame>`: slot, behavior as
`<section>+<offset>`, `oBehParams`, position and home. Pipe it through
`python scripts\dll_symbols.py <dll> -` to turn the offsets into behavior names. This is how
the slot declarations in `BitFsObjects.hpp` were established (2026-09-08: 89 active objects
at frame 3330, two of them `bhvBitfsTiltingInvertedPyramid`, at homes x = -2866 in slot 83
and x = -1945 in slot 84; `bhvPlatformOnTrack` in slot 85 at (-5744, -3072, 0)).

`--levels` lists every frame up to `<frame>` at which the movie changes level or area (the
`LevelTransitions` script, `tasfw-scripts/inc/LevelTransitions.hpp`): the frame, the level
number with the decomp's name for it, the area and the course. The state at frame f is
what inputs 0..f-1 produced, so the frame listed is the first one inside the new level.
This is how the frame a movie enters a level is found: a stage's `startFrame`, the frame
the tests play to, or the frame `m64splice` joins two movies at. `movies/bitfs-pyramid-jp.m64`
enters BitFS at frame 3001 and `movies/1keyU.m64` at 3068, both through the castle grounds,
the vanish cap course and the basement.

`--trace [frames]` prints Mario, the camera and what a movie carries between levels for the
`frames` (default 30) frames ending at `<frame>` (the `MarioTrace` script): position, speed,
action, facing and intended yaw; the camera's mode and yaw, `gCameraMovementFlags`, the
R-button camera selection, the 8-directions camera's base yaw and C-button offset; health,
coins, lives, `MarioState::flags` and `gRandomSeed16`. What a movie is doing around a frame,
and, when two movies that enter a level alike part inside it, why ("A movie for the US game").

`--leak-scan` answers the other question about a save mode: not "are the symbols we know
about inside the slices" but "does a load restore everything that changed". It saves a state
at `<frame>`, snapshots `.data` and `.bss`, plays 120 frames (or the count given) of a fixed
input pattern, loads the state back and snapshots again; every byte range that differs is
state the load did not restore. A second pass with a different pattern shows which ranges
depend on what was played. `python scripts\dll_symbols.py <dll> -` reads that output and
names each range from the DLL's export table. On the pinned DLL a `full` or `dirty` load
restores both sections exactly (`dirty` also on the 2023 and 2026 builds and the Linux
`.so`), and a `fixed` load misses only four to six bytes: the texture-scroll offsets of the
BitFS lava animation (`bitfs_movtex_tris_lava_*`), which nothing in the physics reads.

## A movie for the US game

The US build runs the same BitFS as the JP one; the way there differs (the text and the
intro run longer, so a movie for one version desyncs on the other before the castle:
`1keyU.m64` on the JP DLL never gets past the castle grounds). So the US test movie was
not TASed but joined from two movies: a US movie that reaches the level, and the JP
movie's part inside it. `movies/1keyU.m64` is a whole US 1-key run (7,628 inputs; on the
US DLL it goes castle grounds, vanish cap course, basement, BitFS at frame 3068, Bowser 2,
then DDD, BitS and Bowser 3; its header carries the JP CRC and country code, which nothing
here reads, and the DLL it syncs on is what says which game it is). `movies/bitfs-pyramid-jp.m64`
takes the same route and enters BitFS at frame 3001. `m64splice` made
`movies/bitfs-pyramid-us.m64` from the two (2026-09-13):

```
build\Release\out\m64splice.exe res\sm64_us_0.dll movies\1keyU.m64 res\sm64_jp_0.dll movies\bitfs-pyramid-jp.m64 bitfs movies\bitfs-pyramid-us.m64 --lead 10
```

`m64splice` (`tasfw-tools/m64splice`) plays each movie on its own DLL through the
`LevelTransitions` script to find the first frame whose state is inside the level, cuts
both there (`--lead 10`: ten frames earlier, see below), and runs the `SpliceMovie` script
(`tasfw-scripts/inc/SpliceMovie.hpp`) on the US DLL: the US movie to its cut, then the JP
movie's inputs from its cut as the script's own diff (`Apply`), exported with `ExportM64`,
so every frame written was played by the game. It then traces the output on the US DLL and
the JP movie on the JP DLL from their entry frames with the `MarioTrace` script and compares
position, speed, action and facing frame by frame. The output plays all 803 in-level
frames of the JP movie identically: JP frame f is US frame f + 67, the pipeline's frame
3330 is 3397, and there Mario's position, action, facing and the pyramid normal equal the
JP golden values (`test_libsm64_smoke.cpp`). Of the 89 objects in the pool only two moving
ones far from the pyramid (slots 28 and 61) sit elsewhere; slots 83, 84 and 85 are as
`BitFsObjects.hpp` declares.

The lead is what the first attempt taught. Cut at the entry frames themselves, the output
parted from the JP movie at the first frame the player controls, 41 frames in, because the
US movie enters with the 8-directions camera turned one step: its author presses C-left
during the pipe warp (frames 3065 and 3067) and keeps pressing after entry, and
`s8DirModeYawOffset`, the camera static those presses step, persists across levels, so the
same stick pointed 45 degrees elsewhere. `dllcheck --trace` shows it (the `8dir=` field);
the 37 frames of warp before entry are otherwise identical in the two movies down to
Mario's coordinates, so cutting ten frames earlier drops the presses and nothing else. The
trace is also where to look when another pair of movies parts: health, coins, lives, cap
flags, the R-button camera selection and the RNG seed are all in it.

The output's header is what `M64::save` writes today, the JP CRC and country code
(ROADMAP 2.5). `scripts\test.ps1` runs the libsm64 group on `res\sm64_us_0.dll` and this
movie at frame 3397 whenever that DLL exists; the group passes there (2026-09-13): layout,
slots, determinism, every save mode, the drift test with max diff 0.

## Why one copy per thread

Windows and Linux both map a given library path into a process once: `LoadLibrary` and
`dlopen` hand back the already-loaded image (checked 2026-09-08 with two `dlopen`s of the
same `.so` and one of a copy; only the copy got its own `gGlobalTimer`). Each scattershot
thread needs its own private game memory, so each thread loads a distinct file.
`Configuration::ResourcePaths` must list at least `TotalThreads` files. To make copies:

```powershell
1..23 | ForEach-Object { Copy-Item res\sm64_jp_0.dll "res\sm64_jp_$_.dll" }
```

```bash
for i in $(seq 1 23); do cp res/sm64_jp_0.so res/sm64_jp_$i.so; done   # and "dllPattern": "sm64_jp_{}.so"
```

## Linux

The `.so` runs in the Ubuntu 26.04 container from docs/compilers.md ("GCC and Clang
locally"); the repository is mounted at `/src`, so `res/sm64_jp_0.so` is
`/src/res/sm64_jp_0.so` inside it.

- **glibc 2.43 or newer.** The bitfs-sbb `.so` imports `sqrtf@GLIBC_2.43` (and nothing else
  newer than 2.17). Ubuntu 24.04, CI's `ubuntu-latest`, has glibc 2.39 and refuses it with
  `version 'GLIBC_2.43' not found (required by .../sm64_jp_0.so)`; Ubuntu 26.04 LTS ships
  2.43 and loads it. The bitfs-sbb README expects this ("may not work on systems built
  against Ubuntu or Fedora") and invites a rebuild per distro, but records no recipe.
- **Save modes.** The same three as on Windows ("Savestates" above), through `mprotect` and a
  `SIGSEGV` handler for `dirty`; several instances per process are fine (each has its own
  page set, the handler finds the owner of a faulting address). `fixed` is refused at
  construction on the `.so` because its sections are smaller than the slices, which is the
  intended answer; the tests fall back to `dirty` there. doctest installs its own `SIGSEGV`
  handler around every test case and restores the
  previous one afterwards, which would drop ours; `LibSm64` therefore re-checks and
  re-installs its handler at every construction and chains to whatever it found for faults
  that are not its own.
- **Numbers**, `dllcheck` on the JP `.so` at frame 3330 in the container (Docker Desktop on
  the Windows machine, so not comparable with the Windows column): 21.0 us per frame
  advance with GCC 15, 22.4 us with Clang 21; save 45 to 46 us; load 42 to 44 us;
  `dlsym` 33 to 37 ns.
- **Running the checks** (from a build tree inside the container):

  ```bash
  ./out/dllcheck /src/res/sm64_jp_0.so /src/movies/bitfs-pyramid-jp.m64 3330
  TASFW_LIBSM64=/src/res/sm64_jp_0.so TASFW_M64=/src/movies/bitfs-pyramid-jp.m64 TASFW_FRAME=3330 ./out/tasfw-tests -tc='libsm64*'
  ```

  Both pass with GCC 15 and Clang 21 (2026-09-08): identical golden state to the pinned
  Windows DLL, save/load determinism, drift test max |diff| = 0 over 240 frames.
- **The search is the same search.** The `.so` is jgcodes2020's build of a newer decomp
  than the DLLs (2026 against 2022), and the CI-sized Tier D search (`perf/tierd-ci-linux.json`,
  100 shots, seed 3) reaches the DLL's exact counts on it, 2,981,801 frame advances and
  10 solutions, with GCC 15 and Clang 21 (2026-09-13). It did not at first: the search
  read 2,867,262 frame advances and 11 solutions on Linux, identically in `dirty` and
  `full` mode and on both compilers, and the cause was the framework, not the game:
  scattershot's hashes went through `std::hash<std::byte>`, which libstdc++ and MSVC's
  STL implement differently, so the RNG chain diverged from the first pellet. The
  framework owns that hash now (`HashByte`, docs/compilers.md). What a longer search
  might still find in the newer decomp is not known; the drift test and this workload
  are the evidence there is.

## What depends on the exact build

Changing the DLL build silently invalidates all of the following. ROADMAP Phase 2 is about
making these derived instead of hardcoded.

1. **Struct headers** in `tasfw-core/inc/sm64/`. They are hand-copied from the decomp with
   64-bit pointer handling (`IS_64_BIT` in `ObjectFields.hpp`). A field added or reordered in
   the decomp version wafel built from shifts every access. (Unchanged between the 2022 and
   2026 builds, as the checks above show.)
2. **The `fixed` save mode's slices** (`LibSm64FixedSlices` in `LibSm64.hpp`): byte offsets
   into `.data` and `.bss` chosen for this build. The `dirty` and `full` modes depend on
   nothing in the build.
3. **Object pool slots 84, 83 and 85** (the pyramid, the far pyramid, the track platform),
   which depend on the level's object spawn order, not the DLL, but are equally fragile. Kept
   by decision (ROADMAP 2.4) and declared with behavior and home in
   `tasfw-scripts/inc/BitFsObjects.hpp`; the `VerifyLayout` script verifies them inside the
   level before the pipeline's first stage, so a shift fails start-up loudly.
4. **ROM/country codes** in `Inputs.hpp` (`Rom::SUPER_MARIO_64`, `CountryCode::SUPER_MARIO_64_J`):
   what `M64::save` writes into every movie it creates; `M64::load` checks nothing (ROADMAP 2.5).
5. **Exported names.** See "Renamed symbols"; new renames go into `LibSm64SymbolAliases`.

## Verifying a DLL by hand

Without the framework: parse the PE export table and confirm `sm64_init`, `sm64_update`,
`gMarioState`, `gObjectPool` and `gControllerPads` are present and that the image is PE32+
(x64). Any PE tool works; a 40-line Python script using `struct` is enough
(`scripts\dll_symbols.py` has the parser). For a `.so`: `nm -D --defined-only` for the
exports, `objdump -T | grep GLIBC` for the glibc versions it needs. With the framework, use
`dllcheck` above.

## Continuous integration

`.github/workflows/build.yml` runs the game when the `LIBSM64_KEY` repository secret is
set: the Windows jobs (MSVC and clang-cl) and two Ubuntu 26.04 container jobs (GCC 15 and
Clang 21; the `.so` needs that release's glibc, "Linux" above) call
`scripts/unlock_libsm64.py --key-env LIBSM64_KEY` to fetch the pinned bitfs-sbb JP build
and unlock it into `res/` on the runner (16 copies on Windows, the real config's thread
count; 4 on Linux), then run everything about the game that is exact:

- the `libsm64*` test group on `movies/bitfs-pyramid-jp.m64` (layout checks, the golden
  state at frame 3330, save/load determinism, the drift test);
- `dllcheck` in `fixed` mode on Windows (the layout checks and the fixed-slice coverage
  report, exit 1 on any `FAIL`) and in `dirty` mode with `--leak-scan` on both platforms,
  where the workflow fails the step if either pass reports a byte a load did not restore;
- the pipeline's dry run: `bitfs-turn --dry-run` on the real `config.json` on Windows, and
  on `perf/tierd-ci-linux.json` on Linux (the `VerifyLayout` script to the first stage's
  frame, the same check a real run makes before its first stage);
- Tier C with `perf_compare.py --counts-only` against the committed baseline;
- Tier D on a CI-sized workload, `perf/tierd-ci.json` (`tierd-ci-linux.json` with the
  `.so` pattern and `dirty` saves): the deterministic tilt-target stage cut to 100 shots
  on 4 threads, cost model off, about 40 s on the desktop, exact counts compared with
  `perf/baselines/tierd-ci.json` through `perf_compare.py tierd` and `--counts-only`. The
  counts are the same in `fixed` and `dirty` mode, on every compiler and on both game
  builds ("Linux" above), so one expected file serves every job; regenerate it with the
  same two commands when the framework legitimately changes its work.

So an extra frame advance, save, load or allocation anywhere in the framework, a struct
that stopped matching the DLL, a slice that stopped covering a hot symbol, or a load that
stopped restoring a byte fails the pull request. The secret is the derived key, not the
ROM (`--print-key`), set by the maintainer under Settings, Secrets; GitHub withholds
secrets from forks and from pull requests opened from them, so there those steps are
skipped and the jobs are green on the DLL-free tests alone. The unlocked binaries are never
uploaded as artifacts. Hosted runners' timings are not gated (they compare to nothing).
The pinned DLL (wafel v0.8.1's) itself never enters CI: it is a local artifact, and the
bitfs-sbb build (wafel v0.8.5's) reaches the same golden state and counts ("Known
builds"). The US game is not in CI either: its key derives from the US ROM, so it would be
a second secret (ROADMAP 2.5).

## Reproducing the DLL from source (not yet done)

Wafel's DLLs are compiled from a decomp fork maintained by branpk that builds the game for
the host and exports the two entry points; bitfs-sbb's Linux `.so` builds follow the same
recipe on a newer decomp. Which wafel commit each Windows build belongs to is now known
("Known builds": the pinned DLL is v0.8.1's, bitfs-sbb's are v0.8.5's), so a copy of the
exact bytes can always be recovered from wafel's git history with wafel's locker and a
ROM. What is still not recorded anywhere, in this repo, in the wafel checkout at
`C:\repos\wafel`, or in bitfs-sbb, is the decomp fork, the commit and the build command
that produced them. ROADMAP 2.1 was closed without them on 2026-09-13: nothing depends on
rebuilding, the bytes are recoverable, and a build from source with a recorded recipe
would be its own item if ever wanted (jgcodes2020 built the Linux `.so` from the current
decomp, so it can be done; start from https://github.com/branpk/wafel/issues/23, which
discusses libsm64 as an external dependency, and from jgcodes2020). Treat the pinned DLL as
an opaque artifact and keep a backup of it; the v0.8.5 build is the tested fallback.
