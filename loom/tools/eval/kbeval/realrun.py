"""Real repository runs with explicit, auditable input boundaries.

Source dates in present-day documents are not evidence of availability at T.
Historical inputs therefore come exclusively from a Git snapshot on HEAD's
first-parent history, never from the working tree or another branch.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
from datetime import date, datetime, time, timezone
from pathlib import Path, PurePosixPath
from typing import Any

from .common import new_workspace, run_knowledge


class EvaluationUnavailable(RuntimeError):
    """The requested measurement cannot be made without relaxing isolation."""


TEXT_SUFFIXES = {
    ".md", ".txt", ".rst", ".json", ".jsonl", ".yaml", ".yml", ".toml",
    ".csv", ".tsv", ".py", ".cpp", ".cc", ".c", ".h", ".hpp", ".js",
    ".jsx", ".ts", ".tsx", ".sh", ".cmake", ".ini", ".xml", ".kt", ".java",
}
EXCLUDED_PARTS = {
    "eval", "evaluation", "tests", "test", "fixtures", "selfhost", "generated",
    "predictions", "products", "results", "build", "dist", "node_modules",
    "vendor", "third_party", "__pycache__",
}
MAX_TEXT_BYTES = 16 * 1024 * 1024


def git(repo: Path, *args: str) -> bytes:
    # Replacement refs can silently substitute an unrelated tree/history.
    env = {**os.environ, "GIT_NO_REPLACE_OBJECTS": "1"}
    p = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, env=env, timeout=120)
    if p.returncode:
        raise EvaluationUnavailable(f"git {' '.join(args[:2])} failed: {p.stderr.decode(errors='replace').strip()}")
    return p.stdout


def cutoff_timestamp(cut: str) -> int:
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", cut):
        raise ValueError("cut must be YYYY-MM-DD")
    day = date.fromisoformat(cut)
    return int(datetime.combine(day, time(23, 59, 59), timezone.utc).timestamp())


def select_snapshot(repo: Path, cut: str | None = None) -> str:
    """Most recent mainline snapshot with no post-cutoff dated ancestors.

