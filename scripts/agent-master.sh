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
  local pending_count all_kept all_discards recent_discards previous_summary
  local done_task_titles discard_count kept_count
  pending_count="$(find "$TASK_ROOT/pending" -maxdepth 1 -name '*.md' | wc -l | tr -d ' ')"
  all_kept="$(awk -F, 'NR>1 && $2=="kept" { print $0 }' "$BENCHMARK_LOG" 2>/dev/null || true)"
  all_discards="$(awk -F, 'NR>1 && $2=="discarded" { print $0 }' "$BENCHMARK_LOG" 2>/dev/null || true)"
  recent_discards="$(awk -F, 'NR>1 && $2=="discarded" { rows[++n]=$0 } END { start=n-19; if (start < 1) start=1; for (i=start; i<=n; ++i) print rows[i] }' "$BENCHMARK_LOG" 2>/dev/null || true)"
  discard_count="$(echo "$all_discards" | grep -c . 2>/dev/null || echo 0)"
  kept_count="$(echo "$all_kept" | grep -c . 2>/dev/null || echo 0)"
  done_task_titles="$(find "$TASK_ROOT/done" -maxdepth 1 -name '*.md' -exec head -2 {} \; 2>/dev/null | grep '^# ' | sed 's/^# //' || true)"
  previous_summary=""
  if [[ -f "$LAST_MESSAGE_FILE" ]]; then
    previous_summary="$(tail -n 40 "$LAST_MESSAGE_FILE")"
  fi

  cat >"$PROMPT_FILE" <<EOF
Act as the optimization master for $ROOT_DIR.

Your only job is to generate concrete worker tasks under:
- $TASK_ROOT/pending

## Critical: Diversity and Dead-End Avoidance

Before generating tasks, you MUST:

1. READ the full discard history below carefully. Count how many times each CATEGORY of idea has been tried and failed. Categories include things like "front-bucket tie-breaking", "TT move ordering", "heuristic weight tuning", "bomb bonus tuning", etc.

2. DO NOT generate tasks in a category that has been tried and discarded 3+ times without a single keep. That vein is exhausted. Move on to fundamentally different approaches.

3. Before writing tasks, RE-READ the source code. Run:
   - cat src/gtsa.hpp (the search algorithm)
   - cat src/board.h (the evaluation and move ordering)
   - cat src/wordescape.cpp (the game state implementation)
   Look for actual bottlenecks and unexplored opportunities, not just parameter tweaks on existing signals.

4. Each batch of tasks must span at least 3 DIFFERENT categories. Never generate 2+ tasks that are variants of the same idea.

## Idea Categories to Explore (rotate through these)

Move-ordering tasks are only ONE category. Also consider:

- **Search algorithm changes**: Late move reductions (search obviously bad moves to shallower depth). Aspiration windows around the iterative-deepening value. Multi-PV search to find backup lines. Null-move pruning or similar forward pruning.
- **Evaluation function changes**: The goodness() function itself — how board positions are scored. New evaluation terms, reweighting existing ones. Piece-square style tables for Wordbase. Territory/connectivity evaluation.
- **Move generation pruning**: Don't generate clearly terrible moves at all. Adaptive MAX_MOVES per ply. Futility pruning — skip moves that can't possibly raise alpha. History-based move generation cutoffs.
- **Transposition table improvements**: Replacement policies (depth-preferred, always-replace, two-tier). Entry aging across iterative deepening iterations. Better hash distribution. Prefetching.
- **Data structure and algorithmic improvements**: Faster board state representation. Incremental hash updates. Cheaper make/unmake move. Memory layout optimizations for cache friendliness.
- **Time management**: Allocate more time to critical positions. Early termination when the best move is clearly dominant. Pondering (searching during opponent's turn in self-play).
- **Game-specific strategic ideas**: Connectivity-based evaluation (are owned squares connected to edge?). Threat detection (opponent one move from winning). Defensive move bonuses. Bomb chain analysis.

## Rules
- Do not modify tracked source files.
- Do not commit or push anything.
- Generate at most $TASK_BATCH new task files, and only if pending tasks are below $MIN_PENDING.
- Each task file must be a standalone markdown file with:
  - Title
  - Hypothesis
  - Concrete change to try
  - Exact benchmark commands
  - Keep/revert criteria
- Use concrete Wordbase examples in the task description when useful.
- Every task must tell the worker to run ./scripts/run-benchmarks.sh profile-suite as the PRIMARY benchmark gate. The worker should also run ./scripts/run-benchmarks.sh profile for additional signal, but keep/discard decisions should be based on the SUITE average, not one board.
- Keep criteria: require a suite-wide average improvement (avg_total_nodes) with no individual board regressing badly, and no depth drops. Single-board-only wins that don't hold across the suite should be discarded.
- Task mix: aim for tasks spanning 3-4 different categories. At most 1 task per batch should be a move-ordering heuristic tweak. At least 1 should be a search algorithm or evaluation change. At least 1 should be something the system has never tried before.

## Statistics
- Total kept: $kept_count
- Total discarded: $discard_count
- Hit rate: roughly $(( kept_count * 100 / (discard_count + kept_count + 1) ))%

Current pending task count:
- $pending_count

All kept experiments (the full win history — study what actually worked):
$all_kept

Last 20 discarded experiments (study patterns of what fails):
$recent_discards

All completed task titles (do not duplicate these):
$done_task_titles

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
