#!/usr/bin/env python3

import argparse
import csv
import curses
import glob
import os
import re
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple


ROOT_DIR = Path(__file__).resolve().parent.parent
FLEET_DIR = ROOT_DIR / "logs" / "agent-fleet"
TASK_DIR = FLEET_DIR / "tasks"
WORKTREE_ROOT = ROOT_DIR / ".codex-workers"
PID_DIR = FLEET_DIR / "pids"


@dataclass
class ProcessInfo:
    pid: Optional[str]
    state: str
    cmd: str


@dataclass
class WorkerInfo:
    name: str
    branch: str
    head: str
    dirty: int
    current_task: str
    latest_kept: str
    latest_discarded: str
    latest_activity: str
    codex: ProcessInfo


def run_cmd(args: List[str], cwd: Optional[Path] = None) -> str:
    try:
      return subprocess.check_output(args, cwd=str(cwd) if cwd else None, text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
      return ""


def parse_pid_file(pid_file: Path) -> ProcessInfo:
    if not pid_file.exists():
        return ProcessInfo(None, "missing", "")
    pid = pid_file.read_text().strip()
    if not pid:
        return ProcessInfo(None, "missing", "")
    alive = run_cmd(["ps", "-p", pid, "-o", "stat=", "-o", "cmd="])
    if not alive:
        return ProcessInfo(pid, "stale", "")
    lines = [line.strip() for line in alive.splitlines() if line.strip()]
    if not lines:
        return ProcessInfo(pid, "stale", "")
    parts = lines[0].split(maxsplit=1)
    stat = parts[0]
    cmd = parts[1] if len(parts) > 1 else ""
    return ProcessInfo(pid, stat, cmd)


def find_codex_process(workspace: Path) -> ProcessInfo:
    out = run_cmd(["pgrep", "-af", "codex exec -C "])
    for line in out.splitlines():
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        pid = parts[0]
        for idx, token in enumerate(parts[:-1]):
            if token == "-C" and parts[idx + 1] == str(workspace):
                alive = run_cmd(["ps", "-p", pid, "-o", "stat=", "-o", "cmd="])
                if not alive:
                    return ProcessInfo(pid, "stale", "")
                ps_parts = alive.split(maxsplit=1)
                stat = ps_parts[0] if ps_parts else ""
                cmd = ps_parts[1].strip() if len(ps_parts) > 1 else ""
                return ProcessInfo(pid, stat, cmd)
    return ProcessInfo(None, "idle", "")


def queue_counts() -> Dict[str, int]:
    counts = {}
    for name in ("pending", "in-progress", "done", "failed"):
        path = TASK_DIR / name
        counts[name] = len(list(path.glob("*.md"))) if path.exists() else 0
    return counts


def latest_file_name(path: Path) -> str:
    files = sorted(path.glob("*.md")) if path.exists() else []
    return files[-1].name if files else "-"


def latest_task_for_worker(worker_name: str) -> str:
    matches = sorted((TASK_DIR / "in-progress").glob(f"*--{worker_name}.md"))
    if matches:
        return matches[-1].name
    matches = sorted((TASK_DIR / "done").glob(f"*--{worker_name}.md"))
    if matches:
        return f"done:{matches[-1].name}"
    matches = sorted((TASK_DIR / "failed").glob(f"*--{worker_name}.md"))
    if matches:
        return f"failed:{matches[-1].name}"
    return "-"


def read_benchmark_rows(csv_path: Path) -> Tuple[str, str]:
    latest_kept = "-"
    latest_discarded = "-"
    if not csv_path.exists():
        return latest_kept, latest_discarded
    try:
        with csv_path.open(newline="") as handle:
            reader = csv.DictReader(handle)
            rows = list(reader)
    except Exception:
        return latest_kept, latest_discarded

    for row in reversed(rows):
        if latest_kept == "-" and row.get("status") == "kept":
            latest_kept = f"{row.get('change_ref', '?')} {row.get('total_nodes', '?')}"
        if latest_discarded == "-" and row.get("status") == "discarded":
            latest_discarded = f"{row.get('change_ref', '?')} {row.get('total_nodes', '?')}"
        if latest_kept != "-" and latest_discarded != "-":
            break
    return latest_kept, latest_discarded


def latest_activity_line(worker_name: str) -> str:
    log_path = FLEET_DIR / f"{worker_name}-launcher.log"
    if not log_path.exists():
        return "-"
    try:
        lines = [line.strip() for line in log_path.read_text(errors="replace").splitlines() if line.strip()]
    except Exception:
        return "-"
    for line in reversed(lines):
        if '"agent_message"' in line:
            match = re.search(r'"text":"(.*)"', line)
            if match:
                return match.group(1).replace('\\"', '"')[:120]
        if '"command_execution"' in line and '"command":"' in line:
            match = re.search(r'"command":"(.*)"', line)
            if match:
                return f"cmd: {match.group(1).replace('\\"', '"')[:108]}"
    return lines[-1][:120] if lines else "-"


def worker_infos() -> List[WorkerInfo]:
    workers: List[WorkerInfo] = []
    for worktree in sorted(WORKTREE_ROOT.glob("worker-*")):
        if not worktree.is_dir():
            continue
        name = worktree.name
        branch = run_cmd(["git", "-C", str(worktree), "rev-parse", "--abbrev-ref", "HEAD"]) or "unknown"
        head = run_cmd(["git", "-C", str(worktree), "rev-parse", "--short", "HEAD"]) or "unknown"
        dirty_out = run_cmd(["git", "-C", str(worktree), "status", "--short"])
        dirty = len([line for line in dirty_out.splitlines() if line.strip()])
        kept, discarded = read_benchmark_rows(worktree / "benchmark-progress.csv")
        workers.append(
            WorkerInfo(
                name=name,
                branch=branch,
                head=head,
                dirty=dirty,
                current_task=latest_task_for_worker(name),
                latest_kept=kept,
                latest_discarded=discarded,
                latest_activity=latest_activity_line(name),
                codex=find_codex_process(worktree),
            )
        )
    return workers


def master_info() -> Tuple[ProcessInfo, ProcessInfo, str]:
    pid_info = parse_pid_file(PID_DIR / "master.pid")
    codex_info = find_codex_process(ROOT_DIR)
    last_message = FLEET_DIR / "master" / "last-message.txt"
    summary = "-"
    if last_message.exists():
        lines = [line.strip() for line in last_message.read_text(errors="replace").splitlines() if line.strip()]
        if lines:
            summary = lines[-1][:140]
    return pid_info, codex_info, summary


def snapshot_lines() -> List[str]:
    counts = queue_counts()
    master_pid, master_job, master_summary = master_info()
    workers = worker_infos()

    lines = [
        f"Fleet Monitor  {time.strftime('%Y-%m-%d %H:%M:%S')}",
        "",
        f"Queue  pending={counts['pending']}  in-progress={counts['in-progress']}  done={counts['done']}  failed={counts['failed']}",
        f"Latest pending: {latest_file_name(TASK_DIR / 'pending')}",
        f"Latest done:    {latest_file_name(TASK_DIR / 'done')}",
        "",
        f"Master wrapper: {master_pid.state} pid={master_pid.pid or '-'}",
        f"Master job:     {master_job.state} pid={master_job.pid or '-'}",
        f"Master note:    {master_summary}",
        "",
        "Workers",
        "name       job        branch                  head     dirty  current task                               latest kept              latest discard",
    ]
    for worker in workers:
        lines.append(
            f"{worker.name:<10} {worker.codex.state:<10} {worker.branch:<22} {worker.head:<8} {worker.dirty:<5} "
            f"{worker.current_task[:40]:<40} {worker.latest_kept[:24]:<24} {worker.latest_discarded[:24]:<24}"
        )
        lines.append(f"  activity: {worker.latest_activity}")
    lines.append("")
    lines.append("Controls: q quit, r refresh")
    return lines


def draw_screen(stdscr, interval: float) -> None:
    curses.curs_set(0)
    stdscr.nodelay(True)
    stdscr.timeout(int(interval * 1000))
    while True:
        stdscr.erase()
        max_y, max_x = stdscr.getmaxyx()
        for row, line in enumerate(snapshot_lines()):
            if row >= max_y:
                break
            stdscr.addnstr(row, 0, line, max_x - 1)
        stdscr.refresh()
        key = stdscr.getch()
        if key in (ord("q"), ord("Q")):
            break
        if key in (ord("r"), ord("R"), -1):
            continue


def main() -> None:
    parser = argparse.ArgumentParser(description="Curses-style monitor for the agent fleet.")
    parser.add_argument("--interval", type=float, default=2.0, help="Refresh interval in seconds")
    parser.add_argument("--once", action="store_true", help="Print one snapshot and exit")
    args = parser.parse_args()

    if args.once:
        print("\n".join(snapshot_lines()))
        return

    curses.wrapper(draw_screen, args.interval)


if __name__ == "__main__":
    main()
