#!/usr/bin/env python3
"""Replay W1's offline method projection and explicit native N3 acceptance."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sqlite3
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[4]
LOOM = REPO / "loom"
EXPECTED_NATIVE_CASES = 60
EXPECTED_STORE_CASES = 32


def native_store_probe(artifact: dict, library: Path, scratch: Path,
                       expected_cases: int = EXPECTED_STORE_CASES) -> dict:
    sys.path.insert(0, str(REPO))
    from loom.tools.coordination.graph_store import GraphStoreError, NativeGraphStore
    from loom.tools.structure.agentic_graph_v1 import packet as codec

    passed: list[str] = []
    receipts: list[dict] = []
    origin = {"kind": "system", "actor": "W1 offline projection fixture",
              "model": None, "recipe_sha256": None, "response_sha256": None}
    known_at = "2026-10-04T19:00:00Z"
    policy = {"schema": "loom.graph_packet_apply_policy/1", "acceptance": "auto",
              "allow_source_tombstones": False}

    def check(condition: bool, label: str) -> None:
        if not condition:
            raise AssertionError(label)
        passed.append(label)

    def profile_packet(profile: dict) -> dict:
        return codec.make_packet(origin=origin, known_at=known_at,
            entities=profile["entities"], claims=profile["claims"], sources=profile["sources"])

    def extend(packet: dict, profile: dict, proposal: str) -> dict:
        diff = codec.empty_diff(packet, proposal_id=proposal, origin=origin, known_at=known_at)
        for name in ("entities", "claims", "sources"):
            previous = {codec.record_id(name, row): row for row in packet[name]}
            after = {codec.record_id(name, row): row for row in profile[name]}
            if not previous.keys() <= after.keys():
                raise AssertionError("projection discarded earlier native records")
            for identity, row in after.items():
                if identity not in previous:
                    diff[name]["add"].append(row)
                elif previous[identity] != row:
                    diff[name]["update"].append({"id": identity,
                        "before_sha256": codec.digest(previous[identity]), "after": row})
        projected, _receipt = codec.apply_diff(packet, diff, policy)
        return projected

    for index, case in enumerate(artifact["graphs"]):
        case_id = case["id"]
        if "definition_profile" in case:
            packet = profile_packet(case["definition_profile"])
            packet = extend(packet, case["prepared_graph"]["profile"], "prepare:" + case_id)
            packet = extend(packet, case["bound_graph"]["profile"], "bind:" + case_id)
            expected_history = 2
        else:
            packet = profile_packet(case["bound_graph"]["profile"])
            expected_history = 0
        codec.validate_packet(packet)
        check(len(packet["history"]) == expected_history,
              case_id + ": exact declared packet history validates")
        selections = {name: [codec.record_id(name, row) for row in packet[name]]
                      for name in ("entities", "claims", "sources")}
        expected = {name: {identity: None for identity in ids}
                    for name, ids in selections.items()}
        directory = scratch / ("store-" + str(index))
        store = NativeGraphStore(library, directory)
        try:
            request = {"operation": "accept", "target": "W1-offline-" + case_id,
                "packet": packet, "selection": selections, "expected_rows": expected,
                "explicitly_accepted": False}
            try:
                store.execute(request)
            except GraphStoreError:
                rejected = True
            else:
                rejected = False
            check(rejected, case_id + ": native boundary requires explicit acceptance")

            result = store.accept(packet, target=request["target"], selection=selections,
                expected_rows=expected, explicitly_accepted=True)
            receipt = result["receipt"]
            check(receipt["packet"] == packet and not result["replayed"],
                  case_id + ": actual N3 stores the complete native graph")
            check(receipt["acceptance_establishes_content_truth"] is False,
                  case_id + ": acceptance preserves content uncertainty")
            with sqlite3.connect(directory / "chatadhd.db") as database:
                rows_match = True
                for name, table in (("entities", "loom_kb_entities"),
                                    ("claims", "loom_kb_claims"),
                                    ("sources", "loom_kb_observations")):
                    actual = {identity: json.loads(body) for identity, body in database.execute(
                        "SELECT id, body FROM " + table + " WHERE run_id=?", (receipt["run_id"],))}
                    expected_rows = {codec.record_id(name, row):
                        (row["observation"] if name == "sources" else row) for row in packet[name]}
                    rows_match &= actual == expected_rows
                check(rows_match, case_id + ": stored DTO bodies preserve every selected row")
            repeated = store.accept(packet, target=request["target"], selection=selections,
                expected_rows=expected, explicitly_accepted=True)
            check(repeated["replayed"] and repeated["receipt"] == receipt,
                  case_id + ": identical acceptance returns the immutable receipt")
        finally:
            store.close()
        with NativeGraphStore(library, directory) as restarted:
            check(restarted.read(receipt["id"])["receipt"] == receipt,
                  case_id + ": restart retains packet sources, edges and history")
            replay = restarted.replay(receipt["id"])
            check(replay["replayed"] and replay["row_drift"]["matches"] and replay["receipt"] == receipt,
                  case_id + ": native replay verifies unchanged rows")
        receipts.append({"id": case_id, "packet_id": packet["packet_id"],
            "receipt": receipt, "packet_sha256": codec.digest(packet)})
    return {"executed_cases": len(passed), "passed_cases": len(passed), "stdout": passed,
            "receipts": receipts, "success": len(passed) == expected_cases,
            "history_validation": "Python codec; current native N3 receipt declares its own validation scope"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=LOOM / "build/dev")
    parser.add_argument("--library", type=Path, help="existing shared library with the N3 store export")
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--evidence", type=Path, help="save source-bound JSON evidence")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    library = (args.library or build / "libloom.so.0.1.0").resolve()
    fixture = LOOM / "src/extract/tests/method_graph_probe.cpp.fixture"
    adapter = LOOM / "src/extract/prompt_method_graph.cpp"
    source_paths = [Path(__file__).resolve(), fixture, adapter,
        LOOM / "src/extract/prompt_method_graph.h", LOOM / "src/extract/prompt_contract.h",
        LOOM / "src/extract/prompt_contract.cpp", LOOM / "src/extract/prompt_contract_data.inc",
        *sorted((LOOM / "data/prompts").glob("*"))]
    source_paths += sorted((LOOM / "src/extract").glob("prompt_method_*.inc"))
    hashes = {str(path.relative_to(REPO)): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in source_paths if path.is_file()}
    evidence: dict = {"schema": "loom.prompt_method_graph_probe/1", "offline": True,
        "provider_calls": 0, "paid_calls": 0, "producer_execution_verified": False,
        "scope": "native data projection and explicit acceptance; synthetic candidate inputs",
        "source_sha256": hashes, "expected_native_cases": EXPECTED_NATIVE_CASES,
        "expected_store_cases": EXPECTED_STORE_CASES}
    evidence["linked_library_sha256"] = {name: hashlib.sha256((build / name).read_bytes()).hexdigest()
        for name in ("libloom_core.a", "libloom_sqlite3_amalgamation.a", "libloom_miniz.a")}
    with tempfile.TemporaryDirectory(prefix="loom-method-graph-probe-") as tmp:
        scratch = Path(tmp)
        binary = scratch / "method_graph_probe"
        command = [args.cxx, "-x", "c++", "-std=c++20", "-g0", "-Wall", "-Wextra", "-Werror",
            "-Iloom/include", "-Iloom/src", "-isystem", "loom/third_party/nlohmann",
            str(fixture.relative_to(REPO)), str(adapter.relative_to(REPO)), "-x", "none",
            str(build / "libloom_core.a"), str(build / "libloom_sqlite3_amalgamation.a"),
            str(build / "libloom_miniz.a"), "-lssl", "-lcrypto", "-lpthread"]
        if sys.platform.startswith("linux"):
            command.append("-Wl,--no-keep-memory")
        command.extend(["-o", str(binary)])
        compiled = subprocess.run(command, cwd=REPO, text=True, capture_output=True)
        result = subprocess.run([str(binary), str(scratch / "overlay"),
            str(LOOM / "data/prompts/jev_active_refute_v2.recipe")], cwd=REPO,
            text=True, capture_output=True) if compiled.returncode == 0 else None
        stdout = result.stdout if result else ""
        passed = sum(line.startswith("PASS ") for line in stdout.splitlines())
        summary = re.search(r"^checks (\d+)/(\d+)$", stdout, re.MULTILINE)
        fixture_ok = result is not None and result.returncode == 0 and summary is not None and (
            passed == int(summary[1]) == int(summary[2]) == EXPECTED_NATIVE_CASES)
        artifact_lines = [line[len("ARTIFACT "):] for line in stdout.splitlines()
                          if line.startswith("ARTIFACT ")]
        artifact = json.loads(artifact_lines[0]) if len(artifact_lines) == 1 else None

        def portable(value: str) -> str:
            return value.replace(str(REPO), "<repo>").replace(str(build), "<build>").replace(tmp, "<scratch>")

        evidence.update({"compile_command": [portable(value) for value in command],
            "compile_exit_code": compiled.returncode,
            "run_exit_code": result.returncode if result else None,
            "executed_native_cases": int(summary[2]) if summary else passed,
            "passed_native_cases": passed, "native_success": fixture_ok,
            "stdout": [line for line in stdout.splitlines() if not line.startswith("ARTIFACT ")],
            "diagnostics": portable(compiled.stderr + (result.stderr if result else ""))})
        if fixture_ok and artifact:
            evidence["projection_artifact"] = artifact
            evidence["projection_artifact_sha256"] = hashlib.sha256(
                json.dumps(artifact, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            try:
                evidence["library_sha256"] = hashlib.sha256(library.read_bytes()).hexdigest()
                evidence["native_store"] = native_store_probe(artifact, library, scratch)
            except Exception as error:
                evidence["native_store"] = {"success": False, "error": str(error)}
        evidence["success"] = fixture_ok and evidence.get("native_store", {}).get("success", False)
    evidence["replay"] = "python3 loom/src/extract/tests/method_graph_probe.py --build-dir loom/build/dev"
    encoded = json.dumps(evidence, ensure_ascii=False, indent=2) + "\n"
    if args.evidence:
        args.evidence.parent.mkdir(parents=True, exist_ok=True)
        args.evidence.write_text(encoded, encoding="utf-8")
    display = {key: value for key, value in evidence.items()
               if key not in ("projection_artifact", "native_store")}
    if "native_store" in evidence:
        display["native_store"] = {key: value for key, value in evidence["native_store"].items()
                                   if key != "receipts"}
    print(json.dumps(display, ensure_ascii=False, indent=2))
    return 0 if evidence["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
