@AGENTS.md

Claude-specific notes:
- Use `scripts\build.ps1` for every build; do not invoke cmake with the stale `build/` cache.
- When you change anything under `tasfw-core/` or `tasfw-scattershot/`, re-read the
  "Hard rules" in AGENTS.md before finishing and state which ones your change touches.
