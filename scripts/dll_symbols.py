#!/usr/bin/env python3
"""Map section-relative offsets of a libsm64 DLL to its exported symbols.

  dll_symbols.py <dll> .bss+30528 .data+39592 ...
  dllcheck.exe ... --leak-scan | dll_symbols.py <dll> -

Parses the PE section table and export directory (no dependencies) and prints, for every
"<section>+<offset>[ <n> bytes]" given (or found in stdin lines), the nearest exported
symbol at or before that address in the same section and the distance from it. Symbols the
DLL does not export cannot be named; the previous exported symbol is still a strong hint of
which source file the bytes belong to.
"""
import bisect
import re
import struct
import sys


def parse(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    dd = opt + (112 if magic == 0x20B else 96)  # data directories (PE32+ / PE32)
    export_rva, export_size = struct.unpack_from("<II", d, dd)

    sections = []
    for i in range(nsec):
        base = opt + optsz + i * 40
        name = d[base:base + 8].rstrip(b"\0").decode(errors="replace")
        vsize, va, rawsz, rawptr = struct.unpack_from("<IIII", d, base + 8)
        sections.append((name, va, vsize, rawptr, rawsz))

    def rva_to_file(rva):
        for _, va, vsize, rawptr, rawsz in sections:
            if va <= rva < va + max(vsize, rawsz):
                return rawptr + (rva - va)
        return None

    symbols = []  # (rva, name)
    if export_rva:
        e = rva_to_file(export_rva)
        n_funcs, n_names, addr_tbl, name_tbl, ord_tbl = struct.unpack_from("<IIIII", d, e + 20)
        funcs = rva_to_file(addr_tbl)
        names = rva_to_file(name_tbl)
        ords = rva_to_file(ord_tbl)
        for i in range(n_names):
            name_rva = struct.unpack_from("<I", d, names + 4 * i)[0]
            ordinal = struct.unpack_from("<H", d, ords + 2 * i)[0]
            rva = struct.unpack_from("<I", d, funcs + 4 * ordinal)[0]
            f = rva_to_file(name_rva)
            end = d.index(b"\0", f)
            symbols.append((rva, d[f:end].decode(errors="replace")))
    symbols.sort()
    return sections, symbols


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    sections, symbols = parse(argv[1])
    by_name = {name: (va, vsize) for name, va, vsize, _, _ in sections}
    rvas = [s[0] for s in symbols]

    queries = argv[2:]
    if queries == ["-"]:
        queries = []
        for line in sys.stdin:
            queries.extend(re.findall(r"(\.\w+\+\d+(?: \d+ bytes)?)", line))

    for q in queries:
        m = re.match(r"(\.\w+)\+(\d+)(?: (\d+) bytes)?", q)
        if not m:
            print(f"{q}: not a <section>+<offset>")
            continue
        section, offset, length = m.group(1), int(m.group(2)), int(m.group(3) or 1)
        if section not in by_name:
            print(f"{q}: unknown section")
            continue
        va = by_name[section][0]
        rva = va + offset
        i = bisect.bisect_right(rvas, rva) - 1
        if i < 0:
            print(f"{q}: before the first exported symbol")
            continue
        sym_rva, name = symbols[i]
        nxt = symbols[i + 1][1] if i + 1 < len(symbols) else "(end)"
        print(f"{q:32s} -> {name}+{rva - sym_rva} (next symbol {nxt})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
