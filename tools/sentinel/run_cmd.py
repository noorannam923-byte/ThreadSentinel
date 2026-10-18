"""
run_cmd.py — `sentinel run` subcommand implementation

Runs an instrumented binary under Thread Sentinel and prints the race report.
"""

import os
import sys
import json
import subprocess
from pathlib import Path

from . import report_cmd


def execute(args) -> None:
    binary = Path(args.binary).resolve()
    if not binary.exists():
        print(f"[sentinel run] ERROR: binary not found: {binary}", file=sys.stderr)
        sys.exit(1)

    env = os.environ.copy()
    env["TS_MODE"] = args.mode
    env["TS_DETECTOR"] = args.detector
    env["TS_REPORT"] = args.report
    if args.protect_file:
        env["TS_PROTECT_FILE"] = args.protect_file
    if args.trace:
        env["TS_TRACE"] = args.trace
    if args.quiet:
        env["TS_QUIET"] = "1"

    print(f"[sentinel run] Running {binary.name} (mode={args.mode}, detector={args.detector})")
    print(f"[sentinel run] Report → {args.report}")

    cmd = [str(binary)] + list(args.args)
    result = subprocess.run(cmd, env=env)

    print()
    if result.returncode != 0:
        print(f"[sentinel run] Binary exited with code {result.returncode}", file=sys.stderr)

    # Pretty-print the report
    report_path = Path(args.report)
    if report_path.exists():
        _print_report(report_path)
    else:
        print(f"[sentinel run] WARNING: report file not found: {report_path}", file=sys.stderr)

    sys.exit(result.returncode)


def _print_report(report_path: Path) -> None:
    """Delegate to report_cmd for pretty-printing."""
    import types
    fake_args = types.SimpleNamespace(report_file=str(report_path), html="")
    report_cmd.execute(fake_args)
