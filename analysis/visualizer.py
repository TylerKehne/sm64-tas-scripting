#!/usr/bin/env python3
"""The scattershot viewer (ROADMAP 4.4): a live plot of a run's CSV, one tab per run.

The search launches it, detached, when a run's CSV opens (`Visualization` in
tasfw-scattershot, `.Visualize(...)` on the builders, a stage's "visualize" block in the
pipeline's config), with the run's parameters file as its one argument. The first viewer
owns a port on localhost; a later launch hands its parameters file to that viewer over the
port and exits, so every run of a pipeline is a tab in one window, and a viewer left open
collects later runs too.

It reads the CSV incrementally (a byte offset per tab, a held-back partial last line),
folds rows into a newest-per-bin table as they arrive (the R script's grouping, once per
row; the raw rows are dropped, so memory is bounded by the bins) and redraws the selected
tab when rows came in, at the refresh rate set in the window. It lowers its own priority so
the brute forcer's threads win any contested core; a tab whose run finished stops polling;
a tab past the segment cap doubles its bins. A tab's filter panel lists every CSV column (one
the run names categorical as checkboxes with counts, any other as a min and a max box under
the range seen; it starts as the run's own `filters`) and keeps a row that fails the
filter out of the table; since the raw rows are gone, a changed filter re-reads the CSV from
its start, as an opening tab does.

    python analysis/visualizer.py <params.json>                                  # what the search runs
    python analysis/visualizer.py --once --csv <file> [--out <png>] [--rows N] [--filter TERMS]   # headless, one PNG
    TASFW_VISUALIZER_HEADLESS=1 python analysis/visualizer.py <params.json>      # tail the run with no window

Headless following (ROADMAP 4.10) is the window's polling, binning and redraw schedule on the
Agg backend, with fixed defaults instead of the settings file, one PNG at the end, and a
summary of what it cost (CPU seconds, ticks, redraws, the longest redraw) written beside the
parameters file as <params stem>.summary.json, which the perf suite gates.

First start: when numpy and matplotlib are missing it creates analysis/.venv, installs
requirements.txt into it and re-executes itself from there (a tiny window says so).
TASFW_VISUALIZER_NO_BOOTSTRAP=1 disables that; CI installs the packages itself. The
refresh rate, port and segment cap live in analysis/visualizer_settings.json, the log in
analysis/visualizer.log. README.md, "The viewer".
"""
import argparse
import collections
import csv
import json
import logging
import math
import os
import queue
import re
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
VENV = HERE / ".venv"
REQUIREMENTS = HERE / "requirements.txt"
SETTINGS_FILE = HERE / "visualizer_settings.json"
LOG_FILE = HERE / "visualizer.log"
DEFAULT_SETTINGS = {"refreshSeconds": 5.0, "port": 47323, "maxSegments": 200000, "showFilters": True}
ANGLE_TO_RADIANS = math.pi / 32768  # an SM64 angle unit
IDLE_LIMIT_SECONDS = 600  # headless: give up on a run that stopped growing without its finished flag
REDRAW_SHARE = 0.05  # a redraw may take this share of the interval before it: the interval stretches to keep it
COST_WINDOW_SECONDS = 30  # the window's CPU readout averages over this long
FACET_VALUES_CAP = 256  # a categorical column with more distinct values than this becomes a range (its counts would grow without bound)
FILTER_GRAMMAR = ("a term is 'column >= low', 'column <= high', 'low <= column <= high', 'column == value' or "
                  "'column in value value ...' (ranges are inclusive), commas or 'and' between terms")


def next_interval(refresh, redraw_seconds):
    """The wait before the next tick: the refresh setting, stretched so that the redraw just
    done was at most REDRAW_SHARE of the interval, which bounds redrawing at that share of one
    core however large a tab's table grows."""
    return max(float(refresh), redraw_seconds / REDRAW_SHARE)

log = logging.getLogger("visualizer")


# ----------------------------------------------------------------------------- bootstrap

def venv_python(windowed):
    if os.name == "nt":
        return VENV / "Scripts" / ("pythonw.exe" if windowed else "python.exe")
    return VENV / "bin" / "python3"


def bootstrap(windowed):
    """Return if numpy and matplotlib import; else make analysis/.venv and re-execute from it."""
    try:
        import numpy  # noqa: F401
        import matplotlib  # noqa: F401
        return
    except ImportError:
        pass
    if os.environ.get("TASFW_VISUALIZER_NO_BOOTSTRAP"):
        raise SystemExit("numpy and matplotlib are not installed (TASFW_VISUALIZER_NO_BOOTSTRAP is set, so nothing is installed)")
    if Path(sys.prefix).resolve() == VENV.resolve():
        raise SystemExit(f"{VENV} lacks numpy or matplotlib; delete it and start the viewer again")

    python = venv_python(windowed=False)
    requirements = REQUIREMENTS.read_text(encoding="utf-8")
    installed = VENV / "requirements.installed"  # what pip last installed; a later start with the same text skips pip
    if not python.exists() or not installed.exists() or installed.read_text(encoding="utf-8") != requirements:
        status = StatusWindow("Installing the viewer's packages into analysis/.venv (first start only)...") if windowed else None
        flags = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
        try:
            if not python.exists():
                log.info("creating %s with %s", VENV, sys.executable)
                run_and_pump(status, [sys.executable, "-m", "venv", str(VENV)], flags)
            log.info("installing %s into %s", REQUIREMENTS, VENV)
            run_and_pump(status, [str(python), "-m", "pip", "install", "--quiet", "-r", str(REQUIREMENTS)], flags)
            installed.write_text(requirements, encoding="utf-8")
        finally:
            if status:
                status.close()
    target = venv_python(windowed)
    command = [str(target), str(Path(__file__).resolve())] + sys.argv[1:]
    log.info("re-executing from %s", target)
    if os.name == "nt":
        # os.execv on Windows keeps this process alive beside the new one: spawn detached and leave.
        subprocess.Popen(command, creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP, close_fds=True)
        sys.exit(0)
    os.execv(command[0], command)


