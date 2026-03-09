#!/usr/bin/env bash

set -euo pipefail

CANONICAL_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT_DIR="$CANONICAL_ROOT"
CANONICAL_BENCHMARK_LOG="$CANONICAL_ROOT/benchmark-progress.csv"
export GIT_EDITOR=true
export GIT_SEQUENCE_EDITOR=true
export GIT_MERGE_AUTOEDIT=no

INTERVAL_SECONDS=1
MAX_ITERATIONS=0
ONCE=0
ENABLE_SEARCH=0
MODEL=""
BASE_REF="origin/master"
TARGET_BRANCH=""
STATE_SUBDIR=""
TASK_FILE=""
TASK_DIR=""
WORKER_NAME=""
BENCHMARK_LOG=""
REQUIRE_TASK=0
FRESH_TREE=0
LAST_WORKTREE_FILE=""

usage() {
  cat <<'EOF'
Usage: ./scripts/agent-loop.sh [options]

Runs one autonomous Codex worker loop.

Options:
  --once                   Run a single iteration and exit
  --interval <seconds>     Sleep between iterations (default: 1)
  --max-iterations <n>     Stop after n iterations (default: unlimited)
  --model <name>           Pass an explicit model name to codex exec
  --search                 Enable web search during runs
  --branch <name>          Reset/create and use a scratch branch from --base-ref
  --base-ref <ref>         Base ref for scratch branches (default: origin/master)
  --state-subdir <name>    Store loop logs under logs/agent-loop/<name>
  --worker-name <name>     Label used in prompts and claimed task filenames
  --benchmark-log <path>   Worker-local benchmark CSV path
  --task-file <path>       Run a specific task file
  --task-dir <path>        Claim tasks from <path>/pending and move them through the queue
  --require-task           Only run when a task is assigned (sleep if none)
  --fresh-tree             Reset to base ref and clean untracked files each iteration
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
    --branch)
      TARGET_BRANCH="$2"
      shift 2
      ;;
    --base-ref)
      BASE_REF="$2"
      shift 2
      ;;
    --state-subdir)
      STATE_SUBDIR="$2"
      shift 2
      ;;
    --worker-name)
      WORKER_NAME="$2"
      shift 2
      ;;
    --benchmark-log)
      BENCHMARK_LOG="$2"
      shift 2
      ;;
    --task-file)
      TASK_FILE="$2"
      shift 2
      ;;
    --task-dir)
      TASK_DIR="$2"
      shift 2
      ;;
    --require-task)
      REQUIRE_TASK=1
      shift
      ;;
    --fresh-tree)
      FRESH_TREE=1
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

trap 'echo "agent-loop exit rc=$? worker=$WORKER_NAME branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)" >&2' EXIT

if [[ -z "$WORKER_NAME" ]]; then
  if [[ -n "$STATE_SUBDIR" ]]; then
    WORKER_NAME="$STATE_SUBDIR"
  else
    WORKER_NAME="$(basename "$ROOT_DIR")"
  fi
fi

STATE_DIR="$ROOT_DIR/logs/agent-loop"
if [[ -n "$STATE_SUBDIR" ]]; then
  STATE_DIR="$STATE_DIR/$STATE_SUBDIR"
fi
PROMPT_FILE="$STATE_DIR/prompt.txt"
STOP_FILE="$STATE_DIR/STOP"
LAST_MESSAGE_FILE="$STATE_DIR/last-message.txt"
LAST_WORKTREE_FILE="$STATE_DIR/last-worktree.txt"
mkdir -p "$STATE_DIR"

if [[ -z "$BENCHMARK_LOG" ]]; then
  BENCHMARK_LOG="$CANONICAL_BENCHMARK_LOG"
fi

if [[ "$FRESH_TREE" -eq 1 ]]; then
  BENCHMARK_LOG="$CANONICAL_ROOT/logs/agent-loop/$WORKER_NAME/benchmark-progress.csv"
fi

mkdir -p "$(dirname "$BENCHMARK_LOG")"
if [[ ! -f "$BENCHMARK_LOG" ]]; then
  if [[ -f "$CANONICAL_BENCHMARK_LOG" ]]; then
    cp "$CANONICAL_BENCHMARK_LOG" "$BENCHMARK_LOG"
  else
    printf 'timestamp,status,scenario,commit,change_ref,total_nodes,avg_nodes_per_turn,deepest_completed_depth,notes\n' >"$BENCHMARK_LOG"
  fi
fi

