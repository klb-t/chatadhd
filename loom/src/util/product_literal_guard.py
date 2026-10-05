#!/usr/bin/env python3
# Read-only lexical inventory. Human review assignments are evidence inputs,
# not proof of semantic correctness. Unknown syntax blocks a complete scan.
from __future__ import annotations

import argparse
import bisect
from collections import Counter
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import tokenize

ROOT = Path(__file__).resolve().parents[3]
POLICY = ROOT / "loom/data/validation/product_literals.pack"
SCHEMA = ROOT / "docs/contracts/product_literals.schema.json"
REPORT_SCHEMA = "loom.product_literals_report/1"
MANIFEST_SCHEMA = "loom.product_literals_manifest/1"
DIAGNOSTICS = {}


def diagnostic(code):
    return DIAGNOSTICS.get(code, code)
NUMBER = re.compile(r"(?:0[xX][0-9a-fA-F][0-9a-fA-F'_]*(?:\.[0-9a-fA-F'_]*)?(?:[pP][+-]?[0-9][0-9'_]*)?|0[bB][01][01'_]*|(?:[0-9][0-9'_]*(?:\.(?!\.)[0-9'_]*)?|\.[0-9][0-9'_]*)(?:[eE][+-]?[0-9][0-9'_]*)?)(?:[uUlLfFdD]*)(?![A-Za-z_0-9])")
RAW = re.compile(r'(?:u8|u|U|L)?R"')
PREFIX = re.compile(r'(?:u8|u|U|L)?["\']')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def unique_object(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ValueError('duplicate_json_member:' + key)
        out[key] = value
    return out


def finite_tree(value):
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError('nonfinite_json_number')
    if isinstance(value, str):
        value.encode("utf-8")
    elif isinstance(value, list):
        for item in value:
            finite_tree(item)
    elif isinstance(value, dict):
        for key, item in value.items():
            finite_tree(key)
            finite_tree(item)


def read_json(path):
    raw = path.read_bytes()
    def invalid_constant(value):
        raise ValueError('nonfinite_json_number:' + value)
    doc = json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique_object,
                     parse_constant=invalid_constant)
    finite_tree(doc)
    return doc, raw


def relative_path(value):
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValueError('expected_a_nonempty_relative_posix_path')
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in value.split("/")):
        raise ValueError('path_escapes_the_declared_root:' + value)
    return path


def inside(root, value):
    relative_path(value)
    path = root / value
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('filesystem_link_escapes_declared_root:' + value)
    return path