def run_and_pump(status, command, flags):
    process = subprocess.Popen(command, **flags)
    while process.poll() is None:
        if status:
            status.pump()
        time.sleep(0.1)
    if process.returncode != 0:
        raise SystemExit(f"{command[0]} failed with {process.returncode} (see {LOG_FILE})")


class StatusWindow:
    """A label in a window while pip runs, so a first start under pythonw is not silent."""

    def __init__(self, text):
        try:
            import tkinter as tk
            self.root = tk.Tk()
            self.root.title("TASFW viewer")
            tk.Label(self.root, text=text, padx=24, pady=16).pack()
            self.root.update()
        except Exception:  # no display: the log is the status
            self.root = None

    def pump(self):
        if self.root:
            self.root.update()

    def close(self):
        if self.root:
            self.root.destroy()


# ------------------------------------------------------------------------------ settings

def load_settings():
    settings = dict(DEFAULT_SETTINGS)
    try:
        with open(SETTINGS_FILE, encoding="utf-8") as f:
            settings.update({k: v for k, v in json.load(f).items() if k in DEFAULT_SETTINGS})
    except (OSError, ValueError):
        pass
    return settings


def save_settings(settings):
    try:
        with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
            json.dump(settings, f, indent=1)
    except OSError as e:
        log.warning("cannot save settings: %s", e)


def lower_priority():
    """The brute forcer's threads win any contested core."""
    try:
        if os.name == "nt":
            import ctypes
            kernel32 = ctypes.windll.kernel32
            kernel32.SetPriorityClass(kernel32.GetCurrentProcess(), 0x00004000)  # BELOW_NORMAL_PRIORITY_CLASS
        else:
            os.nice(10)
    except Exception as e:
        log.warning("cannot lower priority: %s", e)


# ------------------------------------------------------------------------- a run's data