if [[ "$FRESH_TREE" -eq 0 ]]; then
  if [[ -n "$TARGET_BRANCH" ]]; then
    git fetch origin >/dev/null 2>&1 || true
    git checkout -B "$TARGET_BRANCH" "$BASE_REF" >/dev/null
  fi

  CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
  if [[ "$CURRENT_BRANCH" == "master" || "$CURRENT_BRANCH" == "main" ]]; then
    echo "Refusing to run worker loop on $CURRENT_BRANCH. Use --branch to create a scratch branch." >&2
    exit 1
  fi
fi

if [[ -n "$TASK_DIR" ]]; then
  mkdir -p "$TASK_DIR/pending" "$TASK_DIR/in-progress" "$TASK_DIR/done" "$TASK_DIR/failed"
fi

worker_workspace() {
  local worker_name="$1"
  if [[ "$worker_name" == "$WORKER_NAME" ]]; then
    printf '%s\n' "$ROOT_DIR"
  else
    printf '%s/.codex-workers/%s\n' "$(dirname "$ROOT_DIR")" "$worker_name"
  fi
}

worker_has_live_codex() {
  local worker_name="$1"
  local workspace
  workspace="$(worker_workspace "$worker_name")"
  pgrep -af 'codex exec -C ' \
    | awk -v workspace="$workspace" '
        {
          for (i = 1; i <= NF; ++i) {
            if ($i == "-C" && (i + 1) <= NF && $(i + 1) == workspace) {
              found = 1;
              exit;
            }
          }
        }
        END { exit(found ? 0 : 1) }
      ' >/dev/null 2>&1
}

