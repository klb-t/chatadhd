#!/usr/bin/env python3
"""Read-only, content-minimising candidate scanner; no product imports/execution."""
from __future__ import annotations

import argparse
import ast
import bisect
from collections import Counter, defaultdict
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tokenize

VERSION = "1.0.0"


def digest(value):
    if not isinstance(value, bytes):
        value = value.encode("utf-8")
    return hashlib.sha256(value).hexdigest()


def encoded(obj):
    return json.dumps(obj, sort_keys=True, ensure_ascii=True, separators=(",", ":"))


def git(repo, *args, stdin=None):
    result = subprocess.run(["git", "-C", str(repo), *args], input=stdin,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        # Avoid echoing credentials, remote URLs, or content in stderr.
        raise RuntimeError("git operation failed: " + args[0])
    return result.stdout


def load_rules(path):
    rules = json.loads(Path(path).read_text(encoding="utf-8"))
    for item in rules["classification_rules"]:
        re.compile(item["path_regex"])
    for item in rules["signal_rules"]:
        re.compile(item["regex"])
    for pattern in rules.get("excluded_content_path_regexes", []):
        re.compile(pattern)
    allowed = {item["id"] for item in rules["r42_exceptions"]}
    if allowed != {"R42." + str(i) for i in range(1, 7)}:
        raise ValueError("rules must retain all six R42 exceptions")
    for item in rules.get("exception_assessments", []):
        if not item.get("reason") or item.get("exception_id") not in allowed:
            raise ValueError("exception assessments need an R42 category and reason")
        if not item.get("candidate_id") or not item.get("reviewer"):
            raise ValueError("exception assessments need candidate ID and reviewer")
    return rules


def classify(path, rules):
    is_code = PurePosixPath(path).suffix.lower() in rules["code_extensions"]
    for item in rules["classification_rules"]:
        if re.search(item["path_regex"], path):
            # A directory named data is not proof that its .kt/.py code is data.
            if item["category"] == "data" and is_code:
                return "product"
            return item["category"]
    if is_code:
        return "product"
    return "other"


def read_snapshot(repo, revision, manifest=None):
    """Manifest is an explicit alternate source, never implied to be a git snapshot."""
    if manifest:
        data = json.loads(Path(manifest).read_text(encoding="utf-8"))
        for key in ("repo", "sha", "base_branch", "files"):
            if key not in data:
                raise ValueError("manifest missing " + key)
        seen = set()
        for row in data["files"]:
            path = row["path"]
            parts = PurePosixPath(path).parts
            if PurePosixPath(path).is_absolute() or ".." in parts or path in seen:
                raise ValueError("manifest paths must be unique relative paths")
            seen.add(path)
            row.setdefault("mode", "100644")
            row.setdefault("type", "blob")
        return data
    sha = git(repo, "rev-parse", "--verify", revision + "^{commit}").decode().strip()
    raw = git(repo, "ls-tree", "-r", "-z", "--full-tree", sha)
    rows = []
    for item in raw.split(b"\0"):
        if not item:
            continue
        meta, path = item.split(b"\t", 1)
        mode, kind, oid = meta.decode("ascii").split()
        rows.append({"path": path.decode("utf-8", "surrogateescape"),
                     "mode": mode, "type": kind, "oid": oid})
    return {"sha": sha, "files": rows}


class BlobReader:
    """One git cat-file process, no checkout and no symlink traversal."""
    def __init__(self, repo):
        self.process = subprocess.Popen(["git", "-C", str(repo), "cat-file", "--batch"],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL)

    def read(self, oid, max_bytes):
        self.process.stdin.write((oid + "\n").encode("ascii"))
        self.process.stdin.flush()
        header = self.process.stdout.readline().decode("ascii").strip().split()
        if len(header) != 3 or header[1] != "blob":
            raise RuntimeError("cannot read requested git blob")
        size = int(header[2])
        if size > max_bytes:
            remaining = size
            while remaining:
                block = self.process.stdout.read(min(remaining, 1024 * 1024))
                if not block:
                    raise RuntimeError("unexpected end of git blob")
                remaining -= len(block)
            self.process.stdout.read(1)
            return None, size
        content = self.process.stdout.read(size)
        if len(content) != size or self.process.stdout.read(1) != b"\n":
            raise RuntimeError("unexpected end of git blob")
        return content, size

    def close(self):
        self.process.stdin.close()
        self.process.stdout.close()
        self.process.wait()


class Source:
    def __init__(self, text):
        self.text = text
        self.starts = [0] + [m.end() for m in re.finditer("\n", text)]
        self.lines = text.splitlines(keepends=True)

    def at(self, offset):
        line = bisect.bisect_right(self.starts, offset)
        return line, offset - self.starts[line - 1] + 1

    def offset(self, line, col):
        return self.starts[line - 1] + col

    def ast_offset(self, line, bytecol):
        # CPython AST uses UTF-8 byte offsets; output uses Unicode code points.
        prefix = self.lines[line - 1].encode("utf-8")[:bytecol].decode("utf-8")
        return self.offset(line, len(prefix))

    def node_span(self, node):
        return (self.ast_offset(node.lineno, node.col_offset),
                self.ast_offset(node.end_lineno, node.end_col_offset))

    def context(self, start):
        line, _ = self.at(start)
        return self.lines[line - 1] if self.lines else ""


def blank_segment(chars, start, end):
    for pos in range(start, end):
        if chars[pos] not in "\r\n":
            chars[pos] = " "


def python_lex(source):
    """Mask strings/comments before generic control rules, including invalid ASTs."""
    chars = list(source.text)
    literals = []
    try:
        tokens = tokenize.generate_tokens(io.StringIO(source.text).readline)
        for token in tokens:
            if token.type in (tokenize.STRING, tokenize.COMMENT):
                start = source.offset(*token.start)
                end = source.offset(*token.end)
                blank_segment(chars, start, end)
                if token.type == tokenize.STRING:
                    literals.append((start, end))
    except (tokenize.TokenError, IndentationError, SyntaxError):
        # The limitation is explicit; don't dump failed source or exception text.
        return "".join(chars), literals, "partial_tokenization"
    return "".join(chars), literals, "complete_tokenization"


def generic_lex(source, extension):
    """Approximate multiline lexer; not a parser for embedded/template languages."""
    chars = list(source.text)
    literals = []
    text = source.text
    index = 0
    hash_comments = extension in (".sh", ".bash", ".ps1")
    while index < len(text):
        start = index
        if text.startswith("//", index) or (hash_comments and text[index] == "#"):
            index = text.find("\n", index)
            if index < 0:
                index = len(text)
            blank_segment(chars, start, index)
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            index = len(text) if end < 0 else end + 2
            blank_segment(chars, start, index)
        elif text[index] in "\"'`":
            quote = text[index]
            delim = quote * 3 if text.startswith(quote * 3, index) else quote
            index += len(delim)
            while index < len(text):
                if text[index] == "\\":
                    index += 2
                elif text.startswith(delim, index):
                    index += len(delim)
                    break
                else:
                    index += 1
            index = min(index, len(text))
            literals.append((start, index))
            blank_segment(chars, start, index)
        else:
            index += 1
    return "".join(chars), literals, "approximate_lexing"


class Candidates:
    def __init__(self, repo, sha, row, source):
        self.repo, self.sha, self.row, self.source = repo, sha, row, source
        self.items = {}

    def add(self, rule, start, end, method, **extra):
        if end <= start:
            return
        key = (rule, start, end)
        if key in self.items:
            return
        line, col = self.source.at(start)
        end_line, end_col = self.source.at(end)
        fingerprint = digest(self.source.text[start:end])
        self.items[key] = {"schema": "klbt.audit-scanner.candidate/1",
            "repo": self.repo, "sha": self.sha, "path": self.row["path"],
            "blob_oid": self.row.get("oid"), "file_category": self.row["category"],
            "classification": "candidate", "rule": rule, "method": method,
            "span": {"start_line": line, "start_column": col,
                     "end_line": end_line, "end_column_exclusive": end_col},
            "fingerprint_sha256": fingerprint,
            "semantic_status": "not_reviewed", "r42_exception": None, **extra}

    def finish(self, assessments):
        occurrences = Counter()
        results = []
        for (rule, start, end), item in sorted(self.items.items(), key=lambda kv: (kv[0][1], kv[0][2], kv[0][0])):
            identity = [self.repo, self.row["path"], rule, item["fingerprint_sha256"]]
            key = encoded(identity)
            occurrence = occurrences[key]
            occurrences[key] += 1
            item["candidate_id"] = "AUDC-" + digest(encoded(identity + [occurrence]))[:24]
            if item["candidate_id"] in assessments:
                item["r42_exception"] = assessments[item["candidate_id"]]
                item["semantic_status"] = "external_exception_assessment"
            results.append(item)
        return results


def scan_text(text, repo, sha, row, rules):
    source = Source(text)
    candidates = Candidates(repo, sha, row, source)
    extension = PurePosixPath(row["path"]).suffix.lower()
    parser_status = "not_python"
    tree = None
    if extension == ".py":
        code, literals, lex_status = python_lex(source)
        try:
            tree = ast.parse(text)
            parser_status = "parsed"
        except (SyntaxError, RecursionError, ValueError):
            parser_status = "syntax_unavailable"
    else:
        code, literals, lex_status = generic_lex(source, extension)
    for start, end in literals:
        candidates.add("string.literal", start, end, "lexical")
        value = text[start:end]
        context = source.context(start)
        for rule in rules["signal_rules"]:
            target = value if rule["target"] == "literal" else context
            if rule["target"] in ("literal", "context") and re.search(rule["regex"], target):
                candidates.add(rule["id"], start, end, "lexical")
    # Numeric candidates use masked code to avoid matching numeric text in strings.
    for match in re.finditer(r"(?<![\w.])(?:0[xX][0-9A-Fa-f]+|\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)(?![\w.])", code):
        candidates.add("numeric.literal", match.start(), match.end(), "lexical")
        for rule in rules["signal_rules"]:
            if rule["id"] == "policy_value.context" and re.search(rule["regex"], source.context(match.start())):
                candidates.add(rule["id"], match.start(), match.end(), "lexical")
    for rule in rules["signal_rules"]:
        if rule["target"] == "code":
            for match in re.finditer(rule["regex"], code):
                candidates.add(rule["id"], match.start(), match.end(), "lexical")
    if tree is not None:
        for node in ast.walk(tree):
            rule = None
            if isinstance(node, (ast.If, ast.IfExp, ast.Match)):
                rule = "branch.ast"
            elif isinstance(node, (ast.Try, ast.ExceptHandler)):
                rule = "exception.ast"
            elif isinstance(node, ast.BoolOp):
                rule = "fallback_or_guard.ast"
            elif isinstance(node, ast.Slice):
                rule = "truncation.ast"
            elif isinstance(node, (ast.ListComp, ast.SetComp, ast.DictComp, ast.GeneratorExp)):
                rule = "selection.ast"
            if rule and hasattr(node, "lineno"):
                candidates.add(rule, *source.node_span(node), "python_ast")
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)):
                for default in node.args.defaults + [n for n in node.args.kw_defaults if n is not None]:
                    candidates.add("default_argument.ast", *source.node_span(default), "python_ast")
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Module)):
                calls = [statement for statement in node.body
                         if isinstance(statement, ast.Expr) and isinstance(statement.value, ast.Call)]
                if len(calls) > 1:
                    for statement in calls:
                        candidates.add("call_order.ast", *source.node_span(statement), "python_ast")
    assessments = {a["candidate_id"]: a for a in rules.get("exception_assessments", [])}
    return candidates.finish(assessments), {"python_ast": parser_status, "lexer": lex_status}


