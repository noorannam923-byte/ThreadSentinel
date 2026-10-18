"""
bench_cmd.py — `sentinel bench` subcommand implementation

Runs the Phase 8 evaluation and writes raw results plus docs/EVALUATION.md:

  accuracy     Sentinel (hybrid, lockset, hb) and ThreadSanitizer verdicts on all
               12 demos against the ground truth in demos/expected/
  overhead     wall time of plain vs Sentinel detect vs Sentinel prevent vs TSan
  scalability  bench/scale_counter at 2, 4, 8, 16 threads (weak scaling)
  prevention   N-trial bug-manifestation rate of the racy demos, unprotected vs
               prevented (protect file and live)

TSan binaries run under `setarch -R` (ASLR off): gcc's TSan runtime aborts with
"unexpected memory mapping" on kernels with 32-bit mmap randomisation.

Generated sections of EVALUATION.md sit between <!-- BENCH:name --> markers;
everything outside them (the discussion) is preserved across re-runs.
"""

import os
import re
import sys
import csv
import json
import time
import platform
import statistics
import subprocess
from datetime import date
from pathlib import Path
from typing import Dict, List, Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEMOS_BIN = REPO_ROOT / "demos" / "bin"
BENCH_BIN = REPO_ROOT / "bench" / "bin"
EXPECTED = REPO_ROOT / "demos" / "expected"
DOCS = REPO_ROOT / "docs"
EVAL_MD = DOCS / "EVALUATION.md"

DETECTORS = ["hybrid", "lockset", "hb"]
SCALE_THREADS = [2, 4, 8, 16]
SCALE_ITERATIONS = 200000

# Racy demos and the line each prints when the bug did NOT manifest
RACY_DEMOS = [
    ("01_counter_race", "COUNTER MATCHES EXPECTED"),
    ("03_bank_transfer", "Total matches expected"),
    ("04_check_then_act", "Initialized exactly once"),
    ("05_producer_consumer_bug", "Consumed all items"),
    ("10_mixed_locks", "x matches expected"),
    ("12_linked_list_race", "All nodes preserved"),
]

TSAN_RACE = "WARNING: ThreadSanitizer: data race"


# ---------------------------------------------------------------------------
# Running binaries
# ---------------------------------------------------------------------------

class Runner:
    def __init__(self, scratch: Path):
        self.scratch = scratch
        self.tsan_prefix = ["setarch", platform.machine(), "-R"]

    def run(self, binary: Path, argv: Optional[List[str]] = None, env: Optional[Dict[str, str]] = None,
            tsan: bool = False, timeout: int = 120) -> Tuple[float, subprocess.CompletedProcess]:
        cmd = (self.tsan_prefix if tsan else []) + [str(binary)] + (argv or [])
        full_env = os.environ.copy()
        for k in ("TS_MODE", "TS_DETECTOR", "TS_TRACE", "TS_PROTECT_FILE"):
            full_env.pop(k, None)
        full_env["TS_QUIET"] = "1"
        full_env["TS_REPORT"] = str(self.scratch / "report.json")
        full_env.update(env or {})
        t0 = time.perf_counter()
        try:
            proc = subprocess.run(cmd, cwd=REPO_ROOT, env=full_env, capture_output=True,
                                  text=True, timeout=timeout)
        except subprocess.TimeoutExpired as e:
            # A hung run counts as a failed run (no success line), not a crash of the bench
            log(f"TIMEOUT after {timeout}s: {' '.join(cmd)}")
            proc = subprocess.CompletedProcess(cmd, -9, e.stdout or "", e.stderr or "")
            if isinstance(proc.stdout, bytes):
                proc.stdout, proc.stderr = proc.stdout.decode(errors="replace"), proc.stderr.decode(errors="replace")
        return time.perf_counter() - t0, proc

    def sentinel(self, binary: Path, mode: str, detector: str = "hybrid", report: Optional[Path] = None,
                 protect_file: Optional[Path] = None, argv: Optional[List[str]] = None):
        env = {"TS_MODE": mode, "TS_DETECTOR": detector}
        if report:
            env["TS_REPORT"] = str(report)
        if protect_file:
            env["TS_PROTECT_FILE"] = str(protect_file)
        return self.run(binary, argv, env)

    def last_report(self) -> dict:
        with open(self.scratch / "report.json") as f:
            return json.load(f)


