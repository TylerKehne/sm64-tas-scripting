#!/usr/bin/env python3
"""Documentation review hook for Claude Code.

Keeps AGENTS.md, ARCHITECTURE.md, ROADMAP.md, README.md and docs/ describing the repository
as it currently is (see AGENTS.md, "Documentation must match the repository").

Modes (first argument; defaults from hook_event_name in the stdin JSON):
  baseline   record a fingerprint of every non-documentation file in the working tree for
             this session (wired to UserPromptSubmit, i.e. the start of every turn)
  stop       compare the tree with the baseline; when non-documentation files changed during
             the turn, block the stop once and hand Claude the review instructions (Stop)
  check      print what `stop` would report, without blocking (manual use)

State lives outside the repository in <tempdir>/tasfw-doc-review/<session_id>.json so that
concurrent sessions do not interfere. Changes that were flagged but whose review was
interrupted stay "pending" until a review completes, so nothing is lost.
Set TASFW_DOC_REVIEW=0 to disable the hook. The hook never fails a turn: any internal error
is reported on stderr and the stop is allowed.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROMPT_FILE = os.path.join(HERE, "doc-review-prompt.md")
DOC_SUFFIXES = (".md",)
DOC_PREFIXES = ("docs/",)


def is_doc(path: str) -> bool:
    p = path.lower()
    return p.endswith(DOC_SUFFIXES) or p.startswith(DOC_PREFIXES)


def repo_root(cwd: str) -> str:
    out = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=cwd,
                         capture_output=True, text=True, check=True)
    return out.stdout.strip()


def fingerprint(root: str) -> dict[str, str]:
    """path -> content hash for every tracked or untracked-but-not-ignored non-doc file."""
    out = subprocess.run(
        ["git", "ls-files", "-c", "-o", "--exclude-standard", "--full-name", "-z"],
        cwd=root, capture_output=True, check=True).stdout
    files: dict[str, str] = {}
    for rel in out.decode("utf-8", "surrogateescape").split("\0"):
        if not rel or is_doc(rel):
            continue
        try:
            h = hashlib.sha1()
            with open(os.path.join(root, rel), "rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            files[rel] = h.hexdigest()
        except OSError:
            continue  # deleted from the working tree, or not a regular file
    return files


def state_path(session_id: str) -> str:
    d = os.path.join(tempfile.gettempdir(), "tasfw-doc-review")
    os.makedirs(d, exist_ok=True)
    safe = "".join(c if c.isalnum() or c in "-_." else "_" for c in session_id) or "default"
    return os.path.join(d, safe + ".json")


def load_state(path: str) -> dict | None:
    try:
        with open(path, encoding="utf-8") as f:
            state = json.load(f)
        return state if isinstance(state.get("files"), dict) else None
    except (OSError, ValueError, AttributeError):
        return None


def save_state(path: str, files: dict[str, str], pending: dict[str, str]) -> None:
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({"time": time.time(), "files": files, "pending": pending}, f)
    os.replace(tmp, path)


def changes(base: dict[str, str], now: dict[str, str]) -> dict[str, str]:
    """path -> 'modified' | 'added' | 'deleted', relative to the baseline."""
    result: dict[str, str] = {}
    for p, h in now.items():
        if p not in base:
            result[p] = "added"
        elif base[p] != h:
            result[p] = "modified"
    for p in base:
        if p not in now:
            result[p] = "deleted"
    return result


def build_reason(current: dict[str, str], pending: dict[str, str]) -> str:
    lines = [
        "Documentation review required before this turn ends "
        "(Stop hook: .claude/hooks/doc-review.py).",
        "",
        "Non-documentation files changed since the turn started (paths relative to the repo root):",
    ]
    for kind in ("modified", "added", "deleted"):
        paths = sorted(p for p, k in current.items() if k == kind)
        if paths:
            lines.append(f"  {kind}: " + ", ".join(paths))
    earlier = sorted(p for p in pending if p not in current)
    if earlier:
        lines.append("  still unreviewed from an earlier, interrupted turn: " + ", ".join(earlier))
    lines.append("")
    try:
        with open(PROMPT_FILE, encoding="utf-8") as f:
            lines.append(f.read().strip())
    except OSError:
        lines.append("(doc-review-prompt.md is missing. Review AGENTS.md, ARCHITECTURE.md, "
                     "ROADMAP.md, README.md and docs/ against these changes: update them for "
                     "extensions of what they say, report deviations and ask for approval.)")
    return "\n".join(lines)


def emit(obj: dict) -> None:
    sys.stdout.write(json.dumps(obj))
    sys.stdout.flush()


def run(argv: list[str], data: dict) -> int:
    event = str(data.get("hook_event_name") or "")
    mode = argv[1] if len(argv) > 1 else ("stop" if event == "Stop" else "baseline")
    root = repo_root(str(data.get("cwd") or os.getcwd()))
    path = state_path(str(data.get("session_id") or "manual"))
    now = fingerprint(root)
    state = load_state(path)
    pending: dict[str, str] = dict(state.get("pending") or {}) if state else {}

    if mode == "baseline":
        save_state(path, now, pending)
        return 0

    if state is None:
        save_state(path, now, {})
        if mode == "stop":
            emit({"systemMessage": "doc-review: no baseline for this session (hook installed "
                  "mid-session?). Baseline recorded; changes from the next turn on are reviewed."})
        else:
            print("doc-review: no baseline for this session; recorded one now.")
        return 0

    current = changes(state["files"], now)

    if mode == "check":
        if not current and not pending:
            print("doc-review: no non-documentation changes since the baseline.")
        else:
            print(build_reason(current, pending))
        return 0

    if mode != "stop":
        sys.stderr.write(f"doc-review hook: unknown mode {mode!r}\n")
        return 0

    if data.get("stop_hook_active"):
        # This stop follows the review we asked for. Everything is reviewed; start clean.
        save_state(path, now, {})
        return 0

    if not current and not pending:
        return 0

    # Remember what is unreviewed in case the review is interrupted before it completes.
    pending.update(current)
    save_state(path, state["files"], pending)
    total = len(current) + len([p for p in pending if p not in current])
    emit({
        "decision": "block",
        "reason": build_reason(current, pending),
        "systemMessage": f"doc-review: {total} changed non-documentation file(s); "
                         "reviewing the documentation before finishing.",
    })
    return 0


def main() -> int:
    if os.environ.get("TASFW_DOC_REVIEW", "1").strip().lower() in ("0", "false", "off", "no"):
        return 0
    try:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(errors="replace")
        raw = "" if sys.stdin.isatty() else sys.stdin.read()
        data = json.loads(raw) if raw.strip() else {}
        if not isinstance(data, dict):
            data = {}
        return run(sys.argv, data)
    except Exception as e:  # never turn a hook bug into a broken turn
        sys.stderr.write(f"doc-review hook: {type(e).__name__}: {e}\n")
        return 0


if __name__ == "__main__":
    sys.exit(main())
