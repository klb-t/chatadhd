#!/opt/codex/runtimes/codex-primary-runtime/dependencies/python/bin/python3
"""Reuse only independently verified current-main core objects; compile everything else.

No test/CLI/server objects or binaries are restored. An input, compiler,
command or cache-object mismatch falls through to the real compiler.
"""
import hashlib
import gzip
import json
import os
from pathlib import Path
import shlex
import shutil
import sys
import tempfile

EVIDENCE = Path(__file__).resolve().parent
MANIFEST = EVIDENCE / "rebased-cache.manifest.json.gz"
MANIFEST_SHA256 = "066537d09632bb7cb88a87126cf8c26656ba350dac054de1541e030ebf6904e6"


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def normal(argv, base):
    out, index = [], 0
    while index < len(argv):
        value = argv[index]
        if value in ("-o", "-MT", "-MF", "-MQ"):
            index += 2
            continue
        if value in ("-MD", "-MMD", "-O0"):
            index += 1
            continue
        # A compiled string/value is not an include/source path. Never hide a
        # different literal workspace path in a -D definition by normalization.
        if value.startswith("-D") and str(base) in value:
            raise ValueError("A literal compiler definition contains a workspace path")
        out.append(value.replace(str(base), "<SOURCE>"))
        index += 1
    return out


def load_manifest():
    payload = gzip.decompress(MANIFEST.read_bytes())
    if hashlib.sha256(payload).hexdigest() != MANIFEST_SHA256:
        raise ValueError("Cache manifest hash differs from its verified receipt")
    return json.loads(payload)


def check_inputs(manifest):
    root, reference = Path(manifest["root"]), Path(manifest["reference"])
    for relative, expected in manifest["repo_inputs"].items():
        if not relative.startswith(("loom/src/", "loom/include/", "loom/third_party/")):
            continue  # Native test objects are deliberately never reused.
        if digest(root / relative) != expected or digest(reference / relative) != expected:
            raise ValueError("Native source/header input changed: " + relative)
    for relative, expected in manifest["cmake_inputs"].items():
        if digest(root / relative) != expected or digest(reference / relative) != expected:
            raise ValueError("CMake input changed: " + relative)
    for path, expected in manifest["system_inputs"].items():
        if digest(path) != expected:
            raise ValueError("System compile dependency changed: " + path)


def check_object(entry):
    obj = Path(entry["reference_object"])
    if obj.stat().st_size != entry["bytes"] or digest(obj) != entry["sha256"]:
        raise ValueError("Reference object changed: " + str(obj))
    if digest(entry["compiler"]) != entry["compiler_sha256"]:
        raise ValueError("Compiler binary changed")
    return obj


def flag_value(argv, flag):
    return argv[argv.index(flag) + 1]


def prepare_cached_object(args, manifest):
    if "-c" not in args or "-o" not in args:
        return None
    root = Path(manifest["root"])
    source = Path(flag_value(args, "-c")).resolve()
    if not source.is_relative_to(root):
        return None
    relative = str(source.relative_to(root))
    if not relative.startswith("loom/src/"):
        return None
    entry = manifest["objects"].get(relative)
    if not entry or entry["ninja_dependency_state"] != "VALID":
        return None
    if normal(args, root) != entry["normalized_command"]:
        return None
    if Path(args[0]).resolve() != Path(entry["compiler"]):
        return None
    check_inputs(manifest)
    return entry, check_object(entry), Path(flag_value(args, "-o"))


def make_dependency_file(args, manifest, entry, output):
    if "-MF" not in args:
        return
    root = Path(manifest["root"])
    paths = [root / relative for relative in manifest["repo_inputs"]
             if relative.startswith(("loom/src/", "loom/include/", "loom/third_party/"))]
    paths += [root / relative for relative in manifest["cmake_inputs"]]
    paths += [Path(path) for path in manifest["system_inputs"]]
    paths += [MANIFEST, Path(__file__).resolve(), Path(entry["reference_object"]), Path(entry["compiler"])]
    def escape(path):
        return str(path).replace("\\", "\\\\").replace(" ", "\\ ").replace("#", "\\#").replace("$", "$$")
    Path(flag_value(args, "-MF")).write_text(escape(output) + ": " + " ".join(escape(path) for path in paths) + "\n")


def main():
    args = sys.argv[1:]
    if args == ["--check"]:
        manifest = load_manifest()
        check_inputs(manifest)
        entries = [entry for relative, entry in manifest["objects"].items() if relative.startswith("loom/src/")]
        for entry in entries:
            check_object(entry)
            if normal(shlex.split(entry["root_command"]), Path(manifest["root"])) != entry["normalized_command"]:
                raise ValueError("Recorded root compile command differs")
            if normal(shlex.split(entry["reference_command"]), Path(manifest["reference"])) != entry["normalized_command"]:
                raise ValueError("Recorded reference compile command differs")
        print(json.dumps({"verified_core_objects": len(entries), "test_objects_enabled": 0,
                          "manifest_sha256": MANIFEST_SHA256, "reference_commit": manifest["reference_commit"]}))
        return
    if not args:
        raise SystemExit("Pass --check or the real compiler command")
    try:
        manifest = load_manifest()
        cached = prepare_cached_object(args, manifest)
        if cached:
            entry, obj, output = cached
            output.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=output.parent, prefix=output.name + ".cache-", delete=False) as stream:
                temporary = Path(stream.name)
            try:
                shutil.copyfile(obj, temporary)
                if digest(temporary) != entry["sha256"]:
                    raise ValueError("Object changed while copying")
                os.replace(temporary, output)
                make_dependency_file(args, manifest, entry, output)
            finally:
                temporary.unlink(missing_ok=True)
            with (EVIDENCE / "rebased-cache.hits.txt").open("a") as stream:
                stream.write(entry["source"] + " " + entry["sha256"] + "\n")
            return
    except (OSError, ValueError, KeyError, IndexError, TypeError) as error:
        print("[verified-cache] compile fresh: " + str(error), file=sys.stderr)
    os.execv(args[0], args)


if __name__ == "__main__":
    main()
