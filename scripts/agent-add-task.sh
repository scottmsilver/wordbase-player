#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TASK_ROOT="$ROOT_DIR/logs/agent-fleet/tasks"
PENDING_DIR="$TASK_ROOT/pending"

TITLE=""
SLUG=""
OPEN_EDITOR=0
PRINT_PATH=0

usage() {
  cat <<'EOF'
Usage: ./scripts/agent-add-task.sh --title <title> [options]

Create a manual worker task in the pending queue used by the agent fleet.

Options:
  --title <title>         Human-readable task title (required)
  --slug <slug>           Optional filename slug; defaults to a slugified title
  --open                  Open the created task in $EDITOR after writing it
  --print-path            Print just the created task path
  --help                  Show this help

Examples:
  ./scripts/agent-add-task.sh --title "Try safer TT move ordering"
  ./scripts/agent-add-task.sh --title "Test clipped average word length" --open
EOF
}

slugify() {
  printf '%s' "$1" \
    | tr '[:upper:]' '[:lower:]' \
    | sed -E 's/[^a-z0-9]+/-/g; s/^-+//; s/-+$//'
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --title)
      TITLE="$2"
      shift 2
      ;;
    --slug)
      SLUG="$2"
      shift 2
      ;;
    --open)
      OPEN_EDITOR=1
      shift
      ;;
    --print-path)
      PRINT_PATH=1
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

if [[ -z "$TITLE" ]]; then
  echo "--title is required" >&2
  usage >&2
  exit 1
fi

mkdir -p "$PENDING_DIR"

if [[ -z "$SLUG" ]]; then
  SLUG="$(slugify "$TITLE")"
fi

DATE_PREFIX="$(date +%F)"
TASK_PATH="$PENDING_DIR/$DATE_PREFIX-$SLUG.md"

if [[ -e "$TASK_PATH" ]]; then
  suffix=2
  while [[ -e "$PENDING_DIR/$DATE_PREFIX-$SLUG-$suffix.md" ]]; do
    suffix=$((suffix + 1))
  done
  TASK_PATH="$PENDING_DIR/$DATE_PREFIX-$SLUG-$suffix.md"
fi

cat >"$TASK_PATH" <<EOF
# Title
$TITLE

# Hypothesis
Explain the expected win in search terms. Be concrete about the board pattern or word family this should help.

# Concrete change to try
Describe the narrow code change to make. Prefer one idea only, and point to the file/function to touch.

# Exact benchmark commands
Run these in order, and only keep a change if both stages pass:

\`\`\`bash
./scripts/run-benchmarks.sh profile
./scripts/run-benchmarks.sh profile-suite
\`\`\`

# Keep/revert criteria
Keep only if \`profile\` shows a real win without dropping depth and \`profile-suite\` shows no obvious regression overall or on an individual suite board. Revert if the result is flat, noisy, or overfits the single-board benchmark.
EOF

if [[ "$OPEN_EDITOR" -eq 1 ]]; then
  "${EDITOR:-vi}" "$TASK_PATH"
fi

if [[ "$PRINT_PATH" -eq 1 ]]; then
  printf '%s\n' "$TASK_PATH"
else
  echo "Created task: $TASK_PATH"
fi
