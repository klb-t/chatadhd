#!/usr/bin/env python3
"""Read original branch metadata or restore PRIVATE original history.

Original commit metadata contains a confirmed private contact address.
This utility never creates public tags, pushes refs or publishes originals.
Public history requires a separately verified sanitized commit mapping.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


class SafetyError(RuntimeError):
    pass


def git(*args: str) -> str:
    result = subprocess.run(
        ["git", *args], check=False, text=True, capture_output=True,
        env={**os.environ, "LC_ALL": "C"},
    )
    if result.returncode:
        raise SafetyError(result.stderr.strip() or result.stdout.strip())
    return result.stdout


def remote_refs(remote: str) -> dict[str, str]:
    if remote.startswith("-"):
        raise SafetyError("Remote may not start with an option prefix")
    refs = {}
    for line in git("ls-remote", "--heads", "--tags", remote).splitlines():
        sha, ref = line.split("\t", 1)
        if not ref.endswith("^{}"):
            refs[ref] = sha
    return refs


def load_inventory(path: Path, repository: str) -> list[dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    rows = data["repositories"][repository]["branches"]
    names = set()
    for row in rows:
        name = row["name"]
        if name in names:
            raise SafetyError("Duplicate inventory branch: " + name)
        names.add(name)
        if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", row["sha"]):
            raise SafetyError("Invalid original commit identity for " + name)
        git("check-ref-format", "refs/heads/" + name)
        if row.get("public_original_history_allowed") is not False:
            raise SafetyError("Inventory must prohibit public original-history publication")
    return rows


def verify_bundle(path: Path | None, expected_hash: str | None,
                  rows: list[dict]) -> str:
    if path is None or not expected_hash:
        raise SafetyError("Supply --bundle and --bundle-sha256 for the private backup")
    if not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
        raise SafetyError("Bundle SHA-256 must be a lowercase 64-character digest")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        signature = source.readline()
        if not signature.startswith((b"# v2 git bundle", b"# v3 git bundle")):
            raise SafetyError("Not a supported Git bundle")
        header_size = len(signature)
        while True:
            line = source.readline()
            header_size += len(line)
            if not line or header_size > 8 * 1024 * 1024:
                raise SafetyError("Invalid or oversized Git bundle header")
            if line.startswith(b"-"):
                raise SafetyError("Bundle has prerequisites; use a self-contained full backup")
            if line in {b"\n", b"\r\n"}:
                break
        source.seek(0)
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    actual = digest.hexdigest()
    if actual != expected_hash:
        raise SafetyError("Private bundle SHA-256 mismatch")
    git("bundle", "verify", str(path.resolve()))
    saved_tips = {
        line.split(" ", 1)[0]
        for line in git("bundle", "list-heads", str(path.resolve())).splitlines()
    }
    missing = [row["name"] for row in rows if row["sha"] not in saved_tips]
    if missing:
        raise SafetyError("Backup does not pin all original tips: " + ", ".join(missing))
    return actual


def plan(rows: list[dict], refs: dict[str, str]) -> dict:
    return {
        "mode": "read_only_metadata",
        "public_original_history_allowed": False,
        "public_ref_changes": "not_performed",
        "warning": "Original history contains private contact metadata. "
                   "Use verified private backups for exact restoration. "
                   "Public refs must target separately verified sanitized commits.",
        "branches": [
            {
                **row,
                "current_remote_sha": refs.get("refs/heads/" + row["name"]),
                "matches_original_snapshot": (
                    refs.get("refs/heads/" + row["name"]) == row["sha"]
                ),
            }
            for row in rows
        ],
    }


def restore_private(args: argparse.Namespace, rows: list[dict]) -> dict:
    digest = verify_bundle(args.bundle, args.bundle_sha256, rows)
    if args.destination is None:
        raise SafetyError("restore-private requires an explicit --destination")
    destination = args.destination.expanduser().resolve()
    if destination.exists():
        raise SafetyError("Destination must not already exist; no checkout is overwritten")
    if not destination.parent.is_dir():
        raise SafetyError("Create the private parent directory before restoration")
    # Clone only the verified local bundle. No network requests are made.
    destination.mkdir(mode=0o700)
    git("clone", "--mirror", "--", str(args.bundle.resolve()), str(destination))
    # The new private recovery mirror deliberately has no fetch/push remote.
    git("--git-dir=" + str(destination), "remote", "remove", "origin")
    for row in rows:
        git("--git-dir=" + str(destination), "cat-file", "-e", row["sha"] + "^{commit}")
        git("--git-dir=" + str(destination), "update-ref",
            "refs/heads/" + row["name"], row["sha"])
    destination.chmod(0o700)
    refs = {
        ref: sha
        for sha, ref in (
            line.split(" ", 1)
            for line in git("--git-dir=" + str(destination), "for-each-ref",
                            "--format=%(objectname) %(refname)", "refs/heads/").splitlines()
        )
    }
    if any(refs.get("refs/heads/" + row["name"]) != row["sha"] for row in rows):
        raise SafetyError("Restored private branch identity mismatch")
    marker = destination / "PRIVATE_HISTORY_DO_NOT_PUBLISH.json"
    marker.write_text(json.dumps({
        "private_original_history": True,
        "source_bundle_sha256": digest,
        "restored_original_refs": len(rows),
        "public_push_prohibited": True,
        "isolated_evaluation": "Original refs preserved; contents were not inspected.",
    }, indent=2) + "\n", encoding="utf-8")
    marker.chmod(0o600)
    return {
        "mode": "private_local_restoration",
        "destination": str(destination),
        "source_bundle_sha256": digest,
        "original_refs_verified": len(rows),
        "network_calls": 0,
        "remotes": [],
        "warning": "Do not publish this original-history mirror or tag its "
                   "historical commits publicly.",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs="?", default="plan",
                        choices=["plan", "restore-private"])
    parser.add_argument("--inventory", type=Path,
                        default=Path(__file__).resolve().parents[1]
                        / "docs/archive/branch-inventory-2026-10-02.json")
    parser.add_argument("--repository", default="klb-t/chatadhd")
    parser.add_argument("--remote", default="origin")
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--bundle-sha256")
    parser.add_argument("--destination", type=Path)
    args = parser.parse_args(argv)
    try:
        rows = load_inventory(args.inventory, args.repository)
        result = (plan(rows, remote_refs(args.remote)) if args.command == "plan"
                  else restore_private(args, rows))
    except (SafetyError, OSError, ValueError, KeyError) as error:
        print("Stopped safely: " + str(error), file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