def verdict_of(report: dict) -> str:
    s = report["summary"]
    if s["confirmed_races"] > 0:
        return "CONFIRMED"
    if s["potential_races"] > 0:
        return "POTENTIAL"
    return "NONE"


def most_common(values: List[str]) -> str:
    return max(set(values), key=values.count)


def log(msg: str) -> None:
    print(f"[sentinel bench] {msg}", flush=True)


# ---------------------------------------------------------------------------
# The four studies
# ---------------------------------------------------------------------------

def bench_accuracy(r: Runner, demos: List[dict], runs: int) -> List[dict]:
    rows = []
    for gt in demos:
        name = gt["program"]
        row = {"demo": name, "racy": gt["expected"]["hybrid"] != "NONE", "expected": gt["expected"]}
        for det in DETECTORS:
            seen = []
            for _ in range(runs):
                r.sentinel(DEMOS_BIN / name, "detect", det)
                seen.append(verdict_of(r.last_report()))
            row[det] = most_common(seen)
            row[f"{det}_stable"] = len(set(seen)) == 1
        seen = []
        for _ in range(runs):
            _, proc = r.run(DEMOS_BIN / f"{name}_tsan", tsan=True)
            seen.append("RACE" if TSAN_RACE in proc.stderr else "NONE")
        row["tsan"] = most_common(seen)
        row["tsan_stable"] = len(set(seen)) == 1
        log(f"accuracy  {name:<26} hybrid={row['hybrid']:<9} lockset={row['lockset']:<9} "
            f"hb={row['hb']:<9} tsan={row['tsan']}")
        rows.append(row)
    return rows


def accuracy_metrics(rows: List[dict]) -> Dict[str, dict]:
    metrics = {}
    for tool in DETECTORS + ["tsan"]:
        tp = fp = fn = tn = 0
        for row in rows:
            flagged = row[tool] != "NONE"
            if row["racy"]:
                tp += flagged
                fn += not flagged
            else:
                fp += flagged
                tn += not flagged
        metrics[tool] = {
            "tp": tp, "fp": fp, "fn": fn, "tn": tn,
            "precision": tp / (tp + fp) if tp + fp else 1.0,
            "recall": tp / (tp + fn) if tp + fn else 1.0,
            "matches_expected": sum(1 for row in rows
                                    if tool != "tsan" and row[tool] == row["expected"][tool]),
        }
    return metrics


def bench_overhead(r: Runner, demos: List[dict], runs: int) -> List[dict]:
    rows = []
    for gt in demos:
        name = gt["program"]
        protect = r.scratch / f"{name}_protect.json"
        r.sentinel(DEMOS_BIN / name, "detect", report=protect)
        configs = {
            "plain": lambda: r.run(DEMOS_BIN / f"{name}_plain"),
            "detect": lambda: r.sentinel(DEMOS_BIN / name, "detect"),
            "prevent": lambda: r.sentinel(DEMOS_BIN / name, "prevent", protect_file=protect),
            "tsan": lambda: r.run(DEMOS_BIN / f"{name}_tsan", tsan=True),
        }
        row = {"demo": name}
        for cfg, fn in configs.items():
            times = [fn()[0] * 1000 for _ in range(runs)]
            row[f"{cfg}_ms"] = statistics.median(times)
            row[f"{cfg}_sd"] = statistics.stdev(times) if len(times) > 1 else 0.0
        for cfg in ("detect", "prevent", "tsan"):
            row[f"{cfg}_x"] = row[f"{cfg}_ms"] / row["plain_ms"]
        log(f"overhead  {name:<26} plain={row['plain_ms']:.1f}ms detect={row['detect_x']:.1f}x "
            f"prevent={row['prevent_x']:.1f}x tsan={row['tsan_x']:.1f}x")
        rows.append(row)
    return rows


