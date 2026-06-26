#!/bin/bash
# Generate a markdown perf-stat comparison table for ihtab vs absl vs ihtab-v0.
# Usage: ./profile_table.sh <profile_lookup> <profile_lookup_v0> [label]

set -e

BENCH="$1"
BENCH_V0="$2"
LABEL="${3:-}"

EVENTS="cycles,instructions,L1-dcache-loads,L1-dcache-load-misses,L1-icache-loads,L1-icache-load-misses,cache-references,cache-misses,branches,branch-misses,dTLB-loads,dTLB-load-misses"

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

ALL_EVENTS="cycles instructions L1-dcache-loads L1-dcache-load-misses L1-icache-loads L1-icache-load-misses cache-references cache-misses branches branch-misses dTLB-loads dTLB-load-misses"

for var in absl ihtab v0; do
  out_var="${var}_out"
  for e in $ALL_EVENTS; do
    safe=$(echo "$e" | tr '-' '_')
    val=$(parse "${!out_var}" "$e:")
    eval "${var}_${safe}=${val:-0}"
  done
done

python3 - "$LABEL" \
  "$absl_cycles" "$absl_instructions" \
  "$absl_L1_dcache_loads" "$absl_L1_dcache_load_misses" \
  "$absl_L1_icache_loads" "$absl_L1_icache_load_misses" \
  "$absl_cache_references" "$absl_cache_misses" \
  "$absl_branches" "$absl_branch_misses" \
  "$absl_dTLB_loads" "$absl_dTLB_load_misses" \
  "$ihtab_cycles" "$ihtab_instructions" \
  "$ihtab_L1_dcache_loads" "$ihtab_L1_dcache_load_misses" \
  "$ihtab_L1_icache_loads" "$ihtab_L1_icache_load_misses" \
  "$ihtab_cache_references" "$ihtab_cache_misses" \
  "$ihtab_branches" "$ihtab_branch_misses" \
  "$ihtab_dTLB_loads" "$ihtab_dTLB_load_misses" \
  "$v0_cycles" "$v0_instructions" \
  "$v0_L1_dcache_loads" "$v0_L1_dcache_load_misses" \
  "$v0_L1_icache_loads" "$v0_L1_icache_load_misses" \
  "$v0_cache_references" "$v0_cache_misses" \
  "$v0_branches" "$v0_branch_misses" \
  "$v0_dTLB_loads" "$v0_dTLB_load_misses" <<'PYEOF'
import sys

label = sys.argv[1]
N = 12
a = [int(x) for x in sys.argv[2:2+N]]
i = [int(x) for x in sys.argv[2+N:2+2*N]]
v = [int(x) for x in sys.argv[2+2*N:2+3*N]]

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

def rate(misses, loads):
    return misses / loads * 100 if loads else 0

def brfmt(misses, r):
    return f"{fmt(misses)} ({r:.2f}%)"

cyc_a, insn_a, l1dl_a, l1dm_a, l1il_a, l1im_a, llcr_a, llcm_a, br_a, brm_a, dtlb_a, dtlbm_a = a
cyc_i, insn_i, l1dl_i, l1dm_i, l1il_i, l1im_i, llcr_i, llcm_i, br_i, brm_i, dtlb_i, dtlbm_i = i
cyc_v, insn_v, l1dl_v, l1dm_v, l1il_v, l1im_v, llcr_v, llcm_v, br_v, brm_v, dtlb_v, dtlbm_v = v

ipc_a, ipc_i, ipc_v = insn_a/cyc_a, insn_i/cyc_i, insn_v/cyc_v
l1dr_a, l1dr_i, l1dr_v = rate(l1dm_a, l1dl_a), rate(l1dm_i, l1dl_i), rate(l1dm_v, l1dl_v)
l1ir_a, l1ir_i, l1ir_v = rate(l1im_a, l1il_a), rate(l1im_i, l1il_i), rate(l1im_v, l1il_v)
llcr_a2, llcr_i2, llcr_v2 = rate(llcm_a, llcr_a), rate(llcm_i, llcr_i), rate(llcm_v, llcr_v)
brr_a, brr_i, brr_v = rate(brm_a, br_a), rate(brm_i, br_i), rate(brm_v, br_v)
dr_a, dr_i, dr_v = rate(dtlbm_a, dtlb_a), rate(dtlbm_i, dtlb_i), rate(dtlbm_v, dtlb_v)

if label:
    print(f"\n### {label}\n")

rows = [
    ("Cycles",              fmt(cyc_a),  fmt(cyc_i),  fmt(cyc_v),  best3(cyc_a, cyc_i, cyc_v)),
    ("Instructions",        fmt(insn_a), fmt(insn_i), fmt(insn_v), best3(insn_a, insn_i, insn_v)),
    ("IPC",                 f"{ipc_a:.2f}", f"{ipc_i:.2f}", f"{ipc_v:.2f}", best3(ipc_a, ipc_i, ipc_v, lower=False)),
    ("L1-dcache miss rate", f"{l1dr_a:.1f}%", f"{l1dr_i:.1f}%", f"{l1dr_v:.1f}%", best3(l1dr_a, l1dr_i, l1dr_v)),
    ("L1-icache miss rate", f"{l1ir_a:.1f}%", f"{l1ir_i:.1f}%", f"{l1ir_v:.1f}%", best3(l1ir_a, l1ir_i, l1ir_v)),
    ("LLC miss rate",       f"{llcr_a2:.1f}%", f"{llcr_i2:.1f}%", f"{llcr_v2:.1f}%", best3(llcr_a2, llcr_i2, llcr_v2)),
    ("Branch misses",       brfmt(brm_a, brr_a), brfmt(brm_i, brr_i), brfmt(brm_v, brr_v), best3(brm_a, brm_i, brm_v)),
    ("dTLB miss rate",      f"{dr_a:.1f}%", f"{dr_i:.1f}%", f"{dr_v:.1f}%", best3(dr_a, dr_i, dr_v)),
]

C0 = 21
C1 = C2 = C3 = max(15, max(len(r[j]) for r in rows for j in (1,2,3)))
C4 = max(23, max(len(r[4]) for r in rows))

print(f"| {'Metric':<{C0}} | {'absl':<{C1}} | {'ihtab':<{C2}} | {'ihtab-v0':<{C3}} | {'Best':<{C4}} |")
print(f"|{'-'*(C0+2)}|{'-'*(C1+2)}|{'-'*(C2+2)}|{'-'*(C3+2)}|{'-'*(C4+2)}|")
for metric, va, vi, vv, vb in rows:
    print(f"| {metric:<{C0}} | {va:<{C1}} | {vi:<{C2}} | {vv:<{C3}} | {vb:<{C4}} |")
PYEOF
