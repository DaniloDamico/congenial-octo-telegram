#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PERIOD="${PERIOD:-5}"
SAMPLES="${SAMPLES:-5}"
LOG_DIR="${LOG_DIR:-$SCRIPT_DIR/logs/selected-benchmarks}"

TARGETS=(
    phold_grid_ckpt
    phold_grid_ckpt_bs
    phold_chunk_ckpt
    phold_chunk_full_ckpt
    pcs_grid_ckpt
    pcs_grid_ckpt_bs
    pcs_chunk_ckpt
    pcs_chunk_full_ckpt
    highway_grid_ckpt
    highway_grid_ckpt_bs
    highway_chunk_ckpt
    highway_chunk_full_ckpt
)

mkdir -p "$LOG_DIR"

echo "Running selected benchmarks with PERIOD=$PERIOD SAMPLES=$SAMPLES"
echo "Logs will be written to $LOG_DIR"

for target in "${TARGETS[@]}"; do
    echo
    echo "==> $target"
    make -C build clean
    make -C build "$target" BENCHMARK=1 PERIOD="$PERIOD" SAMPLES="$SAMPLES"
    ./bin/PARSIR-simulator | tee "$LOG_DIR/${target}.log"
done

echo
echo "All selected benchmarks completed."
