"""Live status of a batch of replay runs (tools/mem_sweep.sh and other runners that keep one folder per run).

usage: python3 -I tools/runs_status.py OUT [--watch [SECONDS]]

A run folder is OUT/<name>/ with log.txt; OUT/plan.txt lists every planned run ("name recording" per line) and
OUT/exits.txt the finished ones ("name exit CODE [seconds S]"). Without plan.txt the folders are the plan.

  pending   planned, no folder yet
  running   its process has the recording open: progress = how far it has read the file (/proc/PID/fdinfo)
  finished  exit code read as: 0 every frame matched, 3 frames differed (drift), 124 timed out, others failed;
            plus how many frames differed, warnings/errors in the log, and pools at 90 % or more (memory.txt)
"""
import argparse
import os
import re
import sys
import time
from pathlib import Path

EXIT_TEXT = {0: "matched", 3: "drifted", 124: "timed out", 130: "interrupted", 137: "killed"}


def processes_reading():
    """recording path -> (pid, read position) for every process with a .rep file open."""
    found = {}
    for pid in filter(str.isdigit, os.listdir("/proc")):
        fd_dir = f"/proc/{pid}/fd"
        try:
            fds = os.listdir(fd_dir)
        except OSError:
            continue
        for fd in fds:
            try:
                target = os.readlink(f"{fd_dir}/{fd}")
            except OSError:
                continue
            if not target.endswith(".rep"):
                continue
            try:
                info = Path(f"/proc/{pid}/fdinfo/{fd}").read_text()
                pos = int(re.search(r"pos:\s+(\d+)", info).group(1))
            except (OSError, AttributeError):
                continue
            found[target] = (int(pid), pos)
    return found


def read_plan(out):
    plan = []
    path = out / "plan.txt"
    if path.exists():
        for line in path.read_text().splitlines():
            parts = line.split(maxsplit=1)
            if parts:
                plan.append((parts[0], parts[1] if len(parts) > 1 else ""))
    known = {name for name, _ in plan}
    for d in sorted(out.iterdir()):
        if d.is_dir() and (d / "log.txt").exists() and d.name not in known:
            plan.append((d.name, ""))
    return plan


def read_exits(out):
    exits = {}
    path = out / "exits.txt"
    if path.exists():
        for line in path.read_text().splitlines():
            m = re.match(r"(\S+) exit (\d+)(?: seconds (\d+))?", line)
            if m:
                exits[m.group(1)] = (int(m.group(2)), int(m.group(3)) if m.group(3) else None)
    return exits


def log_facts(log):
    facts = {"seconds": 0.0, "warnings": 0, "errors": 0, "recording": "", "ends": None, "differed": None,
             "first_diff": None, "crash": ""}
    try:
        text = log.read_text(errors="replace")
    except OSError:
        return facts
    for line in text.splitlines():
        m = re.match(r"\[\s*([\d.]+)\]\s+(\w+)", line)
        if not m:
            continue
        facts["seconds"] = float(m.group(1))
        level = m.group(2)
        if level == "warn":
            facts["warnings"] += 1
        elif level in ("error", "fatal"):
            facts["errors"] += 1
            if not facts["crash"] and re.search(r"crash|exception|fatal|stopped", line, re.I):
                facts["crash"] = line.split(None, 3)[-1][:70]
        r = re.search(r"replaying (\S+\.rep)", line)
        if r:
            facts["recording"] = r.group(1)
        r = re.search(r"recording ends at frame (\d+)", line)
        if r:
            facts["ends"] = int(r.group(1))
        r = re.search(r"(\d+) of (\d+) frames differed, the first at frame (\d+)", line)
        if r:
            facts["differed"] = int(r.group(1))
            facts["first_diff"] = int(r.group(3))
    return facts