class RunData:
    """One run's CSV, read incrementally into a newest-per-bin table of (shot, frame, x, y, angle, speed).

    The filter is a dict, column -> {"min", "max", "values"} with the keys in force: a row is
    binned when every filtered column of it lies in the range and, with "values", is one of
    them. The parameters file's `filters` ({column, min, max}) start it; the tab's panel sets
    it, and --filter from text, "MarioFSpd <= 8, Phase in 1 5" (FILTER_GRAMMAR). Beside the
    table the facets: per column, the least and greatest value seen and, for a column the
    parameters file's `categorical` names, how many sampled rows hold each value."""

    def __init__(self, params_path=None, params=None):
        self.params_path = Path(params_path).resolve() if params_path else None
        self.params_mtime = None
        self.params = params if params is not None else self._read_params()
        self.csv_path = Path(self.params["csv"])
        self.title = self.params.get("title") or self.csv_path.name
        self.columns = [self.params.get(k, d) for k, d in (("x", "MarioZ"), ("y", "MarioX"), ("angle", "MarioFYaw"), ("speed", "MarioFSpd"))]
        self.base_bins = [float(self.params.get(k, d)) for k, d in (("binX", 0.1), ("binY", 0.1), ("binAngle", 16), ("binSpeed", 0.1))]
        self.filter = {}
        for f in self.params.get("filters", []):  # an open bound is an absent key
            part = {k: float(f[k]) for k in ("min", "max") if k in f}
            if part:
                self.filter.setdefault(f["column"], {}).update(part)
        self.categorical = [str(c) for c in self.params.get("categorical", [])]  # filtered by value; every other column by range
        view = self.params.get("view")  # a fixed window centered on (x, y), square to its units
        self.view = {k: float(view[k]) for k in ("x", "y", "width", "height")} if view else None
        self.sampled_only = bool(self.params.get("sampledOnly", True))
        self.finished = bool(self.params.get("finished", False))
        self.reported_rows = int(self.params.get("rows", 0))
        self.dirty = False
        self.table = {}
        self.reset()

    def reset(self):
        """Forget what was read: the next poll reads the CSV from its start (the filter changed,
        or the file was rewritten). What was drawn is stale until the table refills."""
        self.dirty |= bool(self.table)
        self.table = {}
        self.offset = 0
        self.partial = b""
        self.header = None
        self.indices = None
        self.rows = 0
        self.skipped = 0
        self.bad = 0
        self.coarsened = 0
        self.bins = list(self.base_bins)
        self.error = None
        self.drained = False  # finished and nothing left to read
        self.lo = self.hi = None  # per column, the least and greatest value seen
        self.counts = None  # per column, value -> sampled rows holding it for a categorical column, else None
        self.counting = []  # the (index, counts) still counted
        self.overflowed = set()  # categorical columns past FACET_VALUES_CAP distinct values, ranges from then on
        self.stats_changed = True  # the facets differ from what the panel shows

    @staticmethod
    def _number(token):
        """The token as a float, hex included, or None when it is not a number."""
        try:
            return float(int(token, 16)) if re.fullmatch(r"[-+]?0[xX][0-9a-fA-F]+", token) else float(token)
        except ValueError:
            return None

    @staticmethod
    def _format_number(value):
        return str(int(value)) if value == int(value) and abs(value) < 2 ** 53 else repr(value)

    @staticmethod
    def parse_filter(text):
        """A filter from its text (FILTER_GRAMMAR); ValueError names the term it cannot read."""
        spec = {}
        for term in re.split(r",|\band\b", text or ""):
            if not term.strip():
                continue
            tokens = re.findall(r"<=|>=|==|!=|<|>|[^\s<>=!,]+", term)
            number = [RunData._number(t) for t in tokens]
            column = part = None
            if len(tokens) == 3 and tokens[1] in ("<=", ">=", "==") and number[0] is None and number[2] is not None:
                column, part = tokens[0], {{"<=": "max", ">=": "min", "==": "values"}[tokens[1]]: number[2]}
            elif len(tokens) == 3 and tokens[1] in ("<=", ">=", "==") and number[0] is not None and number[2] is None:
                column, part = tokens[2], {{"<=": "min", ">=": "max", "==": "values"}[tokens[1]]: number[0]}
            elif (len(tokens) == 5 and tokens[1] == "<=" and tokens[3] == "<="
                  and number[0] is not None and number[2] is None and number[4] is not None):
                column, part = tokens[2], {"min": number[0], "max": number[4]}
            elif len(tokens) >= 3 and tokens[1] == "in" and number[0] is None and all(n is not None for n in number[2:]):
                column, part = tokens[0], {"values": set(number[2:])}
            if column is None:
                raise ValueError(f"cannot read the filter term '{term.strip()}': {FILTER_GRAMMAR}")
            if "values" in part and not isinstance(part["values"], set):
                part["values"] = {part["values"]}
            spec.setdefault(column, {}).update(part)
        return spec

    @staticmethod
    def format_filter(spec):
        """The filter as text the grammar reads back."""
        n = RunData._format_number
        parts = []
        for column, part in spec.items():
            if "min" in part and "max" in part:
                parts.append(f"{n(part['min'])} <= {column} <= {n(part['max'])}")
            elif "min" in part:
                parts.append(f"{column} >= {n(part['min'])}")
            elif "max" in part:
                parts.append(f"{column} <= {n(part['max'])}")
            values = sorted(part.get("values", ()))
            if len(values) == 1:
                parts.append(f"{column} == {n(values[0])}")
            elif values:
                parts.append(f"{column} in {' '.join(n(v) for v in values)}")
        return ", ".join(parts)

    def set_filter_text(self, text):
        """set_filter from text; the message when the text does not parse."""
        try:
            spec = self.parse_filter(text)
        except ValueError as e:
            return str(e)
        return self.set_filter(spec)

    def set_filter(self, spec):
        """Replace the filter and re-read the CSV from its start. The message when it names a
        column the CSV lacks or puts a min above its max, else None (nothing happens when the
        filter is the one in force)."""
        spec = {column: {k: v for k, v in part.items() if v is not None and v != set()} for column, part in spec.items()}
        spec = {column: part for column, part in spec.items() if part}
        if self.header is not None:
            missing = [c for c in spec if c not in self.header]
            if missing:
                return f"columns not in the CSV: {', '.join(missing)} (it has {', '.join(self.header)})"
        for column, part in spec.items():
            if "min" in part and "max" in part and part["min"] > part["max"]:
                return f"{column}: the min is above the max"
        if spec == self.filter:
            return None
        self.filter = spec
        self.reset()
        return None

    def _read_params(self):
        with open(self.params_path, encoding="utf-8") as f:
            params = json.load(f)
        self.params_mtime = self.params_path.stat().st_mtime
        return params

    def _reread_params(self):
        if self.params_path is None:
            return
        try:
            mtime = self.params_path.stat().st_mtime
        except OSError:
            return
        if mtime == self.params_mtime:
            return
        try:
            params = self._read_params()
        except (OSError, ValueError):
            return
        self.finished = bool(params.get("finished", False))
        self.reported_rows = int(params.get("rows", 0))

    def poll(self, max_rows=None):
        """Read what the CSV gained since the last call. True when the table changed."""
        if self.drained or self.error:
            return False
        self._reread_params()
        try:
            size = self.csv_path.stat().st_size
        except OSError:
            return False
        if size < self.offset:  # rewritten from the start
            self.reset()
        if size == self.offset:
            self.drained = self.finished
            return False
        with open(self.csv_path, "rb") as f:
            f.seek(self.offset)
            chunk = f.read()
        self.offset += len(chunk)
        data = self.partial + chunk
        cut = data.rfind(b"\n")
        if cut < 0:
            self.partial = data
            return False
        self.partial = data[cut + 1:]
        changed = self._parse(data[:cut].decode("utf-8", "replace").splitlines(), max_rows)
        self.dirty |= changed
        return changed

    def _index(self):
        names = [self.header.index(c) if c in self.header else None for c in self.columns]
        missing = [c for c, i in zip(self.columns, names) if i is None]
        filters = []  # (column index, least, greatest, the values allowed or None)
        for column, part in self.filter.items():
            if column in self.header:
                filters.append((self.header.index(column), part.get("min", -math.inf), part.get("max", math.inf),
                                frozenset(part["values"]) if part.get("values") else None))
            else:
                missing.append(column)
        for fixed in ("Shot", "Frame", "Sampled"):
            if fixed not in self.header:
                missing.append(fixed)
        for column in self.categorical:
            if column not in self.header and column not in missing:
                missing.append(column)
        if missing:
            self.error = f"columns not in the CSV: {', '.join(missing)} (it has {', '.join(self.header)})"
            log.error("%s: %s", self.title, self.error)
            return
        self.indices = {
            "shot": self.header.index("Shot"), "frame": self.header.index("Frame"), "sampled": self.header.index("Sampled"),
            "x": names[0], "y": names[1], "angle": names[2], "speed": names[3], "filters": filters,
        }
        self.counts = [{} if name in self.categorical else None for name in self.header]
        self.counting = [(j, counts) for j, counts in enumerate(self.counts) if counts is not None]

    def _key(self, x, y, angle, speed):
        b = self.bins
        return (math.floor(x / b[0]), math.floor(y / b[1]), math.floor(angle / b[2]), math.floor(speed / b[3]))

    def _parse(self, lines, max_rows):
        changed = False
        for row in csv.reader(lines):
            if self.header is None:
                self.header = row
                self._index()
                if self.error:
                    return False
                continue
            if max_rows is not None and self.rows >= max_rows:
                self.drained = True
                break
            self.rows += 1
            i = self.indices
            try:
                if self.sampled_only and row[i["sampled"]] != "1":
                    self.skipped += 1
                    continue
                values = [float(v) for v in row]  # every cell is a number
                shot, frame = int(values[i["shot"]]), int(values[i["frame"]])
            except (ValueError, IndexError, OverflowError):
                self.bad += 1
                continue
            if len(values) != len(self.header):
                self.bad += 1
                continue
            # The facets take every sampled row, filtered or not: each column's range and, for
            # a categorical column, how many rows hold each value.
            lo, hi = self.lo, self.hi
            if lo is None:
                self.lo, self.hi = list(values), list(values)
            else:
                for j, v in enumerate(values):
                    if v < lo[j]:
                        lo[j] = v
                    elif v > hi[j]:
                        hi[j] = v
            for j, counts in self.counting:
                text = row[j]
                n = counts.get(text)
                if n is not None:
                    counts[text] = n + 1
                elif len(counts) < FACET_VALUES_CAP:
                    counts[text] = 1
                else:  # too many to list: a range from here on (the loop runs on over the old list)
                    self.counts[j] = None
                    self.overflowed.add(self.header[j])
                    self.counting = [c for c in self.counting if c[0] != j]
            self.stats_changed = True
            if i["filters"] and any(not (low <= values[j] <= high) or (chosen is not None and values[j] not in chosen)
                                    for j, low, high, chosen in i["filters"]):
                self.skipped += 1
                continue
            x, y, angle, speed = values[i["x"]], values[i["y"]], values[i["angle"]], values[i["speed"]]
            key = self._key(x, y, angle, speed)
            old = self.table.get(key)
            if old is None or shot >= old[0]:  # the newest shot keeps the bin, the later row within it
                self.table[key] = (shot, frame, x, y, angle, speed)
                changed = True
        return changed

    def coarsen(self, max_segments):
        """Double the bins until the table fits the segment cap; the newest block keeps each bin."""
        while max_segments > 0 and len(self.table) > max_segments:
            self.bins = [b * 2 for b in self.bins]
            merged = {}
            for row in self.table.values():
                key = self._key(row[2], row[3], row[4], row[5])
                old = merged.get(key)
                if old is None or row[0] >= old[0]:
                    merged[key] = row
            self.table = merged
            self.coarsened += 1
            self.dirty = True
            log.info("%s: %d bins over the cap of %d, bins doubled to %s", self.title, len(merged), max_segments, self.bins)

    def facets(self):
        """Per CSV column, in the CSV's order: (name, value -> sampled rows holding it or None
        past FACET_VALUES_CAP distinct values, the least seen, the greatest seen)."""
        if self.header is None or self.counts is None:
            return []
        lo = self.lo or [None] * len(self.header)
        hi = self.hi or [None] * len(self.header)
        return [(name, self.counts[j], lo[j], hi[j]) for j, name in enumerate(self.header)]

    def arrays(self):
        import numpy as np
        if not self.table:
            return None
        rows = np.array(list(self.table.values()), dtype=float)
        return rows[:, 0], rows[:, 1], rows[:, 2], rows[:, 3], rows[:, 4], rows[:, 5]

    def status(self):
        parts = [f"{self.rows} rows", f"{len(self.table)} drawn"]
        if self.skipped:
            parts.append(f"{self.skipped} filtered")
        if self.bad:
            parts.append(f"{self.bad} unreadable")
        if self.coarsened:
            parts.append(f"bins x{2 ** self.coarsened}")
        if self.filter:
            parts.append(f"filter {self.format_filter(self.filter)}")
        parts.append("finished" if self.finished else "running")
        if self.error:
            parts.append(self.error)
        return ", ".join(parts)


