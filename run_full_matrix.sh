#!/usr/bin/env bash

set -euo pipefail

RESULT_DIR="${RESULT_DIR:-logs/full-matrix}"
PERIOD="${PERIOD:-5}"
SAMPLES="${SAMPLES:-30}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-5}"

mkdir -p "$RESULT_DIR"

RESULTS="$RESULT_DIR/results.tsv"
: > "$RESULTS"
printf "target\tmodel\tmode\tstatus\tbuild_status\trun_status\tthroughput_mean\tthroughput_ci\trollbacks\tepochs\tfiltered_events\tnote\n" >> "$RESULTS"

plain_targets=(
  phold
  pcs
  highway
)

bench_targets=(
  phold_grid_ckpt
  pcs_grid_ckpt
  highway_grid_ckpt
  phold_grid_ckpt_bs
  pcs_grid_ckpt_bs
  highway_grid_ckpt_bs
  phold_chunk_ckpt
  pcs_chunk_ckpt
  highway_chunk_ckpt
  phold_chunk_full_ckpt
  pcs_chunk_full_ckpt
  highway_chunk_full_ckpt
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

append_result() {
  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8" "$9" "${10}" "${11}" "${12}" >> "$RESULTS"
}

run_target() {
  local target="$1"
  local model="${target%%_*}"
  local mode
  local build_log="$RESULT_DIR/${target}.build.log"
  local run_log="$RESULT_DIR/${target}.run.log"
  local clean_log="$RESULT_DIR/${target}.clean.log"
  local build_cmd
  local extra_args=()
  local rc
  local mean
  local ci
  local rollbacks
  local epochs
  local filtered

  if [[ "$target" == "$model" ]]; then
    mode="plain"
  else
    mode="${target#${model}_}"
  fi

  echo "[start] $target"

  if ! make -C build clean >"$clean_log" 2>&1; then
    append_result "$target" "$model" "$mode" "FAIL" "FAIL" "SKIP" "" "" "" "" "" "clean failed"
    echo "[fail] $target clean"
    return
  fi

  case "$target" in
    *_mmap_mv_store_grid)
      extra_args=(MVMM_MAX_VERSIONS=2 ROTATE_EVERY=1000 MVMM_GRID_SIZE=64)
      ;;
    *_mmap_mv_store|*_mmap_mv)
      extra_args=(MVMM_MAX_VERSIONS=2 ROTATE_EVERY=1000)
      ;;
  esac

  if [[ "$mode" == "plain" ]]; then
    build_cmd=(make -C build "$target")
  else
    build_cmd=(make -C build "$target" BENCHMARK=1 PERIOD="$PERIOD" SAMPLES="$SAMPLES" "${extra_args[@]}")
  fi

  if ! "${build_cmd[@]}" >"$build_log" 2>&1; then
    append_result "$target" "$model" "$mode" "FAIL" "FAIL" "SKIP" "" "" "" "" "" "build failed"
    echo "[fail] $target build"
    return
  fi

  if [[ "$mode" == "plain" ]]; then
    set +e
    timeout "${TIMEOUT_SECONDS}s" ./bin/PARSIR-simulator >"$run_log" 2>&1
    rc=$?
    set -e

    if [[ "$rc" -eq 0 || "$rc" -eq 124 ]] && grep -q "PARSIR started with" "$run_log"; then
      append_result "$target" "$model" "$mode" "OK" "OK" "$rc" "" "" "" "" "" "startup verified via timeout"
      echo "[ok] $target plain"
    else
      append_result "$target" "$model" "$mode" "FAIL" "OK" "$rc" "" "" "" "" "" "run failed"
      echo "[fail] $target run"
    fi
    return
  fi

  set +e
  ./bin/PARSIR-simulator >"$run_log" 2>&1
  rc=$?
  set -e

  if [[ "$rc" -ne 0 ]]; then
    append_result "$target" "$model" "$mode" "FAIL" "OK" "$rc" "" "" "" "" "" "run failed"
    echo "[fail] $target run"
    return
  fi

  mean="$(awk -F': ' '/THROHGHPUT_MEAN/ {print $2}' "$run_log" | tail -n1)"
  ci="$(awk -F': ' '/THROHGHPUT_CI/ {print $2}' "$run_log" | tail -n1)"
  rollbacks="$(awk -F': ' '/ROLLBACKS/ {print $2}' "$run_log" | tail -n1)"
  epochs="$(awk -F': ' '/EPOCHS/ {print $2}' "$run_log" | tail -n1)"
  filtered="$(awk -F': ' '/FILTERED_EVENTS/ {print $2}' "$run_log" | tail -n1)"

  if [[ -z "$mean" ]]; then
    append_result "$target" "$model" "$mode" "FAIL" "OK" "$rc" "" "" "" "" "" "missing benchmark output"
    echo "[fail] $target missing metrics"
    return
  fi

  append_result "$target" "$model" "$mode" "OK" "OK" "0" "$mean" "$ci" "$rollbacks" "$epochs" "$filtered" "benchmark ok"
  echo "[ok] $target bench"
}

for target in "${plain_targets[@]}"; do
  run_target "$target"
done

for target in "${bench_targets[@]}"; do
  run_target "$target"
done

echo "[done] results -> $RESULTS"
