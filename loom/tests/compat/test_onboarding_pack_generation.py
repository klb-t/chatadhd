"""Offline CLI regressions for canonical onboarding data compilation.

Every invocation runs a copied generator in an isolated data tree. The tests
exercise written artifacts and exit behavior rather than duplicating its
reference/pointer implementation or modifying the repository's sources.
"""
from copy import deepcopy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


LOOM_ROOT = Path(__file__).resolve().parents[2]
GENERATOR = LOOM_ROOT / "src/onboarding/gen_onboarding_pack.py"
CANONICAL_FILES = (
    "profiles/user.pack",
    "onboarding/scenario.pack",
    "onboarding/ui.pack",
)

# Approved expanded defaults before the include migration (3c0bc36). These
# independent compatibility values make accidental policy changes visible.
BASELINE_DEFAULTS = {
    "settings": {"preference_mode": "ask"},
    "privacy": {
        "rules": [{
            "id": "local-explicit-preset",
            "category": "*",
            "store": True,
            "infer": False,
            "explicit_only": True,
            "providers": [],
            "max_detail": None,
            "max_sensitivity": None,
            "retention": {
                "history": "full",
                "max_events": None,
                "metadata_keys": [
                    "id", "op", "field", "category", "time", "provenance",
                    "review", "review_time", "presentation", "detail",
                    "sensitivity", "candidate", "decision", "status",
                    "source_refs", "method_version_id", "method_run_id",
                ],
            },
            "provenance": "builtin_preset",
            "consent_event": None,
        }],
    },
}


class OnboardingPackGenerationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="loom-onboarding-generator-")
        self.addCleanup(temporary.cleanup)
        self.workspace = Path(temporary.name)
        self.root = self.workspace / "loom"
        self.data = self.root / "data"
        self.script = self.root / "src/onboarding/gen_onboarding_pack.py"
        self.script.parent.mkdir(parents=True)
        shutil.copyfile(GENERATOR, self.script)
        for relative in CANONICAL_FILES:
            target = self.data / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(LOOM_ROOT / "data" / relative, target)
        self.native = self.script.with_name("builtin.inc")
        self.web = self.root / "web/src/onboarding/generated/ui.json"
        self.caller = self.workspace / "unrelated-cwd"
        self.caller.mkdir()

    def read_document(self, relative):
        return json.loads((self.data / relative).read_text(encoding="utf-8"))

    def write_document(self, relative, value):
        target = self.data / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    def invoke(self, *arguments):
        return subprocess.run(
            [sys.executable, str(self.script), *arguments],
            cwd=self.caller, capture_output=True, text=True, timeout=10,
        )

    def generate(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.native.is_file())
        self.assertTrue(self.web.is_file())
        return self.artifact_bytes()

    def artifact_bytes(self):
        return {"native": self.native.read_bytes(), "web": self.web.read_bytes()}

    def embedded(self, name):
        text = self.native.read_text(encoding="utf-8")
        marker = f'static constexpr char {name}[] = R"loom_onboard('
        self.assertEqual(text.count(marker), 1, f"missing or duplicate native declaration: {name}")
        raw = text.split(marker, 1)[1].split(')loom_onboard";', 1)[0]
        return json.loads(raw)

    def invalid_preserves_artifacts(self, expected_message):
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(expected_message, result.stderr)
        self.assertEqual(self.artifact_bytes(), self.valid_artifacts)

    def set_include(self, value):
        scenario = self.read_document("onboarding/scenario.pack")
        scenario["synthetic_reference"] = value
        self.write_document("onboarding/scenario.pack", scenario)

    def test_expansion_preserves_approved_runtime_defaults_and_source_bytes(self):
        before = {relative: (self.data / relative).read_bytes() for relative in CANONICAL_FILES}
        self.generate()
        pack = self.embedded("kOnboardingPack")
        self.assertEqual(pack["runtime_definition"]["defaults"], BASELINE_DEFAULTS)
        canonical = self.read_document("profiles/user.pack")
        by_key = {entry["key"]: entry["value"] for entry in canonical["entries"]}
        self.assertEqual(by_key["onboarding.settings"], BASELINE_DEFAULTS["settings"])
        self.assertEqual(by_key["onboarding.privacy"], BASELINE_DEFAULTS["privacy"])
        self.assertEqual(
            canonical["runtime_definition"]["defaults"],
            {"$layer_bindings": "runtime_bindings"},
        )
        after = {relative: (self.data / relative).read_bytes() for relative in CANONICAL_FILES}
        self.assertEqual(after, before, "generation must not rewrite canonical settings/privacy or UI sources")
        self.assertEqual(self.embedded("kOnboardingScenario"), self.read_document("onboarding/scenario.pack"))
        ui = self.read_document("onboarding/ui.pack")
        self.assertEqual(self.embedded("kOnboardingPresentation"), ui)
        self.assertEqual(json.loads(self.web.read_text(encoding="utf-8")), ui)
        presentation = next(entry for entry in pack["entries"] if entry["key"] == "presentation.onboarding")
        self.assertEqual(presentation["value"], ui)
        self.assertNotIn("$include", presentation["value"])
        self.assertEqual(self.invoke("--check").returncode, 0)

    def test_canonical_ui_edit_changes_both_consumers_and_native_presentation_layer(self):
        original = self.generate()
        pack_source = (self.data / "profiles/user.pack").read_bytes()
        ui = self.read_document("onboarding/ui.pack")
        ui["locales"]["en"]["field.submit"] = "Żółć – synthetic edited presentation"
        ui["defaults"]["rows"]["field"] = 9
        self.write_document("onboarding/ui.pack", ui)
        stale = self.invoke("--check")
        self.assertNotEqual(stale.returncode, 0)
        self.assertIn("stale", stale.stderr)
        self.assertEqual(self.artifact_bytes(), original)
        changed = self.generate()
        self.assertNotEqual(changed["native"], original["native"])
        self.assertNotEqual(changed["web"], original["web"])
        self.assertEqual(self.embedded("kOnboardingPresentation"), ui)
        self.assertEqual(json.loads(changed["web"].decode("utf-8")), ui)
        presentation = next(entry for entry in self.embedded("kOnboardingPack")["entries"]
                            if entry["key"] == "presentation.onboarding")
        self.assertEqual(presentation["value"], ui)
        self.assertEqual(self.embedded("kOnboardingPack")["runtime_definition"]["defaults"], BASELINE_DEFAULTS)
        self.assertEqual((self.data / "profiles/user.pack").read_bytes(), pack_source)
        self.assertEqual(self.invoke("--check").returncode, 0)

    def test_generation_is_byte_deterministic_from_unrelated_working_directory(self):
        first = self.generate()
        self.assertEqual(self.generate(), first)
        result = self.invoke("--check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(any(self.caller.iterdir()), "outputs must be relative to the generator's own data tree")

    def test_nested_and_repeated_includes_keep_complete_arbitrary_json_values(self):
        values = [None, False, 17, "synthetic text", {"unicode": "Zażółć", "items": [1, 2]}]
        self.write_document("shared/value.pack", values)
        self.write_document("shared/wrapper.pack", {"first": {"$include": "shared/value.pack"}})
        self.set_include([{"$include": "shared/wrapper.pack"}, {"$include": "shared/value.pack"}])
        self.generate()
        self.assertEqual(self.embedded("kOnboardingScenario")["synthetic_reference"], [{"first": values}, values])

    def test_invalid_include_shapes_do_not_replace_previous_valid_artifacts(self):
        self.valid_artifacts = self.generate()
        for invalid in ({"$include": 17}, {"$include": None}, {"$include": []},
                        {"$include": "onboarding/ui.pack", "extra": True}):
            with self.subTest(include=invalid):
                self.set_include(invalid)
                self.invalid_preserves_artifacts("data include needs exactly one string reference")

    def test_outside_relative_and_absolute_references_are_rejected_before_read(self):
        self.valid_artifacts = self.generate()
        outside = self.root / "outside.pack"
        outside.write_text("synthetic-invalid-JSON-should-not-be-read", encoding="utf-8")
        for reference in ("../outside.pack", str(outside)):
            with self.subTest(reference=reference):
                self.set_include({"$include": reference})
                self.invalid_preserves_artifacts("data reference leaves the canonical data directory")
        self.assertEqual(outside.read_text(encoding="utf-8"), "synthetic-invalid-JSON-should-not-be-read")

    def test_normalized_references_remaining_inside_data_are_permitted(self):
        self.set_include({"$include": "onboarding/../onboarding/ui.pack"})
        self.generate()
        self.assertEqual(self.embedded("kOnboardingScenario")["synthetic_reference"], self.read_document("onboarding/ui.pack"))

    def test_symlink_cannot_escape_canonical_data_boundary(self):
        self.valid_artifacts = self.generate()
        outside = self.root / "outside.pack"
        outside.write_text("synthetic-invalid-JSON-should-not-be-read", encoding="utf-8")
        link = self.data / "shared/outside.pack"
        link.parent.mkdir()
        try:
            link.symlink_to(outside)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"symlinks unavailable: {error}")
        self.set_include({"$include": "shared/outside.pack"})
        self.invalid_preserves_artifacts("data reference leaves the canonical data directory")

    def test_direct_and_indirect_include_cycles_are_rejected_without_partial_writes(self):
        self.valid_artifacts = self.generate()
        for documents in (
            {"shared/first.pack": {"$include": "shared/first.pack"}},
            {"shared/first.pack": {"$include": "shared/second.pack"},
             "shared/second.pack": {"next": {"$include": "shared/first.pack"}}},
        ):
            with self.subTest(documents=documents):
                for relative, value in documents.items():
                    self.write_document(relative, value)
                self.set_include({"$include": "shared/first.pack"})
                self.invalid_preserves_artifacts("cyclic data reference")

    def test_missing_include_target_does_not_replace_previous_valid_artifacts(self):
        self.valid_artifacts = self.generate()
        self.set_include({"$include": "shared/missing.pack"})
        self.invalid_preserves_artifacts("missing.pack")

    def test_runtime_bindings_support_escaped_keys_and_preserve_array_values(self):
        pack = self.read_document("profiles/user.pack")
        value = ["synthetic value", None, False, {"nested": [1, 2]}]
        pack["entries"].append({"key": "synthetic.binding", "value": value})
        pack["runtime_bindings"] = {"/communication/a~1b~0c": "synthetic.binding"}
        self.write_document("profiles/user.pack", pack)
        self.generate()
        self.assertEqual(self.embedded("kOnboardingPack")["runtime_definition"]["defaults"],
                         {"communication": {"a/b~c": value}})

    def test_missing_runtime_binding_maps_and_entries_fail_without_partial_writes(self):
        self.valid_artifacts = self.generate()
        original = self.read_document("profiles/user.pack")
        absent_map = deepcopy(original)
        absent_map["runtime_definition"]["defaults"]["$layer_bindings"] = "absent-binding-map"
        self.write_document("profiles/user.pack", absent_map)
        self.invalid_preserves_artifacts("runtime defaults directive names no binding map")
        absent_entry = deepcopy(original)
        absent_entry["runtime_bindings"]["/synthetic"] = "absent-default-entry"
        self.write_document("profiles/user.pack", absent_entry)
        self.invalid_preserves_artifacts("runtime binding names no default entry")

    def test_overlapping_runtime_pointers_are_rejected_in_either_insertion_order(self):
        self.valid_artifacts = self.generate()
        original = self.read_document("profiles/user.pack")
        for bindings in (
            {"/settings": "onboarding.settings", "/settings/child": "onboarding.privacy"},
            {"/settings/child": "onboarding.privacy", "/settings": "onboarding.settings"},
        ):
            with self.subTest(bindings=bindings):
                pack = deepcopy(original)
                pack["runtime_bindings"] = bindings
                self.write_document("profiles/user.pack", pack)
                self.invalid_preserves_artifacts("overlapping runtime binding pointers")

    def test_invalid_runtime_pointer_syntax_is_rejected(self):
        self.valid_artifacts = self.generate()
        original = self.read_document("profiles/user.pack")
        for pointer, message in (
            ("", "non-root RFC6901 pointer"),
            ("settings", "non-root RFC6901 pointer"),
            ("/setting~", "malformed RFC6901 escape"),
            ("/setting~2", "malformed RFC6901 escape"),
        ):
            with self.subTest(pointer=pointer):
                pack = deepcopy(original)
                pack["runtime_bindings"] = {pointer: "onboarding.settings"}
                self.write_document("profiles/user.pack", pack)
                self.invalid_preserves_artifacts(message)

    def test_duplicate_layer_keys_and_mixed_defaults_directives_are_not_silently_resolved(self):
        self.valid_artifacts = self.generate()
        original = self.read_document("profiles/user.pack")
        duplicate = deepcopy(original)
        duplicate["entries"].append(deepcopy(duplicate["entries"][0]))
        self.write_document("profiles/user.pack", duplicate)
        self.invalid_preserves_artifacts("duplicate default layer key")
        mixed = deepcopy(original)
        mixed["runtime_definition"]["defaults"]["other"] = "synthetic-literal"
        self.write_document("profiles/user.pack", mixed)
        self.invalid_preserves_artifacts("runtime defaults directive has additional members")

    def test_raw_cpp_delimiter_collision_is_rejected_before_replacing_outputs(self):
        self.valid_artifacts = self.generate()
        scenario = self.read_document("onboarding/scenario.pack")
        scenario["synthetic_text"] = ')loom_onboard'
        self.write_document("onboarding/scenario.pack", scenario)
        self.invalid_preserves_artifacts("raw string delimiter collision")

    def test_check_detects_stale_native_and_web_outputs_without_rewriting_them(self):
        self.generate()
        for path, relative in ((self.native, "src/onboarding/builtin.inc"),
                               (self.web, "web/src/onboarding/generated/ui.json")):
            with self.subTest(output=relative):
                self.generate()
                path.write_bytes(path.read_bytes() + b"\nsynthetic stale artifact\n")
                stale = self.artifact_bytes()
                result = self.invoke("--check")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("stale: " + relative, result.stderr)
                self.assertEqual(self.artifact_bytes(), stale, "--check must not repair or rewrite either artifact")

    def test_check_detects_either_missing_output_without_recreating_it(self):
        for path, relative in ((self.native, "src/onboarding/builtin.inc"),
                               (self.web, "web/src/onboarding/generated/ui.json")):
            with self.subTest(output=relative):
                self.generate()
                path.unlink()
                remaining = self.web if path == self.native else self.native
                remaining_bytes = remaining.read_bytes()
                result = self.invoke("--check")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("stale: " + relative, result.stderr)
                self.assertFalse(path.exists())
                self.assertEqual(remaining.read_bytes(), remaining_bytes)


if __name__ == "__main__":
    unittest.main()
