#!/usr/bin/env python3
"""Compare or merge Google Benchmark JSON result files.

  perf_compare.py compare BASELINE.json CURRENT.json [--reference REFERENCE.json]
                          [--threshold PCT] [--min-abs-ns NS]
  perf_compare.py merge -o OUT.json [--context KEY=VALUE ...] PART.json [PART.json ...]

`compare` prints a delta table and exits 1 if any benchmark's real time regressed by more
than the threshold (and by more than --min-abs-ns in absolute terms, so sub-nanosecond
benchmarks cannot trip the gate on jitter). With repetitions, the fastest repetition is
compared by default (--stat min); external noise only adds time, so the minimum is the best
estimate of intrinsic cost.

Time is gated against --reference when it is given: the baseline commit's binaries, which
scripts/perf.ps1 runs interleaved with the current ones in the same session. Both saw the
same machine state, so the day's drift cancels. The committed baseline then anchors the
exact counts and shows the drift as the "machine factor" (median of reference over baseline
across the rows both have). A row the reference lacks gates against the baseline and says
so. Without --reference every row gates against the committed baseline, which reads the
machine's drift as a regression (docs/performance.md, "Noise control").

Benchmarks also report "allocs", heap allocations per iteration (tasfw-perf counts every
operator new). That number is deterministic, so it is gated separately and almost exactly:
an increase of more than --alloc-tolerance allocations per iteration (default 0.1, which
only absorbs one-time set-up amortised over the fixed iteration counts) is a regression
regardless of timing.

Rows that carry "cycles" (CPU cycles per iteration on the benchmark thread; for Tier D the
process total over every thread) show their change against the same anchor as the time in
the "cycles" column. Cycles ignore time spent descheduled and mostly ignore the clock, so
they are the second opinion on a wall-time shift; they are reported, not gated.

`--counts-only` keeps just the exact-count and allocation gates and reports everything else;
CI runs Tier C that way, since a hosted runner's timings compare to nothing.

Tier C and D rows carry work counts (frameAdvances, saves, loads, shots, scripts, blocks,
solutions, validationFailures). They are exact: the workloads run with the cost model off,
or report only what is deterministic, so any increase is a regression ("the framework now
does more work") and fails the compare; a decrease is printed and left for the reviewer to
confirm and re-baseline. Counts always compare against the committed baseline; a reference
whose counts differ from the baseline's is not the baseline commit's binary, and the
compare says so.

Multithreaded rows (name ending in "/threads:N", Google Benchmark's ThreadRange) report
time and items_per_second per thread, so the aggregate rate is N times the row's. Their
efficiency is the per-thread rate at N threads over the rate of the same benchmark at one
thread, computed from each file's own rows; a drop of more than --efficiency-tolerance
percentage points against the anchor (reference when it has the row, else baseline) is a
regression (the Tier B thread-scaling gate) at up to EFFICIENCY_GATE_MAX_THREADS threads.
Beyond that the rows share physical cores with each other and with whatever else runs,
move a few points between runs of the same binary, and are reported only.

`merge` concatenates the benchmark rows of several result files (scripts/perf.ps1 runs each
benchmark family in its own process so heap state from one family cannot skew another) and
keeps the context of the first file, plus every --context KEY=VALUE given.

Standard library only.
"""
import argparse
import json
import re
import statistics
import sys

# Counters gated on exact equality (see the module docstring).
EXACT_COUNTERS = ("frameAdvances", "saves", "loads", "shots", "scripts", "blocks", "solutions", "validationFailures",
                  "stateBytes")

THREADS_RE = re.compile(r"^(.*)/threads:(\d+)$")
EFFICIENCY_GATE_MAX_THREADS = 8

UNIT_TO_NS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}


def efficiencies(rows):
    """{name: percent} for multithreaded rows: the per-thread rate at N threads over the rate
    of the same benchmark at one thread, from this file's own rows."""
    out = {}
    for name, row in rows.items():
        m = THREADS_RE.match(name)
        if not m or not row.get("items_per_second"):
            continue
        n = int(m.group(2))
        single = rows.get("%s/threads:1" % m.group(1))
        if n <= 1 or single is None or not single.get("items_per_second"):
            continue
        out[name] = float(row["items_per_second"]) / float(single["items_per_second"]) * 100.0
    return out


def read(path):
    # utf-8-sig: PowerShell's Set-Content -Encoding utf8 writes a BOM (the Tier D part).
    with open(path, "r", encoding="utf-8-sig") as f:
        return json.load(f)


