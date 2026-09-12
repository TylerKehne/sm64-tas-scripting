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

**Never commit a ROM or an unlocked binary, and do not vendor the locked ones either**; link
to wafel or bitfs-sbb (AGENTS.md hard rule 6). If CI ever needs the game, the unlock key or
the ROM goes in a maintainer-only repository secret so that forks and pull requests from
outside never see it, and the job skips when the secret is absent (ROADMAP 3.4).

## Known builds

Every build below has been run through `dllcheck` and, where a movie exists for it, through
the libsm64 test group (`scripts\test.ps1 -Dll <file> -Filter 'libsm64*'`): the layout
checks, the golden state at frame 3330 of `comissonPyra2-Fanart_x-Z.m64`, save/load
determinism, and the `PyramidUpdate` drift test (docs/performance.md, ROADMAP 3.3).

| Build | File | Size, MD5 | Status (2026-09-08) |
|---|---|---|---|
| wafel, 2022-03-12 (**pinned**) | `res\sm64_jp_0.dll` .. `_23.dll` | 32,958,049 B, `463a5be6` | The reference: every offset, check and golden value in this repo was taken from it. |
| wafel, 2023-09-07, JP | `C:\repos\wafel\libsm64\sm64_jp.dll` | 31,872,634 B, `da7b3620` | Passes every layout check including fixed-slice coverage; identical golden state; drift test max diff 0 (MSVC). `dirty` mode: 125 pages per save, 7 us, leak scan zero bytes. |
| bitfs-sbb, 2026-06-30, JP | `C:\repos\bitfs-sbb\libsm64\lib\sm64_jp.dll` | 33,068,386 B, `625fd921` | Passes every layout check including fixed-slice coverage; identical golden state; drift test max diff 0 on MSVC and clang-cl. `dllcheck`: 8.5 us per frame; `dirty` mode 123 pages per save, 7 to 8 us, leak scan zero bytes. |
| bitfs-sbb, 2026-06-30, US | `...\lib\sm64_us.dll` | 33,508,967 B, `e2efd078` | Unlocked only. There is no US movie to play and the m64 country check is JP (ROADMAP 2.5). |
| bitfs-sbb, 2026-06-30, JP, Linux | `res/sm64_jp_0.so` (copied from `...\lib\sm64_jp.so`) | 16,733,032 B, `06c58c69` | On Ubuntu 26.04: every layout check, identical golden state, drift test max diff 0 with GCC 15 and Clang 21 (see "Linux"). |
| bitfs-sbb, 2026-06-30, US, Linux | `res/sm64_us_0.so` | 17,168,016 B, `61e27e4f` | Loads; no US movie. |

