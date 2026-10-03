#!/usr/bin/env python3
"""Build a local, refs-scoped identity-redacted mirror; never touch originals.

Commit trees, messages, dates and ordered parent topology are preserved.
Only explicit commit identities from the private audit are replaced. Commit
signatures on rewritten objects are removed, because they no longer authenticate
those bytes. Original signed objects remain in the private input bundle.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess


def run(args, *, cwd=None, data=None):
    r = subprocess.run(args, cwd=cwd, input=data, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if r.returncode:
        # Avoid logging object contents, names or contact metadata on failure.
        raise RuntimeError(f"Command failed ({r.returncode}): {args[0:2]}")
    return r.stdout


def fields(raw):
    header, message = raw.split(b"\n\n", 1)
    blocks = []
    for line in header.split(b"\n"):
        if line.startswith(b" "):
            blocks[-1] += b"\n" + line
        else:
            blocks.append(line)
    return blocks, message


def batch_commits(repo, ids):
    p = subprocess.Popen(["git", "--git-dir", str(repo), "cat-file", "--batch"],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL)
    try:
        for oid in ids:
            p.stdin.write(oid.encode() + b"\n")
            p.stdin.flush()
            head = p.stdout.readline().decode().strip().split()
            if len(head) != 3 or head[1] != "commit":
                raise RuntimeError("Unexpected commit object type")
            raw = p.stdout.read(int(head[2]))
            if p.stdout.read(1) != b"\n":
                raise RuntimeError("Invalid batch boundary")
            yield oid, raw
    finally:
        p.stdin.close()
        p.wait()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bundle", required=True)
    ap.add_argument("--audit", required=True)
    ap.add_argument("--repository", default="chatadhd")
    ap.add_argument("--output", required=True)
    ap.add_argument("--report-prefix", required=True)
    ns = ap.parse_args()
    bundle = pathlib.Path(ns.bundle).resolve()
    out = pathlib.Path(ns.output).resolve()
    prefix = pathlib.Path(ns.report_prefix).resolve()
    if out.exists():
        raise RuntimeError("Output must be a new directory")
    audit = json.loads(pathlib.Path(ns.audit).read_text())[ns.repository]
    targets = set(audit["affected_commits"])
    allowed = set(audit["affected_refs"])
    heads = {}
    for line in run(["git", "bundle", "list-heads", str(bundle)]).decode().splitlines():
        oid, ref = line.split(" ", 1)
        if ref.startswith("refs/"):
            heads[ref] = oid
    if not allowed <= heads.keys():
        raise RuntimeError("Audited refs missing from input bundle")
    excluded = {r: s for r, s in heads.items() if r not in allowed}
    run(["git", "init", "--bare", str(out)])
    run(["git", "--git-dir", str(out), "bundle", "unbundle", str(bundle)])
    old_ids = run(["git", "--git-dir", str(out), "rev-list", "--topo-order",
                   "--reverse", "--stdin"],
                  data=("\n".join(heads[r] for r in sorted(allowed)) + "\n").encode()).decode().splitlines()
    if not targets <= set(old_ids):
        raise RuntimeError("Affected commits missing from scoped history")
    mapping = {}
    records = []
    target_emails = set()
    stripped = 0
    identity_changes = 0
    identity_re = re.compile(rb"^(author|committer) (.*?) <([^>]*)> (-?\d+ [+-]\d{4})$")
    # Git object writes are entirely local. No refs/original are created.
    for oid, raw in batch_commits(out, old_ids):
        blocks, message = fields(raw)
        changed = oid in targets
        new_blocks = []
        original_tree = None
        old_parents = []
        for block in blocks:
            if block.startswith(b"tree "):
                original_tree = block[5:].decode()
            if block.startswith(b"parent "):
                parent = block[7:].decode()
                old_parents.append(parent)
                replacement = b"parent " + mapping[parent].encode()
                changed |= replacement != block
                new_blocks.append(replacement)
                continue
            if oid in targets and block.startswith((b"author ", b"committer ")):
                m = identity_re.fullmatch(block)
                if not m:
                    raise RuntimeError("Unexpected identity date syntax")
                target_emails.add(m.group(3))
                replacement = m.group(1) + b" klb-t <klb-t@users.noreply.github.com> " + m.group(4)
                if replacement != block:
                    identity_changes += 1
                new_blocks.append(replacement)
                continue
            new_blocks.append(block)
        if changed:
            filtered = []
            for block in new_blocks:
                if block.startswith((b"gpgsig ", b"gpgsig-sha256 ", b"mergetag ")):
                    stripped += 1
                else:
                    filtered.append(block)
            new_blocks = filtered
        new_raw = b"\n".join(new_blocks) + b"\n\n" + message
        new = (run(["git", "--git-dir", str(out), "hash-object", "-t", "commit", "-w", "--stdin"], data=new_raw).decode().strip()
               if new_raw != raw else oid)
        mapping[oid] = new
        # Validate without reading tree/file contents, including sealed pointers.
        check = run(["git", "--git-dir", str(out), "cat-file", "commit", new])
        cb, cm = fields(check)
        if cm != message or next(b[5:].decode() for b in cb if b.startswith(b"tree ")) != original_tree:
            raise RuntimeError("Source tree or commit message changed")
        new_parents = [b[7:].decode() for b in cb if b.startswith(b"parent ")]
        if new_parents != [mapping[p] for p in old_parents]:
            raise RuntimeError("Parent topology changed")
        for key in [b"author", b"committer"]:
            original = next(b for b in blocks if b.startswith(key + b" "))
            replacement = next(b for b in cb if b.startswith(key + b" "))
            if original.rsplit(b"> ", 1)[1] != replacement.rsplit(b"> ", 1)[1]:
                raise RuntimeError("Commit date changed")
            if oid not in targets and original != replacement:
                raise RuntimeError("Non-target identity changed")
        records.append({"original": oid, "sanitized": new, "tree": original_tree,
                        "identity_replaced": oid in targets,
                        "parents_original": old_parents, "parents_sanitized": new_parents})
    # Only addresses actually changed are prohibited; the chosen public identity
    # remains allowed if it was already used on one audited identity role.
    target_emails.discard(b"klb-t@users.noreply.github.com")
    public_refs = {}
    for ref in sorted(allowed):
        public_refs[ref] = mapping[heads[ref]]
        run(["git", "--git-dir", str(out), "update-ref", ref, public_refs[ref]])
    run(["git", "--git-dir", str(out), "symbolic-ref", "HEAD", "refs/heads/main"])
    advertised = run(["git", "--git-dir", str(out), "for-each-ref", "--format=%(refname)"]).decode().splitlines()
    if set(advertised) != allowed or any(r.startswith("refs/original/") for r in advertised):
        raise RuntimeError("Unexpected advertised ref")
    for oid, raw in batch_commits(out, list(mapping.values())):
        if any(email in raw for email in target_emails):
            raise RuntimeError("Target contact metadata remains in sanitized reachable commit")
    opaque = []
    for ref, oid in sorted(excluded.items()):
        # Tree pointer only; no sealed source contents, labels or datasets read.
        tree = run(["git", "--git-dir", str(out), "rev-parse", oid + "^{tree}"]).decode().strip()
        opaque.append({"ref": ref, "original_tip": oid, "opaque_tree": tree,
                       "disposition": "private original bundle only; not advertised"})
    report = {"schema": "chatadhd.sanitized_commit_metadata/1",
              "input_bundle_sha256": hashlib.sha256(bundle.read_bytes()).hexdigest(),
              "allowed_ref_count": len(allowed), "excluded_opaque_ref_count": len(excluded),
              "verified_commit_count": len(mapping), "rehashed_commit_count": sum(a != b for a, b in mapping.items()),
              "target_commit_count": len(targets), "changed_identity_header_count": identity_changes,
              "stripped_signature_or_mergetag_headers": stripped,
              "source_trees_unchanged": True, "ordered_parent_topology_preserved": True,
              "commit_dates_preserved": True, "commit_messages_preserved": True,
              "target_contact_metadata_absent": True, "original_refs_absent": True,
              "sanitized_main": public_refs["refs/heads/main"],
              "refs": public_refs, "excluded_opaque_refs": opaque}
    prefix.parent.mkdir(parents=True, exist_ok=True)
    prefix.with_suffix(".map.json").write_text(json.dumps(records, indent=2) + "\n")
    prefix.with_suffix(".validation.json").write_text(json.dumps(report, indent=2) + "\n")
    prefix.with_suffix(".map.tsv").write_text("original\tsanitized\ttree\n" + "".join(f"{r['original']}\t{r['sanitized']}\t{r['tree']}\n" for r in records))
    # Drop unadvertised original commits from this derivative mirror; the exact
    # original bundle is immutable and retained outside it.
    run(["git", "--git-dir", str(out), "reflog", "expire", "--expire=now", "--all"])
    run(["git", "--git-dir", str(out), "gc", "--prune=now"])
    run(["git", "--git-dir", str(out), "fsck", "--no-reflogs", "--full"])
    print(json.dumps({k: report[k] for k in ["allowed_ref_count", "excluded_opaque_ref_count", "verified_commit_count", "rehashed_commit_count", "target_commit_count", "stripped_signature_or_mergetag_headers", "sanitized_main", "source_trees_unchanged", "target_contact_metadata_absent"]}))


if __name__ == "__main__":
    main()
