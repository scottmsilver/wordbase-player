#!/usr/bin/env python3

import argparse
import csv
import curses
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
    current_task_state: str
    latest_kept: str
    latest_discarded: str
    latest_activity: str
    latest_activity_age_seconds: Optional[int]
    display_state: str
    codex: ProcessInfo
    recent_profile_nodes: List[int]
    baseline_profile_nodes: Optional[int]
    profile_trend: str


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


def file_age_seconds(path: Path) -> Optional[int]:
    if not path.exists():
        return None
    try:
        return max(0, int(time.time() - path.stat().st_mtime))
    except OSError:
        return None


def format_age(age_seconds: Optional[int]) -> str:
    if age_seconds is None:
        return "-"
    if age_seconds < 60:
        return f"{age_seconds}s"
    if age_seconds < 3600:
        return f"{age_seconds // 60}m"
    return f"{age_seconds // 3600}h"


def queue_counts() -> Dict[str, int]:
    counts = {}
    for name in ("pending", "in-progress", "done", "failed"):
        path = TASK_DIR / name
        counts[name] = len(list(path.glob("*.md"))) if path.exists() else 0
    return counts


def latest_file_name(path: Path) -> str:
    files = sorted(path.glob("*.md")) if path.exists() else []
    return files[-1].name if files else "-"


def latest_task_for_worker(worker_name: str) -> Tuple[str, str]:
    matches = sorted((TASK_DIR / "in-progress").glob(f"*--{worker_name}.md"))
    if matches:
        return matches[-1].name, "in-progress"
    matches = sorted((TASK_DIR / "done").glob(f"*--{worker_name}.md"))
    if matches:
        return f"done:{matches[-1].name}", "done"
    matches = sorted((TASK_DIR / "failed").glob(f"*--{worker_name}.md"))
    if matches:
        return f"failed:{matches[-1].name}", "failed"
    return "-", "none"


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


def read_recent_profile_nodes(csv_path: Path, count: int = 4) -> List[int]:
    if not csv_path.exists():
        return []
    try:
        with csv_path.open(newline="") as handle:
            reader = csv.DictReader(handle)
            rows = [row for row in reader if row.get("scenario") == "profile" and row.get("total_nodes")]
    except Exception:
        return []
    recent = []
    for row in reversed(rows):
        try:
            recent.append(int(row.get("total_nodes", "0")))
        except ValueError:
            continue
        if len(recent) >= count:
            break
    return list(reversed(recent))


def read_profile_baseline(csv_path: Path) -> Optional[int]:
    if not csv_path.exists():
        return None
    try:
        with csv_path.open(newline="") as handle:
            reader = csv.DictReader(handle)
            rows = [row for row in reader if row.get("scenario") == "profile" and row.get("status") == "kept" and row.get("total_nodes")]
    except Exception:
        return None
    for row in reversed(rows):
        try:
            return int(row.get("total_nodes", "0"))
        except ValueError:
            continue
    return None


def render_trend(values: List[int]) -> str:
    if not values:
        return "-"
    chars = " .:-=+*#"
    vmin = min(values)
    vmax = max(values)
    if vmax == vmin:
        return chars[-1] * len(values)
    out = []
    for value in values:
        idx = int(round((value - vmin) / (vmax - vmin) * (len(chars) - 1)))
        out.append(chars[idx])
    return "".join(out)


def latest_activity_line(worker_name: str) -> Tuple[str, Optional[int]]:
    log_path = FLEET_DIR / f"{worker_name}-launcher.log"
    if not log_path.exists():
        return "-", None
    try:
        lines = [line.strip() for line in log_path.read_text(errors="replace").splitlines() if line.strip()]
    except Exception:
        return "-", None
    age_seconds = file_age_seconds(log_path)
    for line in reversed(lines):
        if '"agent_message"' in line:
            match = re.search(r'"text":"(.*)"', line)
            if match:
                return match.group(1).replace('\\"', '"')[:120], age_seconds
        if '"command_execution"' in line and '"command":"' in line:
            match = re.search(r'"command":"(.*)"', line)
            if match:
                return f"cmd: {match.group(1).replace('\\"', '"')[:108]}", age_seconds
        if '"turn.started"' in line:
            return "turn started", age_seconds
    return (lines[-1][:120] if lines else "-"), age_seconds


