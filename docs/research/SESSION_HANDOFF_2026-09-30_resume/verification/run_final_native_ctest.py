"""Preserve one final integration outcome; invoke only after ROOT freeze grant."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import xml.etree.ElementTree as ET


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
RECOVERY = ROOT.parent / "research-recovery"
CTEST = RECOVERY / "native-build-tools/cmake/data/bin/ctest"
BASELINE = ROOT / "loom/tools/structure/graph_composed_validation_v1/native_baseline"
EXPECTED_CLI_SHA256 = "8e784b882b9a509f4e1f55bae50748d738a896e5a198a02781cb0c4168c23c2c"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(name, value):
    with (HERE / name).open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())


def main():
    outcomes = ("final_native_ctest_environment.json", "final_native_ctest.log",
                "final_native_ctest.xml", "final_native_ctest_summary.json")
    if any((HERE / name).exists() for name in outcomes):
        raise SystemExit("First final integration artifacts already exist; refusing overwrite")
    baseline = json.loads((BASELINE / "environment_before_build.json").read_text())
    native_pins = {name: sha(ROOT / name) for name in baseline["source_files_sha256"]}
    if native_pins != baseline["source_files_sha256"]:
        raise SystemExit("Native build inputs drifted: existing build cannot be reused")
    cli = ROOT / "loom/build/dev/cli/loom"
    if sha(cli) != EXPECTED_CLI_SHA256:
        raise SystemExit("Existing CLI binary differs from the measured native baseline")
    binary_paths = [cli, ROOT / "loom/build/dev/loom_tests",
                    ROOT / "loom/build/dev/loom_compat_tool",
                    ROOT / "loom/build/dev/loom_candidate_graph_native_tool",
                    ROOT / "loom/build/dev/server/loom-server",
                    ROOT / "loom/build/dev/libloom.so.0.1.0"]
    public_env = {
        "TMPDIR": "/var/tmp",
        "PYTHONPATH": str(ROOT) + ":" + str(RECOVERY / "contract-deps"),
        "PYTHONUSERBASE": str(RECOVERY / "native-python-userbase"),
        "PYTHONDONTWRITEBYTECODE": "1",
    }
    command = [str(CTEST), "--test-dir", str(ROOT / "loom/build/dev"),
               "--output-on-failure", "--output-junit", str(HERE / outcomes[2])]
    python_paths = sorted((ROOT / "loom/tools/structure").rglob("*.py"))
    python_paths += [ROOT / "loom/tools/contracts/analysis_plan_ref.py"]
    code_pins = {str(path.relative_to(ROOT)): sha(path) for path in python_paths}
    save(outcomes[0], {
        "schema": "loom.final_native_integration_environment/1",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "source_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "command": command, "selected_public_environment": public_env,
        "ctest_sha256": sha(CTEST), "native_source_files_sha256": native_pins,
        "native_source_aggregate_sha256": baseline["source_aggregate_sha256"],
        "native_build_reused": True,
        "binaries_sha256": {str(path.relative_to(ROOT)): sha(path) for path in binary_paths},
        "python_code_sha256": code_pins,
        "actual_model_api_calls": 0, "sealed_validation_opened": False,
    })
    started = time.monotonic()
    with (HERE / outcomes[1]).open("x", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=ROOT, env=os.environ | public_env,
                                stdout=log, stderr=subprocess.STDOUT)
        log.flush()
        os.fsync(log.fileno())
    elapsed = time.monotonic() - started
    junit = ET.parse(HERE / outcomes[2]).getroot() if (HERE / outcomes[2]).exists() else None
    cases = list(junit.iter("testcase")) if junit is not None else []
    summary = {
        "exit_code": result.returncode, "elapsed_seconds": elapsed,
        "testcases": len(cases),
        "failed": sum(bool(list(case.iter("failure"))) for case in cases),
        "skipped": sum(bool(list(case.iter("skipped"))) for case in cases),
        "structure_included": any(case.get("name") == "research.structure" for case in cases),
        "python_source_pins_unchanged": all(sha(ROOT / name) == expected for name, expected in code_pins.items()),
        "native_code_unchanged": True, "existing_gates_preserved": True,
    }
    save(outcomes[3], summary)
    print(json.dumps(summary, indent=2))
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