def rows_of(data, stat="min", metric="real_time"):
    """Return {benchmark name: row} with one representative row per benchmark.

    stat="min": the fastest repetition (external noise only ever adds time, so the minimum
    is the best estimate of intrinsic cost for a microbenchmark).
    stat="median": the median aggregate if present, else the median of the repetitions.
    """
    reps = {}
    medians = {}
    for b in data.get("benchmarks", []):
        # Skipped rows (the DLL-gated Tier B family without a DLL) carry no timing.
        if b.get("error_occurred") or b.get("skipped") or b.get("skip_reason"):
            continue
        name = b.get("run_name") or b["name"]
        if b.get("run_type", "iteration") == "aggregate":
            if b.get("aggregate_name") == "median":
                medians[name.rsplit("_median", 1)[0] if name.endswith("_median") else name] = b
        else:
            reps.setdefault(name, []).append(b)

    if stat == "median" and medians:
        return medians
    rows = {}
    for name, rs in reps.items():
        rs = sorted(rs, key=lambda r: float(r[metric]))
        rows[name] = rs[0] if stat == "min" else rs[len(rs) // 2]
    if not rows:
        return medians
    return rows


def fmt(value, unit):
    if value >= 1000 and unit == "ns":
        return "%.2f us" % (value / 1000.0)
    if value >= 1000 and unit == "us":
        return "%.2f ms" % (value / 1000.0)
    if value >= 1000 and unit == "ms":
        return "%.2f s" % (value / 1000.0)
    return "%.1f %s" % (value, unit)


def fmt_count(value):
    return "%.0f" % value if abs(value - round(value)) < 0.005 else "%.2f" % value


def count_changes(base_row, cur_row):
    """[(counter, baseline, current)] for the exact counters both rows carry, where they differ."""
    changes = []
    for key in EXACT_COUNTERS:
        if key in base_row and key in cur_row:
            b, c = float(base_row[key]), float(cur_row[key])
            if abs(b - c) > 1e-9:
                changes.append((key, b, c))
    return changes


def counts_of(row):
    """The exact counters a row carries, as 'key value' pairs for display."""
    return ", ".join("%s %s" % (key, fmt_count(float(row[key]))) for key in EXACT_COUNTERS if key in row)


def ctx_line(ctx):
    return "%s, %s cpus @ %s MHz, %s" % (
        ctx.get("host_name", "?"),
        ctx.get("num_cpus", "?"),
        ctx.get("mhz_per_cpu", "?"),
        ctx.get("library_build_type", "?"),
    )


def alloc_cell(base_row, cur_row):
    """'baseline -> current' allocations per iteration, or what is known of them."""
    def one(row):
        if row is None or row.get("allocs") is None:
            return "n/a"
        v = float(row["allocs"])
        return "%.0f" % v if abs(v - round(v)) < 0.005 else "%.2f" % v
    return "%s -> %s" % (one(base_row), one(cur_row))


def cycles_cell(anchor_row, cur_row):
    """Percent change of cycles per iteration against the anchor, or '-' when either lacks it."""
    if anchor_row is None or cur_row is None:
        return "-"
    a, c = anchor_row.get("cycles"), cur_row.get("cycles")
    if a is None or c is None or float(a) == 0.0:
        return "-"
    return "%+.1f%%" % ((float(c) - float(a)) / float(a) * 100.0)


def cmd_merge(args):
    merged = None
    for path in args.parts:
        data = read(path)
        if merged is None:
            merged = {"context": dict(data.get("context", {})), "benchmarks": []}
        merged["benchmarks"].extend(data.get("benchmarks", []))
    if merged is None:
        print("no input files", file=sys.stderr)
        return 1
    for item in args.context or []:
        key, sep, value = item.partition("=")
        if not sep:
            print("--context expects KEY=VALUE, got %r" % item, file=sys.stderr)
            return 2
        try:
            merged["context"][key] = float(value)
        except ValueError:
            merged["context"][key] = value
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(merged, f, indent=2)
    return 0


def cmd_compare(args):
    base_data = read(args.baseline)
    cur_data = read(args.current)
    ref_data = read(args.reference) if args.reference else None
    base = rows_of(base_data, args.stat, args.metric)
    cur = rows_of(cur_data, args.stat, args.metric)
    ref = rows_of(ref_data, args.stat, args.metric) if ref_data else {}
    base_eff = efficiencies(base)
    cur_eff = efficiencies(cur)
    ref_eff = efficiencies(ref)
    base_ctx = base_data.get("context", {})
    cur_ctx = cur_data.get("context", {})
    has_ref = bool(ref)

    print("baseline:  %s  (%s)" % (args.baseline, ctx_line(base_ctx)))
    if args.reference:
        print("reference: %s  (%s)" % (args.reference, ctx_line(ref_data.get("context", {}))))
    print("current:   %s  (%s)" % (args.current, ctx_line(cur_ctx)))
    print("metric: %s, %s of repetitions; gate: >%.0f%% and >%.1f ns against the %s"
          % (args.metric, args.stat, args.threshold, args.min_abs_ns,
             "reference (baseline where the reference lacks the row)" if has_ref else "baseline"))
    if args.counts_only:
        print("counts only: time, overhead and efficiency are reported, not gated (a CI runner's timings are not comparable)")
    if cur_ctx.get("cpu_scaling_enabled"):
        print("warning: CPU frequency scaling is enabled on the current run; timings are noisier.")
    if args.reference and not has_ref:
        print("warning: the reference file has no rows; every row gates against the baseline.")

    # Machine factor: how today's machine reads against the day the baseline was saved, from
    # the same binary. Also the check that the reference really is the baseline commit.
    if has_ref:
        ratios = []
        mismatched = []
        for name in base:
            if name not in ref:
                continue
            bv = float(base[name][args.metric])
            if bv:
                ratios.append(float(ref[name][args.metric]) / bv)
            if count_changes(base[name], ref[name]):
                mismatched.append(name)
        if ratios:
            print("machine factor: the reference reads %.2fx the baseline (median over %d rows; the day's drift, "
                  "cancelled by the gate)" % (statistics.median(ratios), len(ratios)))
        if mismatched:
            print("warning: the reference's exact counts differ from the baseline's on %d row(s) (%s); "
                  "is perf/reference the baseline commit's build? Counts still gate against the baseline."
                  % (len(mismatched), ", ".join(mismatched[:3]) + (", ..." if len(mismatched) > 3 else "")))
    print()

    name_w = max([len(n) for n in list(base) + list(cur)] + [10])
    if has_ref:
        header = "%-*s %14s %14s %14s %9s %9s %8s %17s  %s" % (
            name_w, "benchmark", "baseline", "reference", "current", "vs ref", "vs base", "cycles", "allocs/iter", "status")
    else:
        header = "%-*s %14s %14s %9s %8s %17s  %s" % (
            name_w, "benchmark", "baseline", "current", "delta", "cycles", "allocs/iter", "status")
    print(header)
    print("-" * len(header))

    def row_line(name, base_s, ref_s, cur_s, d_anchor, d_base, cyc, allocs, status):
        if has_ref:
            return "%-*s %14s %14s %14s %9s %9s %8s %17s  %s" % (
                name_w, name, base_s, ref_s, cur_s, d_anchor, d_base, cyc, allocs, status)
        return "%-*s %14s %14s %9s %8s %17s  %s" % (name_w, name, base_s, cur_s, d_base, cyc, allocs, status)

    regressions = []
    improvements = []
    alloc_regressions = []
    count_regressions = []
    efficiency_regressions = []
    for name in base:
        if name not in cur:
            print(row_line(name, "", "", "", "", "", "", "", "MISSING"))
            continue
        b = base[name]
        c = cur[name]
        r = ref.get(name)
        anchor = r if r is not None else b
        unit = c.get("time_unit", "ns")
        bv = float(b[args.metric])
        cv = float(c[args.metric])
        av = float(anchor[args.metric])
        delta_base = (cv - bv) / bv * 100.0 if bv else 0.0
        delta = (cv - av) / av * 100.0 if av else 0.0
        abs_ns = abs(cv - av) * UNIT_TO_NS.get(unit, 1.0)
        if args.counts_only:
            status = "ok (counts only)"
        elif abs_ns < args.min_abs_ns:
            status = "ok (below %.1f ns)" % args.min_abs_ns
        elif delta > args.threshold:
            status = "REGRESSION"
            regressions.append((name, delta))
        elif delta < -args.threshold:
            status = "faster"
            improvements.append((name, delta))
        else:
            status = "ok"
        if has_ref and r is None:
            status += " (vs baseline)"

        allocs = alloc_cell(b, c)
        ba, ca = b.get("allocs"), c.get("allocs")
        if ba is not None and ca is not None and float(ca) - float(ba) > args.alloc_tolerance:
            status = "ALLOC REGRESSION" if "REGRESSION" not in status else "REGRESSION + ALLOCS"
            alloc_regressions.append((name, float(ba), float(ca)))

        changes = count_changes(b, c)
        if any(cur_v > base_v for _, base_v, cur_v in changes):
            status = "COUNT REGRESSION" if "REGRESSION" not in status else status + " + COUNTS"
            count_regressions.append(name)
        elif changes:
            status += " (counts down, re-baseline after review)"

        # Tier C overhead: the share of wall time outside the resource, gated in points.
        bo, co = anchor.get("overheadPct"), c.get("overheadPct")
        if not args.counts_only and bo is not None and co is not None and float(co) - float(bo) > args.overhead_tolerance:
            status = "OVERHEAD REGRESSION" if "REGRESSION" not in status else status + " + OVERHEAD"
            count_regressions.append(name)
            changes = changes + [("overheadPct", float(bo), float(co))]

        # Thread scaling: efficiency relative to one thread, gated in points (drops only).
        ae = ref_eff.get(name) if r is not None else base_eff.get(name)
        ce = cur_eff.get(name)
        if ae is not None and ce is not None:
            changes = changes + [("efficiencyPct", ae, ce)]
            gated = int(THREADS_RE.match(name).group(2)) <= EFFICIENCY_GATE_MAX_THREADS
            if gated and not args.counts_only and ae - ce > args.efficiency_tolerance:
                status = "EFFICIENCY REGRESSION" if "REGRESSION" not in status else status + " + EFFICIENCY"
                efficiency_regressions.append(name)

        print(row_line(name, fmt(bv, unit), fmt(float(r[args.metric]), unit) if r is not None else "-", fmt(cv, unit),
                       "%+.1f%%" % delta if r is not None else "-", "%+.1f%%" % delta_base,
                       cycles_cell(anchor, c), allocs, status))
        for key, base_v, cur_v in changes:
            print("%-*s   %s %s -> %s" % (name_w, "", key, fmt_count(base_v), fmt_count(cur_v)))

    for name in cur:
        if name not in base:
            c = cur[name]
            print(row_line(name, "", "", fmt(float(c[args.metric]), c.get("time_unit", "ns")), "", "", "-",
                           alloc_cell(None, c), "NEW"))
            if counts_of(c):
                print("%-*s   %s" % (name_w, "", counts_of(c)))
            if name in cur_eff:
                print("%-*s   efficiencyPct %s" % (name_w, "", fmt_count(cur_eff[name])))

    print()
    print("%d regression(s) over %.0f%%, %d improvement(s), %d allocation regression(s) over %.2f/iter, %d count regression(s), %d efficiency regression(s) over %.0f points"
          % (len(regressions), args.threshold, len(improvements), len(alloc_regressions), args.alloc_tolerance,
             len(count_regressions), len(efficiency_regressions), args.efficiency_tolerance))
    return 1 if regressions or alloc_regressions or count_regressions or efficiency_regressions else 0


def main(argv):
    # Backward-compatible form: perf_compare.py BASELINE CURRENT [...]
    if argv and argv[0] not in ("compare", "merge", "-h", "--help"):
        argv = ["compare"] + argv

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    cp = sub.add_parser("compare", help="compare a run against a baseline")
    cp.add_argument("baseline")
    cp.add_argument("current")
    cp.add_argument("--reference",
                    help="results of the baseline commit's binaries run interleaved with the current ones "
                         "in the same session; time and efficiency gate against it where it has the row")
    cp.add_argument("--counts-only", action="store_true",
                    help="gate only the exact counts and allocations (CI runners, whose timings are not "
                         "comparable to a baseline); time, overhead and efficiency are reported")
    cp.add_argument("--threshold", type=float, default=10.0, help="regression threshold in percent")
    cp.add_argument("--min-abs-ns", type=float, default=1.0,
                    help="ignore deltas smaller than this many nanoseconds in absolute terms")
    cp.add_argument("--metric", default="real_time", choices=["real_time", "cpu_time"])
    cp.add_argument("--alloc-tolerance", type=float, default=0.1,
                    help="allowed increase in allocations per iteration before it counts as a regression")
    cp.add_argument("--stat", default="min", choices=["min", "median"],
                    help="which repetition to compare (default min)")
    cp.add_argument("--overhead-tolerance", type=float, default=2.0,
                    help="allowed increase of the Tier C overheadPct counter, in percentage points")
    cp.add_argument("--efficiency-tolerance", type=float, default=5.0,
                    help="allowed drop of thread-scaling efficiency (per-thread rate at N threads over the "
                         "single-thread rate), in percentage points")
    cp.set_defaults(func=cmd_compare)

    mp = sub.add_parser("merge", help="merge several result files into one")
    mp.add_argument("-o", "--output", required=True)
    mp.add_argument("--context", action="append", metavar="KEY=VALUE",
                    help="add or overwrite a context entry of the merged file (numbers are stored as numbers)")
    mp.add_argument("parts", nargs="+")
    mp.set_defaults(func=cmd_merge)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
