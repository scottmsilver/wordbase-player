#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-release}"
PERF_TEST_BIN="${PERF_TEST_BIN:-$BUILD_DIR/perf-test}"
DICTIONARY_PATH="${DICTIONARY_PATH:-$ROOT_DIR/src/twl06_with_wordbase_additions.txt}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [scenario]

Scenarios:
  short        Two-turn comparison baseline with TT enabled.
  long         Six-turn confirmation baseline with two warm-up turns.
  short-no-tt  Two-turn comparison baseline with TT disabled.
  profile      Repeat-search steady-state baseline for sampling profilers.
  all          Run all scenarios (default).

Environment overrides:
  BUILD_DIR        Build directory containing perf-test.
  PERF_TEST_BIN    Full path to the perf-test binary.
  DICTIONARY_PATH  Full path to the dictionary file.
EOF
}

run_short() {
  "$PERF_TEST_BIN" "$DICTIONARY_PATH" \
    --seconds 0.2 \
    --max-depth 4 \
    --max-moves 200 \
    --max-turns 2 \
    --warmup-turns 0
}

run_long() {
  "$PERF_TEST_BIN" "$DICTIONARY_PATH" \
    --seconds 0.2 \
    --max-depth 4 \
    --max-moves 200 \
    --max-turns 6 \
    --warmup-turns 2
}

run_short_no_tt() {
  "$PERF_TEST_BIN" "$DICTIONARY_PATH" \
    --seconds 0.2 \
    --max-depth 4 \
    --max-moves 200 \
    --max-turns 2 \
    --warmup-turns 0 \
    --no-tt
}

run_profile() {
  "$PERF_TEST_BIN" "$DICTIONARY_PATH" \
    --seconds 0.2 \
    --max-depth 4 \
    --max-moves 200 \
    --max-turns 2 \
    --warmup-turns 2 \
    --repeat-searches 5
}

ensure_binary() {
  if [[ ! -x "$PERF_TEST_BIN" ]]; then
    echo "perf-test binary not found: $PERF_TEST_BIN" >&2
    echo "Build it first with: cmake --build \"$BUILD_DIR\" --target perf-test" >&2
    exit 1
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
    short|long|short-no-tt|profile)
      run_named "$scenario"
      ;;
    all)
      run_named short
      run_named long
      run_named short-no-tt
      run_named profile
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
