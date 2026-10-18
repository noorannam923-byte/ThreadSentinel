"""
report_cmd.py — `sentinel report` subcommand implementation

Pretty-prints a JSON race report to stdout with ANSI colour, and optionally
generates a standalone HTML report with embedded Plotly charts.
"""

import sys
import json
from pathlib import Path

# ANSI colour codes
RED    = "\033[91m"
YELLOW = "\033[93m"
CYAN   = "\033[96m"
GREEN  = "\033[92m"
MAGENTA = "\033[95m"
BOLD   = "\033[1m"
RESET  = "\033[0m"

VERDICT_COLOUR = {
    "CONFIRMED": RED,
    "POTENTIAL": YELLOW,
    "NONE":      GREEN,
}


def execute(args) -> None:
    report_path = Path(args.report_file)
    if not report_path.exists():
        print(f"[sentinel report] ERROR: file not found: {report_path}", file=sys.stderr)
        sys.exit(1)

    with open(report_path, "r") as f:
        data = json.load(f)

    _print_report(data)

    if getattr(args, "html", ""):
        from . import report_html
        html_path = Path(args.html)
        html_path.parent.mkdir(parents=True, exist_ok=True)
        report_html.generate(data, str(html_path))
        print(f"\n{CYAN}[sentinel report] HTML report written → {html_path}{RESET}")


def _print_report(data: dict) -> None:
    meta    = data.get("metadata", {})
    summary = data.get("summary", {})
    races   = data.get("races", [])

    confirmed = summary.get("confirmed_races", 0)
    potential = summary.get("potential_races", 0)

    mode     = meta.get("mode", "unknown")
    detector = meta.get("detector", "unknown")
    runtime  = meta.get("runtime_ms", 0)

    print(f"\n{BOLD}═══ Thread Sentinel Race Report ═══{RESET}")
    print(f"  Mode:      {mode}  |  Detector: {detector}  |  Runtime: {runtime:.1f} ms")
    print(f"  {BOLD}Confirmed races:{RESET} {RED if confirmed else GREEN}{confirmed}{RESET}   "
          f"{BOLD}Potential races:{RESET} {YELLOW if potential else GREEN}{potential}{RESET}")

    deadlocks = data.get("deadlocks", [])
    if deadlocks:
        print(f"\n  {BOLD}Potential deadlocks:{RESET} {MAGENTA}{len(deadlocks)}{RESET}")
        for i, d in enumerate(deadlocks, 1):
            cycle = d.get("cycle", [])
            print(f"  {BOLD}Deadlock #{i}:{RESET} {MAGENTA}lock-order cycle{RESET} "
                  + " -> ".join(cycle + cycle[:1]))
            for e in d.get("edges", []):
                print(f"    T{e.get('thread','?')} takes {e.get('acquired','?')} while holding "
                      f"{e.get('held','?')} at {e.get('location','?')}")

    if not races:
        print(f"\n  {GREEN}✓ No races detected.{RESET}\n")
        return

    print()
    for i, race in enumerate(races, 1):
        verdict = race.get("verdict", "UNKNOWN")
        colour  = VERDICT_COLOUR.get(verdict, RESET)
        var     = race.get("variable", "?")
        addr    = race.get("address", "?")
        reason  = race.get("reason", "")
        sev     = race.get("severity", "")

        print(f"  {BOLD}Race #{i}:{RESET} {colour}[{verdict}]{RESET} "
              f"({sev}) on {CYAN}{var}{RESET} @ {addr}")
        print(f"    Reason: {reason}")

        fa = race.get("first_access", {})
        sa = race.get("second_access", {})
        print(f"    Access 1: {fa.get('type','?')} by thread {fa.get('thread','?')} "
              f"at {fa.get('location','?')}")
        print(f"    Access 2: {sa.get('type','?')} by thread {sa.get('thread','?')} "
              f"at {sa.get('location','?')}")
        print()

    prot = data.get("protected_items", [])
    if prot:
        print(f"  {BOLD}Protected items (prevent mode):{RESET}")
        for item in prot:
            print(f"    {item.get('variable','?')} @ {item.get('address','?')}")
        print()
