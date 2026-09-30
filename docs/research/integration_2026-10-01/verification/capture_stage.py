"""Capture one explicitly requested verification stage without replacing receipts."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import subprocess
import sys
import time

WORK = Path(__file__).resolve().parent
SOURCE = WORK.parent / "chatadhd"
OUTPUT = WORK / "integration-2026-10-01"
SOURCE_PIN = "da77c769d3e020c396db5bef6a3a3755314d75c0"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot():
    def git(*args):
        return subprocess.check_output(["git", *args], cwd=SOURCE, text=True).strip()
    tracked = git("ls-files", "loom").splitlines()
    selected = [p for p in tracked if (
        p.endswith((".cpp", ".h", ".c", ".py")) and "/fixtures/" not in p
        or p.startswith("loom/tools/w6_evidence/")
        or p in ("loom/CMakeLists.txt", "loom/CMakePresets.json", "loom/server/CMakeLists.txt", "loom/cli/CMakeLists.txt"))]
    binaries = ["loom_tests", "loom_compat_tool", "loom_candidate_graph_native_tool",
                "libloom.so.0.1.0", "cli/loom", "server/loom-server"]
    return {
        "source_head": git("rev-parse", "HEAD"),
        "source_tree": git("rev-parse", "HEAD^{tree}"),
        "source_status": git("status", "--porcelain"),
        "sources_sha256": {p: sha(SOURCE / p) for p in selected},
        "binary_sha256": {p: sha(WORK / "native-dev" / p) for p in binaries if (WORK / "native-dev" / p).is_file()},
    }


if len(sys.argv) < 3 or "/" in sys.argv[1]:
    raise SystemExit("usage: run_night_stage.py UNIQUE_STAGE COMMAND [ARGS...]")
stage, command = sys.argv[1], sys.argv[2:]
subprocess.run(["git", "diff", "--exit-code", SOURCE_PIN, "--", "loom"], cwd=SOURCE, check=True,
               stdout=subprocess.DEVNULL)
OUTPUT.mkdir(exist_ok=True)
receipt = OUTPUT / (stage + ".receipt.json")
log = OUTPUT / (stage + ".log")
if receipt.exists() or log.exists():
    raise SystemExit("refusing to overwrite an existing stage")
env = dict(os.environ,
           PYTHONDONTWRITEBYTECODE="1",
           PYTHONUSERBASE=str(WORK / "python-userbase"),
           PYTHONPATH=str(SOURCE) + ":" + str(WORK / "deps"),
           TMPDIR="/var/tmp")
before = snapshot()
record = {"schema": "loom.integration_stage.v1", "stage": stage,
          "evaluation_source_pin": SOURCE_PIN,
          "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
          "cwd": str(SOURCE), "command": command,
          "environment": {k: env[k] for k in ("PYTHONDONTWRITEBYTECODE", "PYTHONUSERBASE", "PYTHONPATH", "TMPDIR", "LOOM_CHAT_RETRIEVAL_EVIDENCE_DIR") if k in env},
          "before": before}
receipt.write_text(json.dumps(record, indent=2) + "\n")
start = time.monotonic()
with log.open("xb") as stream:
    result = subprocess.run(command, cwd=SOURCE, env=env, stdout=stream, stderr=subprocess.STDOUT)
record.update(exit_code=result.returncode, seconds=time.monotonic() - start, after=snapshot())
record["sources_unchanged"] = record["before"]["sources_sha256"] == record["after"]["sources_sha256"]
record["log_sha256"] = sha(log)
receipt.write_text(json.dumps(record, indent=2) + "\n")
print(json.dumps({k: record[k] for k in ("stage", "exit_code", "seconds", "sources_unchanged")}))
print(str(log))
sys.exit(result.returncode)