def bench_scalability(r: Runner, runs: int) -> List[dict]:
    rows = []
    for n in SCALE_THREADS:
        argv = [str(n), str(SCALE_ITERATIONS)]
        configs = {
            "plain": lambda: r.run(BENCH_BIN / "scale_counter_plain", argv),
            "detect": lambda: r.sentinel(BENCH_BIN / "scale_counter", "detect", argv=argv),
            "tsan": lambda: r.run(BENCH_BIN / "scale_counter_tsan", argv, tsan=True),
        }
        row = {"threads": n}
        for cfg, fn in configs.items():
            times = []
            for _ in range(runs):
                elapsed, proc = fn()
                if proc.returncode != 0:
                    raise RuntimeError(f"scale_counter ({cfg}, {n} threads) failed:\n{proc.stdout}{proc.stderr}")
                times.append(elapsed * 1000)
            row[f"{cfg}_ms"] = statistics.median(times)
        log(f"scaling   threads={n:<3} plain={row['plain_ms']:.1f}ms detect={row['detect_ms']:.1f}ms "
            f"tsan={row['tsan_ms']:.1f}ms")
        rows.append(row)
    return rows


def bench_prevention(r: Runner, trials: int) -> List[dict]:
    rows = []
    for name, ok_line in RACY_DEMOS:
        protect = r.scratch / f"{name}_protect.json"
        r.sentinel(DEMOS_BIN / name, "detect", report=protect)
        row = {"demo": name, "trials": trials}
        configs = {
            "plain": lambda: r.run(DEMOS_BIN / f"{name}_plain"),
            "detect": lambda: r.sentinel(DEMOS_BIN / name, "detect"),
            "prevent_file": lambda: r.sentinel(DEMOS_BIN / name, "prevent", protect_file=protect),
            "prevent_live": lambda: r.sentinel(DEMOS_BIN / name, "prevent"),
        }
        for cfg, fn in configs.items():
            buggy = confirmed_runs = 0
            for _ in range(trials):
                _, proc = fn()
                buggy += ok_line not in proc.stdout
                if cfg.startswith("prevent"):
                    confirmed_runs += r.last_report()["summary"]["confirmed_races"] > 0
            row[f"{cfg}_buggy"] = buggy
            if cfg.startswith("prevent"):
                row[f"{cfg}_confirmed_runs"] = confirmed_runs
        base = row["plain_buggy"]
        row["elimination"] = (base - row["prevent_file_buggy"]) / base if base else None
        log(f"prevent   {name:<26} plain {row['plain_buggy']}/{trials}  detect {row['detect_buggy']}/{trials}  "
            f"prevent(file) {row['prevent_file_buggy']}/{trials}  prevent(live) {row['prevent_live_buggy']}/{trials}")
        rows.append(row)
    return rows


# ---------------------------------------------------------------------------
# SVG charts (static, theme-aware, per-mark <title> tooltips)
# ---------------------------------------------------------------------------

SVG_STYLE = """<style>
  svg { --surface:#fcfcfb; --ink:#0b0b0b; --ink2:#52514e; --grid:#e4e3df;
        --s-detect:#2a78d6; --s-prevent:#eb6834; --s-tsan:#1baf7a; --s-plain:#eda100; }
  @media (prefers-color-scheme: dark) {
    svg { --surface:#1a1a19; --ink:#f0efec; --ink2:#c3c2b7; --grid:#3a3a37;
          --s-detect:#3987e5; --s-prevent:#d95926; --s-tsan:#199e70; --s-plain:#c98500; }
  }
  .bg { fill: var(--surface); }
  text { font-family: system-ui, -apple-system, 'Segoe UI', sans-serif; fill: var(--ink2); font-size: 12px; }
  .title { fill: var(--ink); font-size: 15px; font-weight: 600; }
  .label { fill: var(--ink); }
  .grid { stroke: var(--grid); stroke-width: 1; }
  .detect { fill: var(--s-detect); stroke: var(--s-detect); }
  .prevent { fill: var(--s-prevent); stroke: var(--s-prevent); }
  .tsan { fill: var(--s-tsan); stroke: var(--s-tsan); }
  .plain { fill: var(--s-plain); stroke: var(--s-plain); }
  rect.bar, path.bar { stroke: none; }
  path.line { fill: none; stroke-width: 2; }
  circle.dot { stroke: var(--surface); stroke-width: 2; }
</style>"""

