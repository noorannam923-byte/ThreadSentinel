import os
import sys
import json
import subprocess
import pytest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEMOS_BIN_DIR = REPO_ROOT / "demos" / "bin"
TRACES_DIR = REPO_ROOT / "results" / "traces"

sys.path.insert(0, str(REPO_ROOT / "tools"))
from sentinel import replay  # noqa: E402

DEMOS = sorted(p.stem for p in (REPO_ROOT / "demos").glob("[0-9][0-9]_*.c"))


@pytest.mark.parametrize("detector", ["hybrid", "hb"])
@pytest.mark.parametrize("demo_name", DEMOS)
def test_replay_matches_runtime(demo_name, detector):
    """The offline Python replay of a demo's trace must find exactly the races the C runtime reported."""
    TRACES_DIR.mkdir(parents=True, exist_ok=True)
    trace = TRACES_DIR / f"{demo_name}_{detector}.jsonl"
    report = TRACES_DIR / f"{demo_name}_{detector}.json"

    env = os.environ.copy()
    env["TS_MODE"] = "detect"
    env["TS_DETECTOR"] = detector
    env["TS_TRACE"] = str(trace)
    env["TS_REPORT"] = str(report)
    env["TS_QUIET"] = "1"

    p = subprocess.run([str(DEMOS_BIN_DIR / demo_name)], cwd=REPO_ROOT, env=env,
                       capture_output=True, text=True, timeout=120)
    assert p.returncode == 0

    with open(report, "r") as f:
        ref = json.load(f)
    engine = replay.replay_trace(str(trace), detector=detector)

    assert engine.race_keys() == replay.report_race_keys(ref)
    trace.unlink()  # traces are large; keep only on failure