reclaim_stale_tasks() {
  if [[ -z "$TASK_DIR" ]]; then
    return 0
  fi

  shopt -s nullglob
  local stale_file
  for stale_file in "$TASK_DIR"/in-progress/*.md; do
    local stale_base stale_name stale_worker restored_name restored_path
    stale_base="$(basename "$stale_file")"
    stale_name="${stale_base%.md}"
    stale_worker="${stale_name##*--}"
    if [[ -z "$stale_worker" || "$stale_worker" == "$stale_name" ]]; then
      continue
    fi
    if worker_has_live_codex "$stale_worker"; then
      continue
    fi

    restored_name="${stale_name%--$stale_worker}.md"
    restored_path="$TASK_DIR/pending/$restored_name"
    if [[ -e "$restored_path" ]]; then
      restored_path="$TASK_DIR/pending/${restored_name%.md}-reclaimed-$(date +%s).md"
    fi
    mv "$stale_file" "$restored_path"
  done
  shopt -u nullglob
}

claim_task() {
  reclaim_stale_tasks

  if [[ -n "$TASK_FILE" ]]; then
    if [[ -f "$TASK_FILE" ]]; then
      printf '%s\n' "$TASK_FILE"
      return 0
    fi
    return 1
  fi

  if [[ -z "$TASK_DIR" ]]; then
    return 1
  fi

  shopt -s nullglob
  local candidates=("$TASK_DIR"/pending/*.md)
  shopt -u nullglob
  if [[ "${#candidates[@]}" -eq 0 ]]; then
    return 1
  fi

  local pending_file="${candidates[0]}"
  local claimed_file="$TASK_DIR/in-progress/$(basename "${pending_file%.md}")--$WORKER_NAME.md"
  mv "$pending_file" "$claimed_file"
  printf '%s\n' "$claimed_file"
}

build_prompt() {
  local task_path="$1"
  local current_head benchmark_floor recent_kept recent_discards previous_summary task_body

  current_head="$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  benchmark_floor="$(tail -n 1 "$BENCHMARK_LOG" 2>/dev/null || true)"
  recent_kept="$(awk -F, 'NR>1 && $2=="kept" { rows[++n]=$0 } END { start=n-2; if (start < 1) start=1; for (i=start; i<=n; ++i) print rows[i] }' "$BENCHMARK_LOG" 2>/dev/null || true)"
  recent_discards="$(awk -F, 'NR>1 && $2=="discarded" { rows[++n]=$0 } END { start=n-4; if (start < 1) start=1; for (i=start; i<=n; ++i) print rows[i] }' "$BENCHMARK_LOG" 2>/dev/null || true)"
  previous_summary=""
  if [[ -f "$LAST_MESSAGE_FILE" ]]; then
    previous_summary="$(tail -n 40 "$LAST_MESSAGE_FILE")"
  fi
  task_body="No queued task was assigned. Continue the benchmark loop conservatively."
  if [[ -n "$task_path" && -f "$task_path" ]]; then
    task_body="$(cat "$task_path")"
  fi

  cat >"$PROMPT_FILE" <<EOF
Continue the optimization loop in $ROOT_DIR as worker "$WORKER_NAME".

Operating rules:
- Work only on the current scratch branch: $CURRENT_BRANCH
- Never push directly to master/main. Commit retained wins to the current branch and push that branch only.
- Use $BENCHMARK_LOG as the running experiment log. Record both discarded and retained experiments.
- Revert losers instead of leaving churn in the tree.
- Add short comments for non-obvious heuristic and search changes, and explain the intuition with concrete Wordbase-style examples.
- Prefer broad, board-derived, static move-ordering signals over brittle max-style features.
- Use the checked-in benchmark scenarios and treat repeated-search profile results as the primary gate for search tuning.
- After a change wins on profile, check profile-suite before keeping it so we do not overfit the README board.
- If a result is marginal, rerun it and keep it only if it reproduces.
- Do not touch unrelated untracked files.

Current repository head:
- $current_head

Most recent benchmark row:
$benchmark_floor

Recent kept experiments:
$recent_kept

Recent discarded experiments:
$recent_discards

Previous worker summary:
$previous_summary

Assigned task:
$task_body

Your job in this iteration:
- Treat $ROOT_DIR as the only workspace root for this run.
- Inspect the current tree state.
- Execute the assigned experiment or the best next conservative experiment if no task was assigned.
- Benchmark with ./scripts/run-benchmarks.sh profile first, then ./scripts/run-benchmarks.sh profile-suite for any candidate you might keep.
- Leave the branch in a sensible state at the end of the iteration.
EOF
}

cleanup_previous_worktree() {
  if [[ ! -f "$LAST_WORKTREE_FILE" ]]; then
    return 0
  fi
  local prev_root
  prev_root="$(cat "$LAST_WORKTREE_FILE")"
  if [[ -n "$prev_root" && -d "$prev_root" ]]; then
    git -C "$CANONICAL_ROOT" worktree remove -f "$prev_root" >/dev/null 2>&1 || true
    rm -rf "$prev_root" >/dev/null 2>&1 || true
  fi
  : >"$LAST_WORKTREE_FILE"
}

prepare_fresh_worktree() {
  cleanup_previous_worktree

  local stamp branch_prefix branch_name work_root
  stamp="$(date +%Y%m%d-%H%M%S)-$ITERATION"
  branch_prefix="${TARGET_BRANCH:-agent-fleet/${WORKER_NAME}}"
  branch_name="${branch_prefix}-iter-${stamp}"
  work_root="$CANONICAL_ROOT/.codex-workers/${WORKER_NAME}-iter-${stamp}"

  git -C "$CANONICAL_ROOT" fetch origin >/dev/null 2>&1 || true
  git -C "$CANONICAL_ROOT" worktree add -B "$branch_name" "$work_root" "$BASE_REF" >/dev/null

  cmake -S "$work_root/src" -B "$work_root/build-release" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$work_root/build-release" --target perf-test -j4 >/dev/null

  ROOT_DIR="$work_root"
  CURRENT_BRANCH="$branch_name"
  echo "$work_root" >"$LAST_WORKTREE_FILE"
}

maybe_commit_kept_result() {
  local last_meta status scenario change_ref
  if [[ ! -f "$BENCHMARK_LOG" ]]; then
    return 0
  fi
  last_meta="$(awk -F, 'NR>1 { status=$2; scenario=$3; change_ref=$5 } END { print status "|" scenario "|" change_ref }' "$BENCHMARK_LOG")"
  if [[ -z "$last_meta" ]]; then
    return 0
  fi
  IFS='|' read -r status scenario change_ref <<<"$last_meta"
  if [[ "$status" != "kept" ]]; then
    return 0
  fi
  if [[ "$scenario" != "profile-suite" && "$scenario" != "profile-suite"* ]]; then
    return 0
  fi

  if [[ -z "$(git -C "$ROOT_DIR" status --porcelain)" ]]; then
    return 0
  fi

  git -C "$ROOT_DIR" add -A >/dev/null 2>&1 || true
  if [[ -f "$BENCHMARK_LOG" ]]; then
    git -C "$ROOT_DIR" add -f "$BENCHMARK_LOG" >/dev/null 2>&1 || true
  fi
  if [[ -z "$(git -C "$ROOT_DIR" diff --cached --name-only)" ]]; then
    return 0
  fi

  local commit_msg
  if [[ -n "$change_ref" ]]; then
    commit_msg="Keep ${change_ref}"
  else
    commit_msg="Keep benchmark win"
  fi
  git -C "$ROOT_DIR" commit -m "$commit_msg" >/dev/null 2>&1 || return 0
  git -C "$ROOT_DIR" push origin "$CURRENT_BRANCH" >/dev/null 2>&1 || true
}

ITERATION=1
if compgen -G "$STATE_DIR/iteration-*.jsonl" >/dev/null; then
  ITERATION="$(
    find "$STATE_DIR" -maxdepth 1 -name 'iteration-*.jsonl' -printf '%f\n' \
      | sed -E 's/iteration-([0-9]+)\.jsonl/\1/' \
      | sort -n \
      | tail -n 1
  )"
  ITERATION=$((ITERATION + 1))
fi

while true; do
  if [[ -f "$STOP_FILE" ]]; then
    echo "Stop file detected at $STOP_FILE"
    exit 0
  fi

  if [[ "$MAX_ITERATIONS" -gt 0 && "$ITERATION" -gt "$MAX_ITERATIONS" ]]; then
    echo "Reached max iterations: $MAX_ITERATIONS"
    exit 0
  fi

  TASK_PATH=""
  if TASK_PATH="$(claim_task)"; then
    :
  else
    if [[ "$ONCE" -eq 1 && -n "$TASK_DIR" ]]; then
      echo "No pending tasks in $TASK_DIR/pending"
      exit 0
    fi
    if [[ "$REQUIRE_TASK" -eq 1 && -n "$TASK_DIR" ]]; then
      echo "No pending tasks in $TASK_DIR/pending; waiting." >&2
      sleep "$INTERVAL_SECONDS"
      ITERATION=$((ITERATION + 1))
      continue
    fi
  fi

  if [[ "$FRESH_TREE" -eq 1 ]]; then
    prepare_fresh_worktree
  fi

  build_prompt "$TASK_PATH"
  ITERATION_PREFIX="$STATE_DIR/iteration-$ITERATION"

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

  printf 'Starting worker %s iteration %d at %s\n' "$WORKER_NAME" "$ITERATION" "$(date -Iseconds)"
  printf 'Prompt file: %s\n' "$PROMPT_FILE"
  set +e
  "${CMD[@]}" - <"$PROMPT_FILE" | tee "$ITERATION_PREFIX.jsonl"
  cmd_status=${PIPESTATUS[0]}
  set -e

  if [[ -f "$LAST_MESSAGE_FILE" ]]; then
    cp "$LAST_MESSAGE_FILE" "$ITERATION_PREFIX.txt"
  else
    : >"$ITERATION_PREFIX.txt"
  fi

  if [[ -n "$TASK_PATH" && -f "$TASK_PATH" ]]; then
    local_result_prefix="$STATE_DIR/results-$(basename "${TASK_PATH%.md}")"
    if [[ -f "$LAST_MESSAGE_FILE" ]]; then
      cp "$LAST_MESSAGE_FILE" "${local_result_prefix}.txt"
    else
      : >"${local_result_prefix}.txt"
    fi
    cp "$PROMPT_FILE" "${local_result_prefix}.prompt.txt"
    if [[ "$TASK_PATH" == *"/in-progress/"* ]]; then
      if [[ "$cmd_status" -eq 0 ]]; then
        if ! mv "$TASK_PATH" "${TASK_PATH/\/in-progress\//\/done\/}"; then
          echo "warning: failed to move task to done: $TASK_PATH" >&2
        fi
      else
        if ! mv "$TASK_PATH" "${TASK_PATH/\/in-progress\//\/failed\/}"; then
          echo "warning: failed to move task to failed: $TASK_PATH" >&2
        fi
        if [[ -n "$TASK_DIR" ]]; then
          mkdir -p "$TASK_DIR/failed-results"
          failed_report="$TASK_DIR/failed-results/$(basename "${TASK_PATH%.md}")--$WORKER_NAME-$(date +%Y%m%d-%H%M%S).txt"
          {
            echo "task: $TASK_PATH"
            echo "worker: $WORKER_NAME"
            echo "timestamp: $(date -Iseconds)"
            echo "last_benchmark: $(tail -n 1 "$BENCHMARK_LOG" 2>/dev/null || true)"
            echo
            if [[ -f "$LAST_MESSAGE_FILE" ]]; then
              cat "$LAST_MESSAGE_FILE"
            fi
          } >"$failed_report"
        fi
      fi
    fi
  fi

  if [[ "$cmd_status" -ne 0 ]]; then
    printf 'Worker %s iteration %d failed with exit code %d\n' "$WORKER_NAME" "$ITERATION" "$cmd_status" >&2
  fi

  maybe_commit_kept_result

  if [[ "$ONCE" -eq 1 ]]; then
    exit "$cmd_status"
  fi

  echo "worker=$WORKER_NAME iteration=$ITERATION complete rc=$cmd_status sleeping=${INTERVAL_SECONDS}s" >&2
  ITERATION=$((ITERATION + 1))
  sleep "$INTERVAL_SECONDS"
done
