#!/usr/bin/env python3
"""
============================================================================
DevGovernor — Terminal Analytics Dashboard (Module 3)
============================================================================
Student 3: Analytics Dashboard

Course: Operating Systems Lab (B.Tech IT)

Purpose:
  Consumes JSON output from Module 1 (monitor) and JSON-lines audit records
  from Module 2 (governor). Renders an informative, human-readable terminal
  dashboard showcasing:
    - Overall system process counts and workload categorization
    - Top resource consumers (CPU and Memory)
    - Tabular process metrics
    - Recent governor scheduling priority adjustments and before/after status
  Robustly handles missing, empty, or malformed data files.

OS Concepts Illustrated:
  - Multiprogramming metrics and process lifecycle states
  - CPU scheduling distribution across user/system workloads
  - Inter-Process Communication (IPC) via structured file contracts
============================================================================
"""

import os
import sys
import json
import time
import argparse
from datetime import datetime


def load_process_data(filepath):
    """
    Safely load processes JSON snapshot.
    Returns: (dict_data or None, error_string or None)
    """
    if not os.path.exists(filepath):
        return None, f"Snapshot file '{filepath}' not found. Start module1_monitor to generate data."

    try:
        if os.path.getsize(filepath) == 0:
            return None, f"Snapshot file '{filepath}' is currently empty (sampling in progress)."

        with open(filepath, "r", encoding="utf-8") as f:
            data = json.load(f)
            return data, None
    except json.JSONDecodeError as e:
        return None, f"Malformed JSON in '{filepath}': {e}"
    except Exception as e:
        return None, f"Error reading '{filepath}': {e}"


def load_action_history(filepath, max_records=10):
    """
    Safely load recent governor actions from JSON-lines history file.
    Returns: (list_of_actions, warning_or_error)
    """
    if not os.path.exists(filepath):
        return [], None

    actions = []
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            for line_no, line in enumerate(f, start=1):
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)
                    actions.append(record)
                except json.JSONDecodeError:
                    # Gracefully skip corrupt lines
                    continue
        return actions[-max_records:], None
    except Exception as e:
        return [], f"Could not read action history '{filepath}': {e}"


def format_category_badge(category):
    """Format categories for neat display."""
    badges = {
        "development_tool": "[DEV-TOOL]",
        "development_server": "[DEV-SRV ]",
        "build_process": "[BUILD   ]",
        "background_process": "[BG-PROC ]",
        "system_or_unknown": "[SYS/UNK ]"
    }
    return badges.get(category, f"[{category[:8]}]")


