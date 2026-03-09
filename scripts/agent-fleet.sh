#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKTREE_ROOT="$ROOT_DIR/.codex-workers"
TASK_ROOT="$ROOT_DIR/logs/agent-fleet/tasks"
PID_DIR="$ROOT_DIR/logs/agent-fleet/pids"

WORKERS=2
MODEL=""
MASTER_MODEL=""
BASE_REF="origin/master"
ENABLE_SEARCH=0

usage() {
  cat <<'EOF'
Usage: ./scripts/agent-fleet.sh [options]

Launch one planning master and multiple worker loops in parallel worktrees.

Options:
  --workers <n>          Number of workers to start (default: 2)
  --model <name>         Model for workers
  --master-model <name>  Model for the master planner
  --base-ref <ref>       Base ref for worker branches (default: origin/master)
  --search               Enable web search for master and workers
  --help                 Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --workers)
      WORKERS="$2"
      shift 2
      ;;
    --model)
      MODEL="$2"
      shift 2
      ;;
    --master-model)
      MASTER_MODEL="$2"
      shift 2
      ;;
    --base-ref)
      BASE_REF="$2"
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

if [[ -z "$MODEL" ]]; then
  MODEL="gpt-5.2-codex-high"
fi
if [[ -z "$MASTER_MODEL" ]]; then
  MASTER_MODEL="$MODEL"
fi

mkdir -p "$WORKTREE_ROOT" "$TASK_ROOT/pending" "$TASK_ROOT/in-progress" "$TASK_ROOT/done" "$TASK_ROOT/failed" "$PID_DIR"

launch_master() {
  local cmd=("$ROOT_DIR/scripts/agent-master.sh")
  if [[ -n "$MASTER_MODEL" ]]; then
    cmd+=(--model "$MASTER_MODEL")
  elif [[ -n "$MODEL" ]]; then
    cmd+=(--model "$MODEL")
  fi
  if [[ "$ENABLE_SEARCH" -eq 1 ]]; then
    cmd+=(--search)
  fi

  nohup "${cmd[@]}" >"$ROOT_DIR/logs/agent-fleet/master-launcher.log" 2>&1 &
  echo $! >"$PID_DIR/master.pid"
  printf 'master pid %s\n' "$(cat "$PID_DIR/master.pid")"
}

launch_worker() {
  local worker_name="$1"
  local worktree_path="$WORKTREE_ROOT/$worker_name"
  local branch_name="agent-fleet/$worker_name"
  local worker_log="$ROOT_DIR/logs/agent-fleet/${worker_name}-launcher.log"

  if [[ ! -d "$worktree_path/.git" && ! -f "$worktree_path/.git" ]]; then
    git worktree add -B "$branch_name" "$worktree_path" "$BASE_REF"
  fi

  git -C "$worktree_path" fetch origin >/dev/null 2>&1 || true
  git -C "$worktree_path" checkout -B "$branch_name" "$BASE_REF" >/dev/null
  git -C "$worktree_path" submodule update --init --recursive >/dev/null
  cmake -S "$worktree_path/src" -B "$worktree_path/build-release" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$worktree_path/build-release" --target perf-test -j4 >/dev/null

  local cmd=(
    "$worktree_path/scripts/agent-loop.sh"
    --branch "$branch_name"
    --base-ref "$BASE_REF"
    --worker-name "$worker_name"
    --state-subdir "$worker_name"
    --benchmark-log "$worktree_path/logs/agent-loop/$worker_name/benchmark-progress.csv"
    --task-dir "$TASK_ROOT"
  )

  if [[ -n "$MODEL" ]]; then
    cmd+=(--model "$MODEL")
  fi
  if [[ "$ENABLE_SEARCH" -eq 1 ]]; then
    cmd+=(--search)
  fi

  nohup "${cmd[@]}" >"$worker_log" 2>&1 &
  echo $! >"$PID_DIR/$worker_name.pid"
  printf '%s pid %s\n' "$worker_name" "$(cat "$PID_DIR/$worker_name.pid")"
}

launch_master
for worker_index in $(seq 1 "$WORKERS"); do
  launch_worker "worker-$worker_index"
done

cat <<EOF
Fleet started.

Task queue:
- $TASK_ROOT

Worker worktrees:
- $WORKTREE_ROOT

PID files:
- $PID_DIR

Stop a worker:
- kill \$(cat $PID_DIR/worker-1.pid)

Stop the master:
- kill \$(cat $PID_DIR/master.pid)
EOF
