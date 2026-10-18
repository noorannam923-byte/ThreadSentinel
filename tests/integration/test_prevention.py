import os
import json
import subprocess
import pytest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEMOS_BIN_DIR = REPO_ROOT / "demos" / "bin"
RESULTS_DIR = REPO_ROOT / "results"

RACY_DEMOS = [
    ("01_counter_race", "COUNTER MATCHES EXPECTED"),
    ("03_bank_transfer", "Total matches expected"),
    ("04_check_then_act", "Initialized exactly once"),
    ("05_producer_consumer_bug", "Consumed all items"),
    ("10_mixed_locks", "x matches expected"),
    ("12_linked_list_race", "All nodes preserved"),
]

@pytest.mark.parametrize("demo_name,expected_output", RACY_DEMOS)
def test_prevention_20_iterations(demo_name, expected_output):
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    binary = DEMOS_BIN_DIR / demo_name
    detect_report = RESULTS_DIR / f"{demo_name}_detect_for_prev.json"
    prevent_report = RESULTS_DIR / f"{demo_name}_prevent_run.json"
    
    # Step 1: Run once in detect mode to generate protect file
    env_detect = os.environ.copy()
    env_detect["TS_MODE"] = "detect"
    env_detect["TS_DETECTOR"] = "hybrid"
    env_detect["TS_REPORT"] = str(detect_report)
    env_detect["TS_QUIET"] = "1"
    
    p_detect = subprocess.run([str(binary)], cwd=REPO_ROOT, env=env_detect, capture_output=True, text=True)
    assert p_detect.returncode == 0
    assert detect_report.exists()
    
    # Step 2: Run in prevent mode with protect file 20 times in a loop
    env_prevent = os.environ.copy()
    env_prevent["TS_MODE"] = "prevent"
    env_prevent["TS_DETECTOR"] = "hybrid"
    env_prevent["TS_PROTECT_FILE"] = str(detect_report)
    env_prevent["TS_REPORT"] = str(prevent_report)
    env_prevent["TS_QUIET"] = "1"
    
    for i in range(20):
        p_prev = subprocess.run([str(binary)], cwd=REPO_ROOT, env=env_prevent, capture_output=True, text=True)
        assert p_prev.returncode == 0
        assert expected_output in p_prev.stdout, f"Iteration {i}: Output mismatch for {demo_name}:\n{p_prev.stdout}"
        
        with open(prevent_report, "r") as f:
            rep = json.load(f)
            
        assert rep["summary"]["confirmed_races"] == 0, f"Iteration {i}: Expected 0 confirmed races in prevent mode for {demo_name}"


@pytest.mark.parametrize("demo_name,_", RACY_DEMOS)
def test_live_protection_without_protect_file(demo_name, _):
    """Prevent mode with no protect file: the first CONFIRMED race must enroll the
    variable into the protected set at runtime, and the run must not hang or crash."""
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    binary = DEMOS_BIN_DIR / demo_name
    report = RESULTS_DIR / f"{demo_name}_live_prevent.json"

    env = os.environ.copy()
    env.pop("TS_PROTECT_FILE", None)
    env["TS_MODE"] = "prevent"
    env["TS_DETECTOR"] = "hybrid"
    env["TS_REPORT"] = str(report)
    env["TS_QUIET"] = "1"

    p = subprocess.run([str(binary)], cwd=REPO_ROOT, env=env, capture_output=True, text=True, timeout=60)
    assert p.returncode == 0

    with open(report, "r") as f:
        rep = json.load(f)
    assert len(rep["protected_items"]) >= 1, f"No variable was live-protected for {demo_name}"
