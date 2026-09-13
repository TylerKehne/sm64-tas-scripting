#!/usr/bin/env python3
"""Write the table of field offsets and struct sizes the sm64 headers must match (ROADMAP 2.2).

  dll_layout.py --json <layout.json> [--dll <dll>] [--out tasfw-tests/src/sm64_layout.inc]
  dll_layout.py --dll <dll> --sm64-layout <sm64_layout.exe> [--out ...]

The DLL's DWARF debug info is the authority on where every field of every struct sits.
wafel's `sm64_layout` tool reads it into JSON (every wafel release ships it under `tools/`;
`cargo build --release -p sm64_layout` in a wafel checkout builds it). This script takes the
structs the framework reads through (STRUCTS below) out of that JSON and writes them as
TASFW_LAYOUT_STRUCT(name, size) and TASFW_LAYOUT_FIELD(struct, field, offset) lines, which
tasfw-tests/src/test_sm64_layout.cpp compiles against tasfw-core/inc/sm64/ with sizeof and
offsetof. The test needs no DLL, so every run of the tests checks the headers against the
build the table was taken from.

For a new build: run this on it with --out somewhere else and diff the two tables. A field
that moved shows as a changed line; replacing the committed table then makes the test say
whether the headers still fit (docs/libsm64.md, "Struct layouts").

Sizes come from the DWARF too, as the stride of the pointers and arrays over each struct.
The Object struct's o* fields (oPosX, ...) are not in the DWARF: wafel adds them from its own
table of the decomp's object_fields.h macros (wafel_layout/sm64_macro_defns.json), so those
lines check ObjectFields.hpp against wafel's reading of the decomp, and every other line
checks the headers against the binary.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile

# The structs the framework reads game memory through, and the structs those contain.
# Every other struct in tasfw-core/inc/sm64/Types.hpp is unused and not checked.
STRUCTS = ["MarioState", "Object", "ObjectNode", "GraphNode", "GraphNodeObject", "Surface", "Camera"]

DEFAULT_OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           "tasfw-tests", "src", "sm64_layout.inc")


def data_types(node):
    """Every data_type node under a JSON node, depth first."""
    if isinstance(node, dict):
        if node.get("kind") in ("Pointer", "Array", "Name", "Struct", "Union", "Void"):
            yield node
        for value in node.values():
            yield from data_types(value)
    elif isinstance(node, list):
        for value in node:
            yield from data_types(value)


def struct_size(layout, name):
    """The struct's size: the stride of any pointer to it or array of it in the layout."""
    base = {"kind": "Name", "data": "struct " + name}
    strides = set()
    for node in data_types(layout):
        if node["kind"] in ("Pointer", "Array") and node["data"].get("base") == base:
            stride = node["data"].get("stride")
            if stride:
                strides.add(stride)
    if len(strides) != 1:
        sys.exit(f"struct {name}: expected one stride in the layout, found {sorted(strides)}")
    return strides.pop()


def run_sm64_layout(exe, dll):
    tmp = tempfile.mkdtemp(prefix="tasfw-layout-")
    out = os.path.join(tmp, "layout.json")
    subprocess.run([exe, "--libsm64", dll, "-o", out], check=True)
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", help="the JSON sm64_layout wrote for the DLL")
    ap.add_argument("--dll", help="the unlocked DLL (named in the table's header; run through --sm64-layout if given)")
    ap.add_argument("--sm64-layout", metavar="EXE", help="wafel's sm64_layout executable, run on --dll")
    ap.add_argument("--out", default=DEFAULT_OUT, help=f"the table to write (default {os.path.relpath(DEFAULT_OUT)})")
    args = ap.parse_args(argv)

    if args.sm64_layout:
        if not args.dll:
            ap.error("--sm64-layout needs --dll")
        args.json = run_sm64_layout(args.sm64_layout, args.dll)
    if not args.json:
        ap.error("give --json, or --dll with --sm64-layout")

    with open(args.json, "rb") as f:
        layout = json.load(f)["data_layout"]

    if args.dll:
        data = open(args.dll, "rb").read()
        source = f"{os.path.basename(args.dll)} ({len(data):,} B, md5 {hashlib.md5(data).hexdigest()})"
    else:
        source = os.path.basename(args.json)

    lines = []
    fields = 0
    for name in STRUCTS:
        entry = layout["type_defns"].get("struct " + name)
        if entry is None or entry["kind"] != "Struct":
            sys.exit(f"struct {name} is not in the layout")
        lines.append(f"TASFW_LAYOUT_STRUCT({name}, {struct_size(layout, name)})")
        members = sorted(entry["data"]["fields"].items(), key=lambda kv: (kv[1]["offset"], kv[0]))
        for field, info in members:
            lines.append(f"TASFW_LAYOUT_FIELD({name}, {field}, {info['offset']})")
            fields += 1

    header = [
        f"// Field offsets and struct sizes from the DWARF of {source},",
        "// written by scripts/dll_layout.py. Do not edit: regenerate for a new build and diff.",
        f"// {len(STRUCTS)} structs, {fields} fields; compiled by test_sm64_layout.cpp against tasfw-core/inc/sm64/.",
    ]
    with open(args.out, "w", newline="\n") as f:
        f.write("\n".join(header + lines) + "\n")
    print(f"{os.path.relpath(args.out)}: {len(STRUCTS)} structs, {fields} fields, from {source}")


if __name__ == "__main__":
    main(sys.argv[1:])
