#!/usr/bin/env python3
"""Rebuild the real W2/W4 C ABI and consume one actual W3 producer artifact offline.

Requires a complete CMake W3 build (compile_commands.json and static archives).
The checkout and the original archives are read-only. No provider calls occur.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

W2 = "5a73a360f44626333ce6510f26261c32239de73c"
W4 = "1377e20c71f200b01416c7c87f06c3ffdafb02b6"


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--usage-policy-ref", default=W2)
    parser.add_argument("--packet-ref", default=W4)
    args = parser.parse_args()
    repo, build, output = args.repo.resolve(), args.build_dir.resolve(), args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("--output-dir must be empty; retain earlier failed/successful evidence")
    output.mkdir(parents=True, exist_ok=True)
    source = output / "source"
    original = build / "libloom_core.a"
    manifest = {"schema": "loom.w3_native_golden_consumer_build/1", "provider_calls": 0,
                "sources": {}, "objects": {}, "commands": [], "fake_abi_shims": False,
                "original_core_sha256": digest(original), "artifact_sha256": digest(args.artifact)}

    def save():
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    def run(command, log, cwd=repo):
        command = list(map(str, command))
        manifest["commands"].append({"argv": command, "cwd": str(cwd), "log": str(log)})
        save()
        result = subprocess.run(command, cwd=cwd, capture_output=True)
        log.write_bytes(result.stdout + result.stderr)
        if result.returncode:
            sys.stderr.buffer.write(result.stdout + result.stderr)
            raise subprocess.CalledProcessError(result.returncode, command)
        return result.stdout

    try:
        selected = [repo / "loom/src/context/context_engine.cpp",
                    repo / "loom/src/context/provider_vector.cpp", repo / "loom/src/chat/graph_reply.cpp"]
        groups = [("usage_policy", args.usage_policy_ref,
                   ["loom/include/loom/usage_policy.h", "loom/src/core/config.cpp",
                    "loom/src/core/config_usage_policy.cpp", "loom/src/policy/usage_policy.cpp",
                    "loom/src/policy/usage_policy_preset.inc", "loom/data/policy/usage_policy.pack"]),
                  ("packet", args.packet_ref,
                   ["loom/src/packet/packet.h", "loom/src/packet/packet.cpp", "loom/src/packet/reply.cpp",
                    "loom/src/kb/graph_packet_store.cpp", "loom/src/capi/capi_packet.cpp",
                    "loom/src/packet/METHOD_GRAPH.md", "loom/src/packet/method-graph.schema.json",
                    "loom/src/packet/tests/verify_method_graph_artifact.py",
                    "loom/tools/coordination/graph_store.py",
                    "loom/tools/structure/agentic_graph_v1/packet.py",
                    "loom/tools/structure/openrouter_runner.py"])]
        for name, ref, paths in groups:
            commit = run(["git", "rev-parse", "--verify", "--end-of-options", ref + "^{commit}"],
                         output / (name + "-resolve.log")).decode().strip()
            manifest[name + "_commit"] = commit
            for index, relative in enumerate(paths):
                blob = run(["git", "show", commit + ":" + relative],
                           output / (name + f"-extract-{index}.log"))
                target = source / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(blob)
                manifest["sources"][relative + "@" + commit] = digest(target)
                if target.suffix == ".cpp":
                    selected.append(target)
        capi_header = source / "loom/src/capi/context.h"
        shutil.copyfile(repo / "loom/src/capi/context.h", capi_header)
        manifest["sources"]["loom/src/capi/context.h@checkout"] = digest(capi_header)
        rows = json.loads((build / "compile_commands.json").read_text())
        original_command = next(row for row in rows if row["file"].replace("\\", "/")
                                .endswith("/context/context_engine.cpp"))
        flags = list(original_command["arguments"]) if "arguments" in original_command else shlex.split(original_command["command"])
        flags.remove("-c")
        flags.remove(original_command["file"])
        index = flags.index("-o")
        del flags[index:index + 2]
        old_repo = str(Path(original_command["file"]).resolve().parents[3])
        flags = [argument.replace(old_repo, str(repo)) for argument in flags]
        flags[1:1] = ["-I", str(source / "loom/include"), "-I", str(source / "loom/src")]
        flags += ["-O0", "-g0", "-fPIC", "-Werror"]
        objects = []
        for index, item in enumerate(selected):
            obj = output / f"component_{index}.o"
            run(flags + ["-c", str(item), "-o", str(obj)], output / f"compile-{index}.log", build)
            objects.append(obj)
            manifest["sources"][str(item)] = digest(item)
            manifest["objects"][obj.name] = {"sha256": digest(obj), "source": str(item)}
        archive = output / "libloom_w3_w2_w4_actual.a"
        shutil.copyfile(original, archive)
        members = run(["ar", "t", archive], output / "archive-before.txt").decode().splitlines()
        required = ["context_engine.cpp.o", "provider_vector.cpp.o", "graph_reply.cpp.o",
                    "config.cpp.o", "graph_packet_store.cpp.o"]
        optional = ["config_usage_policy.cpp.o", "usage_policy.cpp.o", "packet.cpp.o",
                    "reply.cpp.o", "capi_packet.cpp.o"]
        for member in required:
            if members.count(member) != 1:
                raise ValueError("missing or ambiguous replaced archive member: " + member)
        removed = required + [name for name in optional if name in members]
        if any(members.count(member) != 1 for member in removed):
            raise ValueError("ambiguous optional archive member")
        manifest["removed_archive_members"] = removed
        run(["ar", "d", archive, *removed], output / "archive-remove.log")
        run(["ar", "r", archive, *objects], output / "archive-add.log")
        run(["ar", "t", archive], output / "archive-after.txt")
        library = output / "libloom_w3_w2_w4_actual.so"
        vendors = [build / "libloom_sqlite3_amalgamation.a", build / "libloom_miniz.a"]
        manifest["vendor_archives"] = {item.name: digest(item) for item in vendors}
        run([flags[0], "-shared", "-fuse-ld=gold", "-Wl,--no-map-whole-files", "-Wl,--no-undefined",
             "-Wl,--whole-archive", archive, "-Wl,--no-whole-archive", *vendors,
             "-lssl", "-lcrypto", "-pthread", "-ldl", "-lm", "-o", library], output / "link.log")
        manifest["library_sha256"] = digest(library)
        manifest["final_core_sha256"] = digest(original)
        if manifest["final_core_sha256"] != manifest["original_core_sha256"]:
            raise ValueError("original core archive changed during isolated build")
        run([sys.executable, source / "loom/src/packet/tests/verify_method_graph_artifact.py",
             "--library", library, "--artifact", args.artifact.resolve(),
             "--evidence-dir", output / "verification"], output / "consumer.log")
        manifest["passed"] = True
        save()
        print(json.dumps({"passed": True, "evidence": str(output / "verification"),
                          "library_sha256": manifest["library_sha256"]}))
    except Exception as error:
        manifest["passed"] = False
        manifest["error"] = str(error)
        save()
        raise


if __name__ == "__main__":
    main()