def worker_display_state(task_state: str, codex: ProcessInfo, activity_age_seconds: Optional[int]) -> str:
    if codex.pid:
        return "active"
    if task_state == "failed":
        return "failed"
    if task_state == "done":
        return "done"
    if task_state == "in-progress":
        if activity_age_seconds is not None and activity_age_seconds <= 30:
            return "handoff"
        return "stalled"
    return "idle"


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
        current_task, current_task_state = latest_task_for_worker(name)
        latest_activity, latest_activity_age_seconds = latest_activity_line(name)
        codex = find_codex_process(worktree)
        bench_path = worktree / "benchmark-progress.csv"
        kept, discarded = read_benchmark_rows(bench_path)
        recent_profile = read_recent_profile_nodes(bench_path)
        baseline_profile = read_profile_baseline(bench_path)
        trend = render_trend(recent_profile)
        workers.append(
            WorkerInfo(
                name=name,
                branch=branch,
                head=head,
                dirty=dirty,
                current_task=current_task,
                current_task_state=current_task_state,
                latest_kept=kept,
                latest_discarded=discarded,
                latest_activity=latest_activity,
                latest_activity_age_seconds=latest_activity_age_seconds,
                display_state=worker_display_state(current_task_state, codex, latest_activity_age_seconds),
                codex=codex,
                recent_profile_nodes=recent_profile,
                baseline_profile_nodes=baseline_profile,
                profile_trend=trend,
            )
        )
    return workers


def master_info() -> Tuple[ProcessInfo, ProcessInfo, str, Optional[int]]:
    pid_info = parse_pid_file(PID_DIR / "master.pid")
    codex_info = find_codex_process(ROOT_DIR)
    last_message = FLEET_DIR / "master" / "last-message.txt"
    summary = "-"
    age_seconds = file_age_seconds(last_message)
    if last_message.exists():
        lines = [line.strip() for line in last_message.read_text(errors="replace").splitlines() if line.strip()]
        if lines:
            summary = lines[-1][:140]
    return pid_info, codex_info, summary, age_seconds


def master_display_state(master_job: ProcessInfo, pending_count: int, master_note_age_seconds: Optional[int]) -> str:
    if master_job.pid:
        return "active"
    if pending_count > 0 and master_note_age_seconds is not None and master_note_age_seconds <= 60:
        return "planning"
    if pending_count > 0:
        return "idle"
    return "quiet"


def snapshot_data() -> Tuple[Dict[str, int], Tuple[ProcessInfo, ProcessInfo, str, Optional[int]], List[WorkerInfo]]:
    counts = queue_counts()
    master = master_info()
    workers = worker_infos()
    return counts, master, workers


def snapshot_lines() -> List[str]:
    counts, master, workers = snapshot_data()
    master_pid, master_job, master_summary, master_note_age_seconds = master
    master_state = master_display_state(master_job, counts["pending"], master_note_age_seconds)

    lines = [
        f"Fleet Monitor  {time.strftime('%Y-%m-%d %H:%M:%S')}",
        "",
        f"Queue  pending={counts['pending']}  in-progress={counts['in-progress']}  done={counts['done']}  failed={counts['failed']}",
        f"Latest pending: {latest_file_name(TASK_DIR / 'pending')}",
        f"Latest done:    {latest_file_name(TASK_DIR / 'done')}",
        "",
        f"Master wrapper: {master_pid.state} pid={master_pid.pid or '-'}",
        f"Master job:     {master_state} pid={master_job.pid or '-'} note_age={format_age(master_note_age_seconds)}",
        f"Master note:    {master_summary}",
        "",
        "Workers",
        "name       state     age   branch                  head     dirty  current task                          latest kept              latest discard",
    ]
    for worker in workers:
        lines.append(
            f"{worker.name:<10} {worker.display_state:<9} {format_age(worker.latest_activity_age_seconds):<4} "
            f"{worker.branch:<22} {worker.head:<8} {worker.dirty:<5} "
            f"{worker.current_task[:36]:<36} {worker.latest_kept[:24]:<24} {worker.latest_discarded[:24]:<24}"
        )
        lines.append(f"  activity: {worker.latest_activity}")
    lines.append("")
    lines.append("Controls: q quit, r refresh")
    return lines


def color_pair_for_state(state: str) -> int:
    if state == "active":
        return 1
    if state in ("planning", "handoff"):
        return 2
    if state == "done":
        return 3
    if state in ("failed", "stalled"):
        return 4
    return 0