def render_dashboard(proc_data, actions, sort_by="cpu", limit=15, filter_cat=None):
    """Render terminal dashboard view."""
    # Terminal clear
    print("\033[2J\033[H", end="")

    print("=" * 84)
    print("      DevGovernor — Linux Developer Productivity & Scheduling Governor")
    print("=" * 84)

    timestamp = proc_data.get("timestamp", "N/A")
    interval = proc_data.get("sampling_interval_seconds", 0)
    processes = proc_data.get("processes", [])
    total_procs = len(processes)

    # 1. System Summary Cards
    category_counts = {}
    total_mem_mb = 0.0
    for p in processes:
        cat = p.get("workload_category", "system_or_unknown")
        category_counts[cat] = category_counts.get(cat, 0) + 1
        mem = p.get("memory_mb")
        if mem is not None:
            total_mem_mb += mem

    print(f"Snapshot Time: {timestamp} (UTC) | Sampling Interval: {interval}s")
    print(f"Monitored Processes: {total_procs} | Total Tracked RSS Memory: {total_mem_mb:.1f} MB")
    print("-" * 84)

    # Workload breakdown
    cat_summary = " | ".join(
        f"{format_category_badge(k)}: {v}"
        for k, v in sorted(category_counts.items())
    )
    print(f"Workload Breakdown: {cat_summary}")
    print("-" * 84)

    # 2. Filter & Sort
    display_procs = processes
    if filter_cat:
        display_procs = [p for p in display_procs if p.get("workload_category") == filter_cat]

    if sort_by == "cpu":
        display_procs.sort(key=lambda p: (p.get("cpu_percent") or 0.0), reverse=True)
    elif sort_by == "memory":
        display_procs.sort(key=lambda p: (p.get("memory_mb") or 0.0), reverse=True)
    elif sort_by == "nice":
        display_procs.sort(key=lambda p: (p.get("nice_value") or 0))
    elif sort_by == "pid":
        display_procs.sort(key=lambda p: p.get("pid", 0))

    # 3. Tabular Process List
    print(f"Active Process Table (Sorted by: {sort_by.upper()}, Top {limit}):")
    header = f"{'PID':>7} {'PPID':>7} {'NAME':<16} {'CATEGORY':<12} {'ST':<3} {'NICE':>5} {'CPU%':>7} {'MEM(MB)':>9}"
    print(header)
    print("-" * 84)

    for p in display_procs[:limit]:
        pid = p.get("pid", 0)
        ppid = p.get("ppid", 0)
        name = str(p.get("name", "unknown"))[:15]
        cat = format_category_badge(p.get("workload_category", ""))
        st = str(p.get("state", "?"))[:2]
        nice = p.get("nice_value", 0)

        cpu = p.get("cpu_percent")
        cpu_str = f"{cpu:.1f}%" if cpu is not None else "  null"

        mem = p.get("memory_mb")
        mem_str = f"{mem:.1f}" if mem is not None else " null"

        row = f"{pid:>7} {ppid:>7} {name:<16} {cat:<12} {st:<3} {nice:>5} {cpu_str:>7} {mem_str:>9}"
        print(row)

    print("-" * 84)

    # 4. Governor Action History & Before/After Comparison
    print("Recent Governor Actions & History Audit:")
    if not actions:
        print("  [No governor actions logged yet. Run module2_governor to evaluate or adjust priorities.]")
    else:
        # Build lookup for live processes to show before/after
        live_nice_map = {p.get("pid"): p.get("nice_value") for p in processes}

        for act in reversed(actions[-6:]):
            ts = act.get("timestamp", "N/A")
            pid = act.get("pid", 0)
            pname = act.get("process_name", "unknown")
            act_type = act.get("action_type", "").upper()
            status = act.get("status", "").upper()
            prev_nice = act.get("previous_nice")
            res_nice = act.get("resulting_nice")
            msg = act.get("message", "")

            status_symbol = "[OK]" if status in ("SUCCESS", "RECOMMENDATION") else "[!]"

            # Check if live process reflects new nice value
            comparison = ""
            if pid in live_nice_map:
                live_nice = live_nice_map[pid]
                if prev_nice is not None:
                    comparison = f" (Live Nice: {live_nice})"

            print(f"  {status_symbol} {ts} | PID {pid} ({pname}) | {act_type}:{status} | "
                  f"Nice: {prev_nice} -> {res_nice}{comparison} | {msg}")

    print("=" * 84)
    print("Commands: [Ctrl+C] Quit | To adjust priorities: ./module2_governor/governor set-nice <PID> <NICE>")


def main():
    parser = argparse.ArgumentParser(description="DevGovernor Analytics Dashboard (Module 3)")
    parser.add_argument("--input", default="data/processes.json",
                        help="Path to monitor output JSON (default: data/processes.json)")
    parser.add_argument("--history", default="data/actions.jsonl",
                        help="Path to governor actions JSON-lines file (default: data/actions.jsonl)")
    parser.add_argument("--sort", choices=["cpu", "memory", "nice", "pid"], default="cpu",
                        help="Sort column (default: cpu)")
    parser.add_argument("--limit", type=int, default=15,
                        help="Max process entries to show (default: 15)")
    parser.add_argument("--filter", choices=[
        "development_tool", "development_server", "build_process",
        "background_process", "system_or_unknown"
    ], default=None, help="Filter by workload category")
    parser.add_argument("--watch", action="store_true",
                        help="Run continuously in watch/refresh mode")
    parser.add_argument("--refresh-sec", type=float, default=2.0,
                        help="Refresh interval in seconds when --watch is active (default: 2.0)")

    args = parser.parse_args()

    while True:
        proc_data, err = load_process_data(args.input)
        actions, hist_err = load_action_history(args.history)

        if err:
            print("\033[2J\033[H", end="")
            print("=" * 80)
            print("      DevGovernor — Linux Developer Productivity & Scheduling Governor")
            print("=" * 80)
            print(f"\n[Awaiting Data] {err}\n")
            print(f"Expected input path: {os.path.abspath(args.input)}")
            print("To start the monitor in WSL/Linux, run:")
            print("  ./module1_monitor/monitor --output data/processes.json --interval 1\n")
            if actions:
                print("Recent Governor Actions Logged:")
                for act in actions[-5:]:
                    print(f"  - {act.get('timestamp')} PID {act.get('pid')}: {act.get('action_type')} -> {act.get('status')}")
            print("=" * 80)
        else:
            render_dashboard(proc_data, actions, sort_by=args.sort, limit=args.limit, filter_cat=args.filter)

        if not args.watch:
            break

        try:
            time.sleep(args.refresh_sec)
        except KeyboardInterrupt:
            print("\nDashboard closed.")
            sys.exit(0)


if __name__ == "__main__":
    main()