def run(args):
    rules_path = Path(args.rules)
    rules = load_rules(rules_path)
    scanner_hash = digest(Path(__file__).read_bytes())
    rules_hash = digest(rules_path.read_bytes())
    snapshot = read_snapshot(args.repo, args.revision, args.manifest)
    repo_name = args.repo_name or snapshot.get("repo")
    if not repo_name:
        raise ValueError("--repo-name owner/name is required for git mode")
    if args.visibility != "public":
        raise ValueError("this audit scanner only accepts explicitly public repositories")
    sha = snapshot["sha"]
    branch = args.base_branch or snapshot.get("base_branch", "unspecified")
    selected = set(args.categories.split(",")) if args.categories else set(rules["scanned_categories"])
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    inventory, all_candidates = [], []
    by_category = Counter()
    statuses = Counter()
    ast_statuses = Counter()
    by_rule = Counter()
    reader = None if args.manifest else BlobReader(args.repo)
    try:
        for original in sorted(snapshot["files"], key=lambda item: item["path"]):
            # Never echo arbitrary manifest fields (which could contain source).
            row = {key: original[key] for key in ("path", "mode", "type", "oid") if key in original}
            row["category"] = classify(row["path"], rules)
            by_category[row["category"]] += 1
            extension = PurePosixPath(row["path"]).suffix.lower()
            row["code_extension"] = extension in rules["code_extensions"]
            row["eligible"] = row["category"] in selected and row["code_extension"]
            if row["type"] != "blob":
                row["scan_status"] = "non_blob_entry"
            elif row["mode"] == "120000":
                row["scan_status"] = "symlink_not_followed"
            elif any(re.search(pattern, row["path"]) for pattern in rules.get("excluded_content_path_regexes", [])):
                row["scan_status"] = "content_excluded_by_path"
            elif not row["eligible"]:
                row["scan_status"] = "out_of_scan_scope"
            else:
                if reader:
                    content, size = reader.read(row["oid"], rules["max_blob_bytes"])
                else:
                    source_path = Path(args.repo) / row["path"]
                    # Resolve under root and reject symlink components in manifest mode.
                    resolved_root = Path(args.repo).resolve()
                    if not source_path.resolve().is_relative_to(resolved_root):
                        raise ValueError("manifest content escapes root")
                    if any(parent.is_symlink() for parent in [source_path, *source_path.parents] if parent != resolved_root):
                        raise ValueError("manifest content uses symlink")
                    size = source_path.stat().st_size
                    content = source_path.read_bytes() if size <= rules["max_blob_bytes"] else None
                row["bytes"] = size
                if content is None:
                    row["scan_status"] = "oversized"
                elif b"\0" in content:
                    row["scan_status"] = "binary"
                else:
                    row["content_sha256"] = digest(content)
                    if args.manifest and original.get("content_sha256") and original["content_sha256"] != row["content_sha256"]:
                        raise ValueError("manifest content hash does not match")
                    try:
                        text = content.decode("utf-8-sig")
                    except UnicodeDecodeError:
                        row["scan_status"] = "unsupported_encoding"
                    else:
                        found, parsing = scan_text(text, repo_name, sha, row, rules)
                        row["scan_status"] = "scanned"
                        row["parsing"] = parsing
                        row["line_count"] = len(text.splitlines())
                        row["candidate_count"] = len(found)
                        all_candidates.extend(found)
                        ast_statuses[parsing["python_ast"]] += 1
                        by_rule.update(item["rule"] for item in found)
            statuses[row["scan_status"]] += 1
            inventory.append(row)
    finally:
        if reader:
            reader.close()
    category_coverage = {}
    for category in sorted(by_category):
        entries = [row for row in inventory if row["category"] == category]
        category_coverage[category] = {
            "tracked_entries": len(entries),
            "eligible_code_entries": sum(row["eligible"] for row in entries),
            "scanned_code_entries": sum(row["scan_status"] == "scanned" for row in entries),
            "scanned_lines": sum(row.get("line_count", 0) for row in entries),
            "statuses": dict(sorted(Counter(row["scan_status"] for row in entries).items()))}
    summary = {"schema": "klbt.audit-scanner.summary/1", "scanner_version": VERSION,
        "repo": repo_name, "visibility": args.visibility, "sha": sha, "base_branch": branch,
        "source": "manifest_assertion_plus_local_files" if args.manifest else "immutable_git_tree",
        "rules_sha256": rules_hash,
        "scanner_sha256": scanner_hash,
        "denominator": {"definition": "All recursive tracked tree entries at the stated commit, including blobs, symlinks and gitlinks; git history and untracked working files excluded. Manifest mode instead uses the explicit supplied list, whose completeness is not independently verified.",
                        "tracked_entries": len(inventory), "by_category": dict(sorted(by_category.items())),
                        "eligible_code_entries": sum(row["eligible"] for row in inventory),
                        "scanned_code_entries": statuses["scanned"],
                        "semantic_reviewed_entries": 0,
                        "scanned_lines": sum(row.get("line_count", 0) for row in inventory)},
        "coverage_by_category": category_coverage,
        "scope": {"categories": sorted(selected), "max_blob_bytes": rules["max_blob_bytes"],
                  "excluded_content_path_regexes": rules.get("excluded_content_path_regexes", []),
                  "code_extensions": rules["code_extensions"]},
        "scan_statuses": dict(sorted(statuses.items())),
        "parser_statuses": dict(sorted(ast_statuses.items())),
        "candidate_count": len(all_candidates), "by_signal": dict(sorted(by_rule.items())),
        "limitations": ["Candidates are not violations or proof of runtime consumption.",
                        "Classification is path-based and can misclassify product code; review inventory.",
                        "AST coverage is Python only; other languages use approximate lexical rules.",
                        "Lexical rules can miss raw strings, embedded expressions, dynamic dispatch and template content.",
                        "No call graph, reachability, consent, graph-to-runtime or UI validation is performed.",
                        "No private repository scanning, product execution, network or paid model calls.",
                        "Output includes paths/hashes/spans, never source excerpts or literal values; paths can still be sensitive."]}
    for filename, rows in (("inventory.jsonl", inventory), ("candidates.jsonl", all_candidates)):
        with (output / filename).open("w", encoding="utf-8") as handle:
            for row in rows:
                handle.write(encoded(row) + "\n")
    (output / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(encoded({"repo": repo_name, "sha": sha, "tracked_entries": len(inventory),
                   "scanned_code_entries": statuses["scanned"], "candidate_count": len(all_candidates)}))
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--repo-name")
    parser.add_argument("--revision", default="HEAD")
    parser.add_argument("--base-branch")
    parser.add_argument("--visibility", choices=["public"], required=True)
    parser.add_argument("--rules", default=str(Path(__file__).with_name("rules.json")))
    parser.add_argument("--output", required=True)
    parser.add_argument("--categories", help="Comma-separated explicit override of scanned categories")
    parser.add_argument("--manifest", help="JSON manifest for exported sources; weaker provenance than git mode")
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, RuntimeError, ValueError) as error:
        # Never print source, command stderr, manifest values, or secret-bearing paths.
        print("audit scanner failed: " + type(error).__name__ + "; verify paths, git revision and rules", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
