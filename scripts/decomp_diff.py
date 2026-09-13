#!/usr/bin/env python3
"""Check the files copied from the SM64 decompilation against the upstream revision each came from.

  decomp_diff.py [--checkout <sm64 clone>] [--show] [--update]

scripts/decomp_pin.json pins every file under tasfw-core/inc/sm64/ that is a copy of an
n64decomp/sm64 file to the upstream commit and path it was copied from, and records the
residual: the sha256 of the normalized diff between the copy and that upstream file, which
is the editing done while copying (#pragma once, IS_64_BIT resolved, a struct cut out of a
larger header, clang-format's wrapping). This script recomputes every residual and fails
when one changed: a copy edited without moving its pin, or a pin moved without re-checking
the copy. --show prints the residual diffs; --update records the residuals as they are now,
the step after a deliberate edit or a moved pin (read --show first). The reimplemented
files (tasfw-core/src/decomp/, PyramidUpdate) are cited in the same file by upstream
function; nothing diffs them (docs/decomp.md).

Upstream files are fetched from raw.githubusercontent.com at the pinned commit, or read
from a local clone with --checkout (git show <commit>:<path>), which needs no network.

Normalization, applied to both sides before diffing: CRLF to LF, backslash-newline
continuations joined, runs of whitespace collapsed to one space, leading and trailing
whitespace and empty lines dropped.
"""
import argparse
import difflib
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIN = os.path.join(ROOT, "scripts", "decomp_pin.json")


def normalize(text):
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = re.sub(r"\\\n[ \t]*", " ", text)
    lines = []
    for line in text.split("\n"):
        line = " ".join(line.split())
        if line:
            lines.append(line)
    return lines


def upstream_text(repo, commit, path, checkout):
    if checkout:
        done = subprocess.run(["git", "-C", checkout, "show", f"{commit}:{path}"], capture_output=True, check=True)
        return done.stdout.decode("utf-8", "replace")
    url = f"https://raw.githubusercontent.com/{repo}/{commit}/{path}"
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read().decode("utf-8", "replace")


def residual(copy, upstream, label_copy, label_upstream):
    """The normalized diff, upstream to copy, and its hash."""
    diff = list(difflib.unified_diff(normalize(upstream), normalize(copy),
                                     fromfile=label_upstream, tofile=label_copy, n=0, lineterm=""))
    text = "\n".join(diff) + "\n"
    added = sum(1 for line in diff if line.startswith("+") and not line.startswith("+++"))
    removed = sum(1 for line in diff if line.startswith("-") and not line.startswith("---"))
    return text, hashlib.sha256(text.encode()).hexdigest(), added, removed


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--checkout", help="a local clone of the upstream repository to read the pinned files from")
    ap.add_argument("--show", action="store_true", help="print every residual diff")
    ap.add_argument("--update", action="store_true", help="record the residuals as they are now")
    args = ap.parse_args(argv)

    with open(PIN, encoding="utf-8") as f:
        pin = json.load(f)
    repo = pin["upstream"]

    failed = False
    for entry in pin["copies"]:
        with open(os.path.join(ROOT, entry["file"]), encoding="utf-8") as f:
            copy = f.read()
        commit = entry["commit"]
        upstream = upstream_text(repo, commit, entry["path"], args.checkout)
        text, digest, added, removed = residual(copy, upstream, entry["file"], f"{repo}@{commit[:7]}:{entry['path']}")
        recorded = entry.get("residual")
        state = "ok" if digest == recorded else ("recorded" if args.update else "CHANGED")
        print(f"{state:8} {entry['file']}: {entry['path']} @ {commit[:7]}, residual +{added} -{removed} lines, {digest[:12]}")
        if args.show or (digest != recorded and not args.update):
            sys.stdout.write(text)
        if digest != recorded:
            if args.update:
                entry["residual"] = digest
            else:
                failed = True
                print(f"         recorded residual was {recorded[:12] if recorded else 'none'}; "
                      "an edit to the copy or a moved pin. Review the diff above, then --update.")

    for entry in pin.get("derived", []):
        print(f"cited    {entry['file']}: {', '.join(entry['paths'])} @ {entry['commit'][:7]} ({', '.join(entry['functions'])})")

    if args.update:
        with open(PIN, "w", encoding="utf-8", newline="\n") as f:
            json.dump(pin, f, indent=2)
            f.write("\n")
        print(f"wrote {os.path.relpath(PIN)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
