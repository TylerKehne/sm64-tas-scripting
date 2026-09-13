<!-- AGENTS.md, "Verifying a change", is the checklist behind this template. -->

## What changed and why

<!-- One paragraph. Name the ROADMAP item if there is one. -->

## Verification

CI (`build.yml`) builds MSVC, clang-cl, GCC and Clang with warnings as errors and runs the
DLL-free tests on every push and pull request; those checks must be green. What only a
machine with the game can run:

- [ ] `scripts\test.ps1` with the DLL in `res\` (the libsm64 smoke test), on both compilers
- [ ] Documentation reconciled (AGENTS.md, ARCHITECTURE.md, ROADMAP.md, README.md, docs/)

## Performance

<!-- Required for anything under tasfw-core, tasfw-scattershot or tasfw-resources
     (AGENTS.md hard rule 8). Paste the delta table printed by scripts\perf.ps1; with the DLL
     in res\ it covers Tier A through D, gated against the baseline commit's binaries run in
     the same session (docs/performance.md, "Running the suite"). A count that went up, or a
     wall-time regression over the threshold, needs a sentence here and the maintainer's
     acceptance. -->

```
(scripts\perf.ps1 delta table)
```

## Not run

<!-- Say exactly what could not be run and why (no DLL, no Linux box, ...). -->
