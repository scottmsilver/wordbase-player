#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TASK_ROOT="$ROOT_DIR/logs/agent-fleet/tasks"
STATE_DIR="$ROOT_DIR/logs/agent-fleet/master"
PROMPT_FILE="$STATE_DIR/prompt.txt"
LAST_MESSAGE_FILE="$STATE_DIR/last-message.txt"
STOP_FILE="$STATE_DIR/STOP"
BENCHMARK_LOG="$ROOT_DIR/benchmark-progress.csv"

INTERVAL_SECONDS=10
MAX_ITERATIONS=0
ONCE=0
ENABLE_SEARCH=0
MODEL=""
MIN_PENDING=3
TASK_BATCH=4

usage() {
  cat <<'EOF'
Usage: ./scripts/agent-master.sh [options]

Runs a planning-only Codex master that generates concrete experiment task files.

Options:
  --once                   Run one planning pass and exit
  --interval <seconds>     Sleep between planning passes (default: 10)
  --max-iterations <n>     Stop after n iterations (default: unlimited)
  --model <name>           Pass an explicit model name to codex exec
  --search                 Enable web search during runs
  --min-pending <n>        Only generate tasks when pending count is below n (default: 3)
  --task-batch <n>         Target number of tasks to add per planning pass (default: 4)
  --help                   Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --once)
      ONCE=1
      shift
      ;;
    --interval)
      INTERVAL_SECONDS="$2"
      shift 2
      ;;
    --max-iterations)
      MAX_ITERATIONS="$2"
      shift 2
      ;;
    --model)
      MODEL="$2"
      shift 2
      ;;
    --search)
      ENABLE_SEARCH=1
      shift
      ;;
    --min-pending)
      MIN_PENDING="$2"
      shift 2
      ;;
    --task-batch)
      TASK_BATCH="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "$MODEL" ]]; then
  MODEL="gpt-5.4"
fi

if ! command -v codex >/dev/null 2>&1; then
  echo "codex CLI not found in PATH" >&2
  exit 1
fi

mkdir -p "$STATE_DIR" "$TASK_ROOT/pending" "$TASK_ROOT/in-progress" "$TASK_ROOT/done" "$TASK_ROOT/failed"

build_prompt() {
  local pending_count recent_kept recent_discards previous_summary
  pending_count="$(find "$TASK_ROOT/pending" -maxdepth 1 -name '*.md' | wc -l | tr -d ' ')"
  recent_kept="$(awk -F, 'NR>1 && $2=="kept" { rows[++n]=$0 } END { start=n-2; if (start < 1) start=1; for (i=start; i<=n; ++i) print rows[i] }' "$BENCHMARK_LOG" 2>/dev/null || true)"
  recent_discards="$(awk -F, 'NR>1 && $2=="discarded" { rows[++n]=$0 } END { start=n-4; if (start < 1) start=1; for (i=start; i<=n; ++i) print rows[i] }' "$BENCHMARK_LOG" 2>/dev/null || true)"
  previous_summary=""
  if [[ -f "$LAST_MESSAGE_FILE" ]]; then
    previous_summary="$(tail -n 40 "$LAST_MESSAGE_FILE")"
  fi

  cat >"$PROMPT_FILE" <<EOF
Act as the optimization master for $ROOT_DIR.

Your only job is to generate concrete worker tasks under:
- $TASK_ROOT/pending

Rules:
- Do not modify tracked source files.
- Do not commit or push anything.
- Generate at most $TASK_BATCH new task files, and only if pending tasks are below $MIN_PENDING.
- Each task file must be a standalone markdown file with:
  - Title
  - Hypothesis
  - Concrete change to try
  - Exact benchmark commands
  - Keep/revert criteria
- Prefer high-signal search and move-ordering ideas backed by the benchmark history.
- Avoid ideas already discarded unless you have a clearly different angle.
- Use concrete Wordbase examples in the task description when useful.
- Every task must tell the worker to run ./scripts/run-benchmarks.sh profile first and then ./scripts/run-benchmarks.sh profile-suite before keeping a change.
- Keep criteria should require a profile win and no obvious regression on the suite. Tiny profile wins with suite regressions should be discarded.
- Task mix: aim for 3 conservative/static move-ordering ideas and 1 "zany" idea per batch. Zany ideas can include ML policy stubs, GPU-assisted evaluation, stochastic rollouts, bold pruning/ordering schemes, or parallelism experiments (e.g., parallel root move evaluation or speculative node expansion), but must still specify a minimal, testable change and the same benchmark gates.

Current pending task count:
- $pending_count

Recent kept experiments:
$recent_kept

Recent discarded experiments:
$recent_discards

Previous master summary:
$previous_summary
EOF
}

ITERATION=1
while true; do
  if [[ -f "$STOP_FILE" ]]; then
    echo "Stop file detected at $STOP_FILE"
    exit 0
  fi

  if [[ "$MAX_ITERATIONS" -gt 0 && "$ITERATION" -gt "$MAX_ITERATIONS" ]]; then
    echo "Reached max iterations: $MAX_ITERATIONS"
    exit 0
  fi

  pending_count="$(find "$TASK_ROOT/pending" -maxdepth 1 -name '*.md' | wc -l | tr -d ' ')"
  if [[ "$pending_count" -lt "$MIN_PENDING" ]]; then
    build_prompt
    CMD=(
      codex exec
      -C "$ROOT_DIR"
      -s danger-full-access
      -c 'approval_policy="never"'
      --json
      -o "$LAST_MESSAGE_FILE"
    )

    if [[ -n "$MODEL" ]]; then
      CMD+=(-m "$MODEL")
    fi

    if [[ "$ENABLE_SEARCH" -eq 1 ]]; then
      CMD+=(--search)
    fi

    printf 'Starting master iteration %d at %s\n' "$ITERATION" "$(date -Iseconds)"
    "${CMD[@]}" - <"$PROMPT_FILE" | tee "$STATE_DIR/iteration-$ITERATION.jsonl"
    cp "$LAST_MESSAGE_FILE" "$STATE_DIR/iteration-$ITERATION.txt"
  else
    printf 'Master idle: pending task count %s >= %s\n' "$pending_count" "$MIN_PENDING"
  fi

  if [[ "$ONCE" -eq 1 ]]; then
    exit 0
  fi

  ITERATION=$((ITERATION + 1))
  sleep "$INTERVAL_SECONDS"
done
