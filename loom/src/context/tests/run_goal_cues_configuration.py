#!/usr/bin/env python3
"""Exercise actual consumer configuration and optional isolated builtin drift.

  python3 loom/src/context/tests/run_goal_cues_configuration.py \\
    --build-dir /tmp/w3-selector-2-after-build --output-dir /tmp/w3-cues-config \\
    --builtin-probes

Never configures/builds core or edits the checkout. Builtin probes compile the
entire actual ContextEngine translation unit with one altered generated getter,
link its object before the frozen core, and verify the old core member was not
pulled. All other getter bytes, including the legacy identity anchor, stay exact.
No duplicate-symbol relaxation, copied classifier or provider call is used.
"""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
import goal_cues_parity as parity


def logged(command: list[str], cwd: Path, output: Path, name: str, manifest: dict) -> None:
    manifest.setdefault("commands", []).append({"name": name, "argv": command, "cwd": str(cwd)})
    parity.write_json(output / "manifest.json", manifest)
    with (output / (name + ".log")).open("wb") as log:
        result = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=False)
    manifest[name + "_exit_code"] = result.returncode
    parity.write_json(output / "manifest.json", manifest)
    if result.returncode:
        raise RuntimeError(name + " failed; preserved " + str(output / (name + ".log")))


def getter(definition: dict) -> str:
    payload = json.dumps(definition, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    literal = json.dumps(payload, ensure_ascii=False)
    return ("inline const Json& builtin_context_goal_cues_definition() {\n"
            f"  static const Json value = Json::parse({literal});\n"
            "  return value;\n}\n")


def execute(binary: Path, descriptor: Path, build: Path, folder: Path, mutated: bool, manifest: dict) -> dict:
    command = [str(binary), str(descriptor)] + (["--mutated-builtin"] if mutated else [])
    manifest.setdefault("commands", []).append({"name": "run", "argv": command, "cwd": str(build)})
    with (folder / "outputs.json").open("wb") as stream, (folder / "run.log").open("wb") as log:
        result = subprocess.run(command, cwd=build, stdout=stream, stderr=log, check=False)
    manifest["run_exit_code"] = result.returncode
    manifest["output_sha256"] = parity.sha(folder / "outputs.json")
    manifest["binary_sha256"] = parity.sha(binary)
    parity.write_json(folder / "manifest.json", manifest)
    if result.returncode:
        raise RuntimeError("actual consumer failed; preserved " + str(folder / "run.log"))
    data = json.loads((folder / "outputs.json").read_text(encoding="utf-8"))
    if data["provider_calls"] != 0 or data["mutated_builtin"] != mutated:
        raise RuntimeError("consumer scope/provider evidence is invalid")
    manifest["assertions"] = data["assertions"]
    manifest["provider_calls"] = data["provider_calls"]
    parity.write_json(folder / "manifest.json", manifest)
    return data


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--builtin-probes", action="store_true")
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    context = Path(__file__).resolve().parents[1]
    loom = context.parents[1]
    source = context / "tests/goal_cues_configuration.cpp"
    descriptor = loom / "data/runtime/context_goal_cues.pack"
    cpp, inc = context / "context_engine.cpp", context / "runtime_presets.inc"
    definition = json.loads(descriptor.read_text(encoding="utf-8"))
    obj, binary = output / "consumer.o", output / "consumer"
    compile_command = ["-DLOOM_GOAL_CUES_CONFIGURATION_MAIN=1" if arg == "-DLOOM_GOAL_CUES_PARITY_MAIN=1" else arg
                       for arg in parity.fixture_compile(build, source, obj)]
    link_command, core = parity.fixture_link(build, obj, binary)
    frozen = {str(path): parity.sha(path) for path in [source, cpp, inc, descriptor, context / "runtime_preset.h"]}
    manifest = {"schema": "loom.goal_cues_configuration_invocation/1", "sources": frozen,
                "runner_sha256": parity.sha(Path(__file__).resolve()), "core_library": str(core),
                "core_sha256": parity.sha(core), "core_build_requested": False,
                "duplicate_symbol_relaxation": False, "provider_calls": None}
    parity.write_json(output / "manifest.json", manifest)
    logged(compile_command, build, output, "fixture_compile", manifest)
    logged(link_command, build, output, "fixture_link", manifest)
    baseline = execute(binary, descriptor, build, output, False, manifest)
    results = {"configured_consumer": {"rows": len(baseline["rows"]), "assertions": baseline["assertions"]}}
    if args.builtin_probes:
        original_inc = inc.read_text(encoding="utf-8")
        pattern = re.compile(r"inline const Json& builtin_context_goal_cues_definition\(\) \{\n.*?\n\}\n", re.DOTALL)
        matches = list(pattern.finditer(original_inc))
        if len(matches) != 1:
            raise RuntimeError("expected exactly one canonical cue definition getter")
        variants = {}
        changed_confidence = copy.deepcopy(definition)
        changed_confidence["defaults"]["no_cue_confidence"] = 0.5
        variants["builtin_confidence"] = changed_confidence
        changed_revision = copy.deepcopy(definition)
        changed_revision["revision"] += 1
        variants["builtin_revision"] = changed_revision
        for name, altered_definition in variants.items():
            folder = output / name
            folder.mkdir()
            copy_dir = folder / "source/context"
            copy_dir.mkdir(parents=True)
            altered_cpp, altered_inc = copy_dir / "context_engine.cpp", copy_dir / "runtime_presets.inc"
            altered_cpp.write_bytes(cpp.read_bytes())
            changed_inc = pattern.sub(lambda _: getter(altered_definition), original_inc, count=1)
            if pattern.sub("", original_inc) != pattern.sub("", changed_inc):
                raise RuntimeError("mutation changed another getter or the legacy anchor")
            altered_inc.write_text(changed_inc, encoding="utf-8")
            altered_descriptor = folder / "context_goal_cues.pack"
            parity.write_json(altered_descriptor, altered_definition)
            variant_obj, variant_binary = folder / "context_engine_override.o", folder / "consumer"
            variant_command = [arg for arg in parity.fixture_compile(build, altered_cpp, variant_obj)
                               if arg != "-DLOOM_GOAL_CUES_PARITY_MAIN=1"]
            variant_command[1:1] = ["-I", str(context)]
            variant_link, _ = parity.fixture_link(build, obj, variant_binary)
            variant_link.insert(2, str(variant_obj))
            map_file = folder / "link.map"
            variant_link.append("-Wl,-Map=" + str(map_file))
            variant_manifest = {"schema": "loom.goal_cues_builtin_mutation_invocation/1", "variant": name,
                                "core_library": str(core), "core_sha256": manifest["core_sha256"],
                                "original_sources": frozen, "source_sha256": parity.sha(altered_cpp),
                                "altered_getter_sha256": parity.sha(altered_inc),
                                "altered_descriptor_sha256": parity.sha(altered_descriptor),
                                "other_getter_bytes_equal": True, "legacy_anchor_unchanged": True,
                                "core_build_requested": False, "duplicate_symbol_relaxation": False,
                                "production_translation_units_recompiled": ["context_engine.cpp"]}
            logged(variant_command, build, folder, "variant_compile", variant_manifest)
            logged(variant_link, build, folder, "variant_link", variant_manifest)
            link_map = map_file.read_text(encoding="utf-8", errors="strict")
            old_member = re.search(re.escape(core.name) + r"\([^\n)]*context_engine\.cpp\.o\)", link_map)
            variant_manifest["original_context_member_pulled"] = old_member is not None
            variant_manifest["link_map_sha256"] = parity.sha(map_file)
            parity.write_json(folder / "manifest.json", variant_manifest)
            if old_member is not None:
                raise RuntimeError("old ContextEngine archive member was pulled into replacement execution")
            actual = execute(variant_binary, altered_descriptor, build, folder, True, variant_manifest)
            before, after = baseline["rows"][0], actual["rows"][0]
            for field in ("type", "text", "targets", "project"):
                if before["goal"][field] != after["goal"][field]:
                    raise RuntimeError("builtin probe did not isolate recipe/identity at " + field)
            if before["goal"]["id"] == after["goal"]["id"] or before["context"]["id"] == after["context"]["id"]:
                raise RuntimeError("actual builtin recipe change retained a stale Goal/Context identity")
            if name == "builtin_revision" and before["goal"]["confidence"] != after["goal"]["confidence"]:
                raise RuntimeError("revision-only mutation changed actual confidence")
            results[name] = {"rows": len(actual["rows"]), "assertions": actual["assertions"],
                             "goal_id_changed": True, "context_id_changed": True,
                             "producer_source": after["goal"]["params"]["goal_cues"]["source"]}
    manifest["core_sha256_after"] = parity.sha(core)
    manifest["sources_after"] = {path: parity.sha(Path(path)) for path in frozen}
    parity.write_json(output / "manifest.json", manifest)
    if manifest["core_sha256_after"] != manifest["core_sha256"] or manifest["sources_after"] != frozen:
        raise RuntimeError("actual core or sources changed during consumer verification")
    parity.write_json(output / "summary.json", results)
    print(json.dumps({"results": results, "provider_calls": 0, "output_dir": str(output)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