Git timestamps establish the available repository record, not independently
attested historical truth. Both author and committer dates are checked. A
merge containing a future-dated ancestor is ineligible even if backdated.
"""
    head = git(repo, "rev-parse", "--verify", "HEAD^{commit}").decode().strip()
    if cut is None:
        return head
    limit = cutoff_timestamp(cut)
    if git(repo, "rev-parse", "--is-shallow-repository").decode().strip() == "true":
        raise EvaluationUnavailable("temporal holdout requires complete ancestry; this repository is shallow")
    records = git(repo, "log", "--topo-order", "--format=%H %at %ct %P", head).decode().splitlines()
    eligible: dict[str, bool] = {}
    for row in reversed(records):
        sha, author, committed, *parents = row.split()
        eligible[sha] = max(int(author), int(committed)) <= limit and all(eligible.get(p, False) for p in parents)
    for sha in git(repo, "rev-list", "--first-parent", head).decode().splitlines():
        if eligible.get(sha, False):
            return sha
    raise EvaluationUnavailable(f"no verifiable snapshot on HEAD's mainline exists on or before {cut} (UTC)")


def excluded_path(path: str) -> str | None:
    p = PurePosixPath(path)
    parts = [x.lower() for x in p.parts]
    name = parts[-1] if parts else ""
    if p.is_absolute() or not parts or ".." in parts or "\\" in path:
        return "unsafe_path"
    if any(x.startswith(".") for x in parts):
        return "hidden"
    if any(x in EXCLUDED_PARTS or x.startswith("real-holdout") for x in parts):
        return "evaluation_or_generated"
    if any(re.search(r"(?:ground[_-]?truth|answer[_-]?key|scorecard|selfhost|holdout)", x) for x in parts):
        return "evaluation_or_generated"
    if name in {"self.md", "backlog.md"} or name.startswith(("dossier_", "extrapolated_")):
        return "materialized_product"
    if parts[:2] == ["loom", "data"]:
        return "policy_pack_not_source"
    if p.suffix.lower() not in TEXT_SUFFIXES and name not in {"cmakelists.txt", "makefile", "dockerfile", "readme"}:
        return "unsupported_or_binary"
    return None


def tree_entries(repo: Path, revision: str) -> list[tuple[str, str, str, str]]:
    entries = []
    for row in git(repo, "ls-tree", "-rz", revision).split(b"\0"):
        if not row:
            continue
        metadata, path = row.split(b"\t", 1)
        mode, kind, oid = metadata.decode("ascii").split()
        entries.append((mode, kind, oid, path.decode("utf-8", errors="surrogateescape")))
    return entries


def stage_snapshot(repo: Path, destination: Path, cut: str | None = None) -> dict[str, Any]:
    """Copy allowed blobs; never inspect excluded file contents or symlink targets."""
    revision = select_snapshot(repo, cut)
    entries = tree_entries(repo, revision)
    generated_dirs = {
        str(PurePosixPath(p).parent) for _, _, _, p in entries
        if PurePosixPath(p).name == ".loom-archive"
    }
    destination.mkdir(parents=True, exist_ok=False)
    included = []
    exclusions: dict[str, int] = {}
    for mode, kind, oid, path in entries:
        reason = excluded_path(path)
        if any(parent == "." or path.startswith(parent + "/") for parent in generated_dirs):
            reason = "marked_generated_directory"
        if kind != "blob" or mode not in {"100644", "100755"}:
            reason = "nonregular_entry"
        if not reason:
            size = int(git(repo, "cat-file", "-s", oid))
            if size > MAX_TEXT_BYTES:
                reason = "oversize"
        if reason:
            exclusions[reason] = exclusions.get(reason, 0) + 1
            continue
        content = git(repo, "cat-file", "blob", oid)
        try:
            content.decode("utf-8")
        except UnicodeDecodeError:
            exclusions["non_utf8"] = exclusions.get("non_utf8", 0) + 1
            continue
        if b"\0" in content or content.startswith(b"version https://git-lfs.github.com/spec/v1"):
            exclusions["binary_or_lfs_pointer"] = exclusions.get("binary_or_lfs_pointer", 0) + 1
            continue
        target = destination.joinpath(*PurePosixPath(path).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
        included.append({"path": path, "git_blob": oid, "sha256": hashlib.sha256(content).hexdigest(), "bytes": len(content)})
    if not included:
        raise EvaluationUnavailable("selected snapshot contains no eligible, tracked UTF-8 sources")
    return {
        "revision": revision, "cut": cut, "cut_timezone": "UTC" if cut else None,
        "basis": "Git snapshot; both author and committer dates, including ancestors, <= cutoff" if cut else "tracked HEAD snapshot",
        "working_tree_included": False, "other_refs_included": False,
        "files": included, "excluded_counts": exclusions,
        "limitations": ["Git timestamps are repository evidence, not independent historical attestation.",
                        "Archives, large files, untracked files, policy packs and generated/evaluation artifacts are excluded."],
    }


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def copy_products(source: Path, destination: Path, manifest: dict[str, Any], result: dict[str, Any]) -> None:
    if destination.exists():
        raise FileExistsError(f"products destination already exists: {destination}; choose a new directory")
    if not source.is_dir():
        raise EvaluationUnavailable("pipeline reported success without a products directory")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination)
    # These are metadata, not future source inputs. The directory is marked as
    # generated as well as excluded by default at common selfhost paths.
    (destination / ".loom-archive").touch()
    write_json(destination / "input_manifest.json", manifest)
    write_json(destination / "run_result.json", result)


def selfhost(loom: str, repo: Path, work: Path, out: Path) -> dict[str, Any]:
    """Run discovery over a sanitized tracked HEAD snapshot (not a benchmark)."""
    if out.exists():
        raise FileExistsError(f"products destination already exists: {out}; choose a new directory")
    run_dir = new_workspace(work, "selfhost-")
    sources = run_dir / "sources"
    manifest = stage_snapshot(repo, sources)
    write_json(run_dir / "input_manifest.json", manifest)
    products = run_dir / "products"
    res = run_knowledge(loom, run_dir / "data", {"sources": [str(sources)], "out_dir": str(products)})
    res["evaluation"] = {"kind": "selfhost_discovery", "predictive_accuracy": None, "inputs": manifest,
                         "work_dir": str(run_dir), "pack": "current embedded pack (discovery, not temporal validation)"}
    copy_products(products, out, manifest, res)
    return res


def holdout(loom: str, repo: Path, cut: str, work: Path, products: Path | None = None) -> dict[str, Any]:
    """Fail closed until the runner can load an exact historical policy pack.

Staging is independently useful and audited even when prediction is unavailable.
Current built-in aliases/operators would otherwise leak later knowledge.
"""
    run_dir = new_workspace(work, "holdout-")
    manifest = stage_snapshot(repo, run_dir / "sources", cut)
    write_json(run_dir / "input_manifest.json", manifest)
    return {
        "status": "unavailable", "protocol": "strict_temporal_holdout", "cut": cut,
        "benchmark_valid": False, "predictive_accuracy": None, "predictions": [],
        "reason": "Historical sources are isolated, but the runner loads a present-day embedded policy pack "
                  "(including owner aliases). An exact historical pack loader is required before prediction.",
        "inputs": manifest, "work_dir": str(run_dir), "pipeline_executed": False,
    }
