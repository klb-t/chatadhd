"""Offline DATA-ONLY import recipes pinned to W5 source; not engine execution.

The complete public source snapshots make this test portable without fetching
another branch. W5 must still wire its engine/CLI to RuntimeProfile and retain
its runtime/provenance regression gates. jsonschema is already a contract-test
dependency; this test never creates a Runtime or calls a provider.
"""
from __future__ import annotations

import ast
import copy
import hashlib
import json
from pathlib import Path
import re
import unittest

import jsonschema


ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = ROOT / "docs/reports/data-in-code/evidence/import-profile-data"
DOMAINS = ("import_formats", "import", "import_audit")


def strings(text: str) -> list[str]:
    return [ast.literal_eval('"' + value + '"')
            for value in re.findall(r'"((?:[^"\\]|\\.)*)"', text)]


def one(pattern: str, text: str) -> re.Match:
    matches = list(re.finditer(pattern, text, re.MULTILINE | re.DOTALL))
    if len(matches) != 1:
        raise ValueError(f"Expected one source binding, got {len(matches)}: {pattern}")
    return matches[0]


def scalar_literal(text: str):
    text = text.strip().replace("'", "")
    if text in ("true", "false"):
        return text == "true"
    return ast.literal_eval(text)


def cpp_fields(source: str, struct: str) -> dict:
    body = one(r"struct " + struct + r"\s*\{(.*?)\n\};", source).group(1)
    result = {}
    for field, value in re.findall(r"(?:bool|double|std::size_t|std::int64_t)\s+(\w+)\s*=\s*([^;]+);", body):
        result[field] = scalar_literal(value)
    for field in re.findall(r"std::optional<[^>]+>\s+(\w+)\s*;", body):
        result[field] = None
    mode = re.search(r"ExportMode\s+export_mode\s*=\s*ExportMode::(\w+)", body)
    if mode:
        result["export_mode"] = mode.group(1).lower()
    return result


def py_function_defaults(source: str, name: str) -> dict:
    function = next(node for node in ast.parse(source).body
                    if isinstance(node, ast.FunctionDef) and node.name == name)
    return {argument.arg: ast.literal_eval(value)
            for argument, value in zip(function.args.kwonlyargs, function.args.kw_defaults)
            if value is not None}


def py_cli_defaults(source: str) -> dict:
    result = {}
    for node in ast.walk(ast.parse(source)):
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                and node.func.attr == "add_argument"):
            continue
        for keyword in node.keywords:
            if keyword.arg == "default":
                result[ast.literal_eval(node.args[0]).removeprefix("--").replace("-", "_")] = ast.literal_eval(keyword.value)
    return result


def vector(source: str, name: str) -> list[str]:
    return strings(one(r"\b" + re.escape(name) + r"\s*=\s*\{(.*?)\};", source).group(1))