# ----------------------------------------------------------------------------- the plot

class Plot:
    """The R script's vector field: an arrow per bin from behind the block along its facing yaw,
    its length the speed, its color the shot that found it (orange for the oldest, green for the
    newest; one shot alone is all newest, so green) and its alpha the frame (late frames fade)."""

    def __init__(self, figure):
        self.figure = figure
        self.axes = figure.add_subplot()
        self.lines = None
        self.tips = None
        from matplotlib.colors import LinearSegmentedColormap
        self.cmap = LinearSegmentedColormap.from_list("progress", ["darkorange", "darkgreen"])

    def draw(self, data, autoscale=True):
        import numpy as np
        from matplotlib.collections import LineCollection
        arrays = data.arrays()
        self.axes.set_title(f"{data.title}\n{data.status()}", fontsize=9)
        self.axes.set_xlabel(data.columns[0])
        self.axes.set_ylabel(data.columns[1])
        if data.view is not None:
            view = data.view
            self.axes.set_xlim(view["x"] - view["width"] / 2, view["x"] + view["width"] / 2)
            self.axes.set_ylim(view["y"] - view["height"] / 2, view["y"] + view["height"] / 2)
            self.axes.set_aspect("equal", adjustable="box")
            autoscale = False
        if arrays is None:
            if self.lines is not None:  # the table emptied (a filter, a rewritten CSV): clear what was drawn
                self.lines.set_segments([])
                self.tips.set_offsets(np.empty((0, 2)))
            return
        shot, frame, x, y, angle, speed = arrays
        radians = angle * ANGLE_TO_RADIANS
        c, s = np.cos(radians), np.sin(radians)
        x0, y0 = x - speed * c / 2, y - speed * s / 2
        x1, y1 = x + c, y + s
        segments = np.stack([np.stack([x0, y0], axis=1), np.stack([x1, y1], axis=1)], axis=1)
        shot_span = max(float(shot.max() - shot.min()), 1.0)
        frame_span = max(float(frame.max() - frame.min()), 1.0)
        colors = self.cmap(1.0 - (shot.max() - shot) / shot_span)  # measured from the newest shot: one shot alone is green
        colors[:, 3] = 1.0 - 0.8 * (frame - frame.min()) / frame_span
        tips = np.stack([x1, y1], axis=1)
        if self.lines is None:
            self.lines = LineCollection(segments, colors=colors, linewidths=0.4)
            self.axes.add_collection(self.lines)
            self.tips = self.axes.scatter(x1, y1, s=3, c=colors, linewidths=0)
        else:
            self.lines.set_segments(segments)
            self.lines.set_color(colors)
            self.tips.set_offsets(tips)
            self.tips.set_facecolor(colors)
        if autoscale:
            xs = np.concatenate([x0, x1])
            ys = np.concatenate([y0, y1])
            pad_x = max((xs.max() - xs.min()) * 0.03, 1.0)
            pad_y = max((ys.max() - ys.min()) * 0.03, 1.0)
            self.axes.set_xlim(xs.min() - pad_x, xs.max() + pad_x)
            self.axes.set_ylim(ys.min() - pad_y, ys.max() + pad_y)


