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

echo "Hash Table Comparison: absl vs umap vs ixhtab vs ihtab"
echo "======================================================="
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

lines = []
lines.append("Hash Table Comparison Report")
lines.append("=" * 70)
lines.append(f"Data: {csv_path}")
lines.append("")
lines.append(f"{'Benchmark':<14} {'Size':<7} {'absl':>10} {'umap':>10} {'ixhtab':>10} {'ihtab':>10}  {'umap/absl':>10} {'ixhtab/absl':>12} {'ihtab/absl':>11}")
lines.append("-" * 100)

absl_vals, umap_vals, ihtab_vals, ixhtab_vals = [], [], [], []

size_order = {"Tiny": 0, "Small": 1, "Medium": 2, "Large": 3}
sorted_keys = sorted(data.keys(), key=lambda k: (k[0], size_order.get(k[1], 99)))

for (bm, sz) in sorted_keys:
    d = data[(bm, sz)]
    a = d.get("absl", 0)
    um = d.get("umap", 0)
    ih = d.get("ihtab", 0)
    ix = d.get("ixhtab", 0)
    if a > 0:
        absl_vals.append(a)
        if um > 0: umap_vals.append(um)
        ihtab_vals.append(ih); ixhtab_vals.append(ix)

    ratio_um = um / a if a > 0 and um > 0 else 0
    ratio_ih = ih / a if a > 0 else 0
    ratio_ix = ix / a if a > 0 else 0

    um_str = f"{um:>10.1f}" if um > 0 else f"{'N/A':>10}"
    ratio_um_str = f"{ratio_um:>8.3f}x" if ratio_um > 0 else f"{'N/A':>10}"
    lines.append(f"{bm:<14} {sz:<7} {a:>10.1f} {um_str} {ix:>10.1f} {ih:>10.1f}  {ratio_um_str}  {ratio_ix:>10.3f}x  {ratio_ih:>10.3f}x")

lines.append("")

def geomean(vals):
    if not vals or any(v <= 0 for v in vals): return 0
    return math.exp(sum(math.log(v) for v in vals) / len(vals))

gm_a  = geomean(absl_vals)
gm_um = geomean(umap_vals)
gm_ih = geomean(ihtab_vals)
gm_ix = geomean(ixhtab_vals)

lines.append("Geometric mean (ns/op):")
lines.append(f"  absl:   {gm_a:.1f}")
lines.append(f"  umap:   {gm_um:.1f}  ({gm_um/gm_a:.3f}x)" if gm_a > 0 and gm_um > 0 else "  umap:   N/A")
lines.append(f"  ixhtab: {gm_ix:.1f}  ({gm_ix/gm_a:.3f}x)" if gm_a > 0 else "  ixhtab: N/A")
lines.append(f"  ihtab:  {gm_ih:.1f}  ({gm_ih/gm_a:.3f}x)" if gm_a > 0 else "  ihtab:  N/A")
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
    absl_v, umap_v, ihtab_v, ixhtab_v = [], [], [], []

    for (bm, sz) in sorted_keys:
        d = data[(bm, sz)]
        benchmarks.append(f"{bm}\n{sz}")
        absl_v.append(d.get("absl", 0))
        umap_v.append(d.get("umap", 0))
        ihtab_v.append(d.get("ihtab", 0))
        ixhtab_v.append(d.get("ixhtab", 0))

    n = len(benchmarks)
    x = np.arange(n)
    w = 0.2

    # Chart 1: Absolute times.
    chart_w = max(16, n * 0.8)
    fig, ax = plt.subplots(figsize=(chart_w, chart_w / 2))
    ax.bar(x - 1.5*w, absl_v,   w, label="absl",   color="#51a9f0")
    ax.bar(x - 0.5*w, umap_v,   w, label="umap",   color="#f0a030")
    ax.bar(x + 0.5*w, ixhtab_v, w, label="ixhtab", color="#9650c8")
    ax.bar(x + 1.5*w, ihtab_v,  w, label="ihtab",  color="#1e64b4")
    ax.set_ylabel("ns/op")
    ax.set_title("Hash Table Benchmark: absl vs umap vs ixhtab vs ihtab (vmum hash)")
    ax.set_xticks(x)
    ax.set_xticklabels(benchmarks, fontsize=9, ha="right", rotation=45)
    ax.legend()
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig("benchmark_results/performance_comparison.png", dpi=200)
    plt.close()

    # Chart 2: Improvement % vs absl (positive = faster than absl).
    pct_um = [(1 - um / a) * 100 if a > 0 and um > 0 else 0 for a, um in zip(absl_v, umap_v)]
    pct_ix = [(1 - ix / a) * 100 if a > 0 else 0 for a, ix in zip(absl_v, ixhtab_v)]
    pct_ih = [(1 - ih / a) * 100 if a > 0 else 0 for a, ih in zip(absl_v, ihtab_v)]

    # Append geomean bars.
    gm_pct_um = (1 - gm_um / gm_a) * 100 if gm_a > 0 and gm_um > 0 else 0
    gm_pct_ix = (1 - gm_ix / gm_a) * 100 if gm_a > 0 else 0
    gm_pct_ih = (1 - gm_ih / gm_a) * 100 if gm_a > 0 else 0
    labels = benchmarks + ["GEOMEAN"]
    pct_um.append(gm_pct_um)
    pct_ix.append(gm_pct_ix)
    pct_ih.append(gm_pct_ih)
    nn = len(labels)
    xx = np.arange(nn)

    chart_w2 = max(16, nn * 0.8)
    fig, ax = plt.subplots(figsize=(chart_w2, chart_w2 / 2))
    ax.bar(xx - w,     pct_um, w, label="umap",   color="#f0a030", alpha=0.8)
    ax.bar(xx,         pct_ix, w, label="ixhtab", color="#9650c8", alpha=0.8)
    ax.bar(xx + w,     pct_ih, w, label="ihtab",  color="#1e64b4", alpha=0.8)

    for i, (v0, v1, v2) in enumerate(zip(pct_um, pct_ix, pct_ih)):
        for v, off in [(v0, -w), (v1, 0), (v2, w)]:
            va = "bottom" if v >= 0 else "top"
            yo = 1.5 if v >= 0 else -1.5
            ax.text(i + off, v + yo, f"{v:+.0f}%", ha="center", va=va,
                    fontsize=6, fontweight="bold", rotation=45)

    ax.axhline(y=0, color="black", linestyle="-", linewidth=1.5)
    ax.axvline(x=nn - 1.5, color="gray", linestyle="--", linewidth=0.8)
    ax.set_ylabel("Improvement vs absl (%)")
    ax.set_title("Improvement vs absl (positive = faster)")
    ax.set_xticks(xx)
    ax.set_xticklabels(labels, fontsize=9, ha="right", rotation=45)
    ax.get_xticklabels()[-1].set_fontweight("bold")
    ax.legend()
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig("benchmark_results/speedup_comparison.png", dpi=200)
    plt.close()

    # Chart 3: Overall summary (geomean improvement %).
    fig, ax = plt.subplots(figsize=(7, 5))
    cats = ["umap", "ixhtab", "ihtab"]
    vals = [gm_pct_um, gm_pct_ix, gm_pct_ih]
    bar_colors = ["#f0a030", "#9650c8", "#1e64b4"]
    bars = ax.bar(cats, vals, color=bar_colors, alpha=0.8, width=0.5)
    for bar, val in zip(bars, vals):
        h = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2, h + (1 if h >= 0 else -2),
                f"{val:+.1f}%", ha="center", va="bottom" if h >= 0 else "top",
                fontsize=14, fontweight="bold")
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
