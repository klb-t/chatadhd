#!/usr/bin/env python3
"""Offline accounting of saved research ledgers; never opens keys or holdouts.

Only docs/research Git blobs, ledger.json/manifest.json ZIP members and the
response files referenced by those ledgers are considered. No provider access.
Copied ZIP/loose/branch attempts are counted once, not once per artifact.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from decimal import Decimal
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import subprocess
import zipfile


LIVE_SCHEMAS = {"loom.openrouter_ledger/1", "loom.jev_ledger/1", "loom.jev_recipe_ledger/1"}


def safe_name(name):
    return not any(part in name.lower() for part in ("holdout", "sealed"))


def git(repo, *args):
    return subprocess.check_output(["git", "-C", repo, *args])


def amount(value):
    result = Decimal(str(value))
    if not result.is_finite() or result < 0:
        raise ValueError("Invalid saved monetary amount")
    return result


def audit(repo, refs):
    snapshots = []
    blobs = {}
    for ref in refs:
        if not safe_name(ref) or ref.startswith("-"):
            raise ValueError("Ref is outside the non-sealed audit scope")
        commit = git(repo, "rev-parse", "--verify", ref + "^{commit}").decode().strip()
        snapshots.append({"ref": ref, "commit": commit})
        tree = git(repo, "ls-tree", "-rz", commit, "--", "docs/research")
        for entry in tree.split(b"\0"):
            if not entry:
                continue
            meta, raw_path = entry.split(b"\t", 1)
            path = raw_path.decode()
            if not safe_name(path):
                continue
            name = PurePosixPath(path).name
            if name in {"ledger.json", "manifest.json"} or path.endswith((".zip", ".response.bin")):
                oid = meta.split()[2].decode()
                blobs.setdefault(oid, set()).add(path)

    documents = []
    response_bytes = {}
    manifests = {}
    for oid, paths in sorted(blobs.items()):
        data = git(repo, "cat-file", "blob", oid)
        path = sorted(paths)[0]
        if path.endswith(".zip"):
            with zipfile.ZipFile(io.BytesIO(data)) as archive:
                members = {}
                for name in archive.namelist():
                    if not safe_name(name):
                        continue
                    basename = PurePosixPath(name).name
                    if basename in {"ledger.json", "manifest.json"} or name.endswith(".response.bin"):
                        members[name] = archive.read(name)
                for name, content in members.items():
                    location = path + "!" + name
                    if name.endswith(".response.bin"):
                        response_bytes[hashlib.sha256(content).hexdigest()] = content
                    elif PurePosixPath(name).name == "manifest.json":
                        manifests[location] = json.loads(content)
                    else:
                        documents.append((location, content, json.loads(content), oid))
        elif path.endswith(".response.bin"):
            response_bytes[hashlib.sha256(data).hexdigest()] = data
        elif PurePosixPath(path).name == "manifest.json":
            for alias in paths:
                manifests[alias] = json.loads(data)
        else:
            documents.append((path, data, json.loads(data), oid))

    attempts = {}
    ignored_scripted_ledgers = 0
    ledger_receipts = []
    for location, content, ledger, oid in sorted(documents):
        if ledger.get("schema") not in LIVE_SCHEMAS or not ledger.get("attempts"):
            continue
        if "scripted" in location.lower():
            ignored_scripted_ledgers += 1
            continue
        manifest = manifests.get(location.rsplit("/", 1)[0] + "/manifest.json", {})
        requested = {row["id"]: row.get("body", {}).get("model")
                     for row in manifest.get("requests", [])}
        ledger_receipts.append({"location": location, "git_container_blob": oid,
                                "sha256": hashlib.sha256(content).hexdigest(),
                                "attempt_records": len(ledger["attempts"])})
        for row in ledger["attempts"]:
            # Request hash alone would incorrectly merge repeated paid calls.
            identity = (ledger["experiment_id"], row["request_hash"], row["started_at"])
            cost = row.get("reported_cost_usd")
            cost = str(amount(cost)) if cost is not None else None
            response_hash = row.get("response_sha256")
            record = {
                "experiment_id": ledger["experiment_id"], "attempt_id": row["id"],
                "started_at": row["started_at"], "request_sha256": row["request_hash"],
                "response_sha256": response_hash, "state": row["state"],
                "reported_cost_usd": cost, "reservation_usd": str(amount(row["reservation_usd"])),
                "requested_model": requested.get(row["id"]),
                "returned_model": None, "returned_provider": None,
                "raw_hash_verified": False, "raw_cost_matches": None,
            }
            if response_hash in response_bytes:
                raw = json.loads(response_bytes[response_hash])
                record.update(raw_hash_verified=True, returned_model=raw.get("model"),
                              returned_provider=raw.get("provider"))
                raw_cost = raw.get("usage", {}).get("cost")
                if raw_cost is not None and cost is not None:
                    record["raw_cost_matches"] = amount(raw_cost) == amount(cost)
            if identity in attempts:
                existing = attempts[identity]
                if {k: v for k, v in existing.items() if k != "ledger_locations"} != record:
                    raise ValueError("Conflicting copies of an attempt: " + str(identity))
                existing["ledger_locations"].append(location)
            else:
                attempts[identity] = {**record, "ledger_locations": [location]}

    def summarize(rows):
        models = Counter(row["returned_model"] or "unavailable" for row in rows)
        requested = Counter(row["requested_model"] or "not_in_adjacent_manifest" for row in rows)
        known = sum((amount(row["reported_cost_usd"]) for row in rows
                     if row["reported_cost_usd"] is not None), Decimal(0))
        unknown = [row for row in rows if row["reported_cost_usd"] is None]
        return {"attempts": len(rows), "known_reported_usd": str(known),
                "unknown_cost_attempts": len(unknown),
                "unknown_attempt_reservations_usd": str(sum((amount(row["reservation_usd"])
                                                            for row in unknown), Decimal(0))),
                "returned_models": dict(sorted(models.items())),
                "requested_models": dict(sorted(requested.items())),
                "raw_hash_verified_attempts": sum(row["raw_hash_verified"] for row in rows),
                "raw_cost_matches": sum(row["raw_cost_matches"] is True for row in rows),
                "raw_cost_mismatches": sum(row["raw_cost_matches"] is False for row in rows)}

    rows = sorted(attempts.values(), key=lambda row: (row["started_at"], row["request_sha256"]))
    days = defaultdict(list)
    experiments = defaultdict(list)
    for row in rows:
        days[row["started_at"][:10]].append(row)
        experiments[row["experiment_id"]].append(row)
    return {
        "schema": "loom.research_saved_cost_audit/1", "network_calls": 0,
        "scope": "saved ledgers in specified non-sealed Git snapshots; no provider invoice or key identity audit",
        "deduplication": "experiment_id + request_sha256 + started_at; all copied ledger locations retained",
        "snapshots": snapshots, "unique_selected_git_blobs": len(blobs),
        "ignored_scripted_ledger_documents": ignored_scripted_ledgers,
        "summary": summarize(rows),
        "by_utc_day": {key: summarize(value) for key, value in sorted(days.items())},
        "by_experiment": {key: summarize(value) for key, value in sorted(experiments.items())},
        "ledger_receipts": ledger_receipts, "attempts": rows,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".")
    parser.add_argument("--ref", action="append", help="Repeat to pin the audited refs/commit IDs")
    parser.add_argument("--remote-heads", help="Saved git ls-remote --heads output; pins actual remote names and commits")
    parser.add_argument("--output", required=True, help="New JSON file; existing files are not overwritten")
    args = parser.parse_args()
    refs = args.ref
    if args.remote_heads:
        if refs:
            parser.error("--ref and --remote-heads are alternative snapshot inputs")
        heads_bytes = Path(args.remote_heads).read_bytes()
        refs = []
        for line in heads_bytes.decode().splitlines():
            commit, ref = line.split()
            if not ref.startswith("refs/heads/") or not safe_name(ref):
                continue
            if len(commit) != 40 or any(char not in "0123456789abcdef" for char in commit):
                parser.error("Invalid commit ID in saved remote heads")
            # Different branch names can point at the same commit. Preserve both
            # names in snapshots without counting their attempts twice.
            refs.append((commit, ref))
        if not refs:
            parser.error("No non-sealed remote heads found")
        result = audit(args.repo, [commit for commit, ref in refs])
        result["snapshots"] = [{"ref": ref, "commit": commit} for commit, ref in refs]
        result["snapshot_source"] = {
            "kind": "saved_git_ls_remote_heads",
            "sha256": hashlib.sha256(heads_bytes).hexdigest(),
            "remote_identity_checked_here": False,
        }
    else:
        if not refs:
            refs = [ref for ref in git(args.repo, "for-each-ref", "--format=%(refname)",
                                      "refs/remotes/origin").decode().splitlines()
                    if safe_name(ref) and not ref.endswith("/HEAD")]
        result = audit(args.repo, refs)
        result["snapshot_source"] = {
            "kind": "explicit_refs_or_local_remote_tracking_refs",
            "remote_identity_checked_here": False,
        }
    with open(args.output, "x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    print(json.dumps({"summary": result["summary"], "by_utc_day": result["by_utc_day"]}, indent=2))


if __name__ == "__main__":
    main()
