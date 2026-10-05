#!/usr/bin/env python3
"""Verify W3 runtime presets offline against an existing static core.

The base variant has the checkout's actual capabilities. The layered variant
extracts only W11/W12's real required headers and three implementation units at
resolved Git commits into isolated evidence. No checkout/build is modified, no
schema or layer resolver is substituted, and no provider is invoked.
--standalone-utilities compiles the checkout's actual utility implementation
units instead of linking an existing full core; that narrower scope is recorded.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def execute(command: list[str], log: Path, manifest: dict, cwd: Path) -> str:
    manifest["commands"].append(command)
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    output = result.stdout + result.stderr
    log.write_text(output, encoding="utf-8")
    if result.returncode:
        print(output, end="")
        raise subprocess.CalledProcessError(result.returncode, command)
    return output


def extract(root: Path, evidence: Path, ref: str, names: list[str], label: str, manifest: dict) -> list[Path]:
    commit = subprocess.check_output(
        ["git", "rev-parse", "--verify", "--end-of-options", ref + "^{commit}"], cwd=root, text=True).strip()
    manifest["dependencies"][label] = {"requested_ref": ref, "commit": commit, "files": {}}
    cpp = []
    for name in names:
        data = subprocess.check_output(["git", "show", f"{commit}:{name}"], cwd=root)
        destination = evidence / "source" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        manifest["dependencies"][label]["files"][name] = hashlib.sha256(data).hexdigest()
        if destination.suffix == ".cpp":
            cpp.append(destination)
    return cpp


def check_counts(output: str, mode: str) -> dict:
    cases = re.search(r"test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed\s*\|\s*(\d+) skipped", output)
    assertions = re.search(r"assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed", output)
    if not cases or not assertions:
        raise ValueError("missing actual doctest case/assertion counts")
    total, passed, failed, skipped = map(int, cases.groups())
    count, good, bad = map(int, assertions.groups())
    if not total or passed != total or failed or skipped or not count or good != count or bad:
        raise ValueError("runtime preset verification did not execute a complete passing suite")
    if mode == "layers" and total < 8:
        raise ValueError("actual W11/W12 layer cases were not compiled/executed")
    return {"cases": total, "passed": passed, "failed": failed, "skipped": skipped,
            "assertions": count, "assertions_passed": good, "assertions_failed": bad}


def generator_regression(root: Path, evidence: Path, flags: list[str], link_inputs: list[str], manifest: dict) -> None:
    """Compile the real generic getter, then compare its full parsed value."""
    if "generator_regression" in manifest:
        return
    output = evidence / "generator_codec"
    output.mkdir()
    spec = importlib.util.spec_from_file_location("actual_w3_preset_generator", root / "loom/src/context/gen_runtime_presets.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    strings = [')W3_PRESET"', '"quoted"', r"backslash\path", "line one\nline two", "NUL\0inside",
               "emoji🙂", "Zażółć gęślą jaźń", r"literal \u0000", "CRLF\r\n", "trailing\\",
               "tab\t", "separator\u2028", 'combined )W3_PRESET"\\\n\0🙂ż']
    expected = {"strings": strings, "metadata": {"id": strings[-1], "area": strings[0], "key": strings[4]},
                "dictionary": {strings[0]: strings[-1], strings[4]: strings[6]}}
    expected_path = output / "expected.json"
    expected_path.write_text(json.dumps(expected, ensure_ascii=True, sort_keys=True), encoding="utf-8")
    generated = generator.cpp_getter("generator_codec_fixture", expected)
    (output / "getter.inc").write_text(generated, encoding="utf-8")
    source = output / "generator_codec.cpp"
    source.write_text('''#include <fstream>
#include <iostream>
#include <sstream>
#include "loom/util/json.h"
using loom::Json;
#include "getter.inc"
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) return 3;
  std::ostringstream text; text << input.rdbuf();
  auto expected = loom::json::parse(text.str());
  if (!expected) return 4;
  const auto& actual = builtin_generator_codec_fixture();
  if (actual != *expected || loom::json::canonical(actual) != loom::json::canonical(*expected)) return 5;
  std::cout << loom::json::dump(Json{{"passed", true}, {"cases", 1},
      {"strings_round_tripped", actual.at("strings").size()}, {"full_value_equal", true},
      {"canonical_value_equal", true}, {"provider_calls", 0}, {"paid_calls", 0}}) << "\\n";
}
''', encoding="utf-8")
    obj = output / "generator_codec.o"
    execute(flags + ["-c", str(source), "-o", str(obj)], output / "compile.log", manifest, root)
    manifest["objects"][str(obj)] = sha(obj)
    executable = output / "generator_codec"
    execute([flags[0], str(obj), *link_inputs, "-fuse-ld=gold", "-Wl,--no-map-whole-files", "-pthread", "-ldl", "-lm", "-o", str(executable)],
            output / "link.log", manifest, root)
    text = execute([str(executable), str(expected_path)], output / "tests.log", manifest, root)
    result = json.loads(text)
    if result != {"passed": True, "cases": 1, "strings_round_tripped": len(strings), "full_value_equal": True,
                  "canonical_value_equal": True, "provider_calls": 0, "paid_calls": 0}:
        raise ValueError("actual C++ getter did not round-trip the complete arbitrary-string fixture")
    manifest["generator_regression"] = {"result": result, "generator_sha256": sha(root / "loom/src/context/gen_runtime_presets.py"),
        "expected_sha256": sha(expected_path), "generated_getter_sha256": sha(output / "getter.inc"),
        "driver_sha256": sha(source), "executable_sha256": sha(executable)}
    print(text, end="")


def variant(root: Path, build: Path, evidence: Path, mode: str, args: argparse.Namespace, manifest: dict) -> None:
    output = evidence / mode
    output.mkdir()
    loom = root / "loom"
    selected = []
    if mode == "layers":
        selected += extract(root, evidence, args.w11_ref, [
            "loom/include/loom/runtime_profile.h", "loom/src/model/runtime_profile.cpp",
            "loom/src/model/runtime_profiles_embedded.inc"], "W11", manifest)
        selected += extract(root, evidence, args.w12_ref, [
            "loom/include/loom/onboarding_layers.h", "loom/src/onboarding/layers.cpp",
            "loom/src/onboarding/runtime_adapter.h", "loom/src/onboarding/runtime_adapter.cpp"], "W12", manifest)
    flags = [args.compiler, "-std=c++20", "-O0", "-g0", "-Wall", "-Wextra", "-Werror",
             "-DJSON_USE_IMPLICIT_CONVERSIONS=1"]
    if mode == "layers":
        flags += ["-I", str(evidence / "source/loom/include"), "-I", str(evidence / "source/loom/src")]
    flags += ["-I", str(loom / "include"), "-I", str(loom / "src"),
              "-isystem", str(loom / "third_party/nlohmann"), "-isystem", str(loom / "third_party/doctest")]
    if args.standalone_utilities:
        utilities = ["json", "sha256", "result", "utf8", "unicode"]
        if mode == "layers":
            # RuntimeProfile's filesystem API uses the actual fs, ID/time and
            # logging implementations. No substitute bodies or core mock.
            utilities += ["fs", "random_ids_time", "log"]
        for utility in utilities:
            source = loom / "src/util" / (utility + ".cpp")
            selected.append(source)
            manifest["source_sha256"][str(source.relative_to(root))] = sha(source)
        manifest["source_sha256"]["loom/src/util/unicode_tables.inc"] = sha(loom / "src/util/unicode_tables.inc")
    objects = []
    for index, source in enumerate(selected):
        obj = output / f"native_{index}.o"
        execute(flags + ["-c", str(source), "-o", str(obj)], output / f"native_{index}.log", manifest, root)
        manifest["objects"][str(obj)] = sha(obj)
        objects.append(obj)
    source = root / "loom/src/context/tests/runtime_preset_test.cpp"
    obj = output / "runtime_preset_test.o"
    execute(flags + ["-DLOOM_RUNTIME_PRESET_TEST_MAIN=1", "-c", str(source), "-o", str(obj)],
            output / "compile.log", manifest, root)
    manifest["objects"][str(obj)] = sha(obj)
    executable = output / "runtime_preset_test"
    command = [args.compiler, str(obj), *map(str, objects)]
    if not args.standalone_utilities:
        command.append(str(build / "libloom_core.a"))
        for name in ("libloom_miniz.a", "libloom_sqlite3_amalgamation.a"):
            if (build / name).exists():
                command.append(str(build / name))
        if not (build / "libloom_sqlite3_amalgamation.a").exists():
            command.append("-lsqlite3")
        command += ["-lssl", "-lcrypto"]
    generator_link_inputs = command[2:]
    command += ["-fuse-ld=gold", "-Wl,--no-map-whole-files", "-pthread", "-ldl", "-lm", "-o", str(executable)]
    execute(command, output / "link.log", manifest, root)
    text = execute([str(executable), "--no-intro=true", "--no-colors=true"], output / "tests.log", manifest, root)
    manifest["variants"][mode] = {"counts": check_counts(text, mode), "executable_sha256": sha(executable),
                                  "provider_calls": 0, "paid_calls": 0}
    print(text, end="")
    generator_regression(root, evidence, flags, generator_link_inputs, manifest)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path, help="existing CMake build containing libloom_core.a; never built by this runner")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[4])
    parser.add_argument("--compiler", default=shutil.which("c++") or "c++")
    parser.add_argument("--mode", choices=("base", "layers", "both"), default="both")
    parser.add_argument("--standalone-utilities", action="store_true",
                        help="compile only actual utility dependencies instead of linking full core; not a full-core gate")
    parser.add_argument("--w11-ref", default="origin/gpt/data-profiles-2026-10-04")
    parser.add_argument("--w12-ref", default="origin/gpt/onboarding-2026-10-04")
    parser.add_argument("--evidence-dir", type=Path, help="new directory for complete positive or negative verification artifacts")
    args = parser.parse_args()
    root, build = args.repo.resolve(), args.build_dir.resolve()
    if not args.standalone_utilities and not (build / "libloom_core.a").is_file():
        parser.error("existing libloom_core.a is required; this runner does not start a core build")
    if args.evidence_dir:
        evidence = args.evidence_dir.resolve()
        if evidence.exists():
            parser.error("evidence directory must not exist; earlier positive/negative artifacts are preserved")
        evidence.mkdir(parents=True)
    else:
        evidence = Path(tempfile.mkdtemp(prefix="w3-runtime-presets-"))
    print(f"Runtime preset evidence: {evidence}", flush=True)
    owned = ["loom/src/context/runtime_preset.h", "loom/src/context/runtime_presets.inc",
             "loom/src/context/gen_runtime_presets.py", "loom/src/chat/reasoning_profile.h",
             "loom/data/runtime/context_goal_cues.pack", "loom/data/runtime/chat_reasoning.pack",
             "loom/data/context/runtime_preset_layers.pack", "loom/src/context/tests/runtime_preset_test.cpp",
             "loom/src/context/tests/run_runtime_preset_test.py"]
    manifest = {"schema": "loom.runtime_preset_verification/1", "mode": args.mode,
                "dependencies": {}, "commands": [], "variants": {}, "objects": {},
                "source_sha256": {}, "core_sha256": None if args.standalone_utilities else sha(build / "libloom_core.a"),
                "link_scope": "actual_utility_dependencies" if args.standalone_utilities else "existing_static_core",
                "core_build_started": False, "passed": False}
    try:
        manifest["source_sha256"] = {name: sha(root / name) for name in owned}
        execute(["python3", str(root / "loom/src/context/gen_runtime_presets.py"), "--check"],
                evidence / "generated_check.log", manifest, root)
        for mode in (("base", "layers") if args.mode == "both" else (args.mode,)):
            variant(root, build, evidence, mode, args, manifest)
        for name, expected in manifest["source_sha256"].items():
            if sha(root / name) != expected:
                raise ValueError("input source changed during verification: " + name)
        manifest["source_before_after_matches"] = True
        manifest["passed"] = True
    finally:
        (evidence / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
