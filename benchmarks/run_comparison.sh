#!/bin/bash
# Comparison of absl, ihtab, ixhtab hash table implementations.

set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

BENCH_ARGS=""
while [[ $# -gt 0 ]]; do
  case $1 in
    --include-tiny)
      BENCH_ARGS="$BENCH_ARGS --include-tiny"
      shift ;;
    --include-string)
      BENCH_ARGS="$BENCH_ARGS --include-string"
      shift ;;
    *)
      echo "Usage: $0 [--include-tiny] [--include-string]"
      exit 1 ;;
  esac
done

echo "Hash Table Comparison: absl vs umap vs C++/C ihtab/ixhtab"
echo "=========================================================="
echo ""

# Phase 1: Compile
if [[ ! -f bench ]] || [[ bench.cpp -nt bench ]] || [[ ../ihtab.hpp -nt bench ]] || [[ ../ixhtab.hpp -nt bench ]]; then
  echo "Compiling benchmark..."
  g++ -I.. -std=c++20 -O3 -DNDEBUG -Wall -Wpedantic bench.cpp -o bench
  echo "Compilation done."
else
  echo "Using existing bench binary."
fi
echo ""

# Phase 2: Run benchmark
echo "Running benchmarks..."
./bench $BENCH_ARGS
echo ""

# Find latest result CSV.
CSV=$(ls -t result_*.csv 2>/dev/null | head -1)
if [[ -z "$CSV" ]]; then
  echo "ERROR: No result CSV found."
  exit 1
fi

# Phase 3: Generate comparison report and charts.
mkdir -p benchmark_results
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
REPORT="benchmark_results/comparison_${TIMESTAMP}.txt"

python3 - "$CSV" "$REPORT" << 'PYEOF'
import sys, csv, math

csv_path = sys.argv[1]
report_path = sys.argv[2]

rows = []
with open(csv_path) as f:
    reader = csv.DictReader(f)
    for row in reader:
        rows.append(row)

# Group by (Benchmark, Size).
from collections import defaultdict
data = defaultdict(dict)
for row in rows:
    key = (row["Benchmark"], row["Size"])
    data[key][row["Implementation"]] = float(row["ns/op"])

impls = ["absl", "ixhtab", "ihtab", "ixht", "iht"]

lines = []
lines.append("Hash Table Comparison Report")
lines.append("=" * 120)
lines.append(f"Data: {csv_path}")
lines.append("")
hdr = f"{'Benchmark':<14} {'Size':<7}"
for im in impls:
    hdr += f" {im:>11}"
for im in impls[1:]:
    hdr += f"  {im+'/absl':>14}"
lines.append(hdr)
lines.append("-" * len(hdr))

impl_vals = {im: [] for im in impls}

size_order = {"Tiny": 0, "Small": 1, "Medium": 2, "Large": 3}
sorted_keys = sorted(data.keys(), key=lambda k: (k[0], size_order.get(k[1], 99)))

for (bm, sz) in sorted_keys:
    d = data[(bm, sz)]
    a = d.get("absl", 0)
    row = f"{bm:<14} {sz:<7}"
    for im in impls:
        v = d.get(im, 0)
        if v > 0:
            row += f" {v:>11.1f}"
        else:
            row += f" {'N/A':>11}"
    for im in impls[1:]:
        v = d.get(im, 0)
        if a > 0 and v > 0:
            row += f"  {v/a:>12.3f}x"
        else:
            row += f"  {'N/A':>14}"
    lines.append(row)
    if a > 0:
        for im in impls:
            v = d.get(im, 0)
            if v > 0:
                impl_vals[im].append(v)

lines.append("")

def geomean(vals):
    if not vals or any(v <= 0 for v in vals): return 0
    return math.exp(sum(math.log(v) for v in vals) / len(vals))

gm = {im: geomean(impl_vals[im]) for im in impls}
gm_a = gm["absl"]

lines.append("Geometric mean (ns/op):")
for im in impls:
    g = gm[im]
    if g <= 0:
        lines.append(f"  {im+':':14s} N/A")
    elif im == "absl":
        lines.append(f"  {im+':':14s} {g:.1f}")
    else:
        lines.append(f"  {im+':':14s} {g:.1f}  ({g/gm_a:.3f}x)")
lines.append("")
lines.append("Ratio < 1.0 means faster than absl. Ratio > 1.0 means slower.")

