"""
demo_cmd.py — `sentinel demo` subcommand implementation

Build (optionally) and run a named demo under Thread Sentinel,
then show the race report and optionally generate an HTML report.
"""

import os
import sys
import json
import subprocess
from pathlib import Path

from . import report_cmd

REPO_ROOT = Path(__file__).resolve().parent.parent.parent


def execute(args) -> None:
    demo_name = args.name
    mode      = args.mode
    detector  = args.detector

    demos_bin = REPO_ROOT / "demos" / "bin"
    results   = REPO_ROOT / "results"
    results.mkdir(parents=True, exist_ok=True)

    binary = demos_bin / demo_name
    if not binary.exists() or not args.no_build:
        print(f"[sentinel demo] Building demos …")
        ret = subprocess.run(["make", "demos"], cwd=REPO_ROOT)
        if ret.returncode != 0:
            print("[sentinel demo] ERROR: make demos failed", file=sys.stderr)
            sys.exit(1)

    if not binary.exists():
        print(f"[sentinel demo] ERROR: binary not found: {binary}", file=sys.stderr)
        print(f"[sentinel demo] Available demos:")
        for f in sorted(demos_bin.glob("*")):
            if "_" not in f.name or not f.name.endswith(("_plain", "_tsan")):
                print(f"  {f.name}")
        sys.exit(1)

    report_path = results / f"{demo_name}_{mode}.json"
    env = os.environ.copy()
    env["TS_MODE"]     = mode
    env["TS_DETECTOR"] = detector
    env["TS_REPORT"]   = str(report_path)

    print(f"\n[sentinel demo] Running {demo_name} (mode={mode}, detector={detector})")
    print(f"[sentinel demo] Binary  → {binary}")
    print(f"[sentinel demo] Report  → {report_path}")
    print()

    result = subprocess.run([str(binary)], env=env)
    print()

    if result.returncode != 0:
        print(f"[sentinel demo] Binary exited with code {result.returncode}", file=sys.stderr)

    if report_path.exists():
        import types
        fake = types.SimpleNamespace(report_file=str(report_path), html=getattr(args, "html", ""))
        report_cmd.execute(fake)
    else:
        print("[sentinel demo] WARNING: no report file generated.", file=sys.stderr)

    sys.exit(result.returncode)
