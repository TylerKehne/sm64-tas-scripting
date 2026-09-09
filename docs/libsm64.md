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
| wafel, 2023-09-07, JP | `C:\repos\wafel\libsm64\sm64_jp.dll` | 31,872,634 B, `da7b3620` | Passes every layout check including lightweight coverage; identical golden state; drift test max diff 0 (MSVC). |
| bitfs-sbb, 2026-06-30, JP | `C:\repos\bitfs-sbb\libsm64\lib\sm64_jp.dll` | 33,068,386 B, `625fd921` | Passes every layout check including lightweight coverage; identical golden state; drift test max diff 0 on MSVC and clang-cl. `dllcheck`: 8.5 us per frame, 41 us lightweight save, 42 us load. |
| bitfs-sbb, 2026-06-30, US | `...\lib\sm64_us.dll` | 33,508,967 B, `e2efd078` | Unlocked only. There is no US movie to play and the m64 country check is JP (ROADMAP 2.5). |
| bitfs-sbb, 2026-06-30, JP, Linux | `res/sm64_jp_0.so` (copied from `...\lib\sm64_jp.so`) | 16,733,032 B, `06c58c69` | On Ubuntu 26.04: every layout check, identical golden state, drift test max diff 0 with GCC 15 and Clang 21 (see "Linux"). |
| bitfs-sbb, 2026-06-30, US, Linux | `res/sm64_us_0.so` | 17,168,016 B, `61e27e4f` | Loads; no US movie. |

The bitfs-sbb binaries were built by jgcodes2020 on EndeavourOS with GCC 16.1.1 (the
`.so`'s `.comment` section says so), the Windows ones from the same decomp revision. Every
2023 and 2026 build comes from a newer decomp than the pinned DLL, which shows in two ways:
the renamed exports below, and `.data`/`.bss` layouts that differ from the pinned build by
tens of bytes on Windows (the hot symbols still fall inside the lightweight slices) and by
hundreds of kilobytes on Linux (where lightweight mode does not exist anyway; the `.so`'s
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

## Checking a DLL: `dllcheck`

```
build\Release\out\dllcheck.exe <libsm64.dll> <movie.m64> <frame> [--lightweight] [--leak-scan [frames]] [--objects] [--dirty-scan [frames]] [--dirty-replay]
```

Plays the movie to `<frame>` (pick one inside a level), runs `LibSm64::layoutCheckReport()`,
prints one `ok:`/`FAIL:` line per check, and prints the measured cost of a frame advance and
of a savestate save/load. Exit code 0 means every check passed. The same checks run
automatically once per scattershot thread through `Resource::verifyLayout()`, so a wrong DLL
fails at start-up with a readable message instead of producing garbage searches.

What is checked: `gMarioState` points at `gMarioStates[0]`; Mario's object lies in
`gObjectPool` at a multiple of `sizeof(Object)`; its `behavior` is `bhvMario`;
`MarioState::marioObj` points back at it; `oPosX/Y/Z` and `header.gfx.pos` equal
`MarioState::pos` (the game mirrors them every frame); the floor normal is unit length and
the floor's object pointer is inside game data; `gCamera` is inside game data; and in
lightweight mode, that `gMarioStates`, the whole object pool, the timer, controller pads,
camera, current floor, `gCurrentArea`, `sSurfacePool` and `gAreas` are inside the slices.
The camera globals (`gLakituState`, `gPlayerCameraState`, `sModeTransition`, ...) and the
controller structs (`gControllers`, `gControllerBits`) are reported as `WARN:` rather than
`FAIL:` when outside the slices; on the pinned DLL all of them are inside. On Linux, where
lightweight saves do not exist (`LibSm64LightweightSupported`), `--lightweight` is accepted
and the coverage checks are replaced by one `note:` line. When `LibSm64Config::expectedObjects`
is set (the pipeline passes `BitFsExpectedObjects`; `dllcheck` does not), the in-level report
also verifies each hardcoded `gObjectPool` slot: active, running the declared behavior and,
unless the declaration opts out, at the declared home. `bitfs-turn --dry-run` prints that
report at the first stage's frame.

`--dirty-scan [frames]` (default 120) and `--dirty-replay` measure what the game writes:
consecutive frames are compared page by page (4 KB), under a fixed input pattern from
`<frame>` or while replaying the movie from power-on to `<frame>`. They print pages and
bytes changed per frame (min / median / max), how the set of touched pages grows at
checkpoints, its total against the full sections and the lightweight slices, and every
touched page the slices do not fully contain, as `<section>+<offset>` for `dll_symbols.py`.
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
names each range from the DLL's export table. On the pinned DLL a full load restores both
sections exactly, and a lightweight load misses only six bytes: the texture-scroll offsets
of the BitFS lava animation (`bitfs_movtex_tris_lava_*`), which nothing in the physics reads.

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
- **Save path.** `LibSm64` on Linux does not copy `.data`/`.bss` or slices. The constructor
  copies both sections once, write-protects them with `mprotect`, and installs a `SIGSEGV`
  handler that unprotects a page on first write and records it; `save` copies the recorded
  pages, `load` writes them back (after resetting to the constructor's copy if pages were
  dirtied since the save). `LibSm64Config::lightweight` is ignored. The recorded-page list
  and the handler are process-wide, so **one `LibSm64` per process**: a second instance
  would save and restore the first one's pages (ROADMAP 3.4).
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
2. **Lightweight save slices** in `LibSm64::save`/`load` (`tasfw-resources/src/LibSm64.cpp`):
   fixed byte offsets into `.data` and `.bss` chosen for this build. Windows only.
3. **Object pool slots 84, 83 and 85** (the pyramid, the far pyramid, the track platform),
   which depend on the level's object spawn order, not the DLL, but are equally fragile. Kept
   by decision (ROADMAP 2.4) and declared with behavior and home in
   `tasfw-scripts/inc/BitFsObjects.hpp`; `LibSm64::objectCheckReport` verifies them inside
   the level as part of the layout check, so a shift fails start-up loudly.
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
