#!/usr/bin/env python3
"""Serial, offline replay of DIC-0301 against a prebuilt current core archive.

Uses exact pre-change common/principles/header/cues Git blobs for the baseline;
does not emulate the legacy dictionaries or alter any repository production file.
Run only after coordinating compilation with the shared-resource owner.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

BASE_PIN = "50e6bb9f80b0cd855e4dd1efedaf3399cac5e4e6"


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run(command, *, cwd, log, stdout=None):
    log.write_text(log.read_text() + json.dumps([str(x) for x in command]) + "\n" if log.exists()
                   else json.dumps([str(x) for x in command]) + "\n")
    with log.open("ab") as errors:
        if stdout is None:
            subprocess.run(command, cwd=cwd, stdout=errors, stderr=errors, check=True)
        else:
            with stdout.open("wb") as output:
                subprocess.run(command, cwd=cwd, stdout=output, stderr=errors, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="fresh scratch directory; never overwritten")
    parser.add_argument("--base-pin", default=BASE_PIN)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[4]
    build = args.core_build.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    legacy = output / "legacy"
    legacy.mkdir()
    current = output / "current"
    current.mkdir()
    test_dir = Path(__file__).resolve().parent
    fixture = test_dir / "dic0301.cpp.fixture"
    oracle = test_dir / "dic0301_legacy_classes.json.fixture"
    sources = {
        "common.cpp": "loom/src/generalize/common.cpp",
        "principles.cpp": "loom/src/generalize/principles.cpp",
        "internal.h": "loom/src/generalize/internal.h",
        "cues.json": "loom/data/lexicons/cues.json",
    }
    for name, path in sources.items():
        content = subprocess.check_output(["git", "show", f"{args.base_pin}:{path}"], cwd=repo)
        (legacy / name).write_bytes(content)
        (current / name).write_bytes((repo / path).read_bytes())
    libraries = [build / name for name in ("libloom_core.a", "libloom_sqlite3_amalgamation.a", "libloom_miniz.a")]
    assert all(path.is_file() for path in libraries), "missing prebuilt archives"
    frozen_paths = [fixture, oracle, test_dir / "dic0301_paradigm_cases.h.fixture", Path(__file__).resolve(),
                    *(repo / path for path in sources.values()), repo / "loom/src/kb/pack_embedded.inc", *libraries]
    frozen_hashes = {path: sha(path) for path in frozen_paths}
    # Exact historic dictionary oracle lives only in this test fixture.
    current_classes = json.loads((repo / "loom/data/lexicons/cues.json").read_text())["classes"]
    expected_classes = json.loads(oracle.read_text())
    assert len(expected_classes) == 7
    assert all(current_classes[name] == expected for name, expected in expected_classes.items())
    assert sum(len(value["phrases"]) for value in expected_classes.values()) == 120
    assert all(name not in json.loads((legacy / "cues.json").read_text())["classes"] for name in expected_classes)
    compiler = shutil.which(os.environ.get("CXX", "c++"))
    if not compiler:
        raise RuntimeError("C++ compiler unavailable")
    flags = [compiler, "-std=c++20", "-O0", "-g0", "-I" + str(repo / "loom/include"),
             "-I" + str(repo / "loom/src"), "-I" + str(repo / "loom/tests"),
             "-isystem", str(repo / "loom/third_party/nlohmann"),
             '-DLOOM_TEST_FIXTURES="' + str(repo / "loom/tests/fixtures") + '"']
    log = output / "commands.log"
    objects = {"before": [], "after": []}
    for phase in ("before", "after"):
        for name in ("common", "principles"):
            source = (legacy if phase == "before" else current) / (name + ".cpp")
            obj = output / (phase + "_" + name + ".o")
            run(flags + ["-c", str(source), "-o", str(obj)], cwd=repo, log=log)
            objects[phase].append(obj)
    shared_link = [str(path) for path in libraries] + ["-lssl", "-lcrypto", "-lpthread",
                   "-Wl,--no-keep-memory,--reduce-memory-overheads"]
    for phase in ("before", "after"):
        executable = output / ("dic0301_" + phase)
        compile_flags = flags + (["-DDIC0301_BEFORE"] if phase == "before" else [])
        extras = [str(path) for path in objects[phase]]
        run(compile_flags + ["-x", "c++", str(fixture), "-x", "none"] + extras + shared_link +
            ["-o", str(executable)], cwd=repo, log=log)
        command = [str(executable), str(oracle)]
        command.append(str((legacy if phase == "before" else current) / "cues.json"))
        run(command, cwd=repo, log=log, stdout=output / (phase + ".json"))
    before = json.loads((output / "before.json").read_text())
    after = json.loads((output / "after.json").read_text())
    preservation = {key: before[key] == after[key] for key in
                    ("defaults", "dev_principles", "dev_paradigms", "empty_pack_override", "replacement")}
    legacy_rows = [row for row in after["dev_paradigms"] if row["mode"] == "legacy"]
    accepted_rows = [row for row in after["dev_paradigms"] if row["mode"] == "accepted"]
    defaults = after["defaults"]
    assert all(sha(path) == digest for path, digest in frozen_hashes.items()), "source or archive changed during replay"
    report = {
        "schema": "loom.test.dic0301/1", "base_pin": args.base_pin,
        "repository_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(),
        "before_counts": before["counts"], "after_counts": after["counts"],
        "preservation": preservation,
        "typed_phrase_samples": sum(len(row["samples"]) for row in defaults),
        "dev_principles": len(after["dev_principles"]["principles"]),
        "legacy_dev_cases": len(legacy_rows), "accepted_dev_cases": len(accepted_rows),
        "accepted_dev_correct": sum(row["correct"] for row in accepted_rows),
        "input_hashes_stable_during_replay": True,
        "limitations": ["Empty phrases[] is rejected by the unchanged foreign Pack schema; deletion disables a class.",
            "Replay pairs exact Git-pinned/current common+principles modules and explicit old/current cues through Pack::from_documents against the same prebuilt archives.",
            "This focused replay does not verify that the prebuilt archive embeds the new data; full test_kb_pack does that after regeneration/build.",
            "Synthetic DEV source only; this is preservation/configuration evidence, not a new precision gain or full CTest."],
        "sha256": {str(path.relative_to(repo)) if path.is_relative_to(repo) else
                   (str(path.relative_to(output)) if path.is_relative_to(output) else str(path)):
                   sha(path) for path in [fixture, oracle, test_dir / "dic0301_paradigm_cases.h.fixture", Path(__file__).resolve(),
                   repo / "loom/src/generalize/common.cpp", repo / "loom/src/generalize/principles.cpp",
                   repo / "loom/src/generalize/internal.h", repo / "loom/data/lexicons/cues.json",
                   repo / "loom/src/kb/pack_embedded.inc", *libraries,
                   *(directory / name for directory in (legacy, current) for name in sources),
                   *(path for phase_objects in objects.values() for path in phase_objects),
                   output / "dic0301_before", output / "dic0301_after", output / "before.json", output / "after.json"]},
    }
    report["passed"] = all(preservation.values()) and after["counts"]["deletion_no_resurrection"] == 7 and \
        before["counts"]["deletion_no_resurrection"] == 0 and len(legacy_rows) == len(accepted_rows) == 16 and \
        report["accepted_dev_correct"] == 16
    (output / "summary.json").write_text(json.dumps(report, indent=1, ensure_ascii=False) + "\n")
    print(json.dumps({key: report[key] for key in ("passed", "before_counts", "after_counts", "preservation",
          "typed_phrase_samples", "dev_principles", "legacy_dev_cases", "accepted_dev_correct")}, indent=1))
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
