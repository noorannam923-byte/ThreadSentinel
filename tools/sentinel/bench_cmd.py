"""
bench_cmd.py — `sentinel bench` subcommand implementation

Benchmarks all 12 demo programs across four configurations:
  1. plain     — uninstrumented baseline (-DTS_DISABLE)
  2. detect    — Sentinel hybrid detect mode
  3. prevent   — Sentinel prevent mode
  4. tsan      — ThreadSanitizer baseline (if available)

Produces a CSV with timing data and a Plotly HTML chart.
"""

import os
import sys
import csv
import time
import json
import subprocess
from pathlib import Path
from typing import List, Dict, Any

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEMOS_DIR = REPO_ROOT / "demos"
DEMOS_BIN = DEMOS_DIR / "bin"
EXPECTED  = DEMOS_DIR / "expected"


def execute(args) -> None:
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)
    n_runs = args.runs

    # Build everything first
    print("[sentinel bench] Building all targets …")
    ret = subprocess.run(["make", "all", "baseline"], cwd=REPO_ROOT)
    if ret.returncode != 0:
        print("[sentinel bench] ERROR: build failed", file=sys.stderr)
        sys.exit(1)

    # Collect demo names from expected/*.json
    expected_files = sorted(EXPECTED.glob("*.json"))
    if not expected_files:
        print("[sentinel bench] ERROR: no expected/*.json files found", file=sys.stderr)
        sys.exit(1)

    demo_names: List[str] = []
    for ef in expected_files:
        with open(ef) as f:
            gt = json.load(f)
        demo_names.append(gt["program"])

    configs = [
        ("plain",   "plain",   {}),
        ("detect",  "detect",  {"TS_MODE": "detect", "TS_DETECTOR": "hybrid"}),
        ("prevent", "prevent", {"TS_MODE": "prevent", "TS_DETECTOR": "hybrid"}),
        ("tsan",    "tsan",    {}),
    ]

    rows: List[Dict[str, Any]] = []

    for demo in demo_names:
        print(f"\n[sentinel bench] Demo: {demo}")
        row = {"demo": demo}

        # Generate a detect report for prevent protect file
        detect_report = output_dir / f"{demo}_bench_detect.json"
        detect_env = {**os.environ, "TS_MODE": "detect", "TS_DETECTOR": "hybrid",
                      "TS_REPORT": str(detect_report), "TS_QUIET": "1"}
        detect_binary = DEMOS_BIN / demo
        if detect_binary.exists():
            subprocess.run([str(detect_binary)], env=detect_env,
                           capture_output=True, timeout=60)

        for cfg_name, suffix, extra_env in configs:
            if suffix in ("plain", "tsan"):
                binary = DEMOS_BIN / f"{demo}_{suffix}"
            else:
                binary = DEMOS_BIN / demo

            if not binary.exists():
                print(f"  [{cfg_name}] SKIP (binary not found)")
                row[f"{cfg_name}_ms"] = None
                continue

            run_env = os.environ.copy()
            run_env.update(extra_env)
            run_env["TS_QUIET"] = "1"
            run_env["TS_REPORT"] = str(output_dir / f"{demo}_{cfg_name}.json")
            if cfg_name == "prevent" and detect_report.exists():
                run_env["TS_PROTECT_FILE"] = str(detect_report)

            times_ms = []
            for i in range(n_runs):
                t0 = time.perf_counter()
                try:
                    ret = subprocess.run([str(binary)], env=run_env,
                                         capture_output=True, timeout=120)
                except subprocess.TimeoutExpired:
                    print(f"  [{cfg_name}] run {i+1} TIMEOUT")
                    break
                elapsed_ms = (time.perf_counter() - t0) * 1000
                times_ms.append(elapsed_ms)
                print(f"  [{cfg_name}] run {i+1}/{n_runs}: {elapsed_ms:.1f} ms")

            if times_ms:
                avg = sum(times_ms) / len(times_ms)
                row[f"{cfg_name}_ms"] = round(avg, 1)
            else:
                row[f"{cfg_name}_ms"] = None

        rows.append(row)

    # Write CSV
    csv_path = output_dir / "benchmark_results.csv"
    fieldnames = ["demo", "plain_ms", "detect_ms", "prevent_ms", "tsan_ms"]
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    print(f"\n[sentinel bench] CSV written → {csv_path}")

    # Generate HTML chart
    try:
        _generate_chart(rows, output_dir)
    except ImportError:
        print("[sentinel bench] plotly not installed — skipping HTML chart. "
              "Run: pip install plotly")


def _generate_chart(rows: List[Dict[str, Any]], output_dir: Path) -> None:
    import plotly.graph_objects as go

    demos = [r["demo"] for r in rows]
    configs = {
        "plain":   ("Plain (no instrumentation)", "#6BCB77"),
        "detect":  ("Sentinel Detect",            "#4D96FF"),
        "prevent": ("Sentinel Prevent",           "#FF6B6B"),
        "tsan":    ("ThreadSanitizer",            "#FFD93D"),
    }

    fig = go.Figure()
    for key, (label, colour) in configs.items():
        values = [r.get(f"{key}_ms") for r in rows]
        fig.add_trace(go.Bar(
            name=label,
            x=demos,
            y=values,
            marker_color=colour,
        ))

    fig.update_layout(
        title="Thread Sentinel — Performance Overhead Benchmark",
        xaxis_title="Demo program",
        yaxis_title="Mean runtime (ms)",
        barmode="group",
        template="plotly_dark",
        legend=dict(orientation="h", yanchor="bottom", y=1.02, xanchor="right", x=1),
        height=520,
    )

    html_path = output_dir / "benchmark_chart.html"
    fig.write_html(str(html_path), include_plotlyjs="cdn")
    print(f"[sentinel bench] Chart written → {html_path}")