SERIES_LABEL = {"plain": "Plain (no instrumentation)", "detect": "Sentinel detect",
                "prevent": "Sentinel prevent", "tsan": "ThreadSanitizer"}


def _esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def _log_ticks(lo: float, hi: float) -> List[float]:
    ticks, v = [], 10 ** int(__import__("math").floor(__import__("math").log10(lo)))
    while v <= hi * 1.0001:
        for m in (1, 2, 5):
            if lo * 0.9999 <= v * m <= hi * 1.0001:
                ticks.append(v * m)
        v *= 10
    return ticks


def _legend(keys: List[str], x: float, y: float) -> str:
    out = []
    for k in keys:
        out.append(f'<rect class="{k}" x="{x:.1f}" y="{y - 9:.1f}" width="10" height="10" rx="2"/>'
                   f'<text x="{x + 15:.1f}" y="{y:.1f}">{SERIES_LABEL[k]}</text>')
        x += 22 + 7 * len(SERIES_LABEL[k])
    return "".join(out)


def svg_overhead(rows: List[dict]) -> str:
    import math
    keys = ["detect", "prevent", "tsan"]
    bar_h, gap, group_gap = 7, 2, 12
    left, right, top = 190, 30, 70
    group_h = len(keys) * (bar_h + gap) - gap
    width = 760
    height = top + len(rows) * (group_h + group_gap) + 40
    vals = [row[f"{k}_x"] for row in rows for k in keys]
    lo, hi = 1.0, max(vals) * 1.15
    span = math.log10(hi) - math.log10(lo)
    sx = lambda v: left + (math.log10(max(v, lo)) - math.log10(lo)) / span * (width - left - right)

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" '
           f'height="{height}" role="img" aria-label="Runtime overhead factor versus uninstrumented build">',
           SVG_STYLE, f'<rect class="bg" width="{width}" height="{height}"/>',
           f'<text class="title" x="16" y="26">Runtime overhead vs. uninstrumented build (×, log scale)</text>',
           _legend(keys, 16, 50)]
    plot_bottom = height - 30
    for t in _log_ticks(lo, hi):
        x = sx(t)
        out.append(f'<line class="grid" x1="{x:.1f}" x2="{x:.1f}" y1="{top - 8}" y2="{plot_bottom}"/>'
                   f'<text x="{x:.1f}" y="{plot_bottom + 16}" text-anchor="middle">{t:g}×</text>')
    y = top
    for row in rows:
        out.append(f'<text class="label" x="{left - 10}" y="{y + group_h / 2 + 4:.1f}" '
                   f'text-anchor="end">{_esc(row["demo"])}</text>')
        for k in keys:
            v = row[f"{k}_x"]
            x0, x1 = sx(lo), sx(v)
            w = max(x1 - x0, 2)
            rr = min(4, w / 2, bar_h / 2)
            # Bar anchored at the baseline, rounded only at its data end
            d = (f"M{x0:.1f},{y:.1f} H{x0 + w - rr:.1f} Q{x0 + w:.1f},{y:.1f} {x0 + w:.1f},{y + rr:.1f} "
                 f"V{y + bar_h - rr:.1f} Q{x0 + w:.1f},{y + bar_h:.1f} {x0 + w - rr:.1f},{y + bar_h:.1f} "
                 f"H{x0:.1f} Z")
            tip = f'{row["demo"]} — {SERIES_LABEL[k]}: {v:.1f}× ({row[f"{k}_ms"]:.0f} ms vs {row["plain_ms"]:.0f} ms)'
            out.append(f'<path class="bar {k}" d="{d}"><title>{_esc(tip)}</title></path>')
            y += bar_h + gap
        y += group_gap - gap
    out.append("</svg>")
    return "\n".join(out)