The bitfs-sbb binaries were built by jgcodes2020 on EndeavourOS with GCC 16.1.1 (the
`.so`'s `.comment` section says so), the Windows ones from the same decomp revision. Every
2023 and 2026 build comes from a newer decomp than the pinned DLL, which shows in two ways:
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
`sm64_update_and_render` exists in the 2022 and 2026 builds but not in wafel 2023; nothing
here calls it.

## What is in `res/` today

| File | Notes |
|---|---|
| `sm64_jp_0.dll` .. `sm64_jp_23.dll` | 24 byte-identical copies of the pinned build. |
| `sm64_jp_0.so`, `sm64_us_0.so` | bitfs-sbb's Linux builds, for the container runs in "Linux". |
| `comissonPyra2-Fanart_x-Z.m64`, `comissonPyra2-Fanart_XZ.m64`, `test3.m64` | Source movies referenced by `config.json`. JP ROM. |
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
| `dirty` (default) | the pages the game wrote since the current baseline: about 122 pages (488 KB) after 60 frames of play, 525 pages (2.1 MB) by the end of a scattershot stage | about 7 us at 122 pages; the search's 525-page states make its loads about 40% dearer than `fixed`'s, 3.6% on the stage's wall | any build, Linux, exploration that stays near a settled state |

**Fixed** was found empirically for the pinned 2022 build. Construction refuses it, with the
reason, on a build whose sections are smaller than the slices (the Linux `.so`'s `.data` is
330 KB and its `.bss` 3.6 MB, against 2.4 MB and 4.9 MB in the DLL); on a build where they
fit, nothing at run time checks coverage: `dllcheck --save-mode fixed` reports whether every
hot symbol lies inside a range, and `--leak-scan` measures what a load misses; run both
before trusting `fixed` on a new build. On the pinned DLL `--leak-scan` shows a
Fixed load misses 4 to 6 bytes of lava texture scroll that nothing in the physics reads.

**Dirty** write-protects the pages of both sections and records the first write to each
page in a fault handler (a vectored exception handler on Windows, `SIGSEGV` on Linux); a
save copies the written pages, a load writes them back and restores every other page
written since the save from the baseline's snapshot, so a load is exact by construction on
any build. The resource takes a fresh baseline on its own whenever it is asked to save
while it holds no live slots: the start save every top-level run begins with, and the
first slot of a run, which is the save `LongLoad` makes at the frame exploration starts
from. From then on a save copies only what the run itself writes. Faults happen once per
page per baseline (about 120 in a BitFS run, 525 during the replay to frame 3330), a
baseline costs one 7 MB snapshot, and the cost of a save grows with what the game writes
until the next baseline: level loads, deaths and warps make Dirty saves bigger, and a run
that spans them ends up slower than Fixed. The BitFS scattershot stage is such a run: its
pellets die and the level reloads, so each thread's set reaches 525 pages (2.1 MB, the same
pages the replay from power-on touches), every load restores that much, and the stage
takes 3.6% longer than in `fixed` mode (139.8 s against 134.9 s, deterministic Tier D
workload, identical counts; `bitfs-turn` prints the set size per stage). Older states stay
loadable: the snapshot the start save refers to is kept,
and others are released only when no slot can refer to them. Two snapshots per resource
(14 MB) is the steady state. Debuggers stop on the deliberate first-write faults unless
told not to; use `full` there.

## Checking a DLL: `dllcheck`

```
build\Release\out\dllcheck.exe <libsm64.dll> <movie.m64> <frame> [--save-mode full|fixed|dirty] [--leak-scan [frames]] [--objects] [--dirty-scan [frames]] [--dirty-replay]
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
  ./out/dllcheck /src/res/sm64_jp_0.so /src/res/comissonPyra2-Fanart_x-Z.m64 3330
  TASFW_LIBSM64=/src/res/sm64_jp_0.so TASFW_M64=/src/res/comissonPyra2-Fanart_x-Z.m64 TASFW_FRAME=3330 ./out/tasfw-tests -tc='libsm64*'
  ```

  Both pass with GCC 15 and Clang 21 (2026-09-08): identical golden state to the pinned
  Windows DLL, save/load determinism, drift test max |diff| = 0 over 240 frames.

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
4. **ROM/country checks** in `Inputs.hpp` (`Rom::SUPER_MARIO_64`, `CountryCode::SUPER_MARIO_64_J`).
5. **Exported names.** See "Renamed symbols"; new renames go into `LibSm64SymbolAliases`.

## Verifying a DLL by hand

Without the framework: parse the PE export table and confirm `sm64_init`, `sm64_update`,
`gMarioState`, `gObjectPool` and `gControllerPads` are present and that the image is PE32+
(x64). Any PE tool works; a 40-line Python script using `struct` is enough
(`scripts\dll_symbols.py` has the parser). For a `.so`: `nm -D --defined-only` for the
exports, `objdump -T | grep GLIBC` for the glibc versions it needs. With the framework, use
`dllcheck` above.

## Reproducing the DLL from source (not yet done)

Wafel's DLLs are compiled from a decomp fork maintained by branpk that builds the game for
the host and exports the two entry points; bitfs-sbb's 2026 builds follow the same recipe
on a newer decomp. Neither the fork, the commit, nor the build command is recorded in this
repo, in the wafel checkout at `C:\repos\wafel`, or in bitfs-sbb; finding and pinning them
is ROADMAP 2.1 (start from https://github.com/branpk/wafel/issues/23, which discusses
libsm64 as an external dependency, and from jgcodes2020). Until then, treat the 2022 DLL as
an opaque, pinned artifact and keep a backup of it; the 2026 builds are the tested fallback.