def load_policy(pack, schema):
    try:
        import jsonschema
    except ImportError as error:
        raise ValueError('jsonschema_dependency_is_unavailable_no_validation_fallback') from error
    definition, schema_raw = read_json(schema)
    if sha(schema_raw) != sha(SCHEMA.read_bytes()):
        raise ValueError("schema_contract_identity_mismatch")
    if definition.get("$id") != "loom.product_literals/1":
        raise ValueError('unexpected_product_literal_schema_identity')
    def local_references(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key in ("$ref", "$dynamicRef") and (not isinstance(child, str) or not child.startswith("#")):
                    raise ValueError("external_schema_reference")
                local_references(child)
        elif isinstance(value, list):
            for child in value:
                local_references(child)
    local_references(definition)
    jsonschema.Draft202012Validator.check_schema(definition)
    policy, pack_raw = read_json(pack)
    jsonschema.Draft202012Validator(definition).validate(policy)
    if set(policy["categories"]) != {"contract", "external_standard", "mechanism", "serialization", "bootstrap", "developer_diagnostic"}:
        raise ValueError("category_contract_identity_mismatch")
    for path in policy["scopes"]:
        relative_path(path)
    if not set(policy["optional_scopes"]).issubset(policy["scopes"]):
        raise ValueError("optional_scope_not_declared")
    suffixes = []
    for scanner in policy["scanners"]:
        suffixes.extend(scanner["suffixes"])
    if len(suffixes) != len(set(suffixes)):
        raise ValueError('ambiguous_scanner_suffix_registrations')
    prefixes = [rule["prefix"] for rule in policy["owner_rules"]]
    if len(prefixes) != len(set(prefixes)):
        raise ValueError('duplicate_owner_prefixes')
    for prefix in prefixes:
        relative_path(prefix)
    anchors = []
    for entry in policy["allowlist"]:
        relative_path(entry["path"])
        if entry["byte_end"] <= entry["byte_start"]:
            raise ValueError('empty_reversed_literal_review_anchor')
        anchors.append((entry["path"], entry["byte_start"], entry["byte_end"]))
    if len(anchors) != len(set(anchors)):
        raise ValueError('duplicate_literal_review_anchors')
    generated = [entry["path"] for entry in policy["generated_outputs"]]
    if len(generated) != len(set(generated)):
        raise ValueError('duplicate_generated_output_registrations')
    for entry in policy["generated_outputs"]:
        relative_path(entry["path"])
        paths = [source["path"] for source in entry["inputs"]]
        if len(paths) != len(set(paths)):
            raise ValueError('duplicate_generated_source_identities')
        for path in paths:
            relative_path(path)
    global DIAGNOSTICS
    DIAGNOSTICS = dict(policy["diagnostics"])
    return policy, pack_raw, schema_raw


def issue(code, reason, path=None, **extra):
    return {"status": "BLOCKED", "code": code, "reason": reason,
            **({"path": path} if path is not None else {}), **extra}


def owner_for(path, policy):
    matches = [rule for rule in policy["owner_rules"]
               if path == rule["prefix"] or path.startswith(rule["prefix"] + "/")]
    return max(matches, key=lambda rule: len(rule["prefix"]))["thread"] if matches else policy["fallback_owner"]


def discover(path):
    files, errors, pending = [], [], [path]
    while pending:
        node = pending.pop()
        try:
            metadata = node.lstat()
            if not stat.S_ISDIR(metadata.st_mode):
                files.append(node)
                continue
            with os.scandir(node) as entries:
                children = [Path(entry.path) for entry in entries]
            pending.extend(sorted(children, reverse=True))
        except OSError as error:
            errors.append((node, str(error)))
    return files, errors


def coordinates(text):
    offsets, lines = [0], [0]
    total = 0
    for pos, char in enumerate(text):
        total += len(char.encode("utf-8"))
        offsets.append(total)
        if char == "\n":
            lines.append(pos + 1)
    return offsets, lines


def literal(text, start, end, kind, offsets, lines):
    row = bisect.bisect_right(lines, start)
    raw = text[start:end]
    return {"kind": kind, "byte_start": offsets[start], "byte_end": offsets[end],
            "line": row, "column": start - lines[row - 1] + 1,
            "raw": raw, "literal_sha256": sha(raw.encode("utf-8"))}


def scan_python(text, offsets, lines, keywords):
    candidates, errors = [], []
    try:
        for token in tokenize.generate_tokens(io.StringIO(text).readline):
            if tokenize.tok_name.get(token.type, "").startswith(("FSTRING_", "TSTRING_")):
                errors.append(issue("unsupported_python_interpolation", diagnostic("unsupported_python_interpolation"), line=token.start[0], column=token.start[1] + 1))
                continue
            if token.type == tokenize.NAME and token.string in keywords:
                start = lines[token.start[0] - 1] + token.start[1]
                end = lines[token.end[0] - 1] + token.end[1]
                candidates.append(literal(text, start, end, keywords[token.string], offsets, lines))
                continue
            if token.type not in (tokenize.STRING, tokenize.NUMBER):
                if token.type == tokenize.ERRORTOKEN and not token.string.isspace():
                    errors.append(issue("unsupported_python_token", diagnostic('unsupported_python_token'), line=token.start[0], column=token.start[1] + 1))
                continue
            start = lines[token.start[0] - 1] + token.start[1]
            end = lines[token.end[0] - 1] + token.end[1]
            kind = "number" if token.type == tokenize.NUMBER else "string"
            candidates.append(literal(text, start, end, kind, offsets, lines))
            if token.type == tokenize.STRING and re.match(r"(?i)[rub]*f|f[rub]*", token.string):
                errors.append(issue("unsupported_python_interpolation", diagnostic('unsupported_python_interpolation'), line=token.start[0], column=token.start[1] + 1))
    except (tokenize.TokenError, IndentationError, SyntaxError) as error:
        errors.append(issue("python_tokenizer_error", str(error)))
    return candidates, errors


def scan_c_family(text, backend, offsets, lines, keywords):
    candidates, errors = [], []
    i = 0
    while i < len(text):
        start = i
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = len(text) if end < 0 else end
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                errors.append(issue("unterminated_comment", diagnostic('unterminated_comment'), byte_start=offsets[i]))
                break
            if backend == "kotlin" and "/*" in text[i + 2:end]:
                errors.append(issue("unsupported_nested_comment", diagnostic('unsupported_nested_comment'), byte_start=offsets[i]))
            i = end + 2
            continue
        raw = RAW.match(text, i) if backend == "cxx" else None
        if raw:
            open_pos = text.find("(", raw.end())
            delimiter = text[raw.end():open_pos] if open_pos >= 0 else ""
            if open_pos < 0 or len(delimiter) > 16 or any(c.isspace() or c in "()\\" for c in delimiter):
                errors.append(issue("malformed_raw_literal", diagnostic('malformed_raw_literal'), byte_start=offsets[i]))
                break
            end = text.find(")" + delimiter + '"', open_pos + 1)
            if end < 0:
                errors.append(issue("unterminated_raw_literal", diagnostic('unterminated_raw_literal'), byte_start=offsets[i]))
                break
            i = end + len(delimiter) + 2
            candidates.append(literal(text, start, i, "string", offsets, lines))
            continue
        prefixed = PREFIX.match(text, i) if backend == "cxx" else None
        quote_pos = prefixed.end() - 1 if prefixed else i
        quote = text[quote_pos]
        if quote in "\"'`" and (prefixed or quote_pos == i):
            if quote == "`" and backend != "javascript":
                errors.append(issue("unsupported_backtick", diagnostic('unsupported_backtick'), byte_start=offsets[i]))
            triple = backend in ("kotlin", "java") and text.startswith('"""', quote_pos)
            delimiter = '"""' if triple else quote
            i = quote_pos + len(delimiter)
            content_start = i
            escaped = False
            while i < len(text):
                if not escaped and text.startswith(delimiter, i):
                    end = i + len(delimiter)
                    body = text[content_start:i]
                    if backend == "javascript" and quote == "`" and "${" in body:
                        errors.append(issue("unsupported_template_expression", diagnostic('unsupported_template_expression'), byte_start=offsets[start]))
                    if backend == "kotlin" and "$" in body:
                        errors.append(issue("unsupported_kotlin_interpolation", diagnostic('unsupported_kotlin_interpolation'), byte_start=offsets[start]))
                    if backend == "java" and triple:
                        errors.append(issue("unsupported_java_text_block", diagnostic('unsupported_java_text_block'), byte_start=offsets[start]))
                    candidates.append(literal(text, start, end, "character" if quote == "'" and backend != "javascript" else "string", offsets, lines))
                    i = end
                    break
                if not escaped and text[i] == "\n" and not (triple or quote == "`"):
                    errors.append(issue("unterminated_quoted_literal", diagnostic('unterminated_quoted_literal'), byte_start=offsets[start]))
                    i += 1
                    break
                if not triple and text[i] == "\\" and not escaped:
                    escaped = True
                else:
                    escaped = False
                i += 1
            else:
                errors.append(issue("unterminated_quoted_literal", diagnostic('unterminated_quoted_literal'), byte_start=offsets[start]))
            continue
        if backend == "javascript" and text[i] == "/":
            errors.append(issue("unsupported_javascript_slash", diagnostic('unsupported_javascript_slash'), byte_start=offsets[i]))
        if backend == "javascript" and text[i] == "<":
            errors.append(issue("unsupported_javascript_markup", diagnostic('unsupported_javascript_markup'), byte_start=offsets[i]))
        number = NUMBER.match(text, i) if text[i].isdigit() or (text[i] == "." and i + 1 < len(text) and text[i + 1].isdigit()) else None
        if number:
            i = number.end()
            candidates.append(literal(text, start, i, "number", offsets, lines))
            continue
        if text[i].isdigit() or (text[i] == "." and i + 1 < len(text) and text[i + 1].isdigit()):
            errors.append(issue("unsupported_numeric_token", diagnostic('unsupported_numeric_token'), byte_start=offsets[i]))
        if text[i].isalpha() or text[i] == "_":
            i += 1
            while i < len(text) and (text[i].isalnum() or text[i] in "_$"):
                i += 1
            word = text[start:i]
            if word in keywords:
                candidates.append(literal(text, start, i, keywords[word], offsets, lines))
            continue
        i += 1
    return candidates, errors


def scan_source(raw, scanner):
    backend, keywords = scanner["backend"], scanner["keyword_literals"]
    try:
        text = raw.decode("utf-8")
    except UnicodeError as error:
        return [], [issue("invalid_source_encoding", str(error))]
    offsets, lines = coordinates(text)
    prelex_errors = []
    if re.search(r"\r(?!\n)", text) or (backend == "javascript" and re.search(r"[\u2028\u2029]", text)):
        prelex_errors.append(issue("unsupported_line_terminator", diagnostic("unsupported_line_terminator")))
    if backend == "java" and re.search(r"\\u+[0-9a-fA-F]{4}", text):
        prelex_errors.append(issue("unsupported_java_unicode_translation", diagnostic("unsupported_java_unicode_translation")))
    if backend == "cxx" and re.search(r"\\\r?\n", text):
        prelex_errors.append(issue("unsupported_cxx_line_translation", diagnostic("unsupported_cxx_line_translation")))
    if backend == "python":
        candidates, errors = scan_python(text, offsets, lines, keywords)
        return candidates, prelex_errors + errors
    if backend in ("cxx", "javascript", "java", "kotlin"):
        candidates, errors = scan_c_family(text, backend, offsets, lines, keywords)
        return candidates, prelex_errors + errors
    return [], [issue("unsupported_file_form", diagnostic('unsupported_file_form'))]


def generated_inputs(expected, root):
    text = expected.decode("utf-8")
    marker = "kRuntimeProfileSources[] = {"
    if marker not in text:
        raise ValueError('generator_output_has_no_source_identity_table')
    table = text.split(marker, 1)[1].split("\n};", 1)[0]
    entries = list(re.finditer(r'^  \{"([a-z0-9_-]+)",\n', table, re.MULTILINE))
    paths = {"loom/data/runtime_sources.pack"}
    if not entries:
        raise ValueError('empty_generated_source_identity_table')
    for index, entry in enumerate(entries):
        end = entries[index + 1].start() if index + 1 < len(entries) else len(table)
        raw = "".join(re.findall(r'R"LPROFILE\((.*?)\)LPROFILE"', table[entry.end():end], re.DOTALL))
        sources = json.loads(raw, object_pairs_hook=unique_object)
        if not isinstance(sources, list) or not sources:
            raise ValueError('invalid_generated_source_identity_records')
        for source in sources:
            path = source["path"]
            actual = inside(root, path).read_bytes()
            if sha(actual) != source["raw_sha256"]:
                raise ValueError('generated_metadata_disagrees_with_actual_source_bytes:' + path)
            paths.add(path)
    return [{"path": path, "sha256": sha(inside(root, path).read_bytes())} for path in sorted(paths)]


def verify_generated(entry, root, raw):
    # A closed trusted operation, not a policy-selected import/callable.
    if entry["operation"] != "runtime_profiles_v1" or entry["generator"]["path"] != "loom/src/model/gen_runtime_profiles.py":
        raise ValueError('unsupported_trusted_generation_operation')
    generator = inside(root, entry["generator"]["path"])
    generator_raw = generator.read_bytes()
    if sha(generator_raw) != sha((ROOT / entry["generator"]["path"]).read_bytes()):
        raise ValueError("trusted_generator_contract_identity_mismatch")
    if sha(generator_raw) != entry["generator"]["sha256"]:
        raise ValueError('generator_source_identity_is_stale')
    if sha(raw) != entry["output_sha256"]:
        raise ValueError('generated_output_identity_is_stale_tampered')
    prior = sys.dont_write_bytecode
    try:
        sys.dont_write_bytecode = True
        spec = importlib.util.spec_from_file_location("verified_runtime_profile_generator", generator)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        output = inside(root, entry["path"])
        expected = module.plan(root / "loom/data", output)[output]
    finally:
        sys.dont_write_bytecode = prior
    if expected != raw:
        raise ValueError('generated_bytes_do_not_equal_the_current_trusted_in_memory_plan')
    actual_inputs = generated_inputs(expected, root)
    if actual_inputs != sorted(entry["inputs"], key=lambda value: value["path"]):
        raise ValueError('generated_input_manifest_is_stale_incomplete')
    return {"operation": entry["operation"], "generator": entry["generator"],
            "inputs": actual_inputs, "output_sha256": sha(raw), "reason": entry["reason"]}


def audit(root, pack, schema):
    policy, pack_raw, schema_raw = load_policy(pack, schema)
    report = {"schema": REPORT_SCHEMA, "valid": False, "scan_mode": "lexical", "ast_claim": False, "semantic_category_claim": False,
              "requirement": policy["requirement"], "declared_scopes": policy["scopes"],
              "category_definitions": policy["categories"], "findings": [], "issues": [],
              "io": {"network_calls": 0, "source_writes": 0, "generator_output_writes": 0}}
    manifest = {"schema": MANIFEST_SCHEMA, "policy": {"path": str(pack), "sha256": sha(pack_raw)},
                "value_schema": {"path": str(schema), "sha256": sha(schema_raw)},
                "declared_scopes": policy["scopes"], "source_files": []}
    paths = {}
    for scope in policy["scopes"]:
        path = inside(root, scope)
        try:
            path.lstat()
        except FileNotFoundError:
            manifest.setdefault("absent_scopes", []).append(scope)
            if scope not in policy["optional_scopes"]:
                report["issues"].append(issue("missing_product_scope", diagnostic("missing_product_scope"), scope))
            continue
        except OSError as error:
            report["issues"].append(issue("filesystem_discovery_error", str(error), scope, owner=owner_for(scope, policy)))
            continue
        files, errors = discover(path)
        for failed_path, error in errors:
            name = failed_path.relative_to(root).as_posix()
            report["issues"].append(issue("filesystem_discovery_error", error, name, owner=owner_for(name, policy)))
            manifest.setdefault("discovery_errors", []).append({"path": name, "reason": error, "status": "BLOCKED"})
        for file in files:
            paths[file.relative_to(root).as_posix()] = file
    if not paths:
        report["issues"].append(issue("empty_product_scope", diagnostic("empty_product_scope")))
    suffixes = {suffix: scanner for scanner in policy["scanners"] for suffix in scanner["suffixes"]}
    allow = {(entry["path"], entry["byte_start"], entry["byte_end"]): entry for entry in policy["allowlist"]}
    used, generated_used = set(), set()
    generated = {entry["path"]: entry for entry in policy["generated_outputs"]}
    excluded = 0
    for name, path in sorted(paths.items()):
        owner = owner_for(name, policy)
        scanner = suffixes.get(path.suffix)
        record = {"path": name, "bytes": None, "owner": owner, "source_sha256": None,
                  "backend": scanner["backend"] if scanner else None, "status": "BLOCKED"}
        manifest["source_files"].append(record)
        try:
            record["bytes"] = path.lstat().st_size
        except OSError as error:
            report["issues"].append(issue("filesystem_discovery_error", str(error), name, owner=owner))
            continue
        rule = next((rule for rule in policy["exclusions"] if rule["path_segment"] in PurePosixPath(name).parts), None)
        if rule:
            record.update(status="EXCLUDED_TEST_FIXTURE", reason=rule["reason"])
            excluded += 1
            continue
        try:
            inside(root, name)
            if not stat.S_ISREG(path.stat().st_mode):
                raise ValueError("unsupported_filesystem_form")
            raw = path.read_bytes()
            record.update(bytes=len(raw), source_sha256=sha(raw))
            if name in generated:
                generated_used.add(name)
                record.update(status="GENERATED_VERIFIED", reason=generated[name]["reason"], generation=verify_generated(generated[name], root, raw))
                continue
            backend = record["backend"]
            if backend is None:
                errors, candidates = [issue("unregistered_file_form", diagnostic('unregistered_file_form'))], []
            else:
                candidates, errors = scan_source(raw, scanner)
            for error in errors:
                report["issues"].append({**error, "path": name, "owner": owner, "source_sha256": record["source_sha256"]})
            record["status"] = "BLOCKED" if errors else "SCANNED"
            for candidate in candidates:
                key = (name, candidate["byte_start"], candidate["byte_end"])
                review = allow.get(key)
                valid_review = review is not None and review["source_sha256"] == record["source_sha256"] and review["literal_sha256"] == candidate["literal_sha256"]
                if valid_review:
                    used.add(key)
                report["findings"].append({"path": name, "source_sha256": record["source_sha256"], "owner": owner,
                    **candidate, "status": "ALLOWED" if valid_review else "UNCLASSIFIED",
                    "category": review["category"] if valid_review else None,
                    "reason": review["reason"] if valid_review else diagnostic("unclassified_literal")})
        except (OSError, ValueError, UnicodeError, KeyError, ImportError) as error:
            report["issues"].append(issue("source_or_generation_error", str(error), name, owner=owner, source_sha256=record["source_sha256"]))
    for key, entry in allow.items():
        if key not in used:
            report["issues"].append(issue("stale_allowlist", diagnostic('stale_allowlist'), entry["path"], owner=owner_for(entry["path"], policy), byte_start=entry["byte_start"], byte_end=entry["byte_end"]))
    for name in generated.keys() - generated_used:
        report["issues"].append(issue("missing_generated_output", diagnostic('missing_generated_output'), name, owner=owner_for(name, policy)))
    statuses = Counter(row["status"] for row in report["findings"])
    blocked_files = {row["path"] for row in manifest["source_files"] if row["status"] == "BLOCKED"}
    stale = sum(error["code"] == "stale_allowlist" for error in report["issues"])
    report["counts"] = {"files": len(paths), "product_files": len(paths) - excluded, "excluded_test_fixture_files": excluded,
        "source_bytes": sum(row["bytes"] for row in manifest["source_files"] if row["status"] != "EXCLUDED_TEST_FIXTURE" and row["bytes"] is not None),
        "unknown_size_files": sum(row["bytes"] is None for row in manifest["source_files"]),
        "discovery_errors": sum(error["code"] == "filesystem_discovery_error" for error in report["issues"]),
        "candidates": len(report["findings"]), "allowed": statuses["ALLOWED"], "unclassified": statuses["UNCLASSIFIED"],
        "blocked_files": len(blocked_files), "stale_allowlist": stale,
        "verified_generated_files": sum(row["status"] == "GENERATED_VERIFIED" for row in manifest["source_files"])}
    owners = {}
    for owner in sorted({row["owner"] for row in manifest["source_files"]}):
        findings = [row for row in report["findings"] if row["owner"] == owner]
        owners[str(owner)] = {"files": sum(row["owner"] == owner for row in manifest["source_files"]),
            "unclassified": sum(row["status"] == "UNCLASSIFIED" for row in findings),
            "allowed": sum(row["status"] == "ALLOWED" for row in findings),
            "blocked_files": sum(row["owner"] == owner and row["status"] == "BLOCKED" for row in manifest["source_files"])}
    report["by_owner"] = owners
    manifest["outside_scope"] = []
    for boundary in policy["outside_scope"]:
        path = inside(root, boundary["path"])
        files, errors = discover(path) if path.exists() else ([], [])
        for failed_path, error in errors:
            report["issues"].append(issue("filesystem_discovery_error", error, failed_path.relative_to(root).as_posix()))
        manifest["outside_scope"].append({**boundary, "files": len(files), "bytes": sum(file.lstat().st_size for file in files), "content_scanned": False})
    report["valid"] = not report["issues"] and not report["counts"]["unclassified"]
    report["source_manifest_sha256"] = sha(json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8"))
    return report, manifest


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--pack", type=Path, default=POLICY)
    parser.add_argument("--schema", type=Path, default=SCHEMA)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args(argv)
    destinations = [path.resolve() for path in (args.report, args.manifest) if path]
    inputs = {args.pack.resolve(), args.schema.resolve()}
    if len(destinations) != len(set(destinations)) or any(path in inputs for path in destinations):
        print(json.dumps({"event": "output_collision_or_input_overwrite"}), file=sys.stderr)
        return 2
    try:
        report, manifest = audit(args.root.resolve(), args.pack.resolve(), args.schema.resolve())
        code = 0 if report["valid"] else 1
    except Exception as error:
        report = {"schema": REPORT_SCHEMA, "valid": False, "counts": {"files": 0, "candidates": 0, "allowed": 0, "unclassified": 0, "blocked_files": 0, "stale_allowlist": 0},
                  "findings": [], "issues": [issue("configuration_error", str(error))],
                  "io": {"network_calls": 0, "source_writes": 0, "generator_output_writes": 0}}
        manifest = {"schema": MANIFEST_SCHEMA, "source_files": []}
        code = 2
    protected = set(inputs)
    protected.add(Path(__file__).resolve())
    protected.update((POLICY.resolve(), SCHEMA.resolve()))
    for source in manifest["source_files"]:
        protected.add((args.root / source["path"]).resolve())
        for field in source.get("generation", {}).get("inputs", []):
            protected.add((args.root / field["path"]).resolve())
        generator = source.get("generation", {}).get("generator")
        if generator:
            protected.add((args.root / generator["path"]).resolve())
    scopes = [args.root.resolve() / scope for scope in manifest.get("declared_scopes", [])]
    if code == 2:
        # Error reporting cannot become a source-write path. These roots only
        # add write protection; neither policy grants an acceptance fallback.
        for pack in (POLICY, args.pack):
            try:
                candidate, _ = read_json(pack)
                for scope in candidate.get("scopes", []):
                    scopes.append(inside(args.root.resolve(), scope))
            except (OSError, ValueError, UnicodeError, AttributeError, TypeError):
                if pack == POLICY:
                    scopes.append(args.root.resolve())
        for path in destinations:
            if path.exists():
                try:
                    previous, _ = read_json(path)
                    if previous.get("schema") not in (REPORT_SCHEMA, MANIFEST_SCHEMA):
                        protected.add(path)
                except (OSError, ValueError, UnicodeError, AttributeError):
                    protected.add(path)
    # Path normalization does not identify aliases sharing a protected inode.
    # Include directory entries even on the configuration-error branch.
    for scope in scopes:
        try:
            scope.lstat()
        except FileNotFoundError:
            continue
        except OSError as error:
            print(json.dumps({"event": "output_protection_discovery_failed", "error": str(error)}), file=sys.stderr)
            return 2
        files, errors = discover(scope)
        if errors:
            print(json.dumps({"event": "output_protection_discovery_failed", "errors": [{"path": str(path), "error": error} for path, error in errors]}), file=sys.stderr)
            return 2
        protected.update(files)
    try:
        identities = {(stat.st_dev, stat.st_ino) for path in protected if path.exists() for stat in (path.stat(),)}
        aliases = [path for path in destinations if path.exists() and (path.stat().st_dev, path.stat().st_ino) in identities]
        output_identities = [(path.stat().st_dev, path.stat().st_ino) for path in destinations if path.exists()]
    except OSError as error:
        print(json.dumps({"event": "output_identity_check_failed", "error": str(error)}), file=sys.stderr)
        return 2
    if aliases:
        print(json.dumps({"event": "output_aliases_protected_input"}), file=sys.stderr)
        return 2
    if len(output_identities) != len(set(output_identities)):
        print(json.dumps({"event": "output_destination_alias_collision"}), file=sys.stderr)
        return 2
    if any(path in protected or any(path == scope.resolve() or path.is_relative_to(scope.resolve()) for scope in scopes) for path in destinations):
        print(json.dumps({"event": "output_would_overwrite_product_input"}), file=sys.stderr)
        return 2
    for path, value in ((args.report, report), (args.manifest, manifest)):
        if path:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"schema": REPORT_SCHEMA, "valid": report["valid"], "counts": report["counts"], "io": report["io"]}, sort_keys=True))
    if report["issues"]:
        print(json.dumps({"issue_count": len(report["issues"]), "issue_codes": dict(Counter(error["code"] for error in report["issues"]))}, sort_keys=True), file=sys.stderr)
    return code if args.check else (2 if code == 2 else 0)


if __name__ == "__main__":
    raise SystemExit(main())
