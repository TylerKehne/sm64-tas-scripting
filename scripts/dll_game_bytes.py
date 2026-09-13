#!/usr/bin/env python3
"""Where the game's bytes of a libsm64 DLL's .data and .bss end and the C runtime's begin.

  dll_game_bytes.py <dll> [<dll> ...]

Prints, for each DLL, the LibSm64KnownGameBytes entry (LibSm64.hpp) that describes it and
the objects at each section's edges the entry was read from, so the boundary can be checked
by eye. No dependencies: parses the PE section table and the COFF symbol table.

Both sections hold the mingw-w64 runtime's own state at their edges (docs/libsm64.md,
"Savestates"): crtdll.c's objects first, then the game's, then the runtime's libraries
(gccmain, natstart, tlssup, tlsthrd, pseudo-reloc, gdtoa, ...). The loader runs that code on
every thread of the process, so a savestate must leave those bytes alone. The COFF symbol
table names, for every object file linked in, where its part of each section starts and how
long it is (the section symbol's auxiliary record), so the game's bytes are the run of
objects between the leading and the trailing runtime objects. Runtime objects are recognised
by source file name; anything after the first trailing one is reported so that a stray game
object there would be seen.
"""
import struct
import sys

# mingw-w64-crt and libgcc sources whose objects bound the game's: those linked first
# (crtdll) and the first ones linked after the game's own object files. Everything after the
# first of these at a section's tail is the runtime's too and is not classified; the sources
# seen there so far are listed in RUNTIME_TAIL_SOURCES so that only a newcomer is reported.
RUNTIME_SOURCES = {
    "crtdll", "dllentry", "dllmain", "cygming-crtbegin", "cygming-crtend", "gccmain", "natstart",
    "tlsmcrt", "tlsmthread", "tlssup", "tlsthrd", "mingw_helpers", "pseudo-reloc", "pesect",
    "usermatherr", "crt_handler", "fake",
}
RUNTIME_TAIL_SOURCES = {
    "mingw_pformat", "misc", "onexit_table", "acrt_iob_func", "CRT_fp10", "mbrtowc", "dtoa", "gdtoa",
    "dmisc", "gmisc", "smisc", "strtodg", "ulp", "hd_init", "",  # gdtoa; "" is a linker-made object with no source
}

# RTL_CRITICAL_SECTION's fields, read at construction to confirm the entry (LibSm64.cpp).
THREAD_KEY_LOCK = "__mingwthr_cs"


def parse(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec, _, symptr, nsyms, optsz = struct.unpack_from("<HIIIH", d, pe + 6)
    sections = []
    off = pe + 24 + optsz
    for _ in range(nsec):
        name = d[off:off + 8].rstrip(b"\0").decode(errors="replace")
        vsize = struct.unpack_from("<I", d, off + 8)[0]
        sections.append((name, vsize))
        off += 40
    if symptr == 0 or nsyms == 0:
        raise SystemExit(f"{path}: no COFF symbol table (stripped); the entry cannot be derived")
    strtab = symptr + nsyms * 18
    objects = []  # (section number, start, length, source file)
    named = {}    # symbol name -> (section number, offset)
    i = 0
    source = "?"
    while i < nsyms:
        e = d[symptr + i * 18: symptr + (i + 1) * 18]
        if e[:4] == b"\0\0\0\0":
            name = d[strtab + struct.unpack_from("<I", e, 4)[0]:].split(b"\0", 1)[0].decode(errors="replace")
        else:
            name = e[:8].rstrip(b"\0").decode(errors="replace")
        value, secnum, _, cls, naux = struct.unpack_from("<IhHBB", e, 8)
        aux = d[symptr + (i + 1) * 18: symptr + (i + 1 + naux) * 18]
        if cls == 103:  # .file: the source of the symbols that follow (its name may be cut short)
            source = aux.rstrip(b"\0").decode(errors="replace").replace(chr(0xFFFD), "")
        elif cls == 3 and secnum > 0 and name == sections[secnum - 1][0] and naux >= 1:
            objects.append((secnum, value, struct.unpack_from("<I", aux, 0)[0], source))
        elif secnum > 0 and cls in (2, 3):
            named.setdefault(name, (secnum, value))
        i += 1 + naux
    return sections, objects, named


def stem(source):
    base = "".join(c for c in source if c.isprintable()).replace(chr(92), "/").rsplit("/", 1)[-1]
    if len(base) < 2:  # the linker's own objects (the pseudo-relocation list) carry a garbage .file
        return ""
    return base.rsplit(".", 1)[0] if "." in base else base


def game_bytes(sections, objects, section):
    index = [i for i, s in enumerate(sections) if s[0] == section][0]
    size = sections[index][1]
    objs = sorted((start, length, src) for sn, start, length, src in objects if sn == index + 1 and length > 0)
    i = 0
    while i < len(objs) and stem(objs[i][2]) in RUNTIME_SOURCES:
        i += 1
    j = i
    while j < len(objs) and stem(objs[j][2]) not in RUNTIME_SOURCES:
        j += 1
    begin = objs[i][0] if i < len(objs) else size
    end = objs[j][0] if j < len(objs) else size
    return size, begin, end, objs[:i], objs[i:j], objs[j:]


def describe(objs):
    return ", ".join(f"{stem(src) or '?'} [0x{start:X}+0x{length:X}]" for start, length, src in objs)


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2
    sys.stdout.reconfigure(errors="replace")
    for path in argv[1:]:
        sections, objects, named = parse(path)
        rows = {s: game_bytes(sections, objects, s) for s in (".data", ".bss")}
        if THREAD_KEY_LOCK not in named:
            raise SystemExit(f"{path}: {THREAD_KEY_LOCK} not in the symbol table; is this a mingw-w64 build?")
        lock_section, lock = named[THREAD_KEY_LOCK]
        if sections[lock_section - 1][0] != ".bss":
            raise SystemExit(f"{path}: {THREAD_KEY_LOCK} is in {sections[lock_section - 1][0]}, not .bss; LibSm64 expects it in .bss")
        data, bss = rows[".data"], rows[".bss"]
        print(f"== {path}")
        print("   LibSm64KnownGameBytes entry:")
        print(f"   {{0x{data[0]:X}, 0x{bss[0]:X}, {{0x{data[1]:X}, 0x{bss[1]:X}}}, {{0x{data[2]:X}, 0x{bss[2]:X}}}, 0x{lock:X}}},")
        for section in (".data", ".bss"):
            size, begin, end, head, game, tail = rows[section]
            print(f"   {section}: size 0x{size:X}; the game's bytes [0x{begin:X}, 0x{end:X}) = {end - begin:,} of {size:,}")
            print(f"      runtime head: {describe(head) or '(none)'}")
            if game:
                print(f"      game: {len(game)} objects, {stem(game[0][2])} [0x{game[0][0]:X}] to {stem(game[-1][2])} [0x{game[-1][0]:X}+0x{game[-1][1]:X}]")
            print(f"      runtime tail: {describe(tail) or '(none)'}")
            stray = [o for o in tail if stem(o[2]) not in RUNTIME_SOURCES and stem(o[2]) not in RUNTIME_TAIL_SOURCES]
            if stray:
                print(f"      !! objects after the tail begins from sources not known as the runtime's (a game object here would be lost from the savestate): {describe(stray)}")
        print(f"   {THREAD_KEY_LOCK}: .bss+0x{lock:X}" + ("" if bss[2] <= lock < bss[0] else "  !! not inside the runtime tail"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