def source_defaults(sources: dict[str, str]) -> dict:
    core = sources["loom/src/import/importer_core.cpp"]
    common = sources["loom/src/import/importer_common.cpp"]
    text = sources["loom/src/import/importer_text.cpp"]
    cli = sources["loom/cli/main.cpp"]
    audit_python = sources["loom/tools/eval/archive_cost.py"]
    detection = one(r"std::string ConversationImporter::detect_format.*?\n\}(?=\n)", core).group(0)
    extension_rules = []
    for condition, format_name in re.findall(r'if \((ext == .*?)\) return "([^"\\]+)";', detection):
        extension_rules.append({"extensions": strings(condition), "format": format_name})
    mime_body = one(r"std::string mime_for_format.*?\n\}", common).group(0)
    mime_by_format = dict(re.findall(r'if \(fmt == "([^"\\]+)"\) return "([^"\\]+)";', mime_body))
    modes = one(r"enum class ExportMode \{(.*?)\}", sources["loom/include/loom/importer.h"]).group(1)
    md_user = one(r'if \(idt::istarts_with\(lower, "## human"\)(.*?)\) \{\s*flush\(\);\s*role = "user";', text).group(0)
    md_assistant = one(r'else if \(idt::istarts_with\(lower, "## assistant"\)(.*?)\) \{\s*flush\(\);\s*role = "assistant";', text).group(0)
    formats = {
        "detection": {
            "extension_rules": extension_rules,
            "sniff_bytes": int(one(r"std::string header\((\d+),", detection).group(1)),
            "zip_prefix": strings(one(r'header\.compare\(0, 4, "(.*?)"\) == 0', detection).group(0))[0],
            "sqlite_contains": strings(one(r'header\.find\("(.*?)"\)', detection).group(0))[0],
            "json_first_non_whitespace": re.findall(r"header\[i\] == '([^']+)'", one(r"if \(i < header.size\(\) && \(header\[i\] ==.*?return \"json\";", detection).group(0)),
            "html_contains_case_insensitive": re.findall(r'idt::icontains\(header, "([^"\\]+)"\)', detection),
            "fallback_format": strings(one(r'return "unknown";', detection).group(0))[0],
        },
        "mime_by_format": mime_by_format,
        "fallback_mime": strings(one(r'return "application/octet-stream";', mime_body).group(0))[0],
        "export_mode_names": [value.strip().lower() for value in modes.split(",")],
        "legacy_json_roles": {
            "field_priority": ["role", "sender"], "absent_role": "user",
            "user_inputs": ["user", "human"], "excluded_roles": ["system"], "other_role": "assistant",
        },
        "legacy_sqlite_roles": {"user_inputs": ["user", "human"], "other_role": "assistant"},
        "provider_exports": {
            "openai": {"source": "import:openai", "conversation_field": "mapping", "absent_role": "unknown"},
            "anthropic": {"source": "import:anthropic", "conversation_field": "chat_messages", "role_map": {"human": "user"}, "absent_role": "assistant"},
            "generic_message_wrappers": strings(one(r'for \(const char\* key : \{(.*?)\}\)', sources["loom/src/import/importer_json.cpp"]).group(1)),
            "streamed_conversations_wrapper": strings(one(r'if \(name == "conversations" &&', sources["loom/src/import/export_common.cpp"]).group(0))[0],
        },
        "text_formats": {
            "markdown": {"user_prefixes": strings(md_user)[:-1], "assistant_prefixes": strings(md_assistant)[:-1], "separators": ["---", "***"], "fallback_role": "assistant"},
            "plain_text": {"user_labels": vector(text, "kUser"), "assistant_labels": vector(text, "kAssistant"), "label_order": vector(text, "all_labels"), "fallback_role": "assistant"},
            "screenshot": {"user_words": vector(text, "kUserWords"), "assistant_words": vector(text, "kAiWords"),
                           "user_arrows": ["►", "▶", "→", ">"], "assistant_arrows": ["◄", "◀", "←", "<"],
                           "user_sidebar_labels": ["h", "human"], "assistant_sidebar_labels": ["a", "assistant"],
                           "block_length_exclusive_minimum": 3, "unlabelled_user_length_exclusive_maximum": 50,
                           "unlabelled_user_question_suffix": "?", "ocr_text_minimum_codepoints": 10,
                           "fallback_role": "assistant", "title_prefix": "Screenshot ", "title_time_format": "%Y-%m-%d %H:%M"},
        },
    }
    # These captures bind the asymmetric legacy/provider rules, not an invented
    # uniform role mapping. Their complete source snapshots remain retained.
    required = {
        "loom/src/import/importer_common.cpp": ['role_v = json::find(msg, "role")', 'role_v = json::find(msg, "sender")', 'role_str = "user"', 'role_str == "system") return std::nullopt', 'role_str == "user" || role_str == "human"'],
        "loom/src/import/importer_sqlite.cpp": ['raw == "user" || raw == "human") ? "user" : "assistant"'],
        "loom/src/import/export_openai.cpp": ['out.source = "import:openai"', 'f.role = role.empty() ? "unknown" : role', 'getp(conv, "mapping")'],
        "loom/src/import/export_anthropic.cpp": ['out.source = "import:anthropic"', 'sender == "human" ? "user" : (sender.empty() ? "assistant" : sender)', 'getp(conv, "chat_messages")'],
        "loom/src/import/importer_text.cpp": ['lower_line == "h" || lower_line == "human"', 'lower_line == "a" || lower_line == "assistant"', 'utf8::length(joined) > 3', 'utf8::length(line) < 50', "line.back() == '?'", 'utf8::strip(ocr->text))) < 10', '"Screenshot " + timeutil::local_now_format("%Y-%m-%d %H:%M")', '{"►", "▶", "→", ">"}', '{"◄", "◀", "←", "<"}', 'idt::istarts_with(lower, "---") || idt::istarts_with(lower, "***")'],
    }
    for path, fragments in required.items():
        for fragment in fragments:
            if fragment not in sources[path]:
                raise ValueError(f"Missing role/text source binding: {path}: {fragment}")
    if 'o.resume = !a.has("no-resume")' not in cli or 'o.include_result_metadata = a.has("full-result")' not in cli:
        raise ValueError("CLI caller preset source changed")
    import_values = {"library": cpp_fields(sources["loom/include/loom/importer.h"], "ImportOptions"),
                     "cli_overrides": {"resume": True, "include_result_metadata": False}}
    py_defaults = py_function_defaults(audit_python, "estimate")
    native_audit = cpp_fields(sources["loom/src/import/import_audit.h"], "ImportAuditOptions")
    shared = {field: native_audit.pop(field) for field in ("chars_per_token_low", "chars_per_token_high", "output_ratio", "input_price", "output_price")}
    for field in ("chars_per_token_low", "chars_per_token_high", "output_ratio"):
        if py_defaults.pop(field) != shared[field]:
            raise ValueError(f"Native/Python estimator default diverged: {field}")
    py_cli = py_cli_defaults(audit_python)
    for field in ("chars_per_token_low", "chars_per_token_high", "output_ratio", "fraction"):
        if py_cli.pop(field) != (shared[field] if field in shared else py_defaults[field]):
            raise ValueError(f"Python CLI default diverged: {field}")
    py_tree = ast.parse(audit_python)
    constants = {node.targets[0].id: ast.literal_eval(node.value) for node in py_tree.body
                 if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name)
                 and node.targets[0].id in ("STATUS_CLASSES", "TOOL_ROLES")}
    audit_values = {"estimator": shared, "native_scope": native_audit,
                    "python_estimate": py_defaults, "python_cli": py_cli,
                    "status_classes": list(constants["STATUS_CLASSES"]), "tool_roles": list(constants["TOOL_ROLES"]),
                    "python_inventory": {"sqlite_cache_kib": py_function_defaults(audit_python, "archive_stats")["sqlite_cache_kib"]}}
    return {"import_formats": formats, "import": import_values, "import_audit": audit_values}


