import os
import json
import subprocess
import pytest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEMOS_BIN_DIR = REPO_ROOT / "demos" / "bin"
EXPECTED_DIR = REPO_ROOT / "demos" / "expected"
RESULTS_DIR = REPO_ROOT / "results"

EXPECTED_FILES = sorted(EXPECTED_DIR.glob("*.json"))

@pytest.fixture(scope="session", autouse=True)
def ensure_binaries():
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    # Ensure all demo binaries are built
    subprocess.run(["make", "demos"], cwd=REPO_ROOT, check=True)

@pytest.mark.parametrize("expected_file", EXPECTED_FILES, ids=lambda p: p.stem)
def test_demo_verdict(expected_file):
    with open(expected_file, "r") as f:
        ground_truth = json.load(f)
    
    prog_name = ground_truth["program"]
    expected_verdict = ground_truth["expected"]["hybrid"]
    
    binary_path = DEMOS_BIN_DIR / prog_name
    assert binary_path.exists(), f"Binary {binary_path} does not exist"
    
    # Run 5 times to verify schedule independence
    for run_idx in range(5):
        report_path = RESULTS_DIR / f"{prog_name}_run{run_idx}.json"
        if report_path.exists():
            report_path.unlink()
            
        env = os.environ.copy()
        env["TS_MODE"] = "detect"
        env["TS_DETECTOR"] = "hybrid"
        env["TS_REPORT"] = str(report_path)
        env["TS_QUIET"] = "1"
        
        proc = subprocess.run([str(binary_path)], cwd=REPO_ROOT, env=env, capture_output=True, text=True)
        assert proc.returncode == 0, f"{prog_name} failed with returncode {proc.returncode}\n{proc.stderr}"
        
        assert report_path.exists(), f"Report file was not generated: {report_path}"
        with open(report_path, "r") as rf:
            rep = json.load(rf)
            
        confirmed = rep["summary"]["confirmed_races"]
        potential = rep["summary"]["potential_races"]
        
        if expected_verdict == "CONFIRMED":
            assert confirmed >= 1, f"Run {run_idx}: Expected CONFIRMED race in {prog_name}, got confirmed={confirmed}, potential={potential}"
        elif expected_verdict == "POTENTIAL":
            assert potential >= 1, f"Run {run_idx}: Expected POTENTIAL race in {prog_name}, got confirmed={confirmed}, potential={potential}"
        elif expected_verdict == "NONE":
            assert confirmed == 0 and potential == 0, f"Run {run_idx}: Expected SAFE (0 races) in {prog_name}, got confirmed={confirmed}, potential={potential}"


def _verdict(rep):
    s = rep["summary"]
    if s["confirmed_races"] > 0:
        return "CONFIRMED"
    if s["potential_races"] > 0:
        return "POTENTIAL"
    return "NONE"


@pytest.mark.parametrize("detector", ["lockset", "hb"])
@pytest.mark.parametrize("expected_file", EXPECTED_FILES, ids=lambda p: p.stem)
def test_demo_verdict_baseline_detectors(expected_file, detector):
    """The pure lockset and pure HB modes must show the false positives / negatives the demos document."""
    with open(expected_file, "r") as f:
        ground_truth = json.load(f)
    prog_name = ground_truth["program"]
    report_path = RESULTS_DIR / f"{prog_name}_{detector}.json"

    env = os.environ.copy()
    env["TS_MODE"] = "detect"
    env["TS_DETECTOR"] = detector
    env["TS_REPORT"] = str(report_path)
    env["TS_QUIET"] = "1"
    proc = subprocess.run([str(DEMOS_BIN_DIR / prog_name)], cwd=REPO_ROOT, env=env, capture_output=True, text=True)
    assert proc.returncode == 0

    with open(report_path, "r") as rf:
        assert _verdict(json.load(rf)) == ground_truth["expected"][detector]
