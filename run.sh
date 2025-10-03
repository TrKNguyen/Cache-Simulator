#!/usr/bin/env bash
set -euo pipefail
OUT="output.txt"
: > "$OUT" 
EXEC="./CacheSimulator"
PROTOCOL="MESI"

# Sweep configs (edit as needed)
CACHE_SIZES=(2048 4096)
ASSOCS=(1 2)
BLOCKS=(16 32)

# Exactly these workloads; script does NOT check for file existence
WORKLOADS=(bodytrack blackscholes fluidanimate)

for workload in "${WORKLOADS[@]}"; do
  for csz in "${CACHE_SIZES[@]}"; do
    for assoc in "${ASSOCS[@]}"; do
      for blk in "${BLOCKS[@]}"; do
        echo "===== RUN: workload=${workload} cs=${csz} assoc=${assoc} blk=${blk} =====" >> "$OUT" 2>&1
        "${EXEC}" "${PROTOCOL}" "${workload}" "${csz}" "${assoc}" "${blk}" >> "$OUT" 2>&1
        echo
      done
    done
  done
done