def full_pools(memory):
    """Pools at 90 % or more in a --mem-stats report (name, %)."""
    out = []
    if not memory.exists():
        return out
    in_table = False
    for line in memory.read_text(errors="replace").splitlines():
        if line.startswith("pool "):
            in_table = True
            continue
        if in_table:
            if not line.strip():
                break
            fields = line[30:].split()
            try:
                value = float(fields[-1])
            except (ValueError, IndexError):
                continue
            if value >= 90 and "high-water" not in line and " top" not in line[:30]:
                out.append((line[:30].strip(), value))
    return out


def duration(seconds):
    if seconds is None:
        return "-"
    seconds = int(seconds)
    return f"{seconds // 60}:{seconds % 60:02d}"


def render(out):
    plan = read_plan(out)
    exits = read_exits(out)
    reading = processes_reading()
    rows, counts = [], {"pending": 0, "running": 0, "finished": 0}
    for name, recording in plan:
        folder = out / name
        log = folder / "log.txt"
        if name in exits:
            state = "finished"
        elif log.exists():
            state = "running"
        else:
            state = "pending"
        counts[state] += 1
        facts = log_facts(log) if log.exists() else None
        recording = recording or (facts["recording"] if facts else "")
        if state == "pending":
            rows.append((name, "pending", "", "", ""))
            continue
        if state == "running":
            hit = reading.get(recording)
            if hit:
                size = os.path.getsize(recording)
                part = hit[1] / size if size else 0
                elapsed = facts["seconds"]
                eta = elapsed / part * (1 - part) if part > 0.02 else None
                bar = "#" * int(part * 20) + "." * (20 - int(part * 20))
                detail = f"[{bar}] {part * 100:5.1f} %  eta {duration(eta)}"
            else:
                detail = "starting / no recording open"
            rows.append((name, "running", duration(facts["seconds"]), detail,
                         f"{facts['warnings']} warn, {facts['errors']} err"))
            continue
        code, seconds = exits[name]
        result = EXIT_TEXT.get(code, f"FAILED ({code})")
        if facts["ends"]:
            result += f" - {facts['ends']} frames"
            if facts["differed"] is not None:
                result += f", {facts['differed']} differed from {facts['first_diff']}"
        if facts["crash"]:
            result += f" - {facts['crash']}"
        pools = full_pools(folder / "memory.txt")
        extra = f"{facts['warnings']} warn, {facts['errors']} err"
        if pools:
            extra += "; full: " + ", ".join(f"{n} {v:.0f}%" for n, v in pools[:4])
        rows.append((name, "finished", duration(seconds if seconds is not None else facts["seconds"]), result,
                     extra))
    lines = [f"{out}  -  {counts['running']} running, {counts['pending']} pending, {counts['finished']} finished "
             f"of {len(plan)}   ({time.strftime('%H:%M:%S')})", ""]
    lines.append(f"{'run':18s} {'state':9s} {'time':>6s}  {'progress / result':52s}  notes")
    order = {"running": 0, "pending": 1, "finished": 2}
    for row in sorted(rows, key=lambda r: order[r[1]]):
        lines.append(f"{row[0][:18]:18s} {row[1]:9s} {row[2]:>6s}  {row[3][:52]:52s}  {row[4]}")
    if (out / "summary.txt").exists() and counts["pending"] == counts["running"] == 0:
        lines += ["", f"summary: {out / 'summary.txt'}"]
    return "\n".join(lines), counts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    parser.add_argument("--watch", nargs="?", const=2.0, type=float, help="redraw every N seconds (default 2)")
    args = parser.parse_args()
    out = Path(args.out).resolve()
    if not out.is_dir():
        sys.exit(f"{out}: no such folder")
    while True:
        text, counts = render(out)
        if args.watch:
            sys.stdout.write("\033[H\033[2J" + text + "\n")
            sys.stdout.flush()
            if counts["running"] == counts["pending"] == 0:
                break
            time.sleep(args.watch)
        else:
            print(text)
            break


if __name__ == "__main__":
    main()
