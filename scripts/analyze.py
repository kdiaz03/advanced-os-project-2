#!/usr/bin/env python3
"""average each experiment setting over its repeats, write summary.csv, and plot the three metrics.

usage: python3 scripts/analyze.py results/<run>/raw.csv
"""
import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

BLUE = "#2a78d6"
GRAY = "#8a8985"
INK = "#52514e"
LABELS = {
    "n": "Number of nodes n",
    "d": "Mean inter-request delay d (ms)",
    "c": "Mean CS execution time c (ms)",
}
METRICS = [
    ("messages_per_cs", "Messages per CS (network only)", "messages"),
    ("response_ms", "Mean response time (ms)", "response"),
    ("throughput", "System throughput (CS per second)", "throughput"),
]


# one run's numbers come from all of its nodes' METRICS lines together.
def run_metrics(rows):
    total_cs = sum(int(r["requests"]) for r in rows)
    network = sum(int(r["network_messages"]) for r in rows)
    with_self = network + sum(int(r["self_messages"]) for r in rows)
    response = sum(float(r["response_total_ms"]) for r in rows)
    # the slowest node's active time bounds the whole run without comparing clocks.
    duration_s = max(float(r["duration_ms"]) for r in rows) / 1000
    return {
        "messages_per_cs": network / total_cs,
        "messages_with_self_per_cs": with_self / total_cs,
        "response_ms": response / total_cs,
        "response_max_ms": max(float(r["response_max_ms"]) for r in rows),
        "throughput": total_cs / duration_s,
    }


def summarize(raw_path):
    runs = defaultdict(list)
    with open(raw_path, newline="") as f:
        for row in csv.DictReader(f):
            runs[row["run_id"]].append(row)
    settings = defaultdict(list)
    for rows in runs.values():
        first = rows[0]
        if len(rows) != int(first["n"]):
            sys.exit(f"run {first['run_id']} has {len(rows)} METRICS lines for n={first['n']}")
        key = (first["sweep"], int(first["n"]), float(first["d"]), float(first["c"]))
        settings[key].append(run_metrics(rows))
    summary = []
    for (sweep, n, d, c), results in sorted(settings.items()):
        row = {"sweep": sweep, "n": n, "d": d, "c": c, "runs": len(results)}
        for name in results[0]:
            values = [r[name] for r in results]
            row[name] = statistics.mean(values)
            row[name + "_sd"] = statistics.stdev(values) if len(values) > 1 else 0.0
        summary.append(row)
    return summary


def style(ax, xlabel, ylabel):
    ax.set_xlabel(xlabel, color=INK)
    ax.set_ylabel(ylabel, color=INK)
    ax.grid(True, color="#e6e5e1", linewidth=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color("#c3c2b7")
    ax.tick_params(colors=INK)
    ax.set_ylim(bottom=0)


def plot(summary, out_dir):
    for sweep in ("n", "d", "c"):
        rows = sorted((r for r in summary if r["sweep"] == sweep), key=lambda r: r[sweep])
        if not rows:
            continue
        xs = [r[sweep] for r in rows]
        for key, ylabel, stem in METRICS:
            fig, ax = plt.subplots(figsize=(5.5, 3.6), dpi=200)
            measured = ax.errorbar(xs, [r[key] for r in rows], yerr=[r[key + "_sd"] for r in rows],
                        color=BLUE, linewidth=2, marker="o", markersize=5, capsize=3,
                        label="Measured (mean ± 1 sd)")
            if key == "messages_per_cs":
                # paper bounds for the K-1 other quorum members: 3(K-1) at light load, 5(K-1) worst case.
                others = [2 * math.isqrt(r["n"]) - 2 for r in rows]
                heavy, = ax.plot(xs, [5 * k for k in others], color=GRAY, linewidth=1.5, linestyle=":",
                                 label="5(K-1), heavy-load bound")
                light, = ax.plot(xs, [3 * k for k in others], color=GRAY, linewidth=1.5, linestyle="--",
                                 label="3(K-1), light-load bound")
                # list the measured series first, then the bounds from top to bottom.
                ax.legend(handles=[measured, heavy, light], frameon=False, fontsize=8, labelcolor=INK)
            style(ax, LABELS[sweep], ylabel)
            # delays span two orders of magnitude, so space them on a log axis.
            if sweep == "d":
                ax.set_xscale("log")
            ax.set_xticks(xs)
            ax.set_xticklabels([f"{x:g}" for x in xs])
            ax.minorticks_off()
            fig.tight_layout()
            fig.savefig(out_dir / f"{stem}_vs_{sweep}.png")
            plt.close(fig)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    raw = Path(sys.argv[1])
    summary = summarize(raw)
    out_dir = raw.parent
    with open(out_dir / "summary.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(summary[0]))
        writer.writeheader()
        for row in summary:
            writer.writerow({k: f"{v:.3f}" if isinstance(v, float) else v for k, v in row.items()})
    plot(summary, out_dir)
    print(f"Wrote {out_dir / 'summary.csv'} and plots in {out_dir}")


if __name__ == "__main__":
    main()
