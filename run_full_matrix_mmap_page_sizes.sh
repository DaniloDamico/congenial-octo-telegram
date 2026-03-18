#!/usr/bin/env bash

set -euo pipefail

RESULT_DIR="${RESULT_DIR:-logs/full-matrix-mmap-page-sizes}"
PERIOD="${PERIOD:-5}"
SAMPLES="${SAMPLES:-30}"
PAGE_SIZES_STRING="${PAGE_SIZES:-128 256 512 1024 2048 4096}"
PAGE_SIZES_STRING="${PAGE_SIZES_STRING//,/ }"

mkdir -p "$RESULT_DIR"

RESULTS="$RESULT_DIR/results.tsv"
: > "$RESULTS"
printf "target\tmodel\tmode\tpage_size\tstatus\tbuild_status\trun_status\tthroughput_mean\tthroughput_ci\trollbacks\tepochs\tfiltered_events\tnote\n" >> "$RESULTS"

mmap_targets=(
  phold_mmap_mv
  pcs_mmap_mv
  highway_mmap_mv
  phold_mmap_mv_store
  pcs_mmap_mv_store
  highway_mmap_mv_store
  phold_mmap_mv_store_grid
  pcs_mmap_mv_store_grid
  highway_mmap_mv_store_grid
)

read -r -a page_sizes <<< "$PAGE_SIZES_STRING"

if [[ "${#page_sizes[@]}" -eq 0 ]]; then
  echo "No PAGE_SIZES configured" >&2
  exit 1
fi

is_power_of_two() {
  local value="$1"

  [[ "$value" =~ ^[0-9]+$ ]] || return 1
  (( value > 0 )) || return 1
  (( (value & (value - 1)) == 0 ))
}

for page_size in "${page_sizes[@]}"; do
  if ! is_power_of_two "$page_size"; then
    echo "Invalid page size: $page_size" >&2
    exit 1
  fi
done

append_result() {
  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8" "$9" "${10}" "${11}" "${12}" "${13}" >> "$RESULTS"
}

run_target() {
  local target="$1"
  local page_size="$2"
  local model="${target%%_*}"
  local mode="${target#${model}_}"
  local target_id="${target}_ps${page_size}"
  local build_log="$RESULT_DIR/${target_id}.build.log"
  local run_log="$RESULT_DIR/${target_id}.run.log"
  local clean_log="$RESULT_DIR/${target_id}.clean.log"
  local build_cmd
  local rc
  local mean
  local ci
  local rollbacks
  local epochs
  local filtered

  echo "[start] $target page_size=$page_size"

  if ! make -C build clean >"$clean_log" 2>&1; then
    append_result "$target" "$model" "$mode" "$page_size" "FAIL" "FAIL" "SKIP" "" "" "" "" "" "clean failed"
    echo "[fail] $target clean page_size=$page_size"
    return
  fi

  build_cmd=(make -C build "$target" BENCHMARK=1 PERIOD="$PERIOD" SAMPLES="$SAMPLES" MMAP_MV_PAGE_SIZE="$page_size")

  if [[ "$target" == *_mmap_mv_store_grid ]]; then
    build_cmd+=(MVMM_GRID_SIZE="${MVMM_GRID_SIZE:-64}")
  fi

  if ! "${build_cmd[@]}" >"$build_log" 2>&1; then
    append_result "$target" "$model" "$mode" "$page_size" "FAIL" "FAIL" "SKIP" "" "" "" "" "" "build failed"
    echo "[fail] $target build page_size=$page_size"
    return
  fi

  set +e
  ./bin/PARSIR-simulator >"$run_log" 2>&1
  rc=$?
  set -e

  if [[ "$rc" -ne 0 ]]; then
    append_result "$target" "$model" "$mode" "$page_size" "FAIL" "OK" "$rc" "" "" "" "" "" "run failed"
    echo "[fail] $target run page_size=$page_size"
    return
  fi

  mean="$(awk -F': ' '/THROHGHPUT_MEAN/ {print $2}' "$run_log" | tail -n1)"
  ci="$(awk -F': ' '/THROHGHPUT_CI/ {print $2}' "$run_log" | tail -n1)"
  rollbacks="$(awk -F': ' '/ROLLBACKS/ {print $2}' "$run_log" | tail -n1)"
  epochs="$(awk -F': ' '/EPOCHS/ {print $2}' "$run_log" | tail -n1)"
  filtered="$(awk -F': ' '/FILTERED_EVENTS/ {print $2}' "$run_log" | tail -n1)"

  if [[ -z "$mean" ]]; then
    append_result "$target" "$model" "$mode" "$page_size" "FAIL" "OK" "$rc" "" "" "" "" "" "missing benchmark output"
    echo "[fail] $target missing metrics page_size=$page_size"
    return
  fi

  append_result "$target" "$model" "$mode" "$page_size" "OK" "OK" "0" "$mean" "$ci" "$rollbacks" "$epochs" "$filtered" "benchmark ok"
  echo "[ok] $target bench page_size=$page_size"
}

for page_size in "${page_sizes[@]}"; do
  for target in "${mmap_targets[@]}"; do
    run_target "$target" "$page_size"
  done
done

echo "[done] results -> $RESULTS"