def initialize_colors() -> None:
    if not curses.has_colors():
        return
    curses.start_color()
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_GREEN, -1)
    curses.init_pair(2, curses.COLOR_YELLOW, -1)
    curses.init_pair(3, curses.COLOR_CYAN, -1)
    curses.init_pair(4, curses.COLOR_RED, -1)


def draw_screen(stdscr, interval: float) -> None:
    curses.curs_set(0)
    initialize_colors()
    stdscr.nodelay(True)
    stdscr.timeout(int(interval * 1000))
    stdscr.keypad(True)
    spinner_frames = "|/-\\"
    tick = 0
    scroll = 0
    while True:
        counts, master, workers = snapshot_data()
        master_pid, master_job, master_summary, master_note_age_seconds = master
        master_state = master_display_state(master_job, counts["pending"], master_note_age_seconds)
        stdscr.erase()
        max_y, max_x = stdscr.getmaxyx()
        spinner = spinner_frames[tick % len(spinner_frames)]

        lines: List[Tuple[str, int]] = [
            (f"Fleet Monitor  {time.strftime('%Y-%m-%d %H:%M:%S')}", 0),
            ("", 0),
            (f"Queue  pending={counts['pending']}  in-progress={counts['in-progress']}  done={counts['done']}  failed={counts['failed']}", 0),
            (f"Latest pending: {latest_file_name(TASK_DIR / 'pending')}", 0),
            (f"Latest done:    {latest_file_name(TASK_DIR / 'done')}", 0),
            ("", 0),
            (f"Master wrapper: {master_pid.state} pid={master_pid.pid or '-'}", 0),
            (f"Master job:     {master_state} {spinner if master_state in ('active', 'planning') else ' '} pid={master_job.pid or '-'} note_age={format_age(master_note_age_seconds)}", color_pair_for_state(master_state)),
            (f"Master note:    {master_summary}", 0),
            ("", 0),
            ("Workers", 0),
            ("name       state     age   branch                  head     dirty  current task                          latest kept              latest discard", 0),
        ]

        for worker in workers:
            state = worker.display_state
            state_text = f"{state} {spinner}" if state in ("active", "handoff") else state
            lines.append((
                f"{worker.name:<10} {state_text:<9} {format_age(worker.latest_activity_age_seconds):<4} "
                f"{worker.branch:<22} {worker.head:<8} {worker.dirty:<5} "
                f"{worker.current_task[:36]:<36} {worker.latest_kept[:24]:<24} {worker.latest_discarded[:24]:<24}",
                color_pair_for_state(state),
            ))
            trend = worker.profile_trend
            if worker.baseline_profile_nodes:
                trend = f"{trend} base={worker.baseline_profile_nodes}"
            if worker.recent_profile_nodes:
                trend = f"{trend} last={worker.recent_profile_nodes[-1]}"
            lines.append((f"  trend: {trend}", 0))
            lines.append((f"  activity: {worker.latest_activity}", 0))

        lines.append(("", 0))
        lines.append(("Legend: green=active, yellow=handoff/planning, cyan=done, red=stalled/failed", 0))
        lines.append(("Controls: q quit, r refresh, j/k or arrows scroll, PgUp/PgDn", 0))
        total_lines = len(lines)
        max_scroll = max(0, total_lines - max_y)
        if scroll > max_scroll:
            scroll = max_scroll
        if scroll < 0:
            scroll = 0

        for row, (line, color_pair) in enumerate(lines[scroll:scroll + max_y]):
            if row >= max_y:
                break
            attr = curses.color_pair(color_pair) if color_pair else curses.A_NORMAL
            stdscr.addnstr(row, 0, line, max_x - 1, attr)
        if max_scroll > 0 and max_y >= 1:
            status = f"scroll {scroll}/{max_scroll} lines {max_y}/{total_lines}"
            stdscr.addnstr(max_y - 1, max(0, max_x - len(status) - 1), status, len(status))
        stdscr.refresh()
        key = stdscr.getch()
        if key in (ord("q"), ord("Q")):
            break
        if key in (ord("r"), ord("R")):
            tick += 1
        elif key in (ord("j"), curses.KEY_DOWN):
            scroll += 1
        elif key in (ord("k"), curses.KEY_UP):
            scroll -= 1
        elif key in (curses.KEY_NPAGE,):
            scroll += max(1, max_y - 2)
        elif key in (curses.KEY_PPAGE,):
            scroll -= max(1, max_y - 2)
        elif key in (curses.KEY_HOME,):
            scroll = 0
        elif key in (curses.KEY_END,):
            scroll = max_scroll
        else:
            tick += 1
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