def captured_sources() -> tuple[dict, dict[str, str]]:
    manifest = json.loads((EVIDENCE / "source-manifest.json").read_text(encoding="utf-8"))
    sources = {}
    for row in manifest["sources"]:
        raw = (EVIDENCE / row["snapshot"]).read_bytes()
        if hashlib.sha256(raw).hexdigest() != row["sha256"] or len(raw) != row["bytes"]:
            raise ValueError(f"Source snapshot drift: {row['path']}")
        blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
        if blob != row["git_blob_oid"]:
            raise ValueError(f"Git blob binding drift: {row['path']}")
        sources[row["path"]] = raw.decode("utf-8")
    return manifest, sources


def load_profiles() -> dict:
    def reject_constant(value):
        raise ValueError(f"Nonfinite JSON constant: {value}")
    return {domain: json.loads((ROOT / f"loom/data/runtime/{domain}.pack").read_text(encoding="utf-8"), parse_constant=reject_constant)
            for domain in DOMAINS}


def frozen_audit(sources: dict[str, str]) -> dict:
    """Load the retained public Python tool, never its main()/database reader."""
    path = EVIDENCE / "sources/loom_tools_eval_archive_cost.py.txt"
    namespace = {"__name__": "frozen_import_profile_audit", "__file__": str(path)}
    exec(compile(sources["loom/tools/eval/archive_cost.py"], str(path), "exec"), namespace)
    return namespace


def estimator_fixture() -> dict:
    # Aggregate-only synthetic fixture: no conversation text, IDs or prices.
    return {"conversations": 2, "chars_active": 24, "chars_versions": 12,
            "chars_unknown_status": 8, "message_groups": [
                {"status_class": "active", "tool": False, "messages": 1, "chars": 24},
                {"status_class": "version", "tool": False, "messages": 1, "chars": 12},
                {"status_class": "unknown", "tool": True, "messages": 1, "chars": 8},
                {"status_class": "excluded", "tool": False, "messages": 1, "chars": 4},
                {"status_class": "deleted", "tool": False, "messages": 1, "chars": 4}],
            "conversation_scope_groups": [
                {"non_tool_statuses": ["active", "version", "excluded"], "tool_statuses": [], "conversations": 1},
                {"non_tool_statuses": ["deleted"], "tool_statuses": ["unknown"], "conversations": 1}]}