report = "\n".join(lines)
print(report)

with open(report_path, "w") as f:
    f.write(report)
print(f"\nReport saved: {report_path}")

# Charts.
try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    benchmarks = []
    impl_v = {im: [] for im in impls}

    for (bm, sz) in sorted_keys:
        d = data[(bm, sz)]
        benchmarks.append(f"{bm}\n{sz}")
        for im in impls:
            impl_v[im].append(d.get(im, 0))

    n = len(benchmarks)
    x = np.arange(n)
    ni = len(impls)
    w = 0.8 / ni
    colors = {"absl": "#51a9f0",
              "ixhtab": "#9650c8", "ihtab": "#1e64b4",
              "ixht": "#c87ee0", "iht": "#4090e0"}

    # Chart 1: Absolute times.
    chart_w = max(18, n * 0.9)
    fig, ax = plt.subplots(figsize=(chart_w, chart_w / 2))
    for j, im in enumerate(impls):
        offset = (j - (ni - 1) / 2) * w
        ax.bar(x + offset, impl_v[im], w, label=im, color=colors[im])
    ax.set_ylabel("ns/op")
    ax.set_title("Hash Table Benchmark (vmum hash)")
    ax.set_xticks(x)
    ax.set_xticklabels(benchmarks, fontsize=8, ha="right", rotation=45)
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig("benchmark_results/performance_comparison.png", dpi=200)
    plt.close()

    # Chart 2: Improvement % vs absl (positive = faster).
    cmp_impls = impls[1:]
    pct = {}
    for im in cmp_impls:
        pct[im] = [(1 - v / a) * 100 if a > 0 and v > 0 else 0
                    for a, v in zip(impl_v["absl"], impl_v[im])]

    # Append geomean bars.
    gm_pct = {}
    for im in cmp_impls:
        g = gm[im]
        gm_pct[im] = (1 - g / gm_a) * 100 if gm_a > 0 and g > 0 else 0
        pct[im].append(gm_pct[im])
    labels = benchmarks + ["GEOMEAN"]
    nn = len(labels)
    xx = np.arange(nn)
    nc = len(cmp_impls)
    wc = 0.8 / nc

    chart_w2 = max(18, nn * 0.9)
    fig, ax = plt.subplots(figsize=(chart_w2, chart_w2 / 2))
    for j, im in enumerate(cmp_impls):
        offset = (j - (nc - 1) / 2) * wc
        ax.bar(xx + offset, pct[im], wc, label=im, color=colors[im], alpha=0.8)

    ax.axhline(y=0, color="black", linestyle="-", linewidth=1.5)
    ax.axvline(x=nn - 1.5, color="gray", linestyle="--", linewidth=0.8)
    ax.set_ylabel("Improvement vs absl (%)")
    ax.set_title("Improvement vs absl (positive = faster)")
    ax.set_xticks(xx)
    ax.set_xticklabels(labels, fontsize=8, ha="right", rotation=45)
    ax.get_xticklabels()[-1].set_fontweight("bold")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig("benchmark_results/speedup_comparison.png", dpi=200)
    plt.close()

    # Chart 3: Overall summary (geomean improvement %).
    fig, ax = plt.subplots(figsize=(8, 5))
    cats = cmp_impls
    vals = [gm_pct[im] for im in cmp_impls]
    bar_colors = [colors[im] for im in cmp_impls]
    bars = ax.bar(cats, vals, color=bar_colors, alpha=0.8, width=0.5)
    for bar, val in zip(bars, vals):
        h = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2, h + (1 if h >= 0 else -2),
                f"{val:+.1f}%", ha="center", va="bottom" if h >= 0 else "top",
                fontsize=12, fontweight="bold")
    ax.axhline(y=0, color="black", linestyle="-", linewidth=1.5)
    ax.set_ylabel("Improvement vs absl (%)")
    ax.set_title("Overall Improvement vs absl (Geometric Mean)\nPositive = faster than absl")
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig("benchmark_results/overall_summary.png", dpi=200)
    plt.close()

    print("Charts: benchmark_results/performance_comparison.png")
    print("Charts: benchmark_results/speedup_comparison.png")
    print("Charts: benchmark_results/overall_summary.png")

except ImportError:
    print("matplotlib not available; skipping charts.")

PYEOF

echo ""
echo "Comparison complete!"