def render_once(params_path, csv_path, out, rows, filter_text, settings):
    import matplotlib
    matplotlib.use("Agg")
    from matplotlib.figure import Figure
    if params_path:
        data = RunData(params_path)
    else:
        data = RunData(params={"csv": csv_path, "title": Path(csv_path).name, "finished": True})
    if filter_text is not None:
        error = data.set_filter_text(filter_text)
        if error:
            raise SystemExit(error)
    data.poll(max_rows=rows)
    if data.error:
        raise SystemExit(data.error)
    data.coarsen(int(settings["maxSegments"]))
    figure = Figure(figsize=(10, 8), dpi=100)
    Plot(figure).draw(data)
    out = Path(out) if out else data.csv_path.with_suffix(".png")
    figure.savefig(str(out))
    print(f"{out}: {data.status()}")


def follow_headless(params_path):
    """Tail a run with no window: the window's polling, binning and redraw schedule on the Agg
    backend, fixed defaults rather than the settings file so runs compare, a PNG at the end
    and a summary of the cost beside the parameters file (ROADMAP 4.10)."""
    import matplotlib
    matplotlib.use("Agg")
    from matplotlib.backends.backend_agg import FigureCanvasAgg
    from matplotlib.figure import Figure
    refresh = float(DEFAULT_SETTINGS["refreshSeconds"])
    cap = int(DEFAULT_SETTINGS["maxSegments"])
    data = RunData(params_path)
    figure = Figure(figsize=(10, 8), dpi=100)
    canvas = FigureCanvasAgg(figure)
    plot = Plot(figure)
    started = time.time()
    last_growth = started
    ticks = redraws = 0
    redraw_total = redraw_max = 0.0
    interval = refresh
    while True:
        ticks += 1
        if data.poll():
            data.coarsen(cap)
            last_growth = time.time()
        if data.dirty:
            began = time.perf_counter()
            plot.draw(data)
            canvas.draw()  # the render the window's canvas does
            took = time.perf_counter() - began
            data.dirty = False
            redraws += 1
            redraw_total += took
            redraw_max = max(redraw_max, took)
            interval = next_interval(refresh, took)
        if data.drained or data.error:
            break
        if time.time() - last_growth > IDLE_LIMIT_SECONDS:
            log.warning("%s: no rows for %d s and no finished flag; stopping", data.title, IDLE_LIMIT_SECONDS)
            break
        time.sleep(interval)
    png = data.params_path.with_name(data.params_path.stem + ".png")
    figure.savefig(str(png))
    summary = {
        "cpuSeconds": round(time.process_time(), 3),
        "wallSeconds": round(time.time() - started, 3),
        "ticks": ticks,
        "redraws": redraws,
        "maxRedrawMs": round(redraw_max * 1000.0, 1),
        "meanRedrawMs": round(redraw_total / redraws * 1000.0, 1) if redraws else 0.0,
        "rows": data.rows,
        "drawn": len(data.table),
        "coarsened": data.coarsened,
        "finished": data.finished,
        "error": data.error,
        "png": str(png),
    }
    summary_path = data.params_path.with_name(data.params_path.stem + ".summary.json")
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    log.info("headless %s: %s", data.title, summary)
    print(json.dumps(summary))


# ------------------------------------------------------------------------- the window

