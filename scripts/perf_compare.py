#!/usr/bin/env python3
"""Compare or merge Google Benchmark JSON result files.

  perf_compare.py compare BASELINE.json CURRENT.json [--threshold PCT] [--min-abs-ns NS]
  perf_compare.py merge -o OUT.json PART.json [PART.json ...]

`compare` prints a delta table and exits 1 if any benchmark's real time regressed by more
than the threshold (and by more than --min-abs-ns in absolute terms, so sub-nanosecond
benchmarks cannot trip the gate on jitter). With repetitions, the fastest repetition is
compared by default (--stat min); external noise only adds time, so the minimum is the best
estimate of intrinsic cost.

`merge` concatenates the benchmark rows of several result files (scripts/perf.ps1 runs each
benchmark family in its own process so heap state from one family cannot skew another) and
keeps the context of the first file.

Standard library only.
"""
import argparse
import json
import sys


def read(path):
    with open(path, "r", encoding="utf-8") as f:
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
        if b.get("error_occurred"):
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
    return "%.1f %s" % (value, unit)


def ctx_line(ctx):
    return "%s, %s cpus @ %s MHz, %s" % (
        ctx.get("host_name", "?"),
        ctx.get("num_cpus", "?"),
        ctx.get("mhz_per_cpu", "?"),
        ctx.get("library_build_type", "?"),
    )


def cmd_merge(args):
    merged = None
    for path in args.parts:
        data = read(path)
        if merged is None:
            merged = {"context": data.get("context", {}), "benchmarks": []}
        merged["benchmarks"].extend(data.get("benchmarks", []))
    if merged is None:
        print("no input files", file=sys.stderr)
        return 1
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(merged, f, indent=2)
    return 0


def cmd_compare(args):
    base_data = read(args.baseline)
    cur_data = read(args.current)
    base = rows_of(base_data, args.stat, args.metric)
    cur = rows_of(cur_data, args.stat, args.metric)
    base_ctx = base_data.get("context", {})
    cur_ctx = cur_data.get("context", {})

    print("baseline: %s  (%s)" % (args.baseline, ctx_line(base_ctx)))
    print("current:  %s  (%s)" % (args.current, ctx_line(cur_ctx)))
    print("metric: %s, %s of repetitions; gate: >%.0f%% and >%.1f ns"
          % (args.metric, args.stat, args.threshold, args.min_abs_ns))
    if cur_ctx.get("cpu_scaling_enabled"):
        print("warning: CPU frequency scaling is enabled on the current run; timings are noisier.")
    print()

    unit_to_ns = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}
    name_w = max([len(n) for n in list(base) + list(cur)] + [10])
    header = "%-*s %14s %14s %9s  %s" % (name_w, "benchmark", "baseline", "current", "delta", "status")
    print(header)
    print("-" * len(header))

    regressions = []
    improvements = []
    for name in base:
        if name not in cur:
            print("%-*s %14s %14s %9s  %s" % (name_w, name, "", "", "", "MISSING"))
            continue
        b = base[name]
        c = cur[name]
        unit = c.get("time_unit", "ns")
        bv = float(b[args.metric])
        cv = float(c[args.metric])
        delta = (cv - bv) / bv * 100.0 if bv else 0.0
        abs_ns = abs(cv - bv) * unit_to_ns.get(unit, 1.0)
        if abs_ns < args.min_abs_ns:
            status = "ok (below %.1f ns)" % args.min_abs_ns
        elif delta > args.threshold:
            status = "REGRESSION"
            regressions.append((name, delta))
        elif delta < -args.threshold:
            status = "faster"
            improvements.append((name, delta))
        else:
            status = "ok"
        print("%-*s %14s %14s %+8.1f%%  %s" % (name_w, name, fmt(bv, unit), fmt(cv, unit), delta, status))

    for name in cur:
        if name not in base:
            c = cur[name]
            print("%-*s %14s %14s %9s  %s" % (name_w, name, "", fmt(float(c[args.metric]), c.get("time_unit", "ns")), "", "NEW"))

    print()
    print("%d regression(s) over %.0f%%, %d improvement(s)" % (len(regressions), args.threshold, len(improvements)))
    return 1 if regressions else 0


def main(argv):
    # Backward-compatible form: perf_compare.py BASELINE CURRENT [...]
    if argv and argv[0] not in ("compare", "merge", "-h", "--help"):
        argv = ["compare"] + argv

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    cp = sub.add_parser("compare", help="compare a run against a baseline")
    cp.add_argument("baseline")
    cp.add_argument("current")
    cp.add_argument("--threshold", type=float, default=10.0, help="regression threshold in percent")
    cp.add_argument("--min-abs-ns", type=float, default=1.0,
                    help="ignore deltas smaller than this many nanoseconds in absolute terms")
    cp.add_argument("--metric", default="real_time", choices=["real_time", "cpu_time"])
    cp.add_argument("--stat", default="min", choices=["min", "median"],
                    help="which repetition to compare (default min)")
    cp.set_defaults(func=cmd_compare)

    mp = sub.add_parser("merge", help="merge several result files into one")
    mp.add_argument("-o", "--output", required=True)
    mp.add_argument("parts", nargs="+")
    mp.set_defaults(func=cmd_merge)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
