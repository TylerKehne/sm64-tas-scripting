<!-- AGENTS.md, "Verifying a change", is the checklist behind this template. -->

## What changed and why

<!-- One paragraph. Name the ROADMAP item if there is one. -->

## Verification

- [ ] `scripts\build.ps1 -Config Release` and `scripts\build.ps1 -Config Release -Compiler clang`: no new warnings
- [ ] `scripts\test.ps1` on both compilers (say whether the libsm64 tests ran)
- [ ] Documentation reconciled (AGENTS.md, ARCHITECTURE.md, ROADMAP.md, README.md, docs/)

## Performance

<!-- Required for anything under tasfw-core, tasfw-scattershot or tasfw-resources
     (AGENTS.md hard rule 8). Paste the delta table printed by scripts\perf.ps1; with the DLL
     in res\ it covers Tier A through D. A count that went up, or a wall-time regression over
     the threshold, needs a sentence here and the maintainer's acceptance. -->

```
(scripts\perf.ps1 delta table)
```

## Not run

<!-- Say exactly what could not be run and why (no DLL, no Linux box, ...). -->
