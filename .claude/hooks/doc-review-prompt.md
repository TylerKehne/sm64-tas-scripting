Reconcile the documentation with the changes listed above before you finish. The documentation
must describe the repository as it is now, so that the next agent can pick up without
re-discovering anything. Documentation means AGENTS.md, ARCHITECTURE.md, ROADMAP.md,
README.md, everything under docs/, and CLAUDE.md when the agent workflow itself changed.

Procedure:

1. Look at what actually changed: `git diff -- <paths>` for tracked files; read added files.
   Changes you did not author this turn (a branch switch, a pull, another session's work)
   need no review; say so and skip them.
2. Find every statement in the documentation that the change touches: build and run steps,
   scripts and their flags, tests and what they pin, perf numbers and baselines, hard rules,
   architecture and invariants, coupling to the DLL, roadmap items and their "Done when",
   compiler pitfalls, known problems, glossary.
3. Classify each change:
   - EXTENSION: the change does what the documentation already asks for or plans, or adds
     something under an existing policy (a roadmap item progressed or completed, a new test,
     benchmark, tool, script or flag of an established kind, a re-measured number, a newly
     found compiler pitfall, a renamed or moved file). Update the documentation yourself,
     now, without asking: tick or annotate roadmap items, add the new thing where similar
     things are listed, refresh numbers and paths, extend the change log in
     docs/performance.md. Minimal, factual edits in the existing style.
   - DEVIATION: the change contradicts, weakens, reinterprets or bypasses something
     documented (a hard rule in AGENTS.md, an invariant or the semantics table in
     ARCHITECTURE.md, a roadmap goal, ordering, scope or "Done when", a policy in docs/, a
     claim in README.md), or it introduces a rule, constraint, dependency or design decision
     the documentation never anticipated. Do NOT edit the documentation for these and do not
     mark anything done. Report each one under a heading "Deviations needing approval": what
     the documentation says (file and section), what the change does instead, and the
     choices (revert the change; amend the documentation, with the exact wording you
     propose; or something else). Then ask the user to choose, one decision per deviation
     (AskUserQuestion when available, otherwise end your message with the questions). Apply
     a decision only after it is given.
4. If the change touches nothing the documentation describes, say "Doc review: no
   documentation impact" and do not invent updates.
5. End your final message with a short "Doc review" section: which files you updated and
   why, and any deviations awaiting approval.

Never silently change a rule, requirement, goal or number. Never mark a roadmap item done
unless this turn verified its "Done when". Documentation describes the current state, not
the history; the change log in docs/performance.md is the only place for history.
