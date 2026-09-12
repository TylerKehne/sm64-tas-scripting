@AGENTS.md

Claude-specific notes:
- Use `scripts\build.ps1` for every build; do not invoke cmake with the stale `build/` cache.
- Before writing anything that changes the shape of `tasfw-core/` or `tasfw-scattershot/`
  (a method, a virtual, a helper, a term), present the design and wait for the maintainer's
  decision (hard rule 10); the Stop hook names framework headers you changed and asks where
  that happened. When you change anything under those directories, re-read the "Hard
  rules" in AGENTS.md before finishing and state which ones your change touches.
- The Stop hook in `.claude/settings.json` runs the documentation review after any turn
  that changed non-documentation files. When it blocks your stop, follow the procedure it
  hands you (update docs for extensions, ask before touching docs for deviations); never
  work around it.
