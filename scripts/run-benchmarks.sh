#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-release}"
PERF_TEST_BIN="${PERF_TEST_BIN:-$BUILD_DIR/perf-test}"
DICTIONARY_PATH="${DICTIONARY_PATH:-$ROOT_DIR/src/twl06_with_wordbase_additions.txt}"
BOARD_SUITE_FILE="${BOARD_SUITE_FILE:-$ROOT_DIR/scripts/benchmark-board-suite.txt}"
NEURAL_MODEL="${NEURAL_MODEL:-}"

# If using a torch-enabled build, ensure torch libraries are on LD_LIBRARY_PATH.
TORCH_LIB_DIR="${TORCH_LIB_DIR:-/home/ssilver/anaconda3/lib/python3.12/site-packages/torch/lib}"
if [[ -n "$NEURAL_MODEL" && -d "$TORCH_LIB_DIR" ]]; then
  export LD_LIBRARY_PATH="${TORCH_LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

# Build neural model flags for perf-test invocations.
NEURAL_FLAGS=()
if [[ -n "$NEURAL_MODEL" ]]; then
  NEURAL_FLAGS=(--neural-model "$NEURAL_MODEL")
fi

usage() {
  cat <<EOF
Usage: $(basename "$0") [scenario]

Scenarios:
  short        Two-turn comparison baseline with TT enabled.
  long         Six-turn confirmation baseline with two warm-up turns.
  short-no-tt  Two-turn comparison baseline with TT disabled.
  profile      Repeat-search steady-state baseline for sampling profilers.
  profile-suite  Repeat-search suite across several checked-in boards.
  all          Run all scenarios (default).

Environment overrides:
  BUILD_DIR        Build directory containing perf-test.
  PERF_TEST_BIN    Full path to the perf-test binary.
  DICTIONARY_PATH  Full path to the dictionary file.
  BOARD_SUITE_FILE Full path to the checked-in benchmark board suite.
  NEURAL_MODEL     Path to TorchScript model for neural move ordering.
  TORCH_LIB_DIR    Path to torch shared libraries (auto-detected).
EOF
}

run_short() {
  "$PERF_TEST_BIN" "$DICTIONARY_PATH" \
    --seconds 0.2 \
    --max-depth 4 \
    --max-moves 200 \
    --max-turns 2 \
    --warmup-turns 0 \
    "${NEURAL_FLAGS[@]}"
}

run_long() {
  "$PERF_TEST_BIN" "$DICTIONARY_PATH" \
    --seconds 0.2 \
    --max-depth 4 \
    --max-moves 200 \
    --max-turns 6 \
    --warmup-turns 2 \
    "${NEURAL_FLAGS[@]}"
}

run_short_no_tt() {
  "$PERF_TEST_BIN" "$DICTIONARY_PATH" \
    --seconds 0.2 \
    --max-depth 4 \
    --max-moves 200 \
    --max-turns 2 \
    --warmup-turns 0 \
    --no-tt \
    "${NEURAL_FLAGS[@]}"
}

run_profile() {
  "$PERF_TEST_BIN" "$DICTIONARY_PATH" \
    --seconds 0.2 \
    --max-depth 4 \
    --max-moves 200 \
    --max-turns 2 \
    --warmup-turns 2 \
    --repeat-searches 5 \
    "${NEURAL_FLAGS[@]}"
}

