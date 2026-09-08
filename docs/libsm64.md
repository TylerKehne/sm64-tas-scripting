# libsm64: the game DLL

The framework does not emulate the N64. It loads a native x64 Windows DLL that is the SM64
decompilation compiled for the host, with two extra exports:

- `sm64_init()`: initialise the game (called once from `LibSm64`'s constructor).
- `sm64_update()`: run one frame (called by `LibSm64::advance`).

Every game global (`gMarioState`, `gObjectPool`, behaviors, tables) is also exported by
name, which is how `Resource::addr` resolves symbols. The DLL used here has about 9,000
exports.

## Where it comes from

The DLL is **wafel's libsm64** (https://github.com/branpk/wafel). It is *not* built from the
decomp checkout at `C:\repos\sm64`; that checkout has no DLL tooling and is only reference
material.

Wafel ships DLLs "locked" against a ROM (`sm64_jp.dll.locked`) so it does not distribute
Nintendo's assets. The wafel application unlocks them when pointed at a vanilla ROM, and the
repo also contains a standalone tool:

```
libsm64_lock --unlock -i sm64_jp.dll.locked -o sm64_jp.dll -r "<path to JP ROM>.z64"
```

(`libsm64_lock` is a Rust binary under `wafel/libsm64_lock`; `cargo build --release` in the
wafel repo produces it. `C:\repos\wafel` has an unlocked `libsm64/sm64_jp.dll` already.)

## What is in `res/` today

| File | Notes |
|---|---|
| `sm64_jp_0.dll` .. `sm64_jp_23.dll` | 24 byte-identical copies of one build dated 2022-03-12, 32,958,049 bytes. |
| `comissonPyra2-Fanart_x-Z.m64`, `comissonPyra2-Fanart_XZ.m64`, `test3.m64` | Source movies referenced by `config.json`. JP ROM. |
| `bitfs_nut_*.m64` (thousands) | Exported solutions from past runs (new runs export under `analysis/m64/<stage>/`). Safe to delete. |

`C:\repos\wafel\libsm64\sm64_jp.dll` is a **different** build (2023-09-07, 31,872,634 bytes)
with slightly different section sizes. `dllcheck` (below) reports that it passes every layout
check, including lightweight-slice coverage: the hot symbols sit 32 to 64 bytes lower in
`.bss` and still inside the saved ranges. So it is a drop-in replacement as far as memory
layout goes; whether it plays the movies frame-identically is a separate question for the
smoke test (ROADMAP 1.2).

## Checking a DLL: `dllcheck`

```
build\Release\out\dllcheck.exe <libsm64.dll> <movie.m64> <frame> [--lightweight]
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

## Why 24 copies

Windows maps a given DLL path into a process once. Each scattershot thread needs its own
private game memory, so each thread loads a distinct file. `Configuration::ResourcePaths`
must list at least `TotalThreads` files. To make copies:

```powershell
1..23 | ForEach-Object { Copy-Item res\sm64_jp_0.dll "res\sm64_jp_$_.dll" }
```

## What depends on the exact build

Changing the DLL build silently invalidates all of the following. ROADMAP Phase 2 is about
making these derived instead of hardcoded.

1. **Struct headers** in `tasfw-core/inc/sm64/`. They are hand-copied from the decomp with
   64-bit pointer handling (`IS_64_BIT` in `ObjectFields.hpp`). A field added or reordered in
   the decomp version wafel built from shifts every access.
2. **Lightweight save slices** in `LibSm64::save`/`load` (`tasfw-resources/src/LibSm64.cpp`):
   fixed byte offsets into `.data` and `.bss` chosen for this build.
3. **Object pool index 84** for the pyramid, which depends on the level's object load order,
   not the DLL, but is equally fragile.
4. **ROM/country checks** in `Inputs.hpp` (`Rom::SUPER_MARIO_64`, `CountryCode::SUPER_MARIO_64_J`).

## Verifying a DLL by hand

Without the framework: parse the PE export table and confirm `sm64_init`, `sm64_update`,
`gMarioState`, `gObjectPool` and `gControllerPads` are present and that the image is PE32+
(x64). Any PE tool works; a 40-line Python script using `struct` is enough. With the
framework, use `dllcheck` above.

## Reproducing the DLL from source (not yet done)

Wafel's DLLs are compiled from a decomp fork maintained by branpk that builds the game for
the host and exports the two entry points. Neither the fork, the commit, nor the build
command is recorded in this repo or in the wafel checkout at `C:\repos\wafel`; finding and
pinning them is ROADMAP 2.1 (start from https://github.com/branpk/wafel/issues/23, which
discusses libsm64 as an external dependency). Until then, treat the 2022 DLL as an opaque,
pinned artifact and keep a backup of it.
