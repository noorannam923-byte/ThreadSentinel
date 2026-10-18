"""
replay_cmd.py — `sentinel replay` subcommand implementation

Reads a JSONL trace, replays it with the Python replay engine, and compares
the result against a reference C detector JSON report from the same run.
"""

import sys
import json
from pathlib import Path

from . import replay as replay_engine


def _fmt_key(key) -> str:
    addr, locs, verdict = key
    return f"{verdict:<9} {addr}  " + " <-> ".join(sorted(locs))


def execute(args) -> None:
    trace_path = Path(args.trace_file)
    if not trace_path.exists():
        print(f"[sentinel replay] ERROR: trace file not found: {trace_path}", file=sys.stderr)
        sys.exit(1)

    ref = None
    detector = "hybrid"
    if args.report:
        report_path = Path(args.report)
        if not report_path.exists():
            print(f"[sentinel replay] ERROR: reference report not found: {report_path}",
                  file=sys.stderr)
            sys.exit(1)
        with open(report_path, "r") as f:
            ref = json.load(f)
        meta = ref.get("metadata", {})
        detector = meta.get("detector", "hybrid")
        if meta.get("mode", "detect") != "detect":
            print(f"[sentinel replay] ERROR: replay verifies detect-mode runs; "
                  f"this report is from '{meta.get('mode')}' mode", file=sys.stderr)
            sys.exit(1)

    print(f"[sentinel replay] Replaying trace: {trace_path} (detector={detector})")
    try:
        engine = replay_engine.replay_trace(str(trace_path), detector=detector)
    except ValueError as e:
        print(f"[sentinel replay] ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    summary = engine.summary_dict()
    print(f"[sentinel replay] Events:  {summary['events']} ({summary['accesses']} accesses)")
    print(f"[sentinel replay] Result:  confirmed={summary['confirmed_races']}  "
          f"potential={summary['potential_races']}")

    if ref is None:
        print(json.dumps(summary, indent=2))
        return

    ref_keys = replay_engine.report_race_keys(ref)
    replay_keys = engine.race_keys()
    match = ref_keys == replay_keys

    print(f"[sentinel replay] Reference: confirmed={ref['summary']['confirmed_races']}  "
          f"potential={ref['summary']['potential_races']}")
    print(f"[sentinel replay] {'MATCH' if match else 'MISMATCH'}")

    if not match:
        for key in sorted(ref_keys - replay_keys, key=_fmt_key):
            print(f"  only in C report: {_fmt_key(key)}")
        for key in sorted(replay_keys - ref_keys, key=_fmt_key):
            print(f"  only in replay:   {_fmt_key(key)}")
        sys.exit(1)