def claim_port(port):
    """The listening socket if this is the first viewer, else None."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        if os.name == "nt":
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        else:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # a closed viewer's TIME_WAIT must not block the next; a listening one still does
        sock.bind(("127.0.0.1", port))
        sock.listen(8)
        return sock
    except OSError:
        sock.close()
        return None


def forward(port, params_path):
    """Hand a run to the viewer that owns the port. True when it took it."""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
            sock.sendall(str(Path(params_path).resolve()).encode("utf-8") + b"\n")
        return True
    except OSError as e:
        log.warning("no viewer on port %d took the run: %s", port, e)
        return False


class FilterPanel:
    """A tab's filters beside its plot, in the manner of a shop's facets: a section per CSV
    column in the CSV's order. A column the run names categorical has a checkbox per value
    with the number of sampled rows holding it (checked values pass; none checked passes
    every row); every other has a min and a max box, applied with Enter, under the least and
    greatest value seen. The filters in force are listed at the top, each
    with a button that drops it. Every change hands the whole filter to on_change, which
    returns the message when it is refused; the panel shows that and bad entries itself."""

    WIDTH = 300

    def __init__(self, parent, data, on_change):
        import tkinter as tk
        from tkinter import ttk
        self.tk = tk
        self.data = data
        self.on_change = on_change
        self.frame = tk.Frame(parent, width=self.WIDTH)
        self.head = tk.Frame(self.frame)
        self.head.pack(fill=tk.X, padx=6, pady=(6, 0))
        tk.Label(self.head, text="Filters", font=("TkDefaultFont", 10, "bold")).pack(side=tk.LEFT)
        self.clear_all = tk.Button(self.head, text="Clear all", state=tk.DISABLED, command=lambda: self.change({}))
        self.clear_all.pack(side=tk.RIGHT)
        self.active = tk.Frame(self.frame)  # the filters in force, a line each; packed under the head only while there are any
        self.message = tk.Label(self.frame, text="", fg="firebrick", anchor="w", justify=tk.LEFT, wraplength=self.WIDTH - 12)
        self.separator = ttk.Separator(self.frame, orient=tk.HORIZONTAL)  # the message, when there is one, packs above this
        self.separator.pack(fill=tk.X, padx=6, pady=(4, 0))
        body = tk.Frame(self.frame)  # the sections scroll
        body.pack(fill=tk.BOTH, expand=True)
        self.canvas = tk.Canvas(body, highlightthickness=0, width=self.WIDTH)
        scrollbar = ttk.Scrollbar(body, orient=tk.VERTICAL, command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.inner = tk.Frame(self.canvas)
        self.window = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.inner.bind("<Configure>", lambda event: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>", lambda event: self.canvas.itemconfigure(self.window, width=event.width))
        self.placeholder = tk.Label(self.inner, text="the columns appear with the first rows", fg="gray")
        self.placeholder.pack(anchor="w", padx=6, pady=6)
        self.sections = {}  # column -> its widgets and what they show
        self.shown_filter = None  # the filter the list at the top shows

    def contains(self, x_root, y_root):
        """Whether the screen point is over the panel (the wheel scrolls it then)."""
        widget = self.frame.winfo_containing(x_root, y_root)
        while widget is not None:
            if widget is self.frame:
                return True
            widget = widget.master
        return False

    def scroll(self, event):
        self.canvas.yview_scroll(-1 if event.num == 4 or event.delta > 0 else 1, "units")

    def say(self, text):
        """The message under the filters in force, taking space only while there is one."""
        self.message.config(text=text)
        if text:
            self.message.pack(fill=self.tk.X, padx=6, before=self.separator)
        else:
            self.message.pack_forget()

    def refresh(self):
        """Match the panel to the data: the columns, their values and counts, the ranges seen,
        and the controls to the filter in force (a box being typed in is left alone)."""
        data = self.data
        facets = data.facets()
        if not facets:
            return
        if self.placeholder is not None:
            self.placeholder.destroy()
            self.placeholder = None
        for name, counts, lo, hi in facets:
            kind = "values" if counts is not None else "range"
            section = self.sections.get(name)
            if section is None:
                section = self.sections[name] = self._section(name)
            if section["kind"] != kind or (kind == "values" and section["rows"].keys() != counts.keys()):
                self._build(section, name, kind, counts)
            part = data.filter.get(name, {})
            if kind == "values":
                chosen = part.get("values") or ()
                for text, (var, check) in section["rows"].items():
                    var.set(float(text) in chosen)
                    check.config(text=f"{RunData._format_number(float(text))}   {counts[text]:,}")
            else:
                hint = f"seen {RunData._format_number(lo)} to {RunData._format_number(hi)}" if lo is not None else ""
                if name in data.overflowed:
                    hint = f"over {FACET_VALUES_CAP} distinct values, so a range; {hint}"
                section["hint"].config(text=hint)
                bounds = (part.get("min"), part.get("max"))
                if bounds != section["shown"]:
                    for key, bound in zip(("min", "max"), bounds):
                        section[key].set("" if bound is None else RunData._format_number(bound))
                    section["shown"] = bounds
        if data.filter != self.shown_filter:
            self._list_active()

    def _section(self, name):
        tk = self.tk
        frame = tk.Frame(self.inner)
        frame.pack(fill=tk.X, padx=6, pady=(6, 0))
        tk.Label(frame, text=name, font=("TkDefaultFont", 9, "bold"), anchor="w").pack(fill=tk.X)
        body = tk.Frame(frame)
        body.pack(fill=tk.X)
        return {"kind": None, "body": body, "rows": {}, "min": None, "max": None, "shown": None, "hint": None}

    def _build(self, section, name, kind, counts):
        """The section's controls for its kind: a checkbox per value, or the min and max boxes."""
        tk = self.tk
        for child in section["body"].winfo_children():
            child.destroy()
        section.update(kind=kind, rows={}, min=None, max=None, shown=None, hint=None)
        if kind == "values":
            for text in sorted(counts, key=float):
                var = tk.BooleanVar(value=False)
                check = tk.Checkbutton(section["body"], variable=var, anchor="w", command=lambda n=name: self._values_changed(n))
                check.pack(fill=tk.X)
                section["rows"][text] = (var, check)
            return
        row = tk.Frame(section["body"])
        row.pack(fill=tk.X)
        for key in ("min", "max"):
            section[key] = tk.StringVar()
            tk.Label(row, text=key).pack(side=tk.LEFT, padx=(0, 2))
            entry = tk.Entry(row, textvariable=section[key], width=11)
            entry.pack(side=tk.LEFT, padx=(0, 8))
            entry.bind("<Return>", lambda event, n=name: self._range_changed(n))
        section["hint"] = tk.Label(section["body"], text="", fg="gray", anchor="w")
        section["hint"].pack(fill=tk.X)

    def _values_changed(self, name):
        chosen = {float(text) for text, (var, _) in self.sections[name]["rows"].items() if var.get()}
        self._update(name, values=chosen or None)

    def _range_changed(self, name):
        bounds = {"min": None, "max": None}
        for key in bounds:
            text = self.sections[name][key].get().strip()
            if text:
                bounds[key] = RunData._number(text)
                if bounds[key] is None:
                    self.say(f"{name}: '{text}' is not a number")
                    return
        self._update(name, **bounds)

    def _update(self, name, **changes):
        """The column's part of the filter with these keys set (None drops one), applied."""
        part = dict(self.data.filter.get(name, {}))
        for key, value in changes.items():
            if value is None:
                part.pop(key, None)
            else:
                part[key] = value
        spec = dict(self.data.filter)
        if part:
            spec[name] = part
        else:
            spec.pop(name, None)
        self.change(spec)

    def change(self, spec):
        self.say(self.on_change(spec) or "")
        self.refresh()  # a refused change puts the controls back

    def _list_active(self):
        tk = self.tk
        for child in self.active.winfo_children():
            child.destroy()
        spec = self.data.filter
        for name, part in spec.items():
            row = tk.Frame(self.active)
            row.pack(fill=tk.X, pady=1)
            tk.Button(row, text="✕", padx=4, pady=0, command=lambda n=name: self._update(n, min=None, max=None, values=None)).pack(side=tk.LEFT)
            tk.Label(row, text=RunData.format_filter({name: part}), anchor="w").pack(side=tk.LEFT, padx=(4, 0))
        if spec:
            self.active.pack(fill=tk.X, padx=6, after=self.head)
        else:
            self.active.pack_forget()  # an emptied frame would keep its last height
        self.clear_all.config(state=tk.NORMAL if spec else tk.DISABLED)
        self.shown_filter = {name: dict(part) for name, part in spec.items()}


