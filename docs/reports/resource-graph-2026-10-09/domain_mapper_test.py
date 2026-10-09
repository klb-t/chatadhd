"""Apply optional pure-mapper patch in isolation; compile only changed TUs.

Requires the previously built pinned-main static core; no repository mutation,
model call, runtime or database is involved in the tested mapper invocation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

MAIN_BASE = "9e20f99ab27e7cd45e1892f83bf60fbe60db3de9"
B_BASE = "b20c0d8ac37934e1600c3b9216492fd524220941"
EXISTING = ("loom/src/import/export_internal.h", "loom/src/import/export_anthropic.cpp")


def run(build_directory, output_directory=None, compiler="c++"):
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    build = Path(build_directory).resolve()
    archive = build / "libloom_core.a"
    if not archive.is_file():
        raise FileNotFoundError("build pinned-main loom_core before checking this optional patch")
    with tempfile.TemporaryDirectory(prefix="resource-domain-hook-") as directory:
        root = Path(directory)
        selected = None
        for base in (MAIN_BASE, B_BASE):
            target = root / base
            for relative in EXISTING:
                output = target / relative
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(subprocess.check_output(["git", "show", base + ":" + relative], cwd=repo))
            subprocess.run(["git", "apply", "--check", str(here / "domain_mapper.patch")], cwd=target, check=True)
            subprocess.run(["git", "apply", str(here / "domain_mapper.patch")], cwd=target, check=True)
            print("patch applies:", base, flush=True)
            selected = target if base == MAIN_BASE else selected
        binary = root / "domain_mapper_test"
        command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-pthread",
                   "-I" + str(selected / "loom/include"), "-I" + str(selected / "loom/src"),
                   "-I" + str(repo / "loom/include"), "-I" + str(repo / "loom/src"),
                   "-isystem", str(repo / "loom/third_party/nlohmann"),
                   str(here / "domain_mapper_test.cpp"),
                   str(selected / "loom/src/import/export_mapping.cpp"),
                   str(selected / "loom/src/import/export_anthropic.cpp"), str(archive),
                   str(build / "libloom_sqlite3_amalgamation.a"), str(build / "libloom_miniz.a"),
                   "-lssl", "-lcrypto", "-ldl", "-lm", "-o", str(binary)]
        subprocess.run(command, check=True)
        work = root / "execution"
        work.mkdir()
        regression = subprocess.check_output([str(binary)], cwd=work, text=True)
        if list(work.iterdir()):
            raise AssertionError("the pure mapper created a database or another file")
        raw = {"uuid": "safe-fixture", "name": "fixture", "chat_messages": [], "unknown": {"preserved": True}}
        cli = subprocess.check_output([str(binary), "anthropic"], input=json.dumps(raw), text=True, cwd=work)
        result = json.loads(cli)
        assert result["raw"] == raw and result["database_required"] is False
        assert not list(work.iterdir())
        receipt = {"base": MAIN_BASE, "patch_applies_to": [MAIN_BASE, B_BASE],
                   "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                   "regression": json.loads(regression), "stdin_stdout_probe": "passed",
                   "no_database_or_files_created": True, "full_rebuild": False,
                   "patch_active_in_repository": False}
        if output_directory:
            import shutil
            output = Path(output_directory)
            output.mkdir(parents=True, exist_ok=True)
            shutil.copy2(binary, output / "domain_mapper_test")
            (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(json.dumps(receipt, indent=2))
        return receipt


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-directory", required=True)
    parser.add_argument("--output-directory")
    parser.add_argument("--compiler", default="c++")
    options = parser.parse_args()
    run(options.build_directory, options.output_directory, options.compiler)
