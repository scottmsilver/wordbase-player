#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FLEET_DIR="$ROOT_DIR/logs/agent-fleet"
PID_DIR="$FLEET_DIR/pids"
TASK_DIR="$FLEET_DIR/tasks"
WORKTREE_ROOT="$ROOT_DIR/.codex-workers"

usage() {
  cat <<'EOF'
Usage: ./scripts/agent-fleet-status.sh

Print a compact status summary for the current agent fleet:
- master/worker PID state
- queue counts
- worker branches and dirty state
- latest task files and launcher log tails
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

process_status() {
  local pid_file="$1"
  local label="$2"

  if [[ ! -f "$pid_file" ]]; then
    printf '%-12s missing\n' "$label"
    return
  fi

  local pid
  pid="$(cat "$pid_file")"
  if ps -p "$pid" >/dev/null 2>&1; then
    local stat cmd
    stat="$(ps -p "$pid" -o stat= | xargs)"
    cmd="$(ps -p "$pid" -o cmd= | sed -E 's/[[:space:]]+/ /g' | cut -c1-120)"
    printf '%-12s running pid=%s stat=%s cmd=%s\n' "$label" "$pid" "$stat" "$cmd"
  else
    printf '%-12s stale pid=%s\n' "$label" "$pid"
  fi
}

count_files() {
  local dir="$1"
  if [[ -d "$dir" ]]; then
    find "$dir" -maxdepth 1 -type f | wc -l | tr -d ' '
  else
    echo 0
  fi
}

latest_file() {
  local dir="$1"
  if [[ -d "$dir" ]]; then
    find "$dir" -maxdepth 1 -type f | sort | tail -n 1
  fi
}

echo "Fleet"
process_status "$PID_DIR/master.pid" "master"
if [[ -d "$PID_DIR" ]]; then
  while IFS= read -r pid_file; do
    base="$(basename "$pid_file")"
    if [[ "$base" == "master.pid" ]]; then
      continue
    fi
    worker="${base%.pid}"
    process_status "$pid_file" "$worker"
  done < <(find "$PID_DIR" -maxdepth 1 -name 'worker-*.pid' | sort)
fi

echo
echo "Queue"
printf 'pending      %s\n' "$(count_files "$TASK_DIR/pending")"
printf 'in-progress  %s\n' "$(count_files "$TASK_DIR/in-progress")"
printf 'done         %s\n' "$(count_files "$TASK_DIR/done")"
printf 'failed       %s\n' "$(count_files "$TASK_DIR/failed")"

latest_pending="$(latest_file "$TASK_DIR/pending")"
latest_in_progress="$(latest_file "$TASK_DIR/in-progress")"
latest_done="$(latest_file "$TASK_DIR/done")"
if [[ -n "${latest_pending:-}" ]]; then
  printf 'latest pending      %s\n' "${latest_pending#$ROOT_DIR/}"
fi
if [[ -n "${latest_in_progress:-}" ]]; then
  printf 'latest in-progress  %s\n' "${latest_in_progress#$ROOT_DIR/}"
fi
if [[ -n "${latest_done:-}" ]]; then
  printf 'latest done         %s\n' "${latest_done#$ROOT_DIR/}"
fi

echo
echo "Workers"
if [[ -d "$WORKTREE_ROOT" ]]; then
  while IFS= read -r worktree; do
    worker_name="$(basename "$worktree")"
    branch="$(git -C "$worktree" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
    dirty="$(git -C "$worktree" status --short 2>/dev/null | wc -l | tr -d ' ')"
    latest_result="$(find "$worktree/logs/agent-loop/$worker_name" -maxdepth 1 -type f \( -name 'results-*.txt' -o -name 'iteration-*.txt' \) 2>/dev/null | sort | tail -n 1)"
    printf '%-12s branch=%-24s dirty=%s' "$worker_name" "$branch" "$dirty"
    if [[ -n "${latest_result:-}" ]]; then
      printf ' latest=%s' "${latest_result#$ROOT_DIR/}"
    fi
    printf '\n'
  done < <(find "$WORKTREE_ROOT" -mindepth 1 -maxdepth 1 -type d | sort)
else
  echo "no worker worktrees"
fi

echo
echo "Logs"
if [[ -f "$FLEET_DIR/master-launcher.log" ]]; then
  echo "master log tail:"
  tail -n 5 "$FLEET_DIR/master-launcher.log"
fi
for worker_log in "$FLEET_DIR"/worker-*-launcher.log; do
  if [[ -f "$worker_log" ]]; then
    echo
    echo "$(basename "$worker_log") tail:"
    tail -n 5 "$worker_log"
  fi
done