run_profile_suite() {
  if [[ ! -f "$BOARD_SUITE_FILE" ]]; then
    echo "benchmark board suite not found: $BOARD_SUITE_FILE" >&2
    exit 1
  fi

  local count=0
  local total_nodes=0
  local total_avg_nodes=0
  local min_nodes=""
  local max_nodes=0
  local min_name=""
  local max_name=""

  while IFS='|' read -r name board_text; do
    if [[ -z "${name}" || "${name}" == \#* ]]; then
      continue
    fi

    echo "== profile-suite:$name =="
    local run_output
    run_output="$("$PERF_TEST_BIN" "$DICTIONARY_PATH" \
      --board "$board_text" \
      --seconds 0.2 \
      --max-depth 4 \
      --max-moves 200 \
      --max-turns 2 \
      --warmup-turns 2 \
      --repeat-searches 5 \
      "${NEURAL_FLAGS[@]}")"
    printf '%s\n' "$run_output"

    local summary_line
    summary_line="$(printf '%s\n' "$run_output" | grep '^summary ' | tail -n 1)"
    local nodes
    nodes="$(printf '%s\n' "$summary_line" | sed -n 's/.*total_nodes=\([0-9][0-9]*\).*/\1/p')"
    local avg_nodes
    avg_nodes="$(printf '%s\n' "$summary_line" | sed -n 's/.*avg_nodes_per_turn=\([0-9.][0-9.]*\).*/\1/p')"
    if [[ -z "$nodes" || -z "$avg_nodes" ]]; then
      echo "Could not parse summary for board: $name" >&2
      exit 1
    fi

    count=$((count + 1))
    total_nodes=$((total_nodes + nodes))
    total_avg_nodes="$(awk -v total="$total_avg_nodes" -v value="$avg_nodes" 'BEGIN { printf "%.6f", total + value }')"

    if [[ -z "$min_nodes" || "$nodes" -lt "$min_nodes" ]]; then
      min_nodes="$nodes"
      min_name="$name"
    fi
    if [[ "$nodes" -gt "$max_nodes" ]]; then
      max_nodes="$nodes"
      max_name="$name"
    fi
  done <"$BOARD_SUITE_FILE"

  if [[ "$count" -eq 0 ]]; then
    echo "No benchmark boards found in: $BOARD_SUITE_FILE" >&2
    exit 1
  fi

  local avg_total_nodes
  avg_total_nodes="$(awk -v total="$total_nodes" -v count="$count" 'BEGIN { printf "%.2f", total / count }')"
  local avg_avg_nodes
  avg_avg_nodes="$(awk -v total="$total_avg_nodes" -v count="$count" 'BEGIN { printf "%.2f", total / count }')"

  echo "== profile-suite:summary =="
  echo "suite boards=$count avg_total_nodes=$avg_total_nodes avg_nodes_per_turn=$avg_avg_nodes min_total_nodes=$min_nodes min_board=$min_name max_total_nodes=$max_nodes max_board=$max_name"
}

ensure_binary() {
  if [[ ! -x "$PERF_TEST_BIN" ]]; then
    echo "perf-test binary not found: $PERF_TEST_BIN" >&2
    echo "Build it first with: cmake --build \"$BUILD_DIR\" --target perf-test" >&2
    exit 1
  fi

  # Rebuild if any source/header is newer than the perf-test binary.
  if find "$ROOT_DIR/src" -type f \( -name '*.cpp' -o -name '*.h' \) -newer "$PERF_TEST_BIN" -print -quit | grep -q .; then
    echo "perf-test is out of date; rebuilding..." >&2
    cmake --build "$BUILD_DIR" --target perf-test
  fi
}

run_named() {
  local name="$1"
  echo "== $name =="
  case "$name" in
    short) run_short ;;
    long) run_long ;;
    short-no-tt) run_short_no_tt ;;
    profile) run_profile ;;
    profile-suite) run_profile_suite ;;
    *)
      echo "Unknown scenario: $name" >&2
      usage >&2
      exit 1
      ;;
  esac
}

main() {
  ensure_binary

  local scenario="${1:-all}"
  case "$scenario" in
    short|long|short-no-tt|profile|profile-suite)
      run_named "$scenario"
      ;;
    all)
      run_named short
      run_named long
      run_named short-no-tt
      run_named profile
      run_named profile-suite
      ;;
    -h|--help)
      usage
      ;;
    *)
      echo "Unknown scenario: $scenario" >&2
      usage >&2
      exit 1
      ;;
  esac
}

main "$@"