def svg_scalability(rows: List[dict]) -> str:
    import math
    keys = ["plain", "detect", "tsan"]
    width, height = 760, 400
    left, right, top, bottom = 70, 150, 70, 50
    xs = [row["threads"] for row in rows]
    vals = [row[f"{k}_ms"] for row in rows for k in keys]
    lo, hi = min(vals) * 0.8, max(vals) * 1.25
    sx = lambda n: left + (math.log2(n) - math.log2(xs[0])) / (math.log2(xs[-1]) - math.log2(xs[0])) * (width - left - right)
    sy = lambda v: height - bottom - (math.log10(v) - math.log10(lo)) / (math.log10(hi) - math.log10(lo)) * (height - top - bottom)

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" '
           f'height="{height}" role="img" aria-label="Wall time by thread count">',
           SVG_STYLE, f'<rect class="bg" width="{width}" height="{height}"/>',
           f'<text class="title" x="16" y="26">Wall time by thread count ({SCALE_ITERATIONS:,} iterations/thread, ms, log scale)</text>',
           _legend(keys, 16, 50)]
    for t in _log_ticks(lo, hi):
        y = sy(t)
        out.append(f'<line class="grid" x1="{left}" x2="{width - right}" y1="{y:.1f}" y2="{y:.1f}"/>'
                   f'<text x="{left - 8}" y="{y + 4:.1f}" text-anchor="end">{t:g}</text>')
    for n in xs:
        out.append(f'<text x="{sx(n):.1f}" y="{height - bottom + 20}" text-anchor="middle">{n}</text>')
    out.append(f'<text x="{(left + width - right) / 2:.1f}" y="{height - 12}" text-anchor="middle">threads</text>')
    for k in keys:
        pts = [(sx(row["threads"]), sy(row[f"{k}_ms"])) for row in rows]
        out.append(f'<path class="line {k}" d="M' + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts) + '"/>')
        for row, (x, y) in zip(rows, pts):
            tip = f'{SERIES_LABEL[k]}, {row["threads"]} threads: {row[f"{k}_ms"]:.0f} ms'
            out.append(f'<circle class="dot {k}" cx="{x:.1f}" cy="{y:.1f}" r="4.5"><title>{_esc(tip)}</title></circle>')
        x, y = pts[-1]
        out.append(f'<text class="label" x="{x + 10:.1f}" y="{y + 4:.1f}">{SERIES_LABEL[k].split(" (")[0]}</text>')
    out.append("</svg>")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# Markdown
# ---------------------------------------------------------------------------

def _pct(v: Optional[float]) -> str:
    return "n/a" if v is None else f"{v * 100:.0f}%"


def md_environment(runs: int, trials: int) -> str:
    def sh(cmd):
        try:
            return subprocess.run(cmd, shell=True, capture_output=True, text=True).stdout.strip()
        except OSError:
            return "?"
    cpu = sh("grep -m1 'model name' /proc/cpuinfo | cut -d: -f2").strip()
    return "\n".join([
        f"- **Date:** {date.today().isoformat()}",
        f"- **CPU:** {cpu or platform.processor()} ({os.cpu_count()} logical cores)",
        f"- **Kernel:** {platform.system()} {platform.release()}",
        f"- **Compiler:** {sh('gcc --version | head -1')}",
        f"- **Repetitions:** {runs} timed runs per configuration (median reported); "
        f"{trials} trials per prevention configuration",
        "- **TSan:** gcc `-fsanitize=thread`, run under `setarch -R` (see Methodology)",
    ])


