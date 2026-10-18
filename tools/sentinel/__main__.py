#!/usr/bin/env python3
"""
Thread Sentinel CLI — entry point for `python -m sentinel <subcommand>`

Subcommands:
  run     — run a binary under Sentinel and show the race report
  report  — pretty-print a JSON report file
  replay  — offline replay a JSONL trace and compare vs C detector results
  demo    — build and run a named demo program
  bench   — run the full benchmark suite
"""

import sys
import argparse
from pathlib import Path

# Add tools/ directory to path so we can import sibling modules
TOOLS_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS_DIR))

from sentinel import run_cmd, report_cmd, replay_cmd, demo_cmd, bench_cmd


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="python -m sentinel",
        description="Thread Sentinel — Hybrid Race Detection and Prevention CLI",
    )
    sub = parser.add_subparsers(dest="command", metavar="<command>")
    sub.required = True

    # --- run ---
    p_run = sub.add_parser("run", help="Run a binary under Sentinel detect/prevent mode")
    p_run.add_argument("binary", help="Path to instrumented binary")
    p_run.add_argument("args", nargs="*",
                       help="Arguments passed to binary (put them after --, e.g. run ./prog -- -n 4)")
    p_run.add_argument("--mode", choices=["detect", "prevent", "off"], default="detect",
                       help="Sentinel operating mode (default: detect)")
    p_run.add_argument("--detector", choices=["hybrid", "lockset", "hb"], default="hybrid",
                       help="Detector algorithm (default: hybrid)")
    p_run.add_argument("--report", default="ts_report.json",
                       help="Output JSON report path (default: ts_report.json)")
    p_run.add_argument("--protect-file", default="",
                       help="Protect file (prior report) for prevent mode")
    p_run.add_argument("--trace", default="",
                       help="JSONL trace output path")
    p_run.add_argument("--quiet", action="store_true",
                       help="Suppress stderr race messages from runtime")

    # --- report ---
    p_rep = sub.add_parser("report", help="Pretty-print a JSON race report")
    p_rep.add_argument("report_file", help="Path to JSON report file")
    p_rep.add_argument("--html", default="",
                       help="Also generate HTML report at this path")

    # --- replay ---
    p_replay = sub.add_parser("replay", help="Offline replay a JSONL trace and verify results")
    p_replay.add_argument("trace_file", help="Path to JSONL trace file")
    p_replay.add_argument("--report", default="",
                          help="Compare replay against this JSON report (prints MATCH/MISMATCH)")

    # --- demo ---
    p_demo = sub.add_parser("demo", help="Build and run a named demo program")
    p_demo.add_argument("name", help="Demo name, e.g. 01_counter_race")
    p_demo.add_argument("--mode", choices=["detect", "prevent", "off"], default="detect",
                        help="Sentinel operating mode (default: detect)")
    p_demo.add_argument("--detector", choices=["hybrid", "lockset", "hb"], default="hybrid",
                        help="Detector algorithm (default: hybrid)")
    p_demo.add_argument("--no-build", action="store_true",
                        help="Skip rebuilding the demo binary")
    p_demo.add_argument("--html", default="",
                        help="Generate HTML report at this path")

    # --- bench ---
    p_bench = sub.add_parser("bench", help="Run benchmark suite across all 12 demos")
    p_bench.add_argument("--output", default="results/bench",
                         help="Output directory for CSV/HTML results (default: results/bench)")
    p_bench.add_argument("--runs", type=int, default=5,
                         help="Number of runs per configuration (default: 5)")

    args = parser.parse_args()

    if args.command == "run":
        run_cmd.execute(args)
    elif args.command == "report":
        report_cmd.execute(args)
    elif args.command == "replay":
        replay_cmd.execute(args)
    elif args.command == "demo":
        demo_cmd.execute(args)
    elif args.command == "bench":
        bench_cmd.execute(args)


if __name__ == "__main__":
    main()
