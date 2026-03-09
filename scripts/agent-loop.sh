#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STATE_DIR="$ROOT_DIR/logs/agent-loop"
PROMPT_FILE="$STATE_DIR/prompt.txt"
STOP_FILE="$STATE_DIR/STOP"
mkdir -p "$STATE_DIR"

INTERVAL_SECONDS=1
MAX_ITERATIONS=0
ONCE=0
ENABLE_SEARCH=0
MODEL=""

usage() {
  cat <<'EOF'
Usage: ./scripts/agent-loop.sh [options]

Runs Codex repeatedly against this repository until a stop file is created.

Options:
  --once                 Run a single iteration and exit
  --interval <seconds>   Sleep between iterations (default: 1)
  --max-iterations <n>   Stop after n iterations (default: unlimited)
  --model <name>         Pass an explicit model name to codex exec
  --search               Enable web search during runs
  --help                 Show this help

Control:
  touch logs/agent-loop/STOP
    Stop after the current iteration finishes.

Outputs:
  logs/agent-loop/last-message.txt
  logs/agent-loop/iteration-<n>.jsonl
  logs/agent-loop/iteration-<n>.txt
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

if ! command -v codex >/dev/null 2>&1; then
  echo "codex CLI not found in PATH" >&2
  exit 1
fi

cat >"$PROMPT_FILE" <<'EOF'
Continue the optimization loop in /home/ssilver/development/wordbase-player.

Operating rules:
- Keep iterating without asking the user for permission unless blocked by missing credentials, external limits, or a true ambiguity that would make changes risky.
- Use benchmark-progress.csv as the running experiment log. Record both discarded and retained experiments.
- Commit and push retained wins. Revert losers instead of leaving churn in the tree.
- Add short comments for non-obvious heuristic and search changes, and explain the intuition with concrete Wordbase-style examples rather than abstract phrasing.
- Prefer broad, board-derived, static move-ordering signals over brittle max-style features.
- Use the checked-in benchmark scenarios and treat repeated-search profile results as the primary gate for search tuning.
- If a result is marginal, rerun it and keep it only if it reproduces.
- Do not touch unrelated untracked files.

Current known-good benchmark floor:
- profile: 364230
- short: 87981
- best pushed checkpoint: 2f95694

Your job in this iteration:
- Inspect the current tree state.
- Continue the optimization and measurement loop.
- Leave the repository in a sensible state at the end of the iteration.
EOF

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

  ITERATION_PREFIX="$STATE_DIR/iteration-$ITERATION"
  LAST_MESSAGE_FILE="$STATE_DIR/last-message.txt"

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

  printf 'Starting iteration %d at %s\n' "$ITERATION" "$(date -Iseconds)"
  printf 'Prompt file: %s\n' "$PROMPT_FILE"
  "${CMD[@]}" - <"$PROMPT_FILE" | tee "$ITERATION_PREFIX.jsonl"
  cp "$LAST_MESSAGE_FILE" "$ITERATION_PREFIX.txt"

  if [[ "$ONCE" -eq 1 ]]; then
    exit 0
  fi

  ITERATION=$((ITERATION + 1))
  sleep "$INTERVAL_SECONDS"
done
