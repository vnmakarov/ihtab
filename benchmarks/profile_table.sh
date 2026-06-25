#!/bin/bash
# Generate a markdown perf-stat comparison table for ihtab vs absl vs ihtab-v0.
# Usage: ./profile_table.sh <profile_lookup> <profile_lookup_v0> [label]

set -e

BENCH="$1"
BENCH_V0="$2"
LABEL="${3:-}"

EVENTS="cycles,instructions,L1-dcache-loads,L1-dcache-load-misses,branches,branch-misses,dTLB-loads,dTLB-load-misses"

run_perf() {
  perf stat -e "$EVENTS" "$@" 2>&1
}

parse() {
  local output="$1"
  local event="$2"
  echo "$output" | grep -m1 "$event" | awk '{gsub(/,/,"",$1); print $1}'
}

absl_out=$(run_perf "$BENCH" --absl)
ihtab_out=$(run_perf "$BENCH" --ihtab)
v0_out=$(run_perf "$BENCH_V0" --ihtab)

for var in absl ihtab v0; do
  out_var="${var}_out"
  for e in cycles instructions L1-dcache-loads L1-dcache-load-misses branches branch-misses dTLB-loads dTLB-load-misses; do
    safe=$(echo "$e" | tr '-' '_')
    eval "${var}_${safe}=$(parse "${!out_var}" "$e:")"
  done
done

python3 - "$LABEL" \
  "$absl_cycles" "$absl_instructions" "$absl_L1_dcache_loads" "$absl_L1_dcache_load_misses" \
  "$absl_branches" "$absl_branch_misses" "$absl_dTLB_loads" "$absl_dTLB_load_misses" \
  "$ihtab_cycles" "$ihtab_instructions" "$ihtab_L1_dcache_loads" "$ihtab_L1_dcache_load_misses" \
  "$ihtab_branches" "$ihtab_branch_misses" "$ihtab_dTLB_loads" "$ihtab_dTLB_load_misses" \
  "$v0_cycles" "$v0_instructions" "$v0_L1_dcache_loads" "$v0_L1_dcache_load_misses" \
  "$v0_branches" "$v0_branch_misses" "$v0_dTLB_loads" "$v0_dTLB_load_misses" <<'PYEOF'
import sys

label = sys.argv[1]
a = [int(x) for x in sys.argv[2:10]]
i = [int(x) for x in sys.argv[10:18]]
v = [int(x) for x in sys.argv[18:26]]

def fmt(n):
    if n >= 1e9: return f"{n/1e9:.1f}B"
    if n >= 1e6: return f"{n/1e6:.1f}M"
    if n >= 1e3: return f"{n/1e3:.1f}K"
    return str(n)

def best3(av, iv, vv, lower=True):
    vals = [("absl", av), ("ihtab", iv), ("ihtab-v0", vv)]
    b = min(vals, key=lambda x: x[1]) if lower else max(vals, key=lambda x: x[1])
    w = max(vals, key=lambda x: x[1]) if lower else min(vals, key=lambda x: x[1])
    pct = abs(b[1] - w[1]) / w[1] * 100
    word = "fewer" if lower else "higher"
    return f"**{b[0]}** {pct:.0f}% {word}"

cyc_a, insn_a, l1l_a, l1m_a, br_a, brm_a, dtlb_a, dtlbm_a = a
cyc_i, insn_i, l1l_i, l1m_i, br_i, brm_i, dtlb_i, dtlbm_i = i
cyc_v, insn_v, l1l_v, l1m_v, br_v, brm_v, dtlb_v, dtlbm_v = v

ipc_a, ipc_i, ipc_v = insn_a/cyc_a, insn_i/cyc_i, insn_v/cyc_v
l1r_a, l1r_i, l1r_v = l1m_a/l1l_a*100, l1m_i/l1l_i*100, l1m_v/l1l_v*100
brr_a, brr_i, brr_v = brm_a/br_a*100, brm_i/br_i*100, brm_v/br_v*100
dr_a, dr_i, dr_v = dtlbm_a/dtlb_a*100, dtlbm_i/dtlb_i*100, dtlbm_v/dtlb_v*100

if label:
    print(f"\n### {label}\n")

def brfmt(misses, rate):
    return f"{fmt(misses)} ({rate:.2f}%)"

W = 15
rows = [
    ("Cycles",              fmt(cyc_a),  fmt(cyc_i),  fmt(cyc_v),  best3(cyc_a, cyc_i, cyc_v)),
    ("Instructions",        fmt(insn_a), fmt(insn_i), fmt(insn_v), best3(insn_a, insn_i, insn_v)),
    ("IPC",                 f"{ipc_a:.2f}", f"{ipc_i:.2f}", f"{ipc_v:.2f}", best3(ipc_a, ipc_i, ipc_v, lower=False)),
    ("L1-dcache miss rate", f"{l1r_a:.1f}%", f"{l1r_i:.1f}%", f"{l1r_v:.1f}%", best3(l1r_a, l1r_i, l1r_v)),
    ("Branch misses",       brfmt(brm_a, brr_a), brfmt(brm_i, brr_i), brfmt(brm_v, brr_v), best3(brm_a, brm_i, brm_v)),
    ("dTLB miss rate",      f"{dr_a:.1f}%", f"{dr_i:.1f}%", f"{dr_v:.1f}%", best3(dr_a, dr_i, dr_v)),
]

C0 = 21
C1 = C2 = C3 = max(W, max(len(r[1]) for r in rows), max(len(r[2]) for r in rows), max(len(r[3]) for r in rows))
C4 = max(23, max(len(r[4]) for r in rows))

print(f"| {'Metric':<{C0}} | {'absl':<{C1}} | {'ihtab':<{C2}} | {'ihtab-v0':<{C3}} | {'Best':<{C4}} |")
print(f"|{'-'*(C0+2)}|{'-'*(C1+2)}|{'-'*(C2+2)}|{'-'*(C3+2)}|{'-'*(C4+2)}|")
for metric, va, vi, vv, vb in rows:
    print(f"| {metric:<{C0}} | {va:<{C1}} | {vi:<{C2}} | {vv:<{C3}} | {vb:<{C4}} |")
PYEOF
