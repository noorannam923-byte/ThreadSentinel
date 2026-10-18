import os
import json
import subprocess
import pytest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEMOS_BIN_DIR = REPO_ROOT / "demos" / "bin"
RESULTS_DIR = REPO_ROOT / "results"
PRELOAD_LIB = REPO_ROOT / "libsentinel_preload.so"

DEMOS = sorted(p.stem for p in (REPO_ROOT / "demos").glob("[0-9][0-9]_*.c"))
DEADLOCK_DEMO = "13_lock_order_inversion"

# Uninstrumented demos whose output proves the shim kept mutex/condvar/barrier semantics
PRELOAD_SAFE = [
    ("02_counter_locked", "Result: CORRECT"),
    ("08_condvar_handoff_safe", "Result: CORRECT"),
    ("11_barrier_phases_safe", "Result: CORRECT"),
    ("05_producer_consumer_bug", "Consumed all items"),
    (DEADLOCK_DEMO, "Total preserved"),
]


def _run(binary, report, preload=False):
    env = os.environ.copy()
    for k in ("TS_MODE", "TS_DETECTOR", "TS_PROTECT_FILE", "TS_TRACE", "LD_PRELOAD"):
        env.pop(k, None)
    env["TS_REPORT"] = str(report)
    env["TS_QUIET"] = "1"
    if preload:
        env["LD_PRELOAD"] = str(PRELOAD_LIB)
    if report.exists():
        report.unlink()
    p = subprocess.run([str(binary)], cwd=REPO_ROOT, env=env, capture_output=True, text=True, timeout=120)
    assert p.returncode == 0, p.stderr
    with open(report) as f:
        return p, json.load(f)


@pytest.mark.parametrize("demo_name", DEMOS)
def test_deadlock_verdict_instrumented(demo_name):
    """Only the lock-order inversion demo has a lock-order cycle."""
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    _, rep = _run(DEMOS_BIN_DIR / demo_name, RESULTS_DIR / f"{demo_name}_deadlock.json")
    expected = 1 if demo_name == DEADLOCK_DEMO else 0
    assert rep["summary"]["potential_deadlocks"] == expected
    assert len(rep["deadlocks"]) == expected


def test_deadlock_report_names_locks_and_sites():
    _, rep = _run(DEMOS_BIN_DIR / DEADLOCK_DEMO, RESULTS_DIR / "deadlock_detail.json")
    assert rep["summary"]["confirmed_races"] == 0
    (cycle,) = rep["deadlocks"]
    assert sorted(cycle["cycle"]) == ["&lock_a", "&lock_b"]
    assert {e["thread"] for e in cycle["edges"]} == {1, 2}
    assert all(e["location"].startswith(f"demos/{DEADLOCK_DEMO}.c:") for e in cycle["edges"])
    # The protect-file parser must not mistake lock names for racy variables
    assert rep["protected_items"] == []


@pytest.mark.parametrize("demo_name,ok_line", PRELOAD_SAFE)
def test_preload_shim_on_uninstrumented_binary(demo_name, ok_line):
    """The LD_PRELOAD shim keeps program behaviour and finds the cycle without recompiling."""
    assert PRELOAD_LIB.exists(), "build with `make all`"
    p, rep = _run(DEMOS_BIN_DIR / f"{demo_name}_plain", RESULTS_DIR / f"{demo_name}_preload.json", preload=True)
    assert ok_line in p.stdout
    assert rep["summary"]["potential_deadlocks"] == (1 if demo_name == DEADLOCK_DEMO else 0)
    assert rep["summary"]["confirmed_races"] == 0  # memory accesses are not visible to the shim