class RunTab:
    def __init__(self, notebook, data, on_filter, show_filters):
        import tkinter as tk
        from tkinter import ttk
        from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
        from matplotlib.figure import Figure
        self.data = data
        self.frame = tk.Frame(notebook)
        self.paned = ttk.PanedWindow(self.frame, orient=tk.HORIZONTAL)  # the panel beside the plot, a sash between
        self.paned.pack(fill=tk.BOTH, expand=True)
        self.panel = FilterPanel(self.paned, data, lambda spec: on_filter(self, spec))
        plot = tk.Frame(self.paned)
        self.paned.add(plot, weight=1)
        self.show_filters(show_filters)
        figure = Figure(figsize=(9, 7), dpi=100)
        self.canvas = FigureCanvasTkAgg(figure, master=plot)
        NavigationToolbar2Tk(self.canvas, plot)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        self.plot = Plot(figure)
        self.drawn = False
        self.last_redraw = 0.0

    def draw(self, autoscale):
        began = time.perf_counter()
        self.plot.draw(self.data, autoscale)
        self.canvas.draw()
        self.last_redraw = time.perf_counter() - began
        self.data.dirty = False
        self.drawn = True

    def show_filters(self, show):
        panes = self.paned.panes()
        if show and str(self.panel.frame) not in panes:
            self.paned.insert(0, self.panel.frame, weight=0)
        elif not show and str(self.panel.frame) in panes:
            self.paned.forget(self.panel.frame)