class ImportProfileData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest, cls.sources = captured_sources()
        cls.expected = source_defaults(cls.sources)
        cls.profiles = load_profiles()

    def test_frozen_source_and_default_bindings(self):
        self.assertEqual(self.manifest["source_commit"], "4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2")
        for domain in DOMAINS:
            with self.subTest(domain=domain):
                self.assertEqual(self.profiles[domain]["defaults"], self.expected[domain])
                self.assertEqual(json.dumps(self.profiles[domain]["defaults"], sort_keys=True, ensure_ascii=False, allow_nan=False),
                                 json.dumps(self.expected[domain], sort_keys=True, ensure_ascii=False, allow_nan=False))

    def test_source_drift_is_explicit(self):
        sources = dict(self.sources)
        sources["loom/include/loom/importer.h"] = sources["loom/include/loom/importer.h"].replace("bool resume = false;", "bool resume = true;")
        self.assertNotEqual(source_defaults(sources)["import"], self.profiles["import"]["defaults"])

    def test_supported_schema_and_roundtrip(self):
        supported = {"type", "required", "properties", "items", "additionalProperties", "minimum", "maximum", "minLength", "minItems", "enum", "description", "title", "x-setting", "x-unit", "x-consumer"}
        def check(node):
            self.assertFalse(set(node) - supported)
            for child in node.get("properties", {}).values(): check(child)
            if "items" in node: check(node["items"])
            if isinstance(node.get("additionalProperties"), dict): check(node["additionalProperties"])
        for domain, profile in self.profiles.items():
            with self.subTest(domain=domain):
                self.assertEqual(profile["schema"], "loom.runtime_profile/1")
                self.assertEqual(profile["domain"], domain)
                self.assertEqual(profile["revision"], 1)
                check(profile["value_schema"])
                jsonschema.Draft202012Validator.check_schema(profile["value_schema"])
                jsonschema.validate(profile["defaults"], profile["value_schema"])
                self.assertEqual(profile, json.loads(json.dumps(profile, ensure_ascii=False, allow_nan=False)))

    def test_native_and_cli_are_separate_layers(self):
        values = self.profiles["import"]["defaults"]
        self.assertFalse(values["library"]["resume"])
        self.assertTrue(values["library"]["include_result_metadata"])
        effective_cli = values["library"] | values["cli_overrides"]
        self.assertTrue(effective_cli["resume"])
        self.assertFalse(effective_cli["include_result_metadata"])
        self.assertEqual(set(values["cli_overrides"]), {"resume", "include_result_metadata"})

    def test_open_aliases_and_large_or_unbounded_presets(self):
        formats = copy.deepcopy(self.profiles["import_formats"]["defaults"])
        formats["detection"]["extension_rules"].append({"extensions": [".custom-export"], "format": "custom"})
        formats["mime_by_format"]["custom"] = "application/x-custom"
        formats["legacy_json_roles"]["user_inputs"].append("owner")
        jsonschema.validate(formats, self.profiles["import_formats"]["value_schema"])
        settings = copy.deepcopy(self.profiles["import"]["defaults"])
        settings["library"].update(json_read_chunk_bytes=2**30, json_max_depth=0, generic_inference_max_bytes=0)
        settings["future_caller_layer"] = {"policy": None}
        jsonschema.validate(settings, self.profiles["import"]["value_schema"])
        audit = copy.deepcopy(self.profiles["import_audit"]["defaults"])
        audit["estimator"].update(input_price=1e9, output_price=1e9)
        jsonschema.validate(audit, self.profiles["import_audit"]["value_schema"])

    def test_malformed_typed_settings_are_rejected(self):
        mutations = [("import", ("library", "json_read_chunk_bytes"), 0),
                     ("import", ("library", "json_max_depth"), -1),
                     ("import", ("library", "resume"), "false"),
                     ("import_audit", ("estimator", "input_price"), -1),
                     ("import_audit", ("estimator", "output_ratio"), -1),
                     ("import_formats", ("text_formats", "plain_text", "user_labels"), "User")]
        for domain, path, value in mutations:
            with self.subTest(domain=domain, path=path):
                settings = copy.deepcopy(self.profiles[domain]["defaults"])
                parent = settings
                for key in path[:-1]: parent = parent[key]
                parent[path[-1]] = value
                with self.assertRaises(jsonschema.ValidationError):
                    jsonschema.validate(settings, self.profiles[domain]["value_schema"])
        settings = copy.deepcopy(self.profiles["import"]["defaults"])
        del settings["library"]["export_mode"]
        with self.assertRaises(jsonschema.ValidationError):
            jsonschema.validate(settings, self.profiles["import"]["value_schema"])

    def test_legacy_role_asymmetry_and_ocr_byte_format(self):
        values = self.profiles["import_formats"]["defaults"]
        self.assertEqual(values["legacy_json_roles"]["excluded_roles"], ["system"])
        self.assertNotIn("excluded_roles", values["legacy_sqlite_roles"])
        self.assertEqual(values["legacy_sqlite_roles"]["other_role"], "assistant")
        self.assertEqual(values["provider_exports"]["openai"]["absent_role"], "unknown")
        self.assertEqual(values["provider_exports"]["anthropic"]["role_map"], {"human": "user"})
        self.assertEqual(values["detection"]["json_first_non_whitespace"], ["{", "["])
        self.assertIn('media_->ocr_bytes(image, format)', self.sources["loom/src/import/importer_text.cpp"])
        self.assertIn('const std::string mime = image_format.empty() ? idt::mime_for_format(fmt) : "image/" + image_format', self.sources["loom/src/import/importer_core.cpp"])
        self.assertEqual(values["mime_by_format"]["screenshot"], "image/*")

    def test_estimator_scope_and_prices_stay_explicit(self):
        values = self.profiles["import_audit"]["defaults"]
        self.assertEqual(values["python_estimate"]["prefix_tokens"], 8000)
        self.assertEqual(values["python_cli"]["prefix_tokens"], 0)
        self.assertEqual(values["python_cli"]["scope"], "all")
        self.assertIsNone(values["estimator"]["input_price"])
        self.assertIsNone(values["estimator"]["output_price"])
        self.assertEqual(values["status_classes"], ["active", "version", "excluded", "deleted", "unknown"])
        self.assertEqual(values["tool_roles"], ["tool", "function"])

    def test_frozen_python_estimator_default_output_parity(self):
        estimate = frozen_audit(self.sources)["estimate"]
        values = self.profiles["import_audit"]["defaults"]
        recipe = values["python_estimate"] | {key: value for key, value in values["estimator"].items()
                                             if key not in ("input_price", "output_price")}
        fixture = estimator_fixture()
        before = estimate(fixture)
        after = estimate(fixture, **recipe)
        self.assertEqual(before, after)
        self.assertEqual(before["tokens"], {"low": 11, "high": 15})
        self.assertEqual(before["prefix_tokens"], 16000)
        self.assertEqual(before["model_calls"], 0)
        self.assertIsNone(before["models"])
        self.assertNotEqual(before, estimate(fixture, **(recipe | {"prefix_tokens": 0})))

    def test_existing_audit_semantic_validator_is_still_required(self):
        estimate = frozen_audit(self.sources)["estimate"]
        values = copy.deepcopy(self.profiles["import_audit"]["defaults"])
        for low, high in ((0, 0), (2, 3)):
            values["estimator"].update(chars_per_token_low=low, chars_per_token_high=high)
            # The supported schema subset deliberately does not invent a
            # positive floor or pretend it can express cross-field ordering.
            jsonschema.validate(values, self.profiles["import_audit"]["value_schema"])
            with self.assertRaises(ValueError):
                estimate(estimator_fixture(), chars_per_token_low=low, chars_per_token_high=high)
        native = self.sources["loom/src/import/import_audit.cpp"]
        self.assertIn("o.chars_per_token_low <= 0 || o.chars_per_token_high <= 0", native)
        self.assertIn("o.chars_per_token_low < o.chars_per_token_high", native)
        self.assertIn("o.input_price.has_value() != o.output_price.has_value()", native)

    def test_data_does_not_claim_engine_activation(self):
        self.assertFalse(self.manifest["engine_binding"]["import"])
        self.assertFalse(self.manifest["engine_binding"]["audit"])
        self.assertFalse(self.manifest["engine_binding"]["archive_shared_scanner"])
        for profile in self.profiles.values():
            self.assertIn("UNWIRED", profile["description"])
            self.assertIn("UNWIRED", profile["value_schema"]["description"])


if __name__ == "__main__":
    unittest.main()