def md_accuracy(rows: List[dict], metrics: Dict[str, dict]) -> str:
    def cell(row, tool):
        v = row[tool]
        exp = row["expected"].get(tool)
        mark = "" if tool == "tsan" or v == exp else " ✗"
        unstable = "" if row[f"{tool}_stable"] else " ~"
        return f"{v}{mark}{unstable}"
    out = ["| Demo | Real race? | Hybrid | Lockset | HB | TSan |", "|---|---|---|---|---|---|"]
    for row in rows:
        out.append(f"| `{row['demo']}` | {'yes' if row['racy'] else 'no'} | " +
                   " | ".join(cell(row, t) for t in DETECTORS + ["tsan"]) + " |")
    out += ["", "✗ = differs from `demos/expected/`; ~ = verdict varied between runs.", "",
            "| Tool | TP | FP | FN | TN | Precision | Recall | Matches expected |",
            "|---|---|---|---|---|---|---|---|"]
    names = {"hybrid": "Sentinel hybrid", "lockset": "Sentinel lockset", "hb": "Sentinel HB", "tsan": "ThreadSanitizer"}
    for tool, m in metrics.items():
        exp = "—" if tool == "tsan" else f"{m['matches_expected']}/{len(rows)}"
        out.append(f"| {names[tool]} | {m['tp']} | {m['fp']} | {m['fn']} | {m['tn']} | "
                   f"{_pct(m['precision'])} | {_pct(m['recall'])} | {exp} |")
    return "\n".join(out)


def md_overhead(rows: List[dict]) -> str:
    out = ["![Runtime overhead](img/overhead.svg)", "",
           "| Demo | Plain (ms) | Detect (ms) | Prevent (ms) | TSan (ms) | Detect × | Prevent × | TSan × |",
           "|---|---|---|---|---|---|---|---|"]
    for row in rows:
        out.append(f"| `{row['demo']}` | {row['plain_ms']:.1f} | {row['detect_ms']:.1f} | {row['prevent_ms']:.1f} | "
                   f"{row['tsan_ms']:.1f} | {row['detect_x']:.1f} | {row['prevent_x']:.1f} | {row['tsan_x']:.1f} |")
    gm = lambda k: statistics.geometric_mean([row[f"{k}_x"] for row in rows])
    out.append(f"| **Geometric mean** | | | | | **{gm('detect'):.1f}** | **{gm('prevent'):.1f}** | **{gm('tsan'):.1f}** |")
    return "\n".join(out)


def md_scalability(rows: List[dict]) -> str:
    out = ["![Scalability](img/scalability.svg)", "",
           "| Threads | Plain (ms) | Detect (ms) | TSan (ms) | Detect × | TSan × |", "|---|---|---|---|---|---|"]
    for row in rows:
        out.append(f"| {row['threads']} | {row['plain_ms']:.1f} | {row['detect_ms']:.1f} | {row['tsan_ms']:.1f} | "
                   f"{row['detect_ms'] / row['plain_ms']:.1f} | {row['tsan_ms'] / row['plain_ms']:.1f} |")
    return "\n".join(out)


def md_prevention(rows: List[dict]) -> str:
    out = ["Cells are runs (out of N) whose output showed the bug.", "",
           "| Demo | N | Plain | Detect only | Prevent (protect file) | Prevent (live) | Elimination (protect file) |",
           "|---|---|---|---|---|---|---|"]
    for row in rows:
        n = row["trials"]
        out.append(f"| `{row['demo']}` | {n} | {row['plain_buggy']} | {row['detect_buggy']} | "
                   f"{row['prevent_file_buggy']} | {row['prevent_live_buggy']} | {_pct(row['elimination'])} |")
    total = lambda k: sum(row[k] for row in rows)
    out.append(f"| **Total** | {rows[0]['trials'] * len(rows)} | **{total('plain_buggy')}** | **{total('detect_buggy')}** | "
               f"**{total('prevent_file_buggy')}** | **{total('prevent_live_buggy')}** | |")
    out += ["", f"Runs that still reported a CONFIRMED race: protect file "
            f"{total('prevent_file_confirmed_runs')}, live {total('prevent_live_confirmed_runs')} "
            f"(live mode reports the race that triggered protection)."]
    return "\n".join(out)


