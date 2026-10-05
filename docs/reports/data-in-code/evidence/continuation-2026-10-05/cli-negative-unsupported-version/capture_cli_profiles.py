#!/usr/bin/env python3
"""Capture real CLI profile outputs and compare stable existing-domain results."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def capture(binary, directory, reference):
    directory.mkdir(parents=True, exist_ok=False)
    commands = []
    with tempfile.TemporaryDirectory(prefix="loom-profile-parity-") as temporary:
        data_root = Path(temporary) / "data"

        def run(arguments, filename):
            argv = [str(binary), "--data-dir", str(data_root), *arguments]
            result = subprocess.run(argv, capture_output=True, check=False)
            (directory / (filename + ".stdout")).write_bytes(result.stdout)
            (directory / (filename + ".stderr")).write_bytes(result.stderr)
            commands.append({"argv": argv, "exit": result.returncode,
                             "stdout_sha256": digest(result.stdout),
                             "stderr_sha256": digest(result.stderr)})
            if result.returncode:
                raise RuntimeError(f"{filename}: exit {result.returncode}")
            return result.stdout

        help_output = run(["--help"], "help")
        version_output = run(["--version"], "version")
        domains = json.loads(run(["--json", "profile", "list"], "domains"))
        profiles = {}
        for domain in domains:
            document = json.loads(run(["--json", "profile", "inspect", domain], domain))
            profiles[domain] = {key: document[key]
                                for key in ("domain", "revision", "hash", "values", "value_schema")}
        # All inputs are fresh synthetic roots. Record actual creation by the
        # historical CLI; never read a user's data or claim that it was read-only.
        files = sorted(str(path.relative_to(data_root))
                       for path in data_root.rglob("*") if path.is_file())
    result = {"schema": "loom.cli_profile_parity_capture/1", "source_ref": reference,
              "binary": {"path": str(binary), "sha256": digest(binary.read_bytes())},
              "domains": domains, "profiles": profiles,
              "help_sha256": digest(help_output), "help_bytes": len(help_output),
              "version_sha256": digest(version_output), "version_bytes": len(version_output),
              "synthetic_root_created_files": files, "commands": commands}
    (directory / "receipt.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result


def compare(before, after):
    missing = sorted(set(before["domains"]) - set(after["domains"]))
    differences = [domain for domain in before["domains"]
                   if domain not in after["profiles"]
                   or before["profiles"][domain] != after["profiles"][domain]]
    same_help = before["help_sha256"] == after["help_sha256"]
    same_version = before["version_sha256"] == after["version_sha256"]
    return {"schema": "loom.cli_profile_parity_comparison/1",
            "valid": not missing and not differences and same_help and same_version,
            "shared_domains": len(before["domains"]), "missing_domains": missing,
            "changed_existing_domains": differences,
            "added_domains": sorted(set(after["domains"]) - set(before["domains"])),
            "help_byte_identical": same_help, "version_byte_identical": same_version,
            "before_created_files": before["synthetic_root_created_files"],
            "after_created_files": after["synthetic_root_created_files"]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-ref", required=True)
    parser.add_argument("--compare", type=Path)
    args = parser.parse_args()
    result = capture(args.binary.resolve(), args.output, args.source_ref)
    if args.compare:
        comparison = compare(json.loads(args.compare.read_text()), result)
        (args.output / "comparison.json").write_text(json.dumps(comparison, indent=2) + "\n")
        print(json.dumps(comparison))
        if not comparison["valid"]:
            raise SystemExit(1)
    else:
        print(json.dumps({"domains": len(result["domains"]), "help_bytes": result["help_bytes"],
                          "synthetic_created_files": result["synthetic_root_created_files"]}))


if __name__ == "__main__":
    main()