class Viewer:
    def __init__(self, settings, server):
        import tkinter as tk
        from tkinter import ttk
        self.settings = settings
        self.server = server
        self.root = tk.Tk()
        self.root.title("TASFW scattershot viewer")
        self.root.geometry("1000x820")
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True)
        bar = tk.Frame(self.root)
        bar.pack(fill=tk.X)
        tk.Label(bar, text="Refresh (s)").pack(side=tk.LEFT, padx=(8, 2))
        self.refresh = tk.DoubleVar(value=float(settings["refreshSeconds"]))
        tk.Spinbox(bar, from_=0.5, to=600, increment=0.5, width=6, textvariable=self.refresh, command=self.save).pack(side=tk.LEFT)
        self.cost = tk.Label(bar, text="viewer CPU: measuring", anchor="w")  # what the viewer costs, measured
        self.cost.pack(side=tk.LEFT, padx=(6, 8))
        self.autoscale = tk.BooleanVar(value=True)
        tk.Checkbutton(bar, text="Autoscale", variable=self.autoscale).pack(side=tk.LEFT, padx=8)
        self.show_filters = tk.BooleanVar(value=bool(settings["showFilters"]))
        tk.Checkbutton(bar, text="Filters", variable=self.show_filters, command=self.toggle_filters).pack(side=tk.LEFT, padx=(0, 8))
        tk.Button(bar, text="Close tab", command=self.close_tab).pack(side=tk.LEFT, padx=(0, 8))
        self.status = tk.Label(bar, text="", anchor="w")
        self.status.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.root.bind("<Control-w>", lambda event: self.close_tab())
        self.notebook.bind("<Button-2>", self.close_tab_under_pointer)  # the middle button on a tab's header
        for wheel in ("<MouseWheel>", "<Button-4>", "<Button-5>"):  # Windows and macOS, then X11
            self.root.bind_all(wheel, self.wheel)
        self.tabs = []
        self.cpu_samples = collections.deque()  # (wall, process CPU) per tick: the measured share over the last COST_WINDOW_SECONDS
        self.incoming = queue.Queue()
        if server:
            threading.Thread(target=self.accept_loop, daemon=True).start()
        self.notebook.bind("<<NotebookTabChanged>>", lambda event: self.redraw_current())
        self.root.protocol("WM_DELETE_WINDOW", self.quit)
        self.root.after(200, self.drain_incoming)
        self.root.after(200, self.tick)

    def run(self, params_path):
        self.open_tab(params_path)
        self.root.mainloop()

    def open_tab(self, params_path):
        try:
            data = RunData(params_path)
        except (OSError, ValueError, KeyError) as e:
            log.error("cannot open %s: %s", params_path, e)
            self.status.config(text=f"cannot open {params_path}: {e}")
            return
        tab = RunTab(self.notebook, data, self.apply_filter, self.show_filters.get())
        self.tabs.append(tab)
        self.notebook.add(tab.frame, text=data.title)
        self.notebook.select(tab.frame)
        log.info("tab: %s (%s)", data.title, data.csv_path)

    def current(self):
        selected = self.notebook.select()
        for tab in self.tabs:
            if str(tab.frame) == selected:
                return tab
        return None

    def close_tab(self, tab=None):
        """Drop a tab (the selected one by default): the window stays for the next run."""
        tab = tab or self.current()
        if tab is None:
            return
        self.notebook.forget(tab.frame)
        self.tabs.remove(tab)
        tab.frame.destroy()
        log.info("closed tab: %s", tab.data.title)
        if self.tabs:
            self.redraw_current()
        else:
            self.status.config(text="no runs open; the next run's launch adds a tab")

    def close_tab_under_pointer(self, event):
        try:
            index = self.notebook.index("@%d,%d" % (event.x, event.y))
        except Exception:  # not on a tab header
            return
        frame = self.notebook.tabs()[index]
        for tab in self.tabs:
            if str(tab.frame) == frame:
                self.close_tab(tab)
                return

    def redraw_current(self):
        tab = self.current()
        if tab and (tab.data.dirty or not tab.drawn):
            tab.draw(self.autoscale.get())
        if tab:
            self.status.config(text=tab.data.status())
            if tab.data.stats_changed:
                tab.data.stats_changed = False
                tab.panel.refresh()

    def apply_filter(self, tab, spec):
        """A tab's panel changed its filter: re-read the CSV under it now. The message when the
        filter is refused, else None."""
        error = tab.data.set_filter(spec)
        if error:
            log.warning("%s: %s", tab.data.title, error)
            return error
        self.status.config(text=f"reading {tab.data.csv_path.name} from its start under the filter")
        self.root.update_idletasks()
        if tab.data.poll():
            tab.data.coarsen(int(self.settings["maxSegments"]))
        self.redraw_current()
        log.info("%s: filter '%s'", tab.data.title, RunData.format_filter(tab.data.filter))
        return None

    def toggle_filters(self):
        for tab in self.tabs:
            tab.show_filters(self.show_filters.get())
        self.save()

    def wheel(self, event):
        tab = self.current()
        if tab and tab.panel.contains(event.x_root, event.y_root):
            tab.panel.scroll(event)

    def tick(self):
        cap = int(self.settings["maxSegments"])
        for tab in self.tabs:
            if tab.data.poll():
                tab.data.coarsen(cap)
        self.redraw_current()
        now = time.time()
        self.cpu_samples.append((now, time.process_time()))
        while len(self.cpu_samples) > 2 and self.cpu_samples[0][0] < now - COST_WINDOW_SECONDS:
            self.cpu_samples.popleft()
        self.update_cost()
        self.root.after(int(self.interval() * 1000), self.tick)

    def interval(self):
        """The wait before the next tick: the refresh setting, stretched by the selected tab's
        last redraw so that a big tab redraws less often, not more expensively."""
        try:
            seconds = max(0.5, float(self.refresh.get()))
        except Exception:  # the spinbox mid-edit
            seconds = float(DEFAULT_SETTINGS["refreshSeconds"])
        tab = self.current()
        if tab:
            seconds = next_interval(seconds, tab.last_redraw)
        return seconds

    def update_cost(self):
        """Next to the refresh rate: what the viewer costs, this process's CPU over the last
        COST_WINDOW_SECONDS as a share of one core and of the machine. The CPU clock ticks in
        15.6 ms steps on Windows, a resolution of about 0.05% of one core over the window."""
        if len(self.cpu_samples) < 2:
            return
        (wall0, cpu0), (wall1, cpu1) = self.cpu_samples[0], self.cpu_samples[-1]
        if wall1 <= wall0:
            return
        cpus = os.cpu_count() or 1
        measured = (cpu1 - cpu0) / (wall1 - wall0) * 100.0
        self.cost.config(text="viewer CPU: %.1f%% of one core, %.2f%% of %d (last %.0f s)"
                         % (measured, measured / cpus, cpus, wall1 - wall0))

    def accept_loop(self):
        while True:
            try:
                connection, _ = self.server.accept()
            except OSError:
                return
            with connection:
                data = b""
                while not data.endswith(b"\n") and len(data) < 65536:
                    more = connection.recv(4096)
                    if not more:
                        break
                    data += more
            path = data.decode("utf-8", "replace").strip()
            if path:
                self.incoming.put(path)

    def drain_incoming(self):
        while True:
            try:
                path = self.incoming.get_nowait()
            except queue.Empty:
                break
            self.open_tab(path)
        self.root.after(200, self.drain_incoming)

    def save(self):
        try:
            self.settings["refreshSeconds"] = float(self.refresh.get())
        except (ValueError, TypeError):
            return
        self.settings["showFilters"] = bool(self.show_filters.get())
        save_settings(self.settings)

    def quit(self):
        self.save()
        if self.server:
            try:
                self.server.close()
            except OSError:
                pass
        self.root.destroy()


# ---------------------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description="The scattershot viewer: a live plot of a run's CSV, one tab per run.")
    parser.add_argument("params", nargs="?", help="the run's parameters file, written by the search beside its CSV")
    parser.add_argument("--once", action="store_true", help="render one PNG headless and exit")
    parser.add_argument("--csv", help="with --once: the CSV to render when there is no parameters file")
    parser.add_argument("--out", help="with --once: the PNG to write (default: the CSV's name with .png)")
    parser.add_argument("--rows", type=int, help="with --once: read at most this many rows")
    parser.add_argument("--filter", metavar="TERMS",
                        help="with --once: the filter box's text in place of the parameters file's filters, "
                             "such as 'MarioFSpd <= 8, Phase == 5'; " + FILTER_GRAMMAR)
    args = parser.parse_args()

    logging.basicConfig(filename=str(LOG_FILE), level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    settings = load_settings()
    if args.once:
        if not args.params and not args.csv:
            parser.error("--once needs a parameters file or --csv")
        bootstrap(windowed=False)
        render_once(args.params, args.csv, args.out, args.rows, args.filter, settings)
        return
    if not args.params:
        parser.error("a parameters file is needed (or --once --csv <file>)")
    if os.environ.get("TASFW_VISUALIZER_HEADLESS"):
        bootstrap(windowed=False)
        lower_priority()
        follow_headless(args.params)
        return

    bootstrap(windowed=True)
    lower_priority()
    port = int(settings["port"])
    server = claim_port(port)
    if server is None:
        if forward(port, args.params):
            log.info("handed %s to the viewer on port %d", args.params, port)
            return
        server = claim_port(port)  # the viewer went away meanwhile
        if server is None:
            log.warning("port %d is taken by something else; this viewer takes no later runs", port)
    Viewer(settings, server).run(args.params)


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception:
        log.exception("the viewer failed")
        raise