EVAL_TEMPLATE = """# Evaluation

All numbers below are produced by `make bench` (`python -m sentinel bench`); raw data is in
`results/bench/`. Sections between `BENCH` markers are regenerated on every run.

## Environment

<!-- BENCH:environment -->
<!-- /BENCH:environment -->

## 1. Detection accuracy

<!-- BENCH:accuracy -->
<!-- /BENCH:accuracy -->

## 2. Runtime overhead

<!-- BENCH:overhead -->
<!-- /BENCH:overhead -->

## 3. Scalability

<!-- BENCH:scalability -->
<!-- /BENCH:scalability -->

## 4. Prevention effectiveness

<!-- BENCH:prevention -->
<!-- /BENCH:prevention -->
"""


def update_markdown(sections: Dict[str, str]) -> None:
    DOCS.mkdir(exist_ok=True)
    text = EVAL_MD.read_text(encoding="utf-8") if EVAL_MD.exists() else EVAL_TEMPLATE
    for name, body in sections.items():
        pattern = re.compile(rf"(<!-- BENCH:{name} -->\n).*?(<!-- /BENCH:{name} -->)", re.S)
        if not pattern.search(text):
            text += f"\n<!-- BENCH:{name} -->\n<!-- /BENCH:{name} -->\n"
        text = pattern.sub(lambda m: m.group(1) + body + "\n" + m.group(2), text)
    EVAL_MD.write_text(text, encoding="utf-8")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def _write_csv(path: Path, rows: List[dict]) -> None:
    flat = [{k: v for k, v in row.items() if not isinstance(v, dict)} for row in rows]
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(flat[0].keys()))
        writer.writeheader()
        writer.writerows(flat)


def execute(args) -> None:
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)
    scratch = out_dir / "scratch"
    scratch.mkdir(exist_ok=True)
    runs, trials = args.runs, args.trials
    if args.quick:
        runs, trials = 2, 5
    sections = args.only.split(",") if args.only else ["accuracy", "overhead", "scalability", "prevention"]

    if not args.no_build:
        log("building all targets …")
        if subprocess.run(["make", "all", "baseline", "bench-bins"], cwd=REPO_ROOT).returncode != 0:
            log("ERROR: build failed")
            sys.exit(1)

    demos = [json.load(open(f)) for f in sorted(EXPECTED.glob("*.json"))]
    r = Runner(scratch)
    results: Dict[str, object] = {}
    md: Dict[str, str] = {"environment": md_environment(runs, trials)}
    img = DOCS / "img"
    img.mkdir(parents=True, exist_ok=True)

    if "accuracy" in sections:
        rows = bench_accuracy(r, demos, max(1, min(runs, 3)))
        metrics = accuracy_metrics(rows)
        results["accuracy"] = {"rows": rows, "metrics": metrics}
        _write_csv(out_dir / "accuracy.csv", rows)
        md["accuracy"] = md_accuracy(rows, metrics)
    if "overhead" in sections:
        rows = bench_overhead(r, demos, runs)
        results["overhead"] = rows
        _write_csv(out_dir / "overhead.csv", rows)
        (img / "overhead.svg").write_text(svg_overhead(rows), encoding="utf-8")
        md["overhead"] = md_overhead(rows)
    if "scalability" in sections:
        rows = bench_scalability(r, max(1, min(runs, 3)))
        results["scalability"] = rows
        _write_csv(out_dir / "scalability.csv", rows)
        (img / "scalability.svg").write_text(svg_scalability(rows), encoding="utf-8")
        md["scalability"] = md_scalability(rows)
    if "prevention" in sections:
        rows = bench_prevention(r, trials)
        results["prevention"] = rows
        _write_csv(out_dir / "prevention.csv", rows)
        md["prevention"] = md_prevention(rows)

    # Merge so a partial run (--only) keeps the other sections' latest results
    results_path = out_dir / "results.json"
    merged = json.load(open(results_path)) if results_path.exists() else {}
    merged.update(results)
    with open(results_path, "w") as f:
        json.dump(merged, f, indent=2)
    update_markdown(md)
    log(f"raw results → {out_dir}/ ; report → {EVAL_MD.relative_to(REPO_ROOT)}")
