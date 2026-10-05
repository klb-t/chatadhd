"""Offline regressions for both exact-byte compiled import preset sources."""

import copy
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


LOOM = Path(__file__).resolve().parents[2]
SCRIPT = LOOM / "src" / "import" / "gen_import_presets.py"
spec = importlib.util.spec_from_file_location("gen_import_presets", SCRIPT)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class ImportPresetGeneratorTests(unittest.TestCase):
    @staticmethod
    def json_bytes(document):
        return (json.dumps(document, separators=(",", ":")) + "\n").encode("utf-8")

    def isolated_fixture(self, root):
        paths = {
            "import": root / "import.pack", "import_audit": root / "audit.pack",
            "spec": root / "layers.spec.pack", "output": root / "presets.inc",
            "layers_output": root / "layers.pack",
        }
        documents = {
            "import": {"schema": "loom.import_preset/1", "id": "loom.preset.import.default",
                       "version": 1, "values": {"arbitrary": 17}},
            "import_audit": {"schema": "loom.import_audit_preset/1", "id": "loom.preset.import-audit.default",
                             "version": 1, "values": {"arbitrary": 17}},
            "spec": {
                "schema": "loom.import_layers_spec/1",
                "target": {"schema": "loom.default_layers_pack/1", "pack_id": "loom.defaults.archive-import",
                           "revision": 1, "policy": {"excluded_area_new_defaults": "proposal"}},
                "entries": [
                    {"id": "test.import", "key": "import.arbitrary", "area": "import.execution",
                     "revision": 1, "resource": "import", "field": "arbitrary"},
                    {"id": "test.audit", "key": "import.audit.arbitrary", "area": "import.audit",
                     "revision": 1, "resource": "import_audit", "field": "arbitrary"},
                ],
            },
        }
        for resource, document in documents.items():
            paths[resource].write_bytes(self.json_bytes(document))
        command = [
            sys.executable, str(SCRIPT), "--import-source", str(paths["import"]),
            "--audit-source", str(paths["import_audit"]), "--output", str(paths["output"]),
            "--layers-spec", str(paths["spec"]), "--layers-output", str(paths["layers_output"]),
        ]
        return paths, documents, command

    def emitted_bytes(self, generated):
        compilers = []
        candidates = ["g++", "clang++"] + [f"clang++-{version}" for version in range(21, 13, -1)]
        for candidate in candidates:
            executable = shutil.which(candidate)
            if executable and Path(executable).resolve() not in [Path(item).resolve() for item in compilers]:
                compilers.append(executable)
        if not compilers:
            executable = shutil.which("c++")
            if executable:
                compilers.append(executable)
        self.assertTrue(compilers, "a C++ compiler is required for byte verification")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "presets.inc").write_bytes(generated)
            (root / "probe.cpp").write_text(
                '#include <string_view>\n#include <cstdio>\n'
                '#include "presets.inc"\n'
                'int main(int argc, char** argv) {\n'
                '  if (argc != 2 || kImportPresetSource != "loom/data/presets/import.pack" ||\n'
                '      kImportAuditPresetSource != "loom/data/presets/import_audit.pack") return 2;\n'
                '  auto write = [](auto const& chunks) {\n'
                '    for (auto chunk : chunks)\n'
                '      if (std::fwrite(chunk.data(), 1, chunk.size(), stdout) != chunk.size()) return 1;\n'
                '    return 0;\n'
                '  };\n'
                '  if (std::string_view(argv[1]) == "import") return write(kImportPresetChunks);\n'
                '  if (std::string_view(argv[1]) == "audit") return write(kImportAuditPresetChunks);\n'
                '  return 3;\n'
                '}\n',
                encoding="utf-8",
            )
            outputs = []
            for compiler in compilers:
                with self.subTest(compiler=compiler):
                    flags = ["-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Woverlength-strings"]
                    version = subprocess.check_output([compiler, "--version"]).decode("utf-8", errors="replace")
                    if "Free Software Foundation" in version:
                        flags.append("-fexec-charset=ISO-8859-1")
                    compiled = subprocess.run(
                        [compiler, *flags, "probe.cpp", "-o", "probe"], cwd=root, capture_output=True,
                    )
                    self.assertEqual(compiled.returncode, 0, compiled.stderr.decode("utf-8", errors="replace"))
                    outputs.append(tuple(
                        subprocess.check_output([str(root / "probe"), resource])
                        for resource in ("import", "audit")
                    ))
            for output in outputs:
                self.assertEqual(output, outputs[0])
            return outputs[0]

    def test_checked_in_generated_source_matches_both_packs(self):
        checked = subprocess.run([sys.executable, str(SCRIPT), "--check"], capture_output=True)
        self.assertEqual(checked.returncode, 0, checked.stderr.decode("utf-8", errors="replace"))

    def test_exact_unicode_bom_crlf_and_large_source_bytes(self):
        def source(label):
            text = json.dumps({label: "zażółć 🧠 " * 7000, "fraction": 10.0}, ensure_ascii=False, indent=2) + "\n"
            return b"\xef\xbb\xbf" + text.replace("\n", "\r\n").encode("utf-8")

        import_raw, audit_raw = source("import_extension"), source("audit_extension")
        generated = generator.generate(import_raw, audit_raw)
        self.assertEqual(generated, generator.generate(import_raw, audit_raw))
        self.assertEqual(self.emitted_bytes(generated), (import_raw, audit_raw))

    def test_delimiter_like_and_hostile_text_remain_inert_bytes(self):
        hostile = ')IP00000000000000"; }; int main() { return 99; } /* \\001 字 🧠'
        import_raw = json.dumps({"text": hostile}, ensure_ascii=False).encode("utf-8")
        audit_raw = json.dumps({"text": hostile[::-1]}, ensure_ascii=False).encode("utf-8")
        generated = generator.generate(import_raw, audit_raw)
        self.assertNotIn(hostile.encode("utf-8"), generated)
        self.assertEqual(self.emitted_bytes(generated), (import_raw, audit_raw))

    def test_unknown_objects_nullable_values_and_large_finite_integers_are_preserved(self):
        import_raw = json.dumps({"extension": {"large_integer": 2 ** 4000, "nullable": None}}).encode("ascii")
        # Validation must not inherit Python's integer conversion digit ceiling.
        audit_raw = (b'{"unrelated":{"array":[true,false,null],"finite_float":1e300,"oversized_integer":'
                     + b"9" * 10000 + b'}}\n')
        self.assertEqual(self.emitted_bytes(generator.generate(import_raw, audit_raw)), (import_raw, audit_raw))

    def test_invalid_either_source_never_overwrites_existing_artifact(self):
        invalid_sources = [
            b"{broken", b"[]", b"null", b'{"x":NaN}', b'{"x":Infinity}',
            b'{"nested":[1e400]}', b'{"x":"\xff"}',
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths, documents, command = self.isolated_fixture(root)
            for resource in ("import", "import_audit"):
                for raw in invalid_sources:
                    with self.subTest(resource=resource, raw=raw):
                        for name in ("import", "import_audit"):
                            paths[name].write_bytes(self.json_bytes(documents[name]))
                        paths[resource].write_bytes(raw)
                        paths["output"].write_bytes(b"retained prior embedding\r\n")
                        paths["layers_output"].write_bytes(b"retained prior layers\r\n")
                        result = subprocess.run(command, capture_output=True)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertEqual(paths["output"].read_bytes(), b"retained prior embedding\r\n")
                        self.assertEqual(paths["layers_output"].read_bytes(), b"retained prior layers\r\n")

    def test_check_detects_each_changed_source_without_rewriting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths, _, command = self.isolated_fixture(root)
            output = paths["output"]
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(subprocess.run(command + ["--check"], capture_output=True).returncode, 0)
            first_output = output.read_bytes()
            first_layers = paths["layers_output"].read_bytes()
            for edit in ("numeric_value", "trailing_newline"):
                for source in (paths["import"], paths["import_audit"]):
                    with self.subTest(source=source.name, edit=edit):
                        raw = source.read_bytes()
                        changed = raw.replace(b'"arbitrary":17', b'"arbitrary":29') if edit == "numeric_value" else raw + b"\n"
                        self.assertNotEqual(changed, raw)
                        source.write_bytes(changed)
                        checked = subprocess.run(command + ["--check"], capture_output=True)
                        self.assertNotEqual(checked.returncode, 0)
                        self.assertEqual(output.read_bytes(), first_output)
                        self.assertEqual(paths["layers_output"].read_bytes(), first_layers)
                        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
                        self.assertNotEqual(output.read_bytes(), first_output)
                        first_output = output.read_bytes()
                        first_layers = paths["layers_output"].read_bytes()

            stale = first_output + b"\n"
            output.write_bytes(stale)
            self.assertNotEqual(subprocess.run(command + ["--check"], capture_output=True).returncode, 0)
            self.assertEqual(output.read_bytes(), stale)
            output.unlink()
            self.assertNotEqual(subprocess.run(command + ["--check"], capture_output=True).returncode, 0)
            self.assertFalse(output.exists())

    def test_layers_resolve_changed_source_values_without_domain_limits(self):
        with tempfile.TemporaryDirectory() as directory:
            paths, documents, command = self.isolated_fixture(Path(directory))
            documents["import"]["values"]["arbitrary"] = 2 ** 4000
            documents["import_audit"]["values"]["arbitrary"] = 1e300
            for resource in ("import", "import_audit"):
                documents[resource]["version"] = 2
            documents["spec"]["target"]["revision"] = 2
            for entry in documents["spec"]["entries"]:
                entry["revision"] = 2
            paths["spec"].write_bytes(self.json_bytes(documents["spec"]))
            for resource in ("import", "import_audit"):
                paths[resource].write_bytes(self.json_bytes(documents[resource]))
            generated = generator.generate_layers(paths["spec"].read_bytes(), paths["import"].read_bytes(),
                                                  paths["import_audit"].read_bytes())
            self.assertEqual(generated, generator.generate_layers(paths["spec"].read_bytes(),
                                                                  paths["import"].read_bytes(),
                                                                  paths["import_audit"].read_bytes()))
            resolved = {entry["key"]: entry["value"] for entry in json.loads(generated)["entries"]}
            self.assertEqual(resolved, {"import.arbitrary": 2 ** 4000, "import.audit.arbitrary": 1e300})
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(paths["layers_output"].read_bytes(), generated)

            # Preserve numeric JSON tokens beyond Python's integer-digit cap
            # and below its floating-point representation, without stringifying.
            import_raw = paths["import"].read_bytes().replace(str(2 ** 4000).encode("ascii"), b"9" * 10000)
            audit_raw = paths["import_audit"].read_bytes().replace(b"1e+300", b"1e-500")
            self.assertNotEqual(import_raw, paths["import"].read_bytes())
            self.assertNotEqual(audit_raw, paths["import_audit"].read_bytes())
            paths["import"].write_bytes(import_raw)
            paths["import_audit"].write_bytes(audit_raw)
            generated = generator.generate_layers(paths["spec"].read_bytes(), import_raw, audit_raw)
            tokens = json.loads(generated, parse_int=lambda token: ("integer", token),
                                parse_float=lambda token: ("float", token))
            resolved = {entry["key"]: entry["value"] for entry in tokens["entries"]}
            self.assertEqual(resolved, {"import.arbitrary": ("integer", "9" * 10000),
                                        "import.audit.arbitrary": ("float", "1e-500")})
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(paths["layers_output"].read_bytes(), generated)

    def test_renamed_source_ids_preserve_raw_bytes_and_stable_layer_entry_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            paths, documents, command = self.isolated_fixture(Path(directory))
            documents["import"]["id"] = "owner.import.alternate"
            documents["import_audit"]["id"] = "owner.import-audit.alternate"
            for resource in ("import", "import_audit"):
                paths[resource].write_bytes(self.json_bytes(documents[resource]))
            import_raw, audit_raw = paths["import"].read_bytes(), paths["import_audit"].read_bytes()
            generated_embedding = generator.generate(import_raw, audit_raw)
            self.assertEqual(self.emitted_bytes(generated_embedding), (import_raw, audit_raw))
            generated_layers = generator.generate_layers(paths["spec"].read_bytes(), import_raw, audit_raw)
            entries = json.loads(generated_layers)["entries"]
            self.assertEqual([entry["id"] for entry in entries],
                             [entry["id"] for entry in documents["spec"]["entries"]])
            self.assertEqual({entry["key"]: entry["value"] for entry in entries},
                             {"import.arbitrary": 17, "import.audit.arbitrary": 17})
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(paths["output"].read_bytes(), generated_embedding)
            self.assertEqual(paths["layers_output"].read_bytes(), generated_layers)

    def test_invalid_layers_bindings_versions_or_source_provenance_preserve_both_outputs(self):
        mutations = [
            ("spec", lambda document: document["entries"][0].update(resource="unknown")),
            ("spec", lambda document: document["entries"][0].update(field="missing")),
            ("spec", lambda document: document["entries"][1].update(id=document["entries"][0]["id"])),
            ("spec", lambda document: document["entries"][1].update(key=document["entries"][0]["key"])),
            ("spec", lambda document: document["entries"][0].update(revision=True)),
            ("spec", lambda document: document["target"].update(revision=0)),
            ("spec", lambda document: document.update(schema="other.schema/1")),
            ("import", lambda document: document.update(version=True)),
            ("import", lambda document: document.update(version=0)),
            ("import", lambda document: document.update(version=1.5)),
            ("import", lambda document: document.update(version="1")),
            ("import_audit", lambda document: document.pop("version")),
            ("import_audit", lambda document: document.update(version=-1)),
            ("import", lambda document: document.update(id="")),
            ("import", lambda document: document.update(id=17)),
            ("import_audit", lambda document: document.update(id=None)),
            ("import_audit", lambda document: document.update(id=True)),
            ("import_audit", lambda document: document.pop("id")),
            ("import", lambda document: document.update(schema="other.schema/1")),
            ("import_audit", lambda document: document.pop("values")),
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for index, (resource, mutate) in enumerate(mutations):
                with self.subTest(index=index, resource=resource):
                    paths, documents, command = self.isolated_fixture(root)
                    document = copy.deepcopy(documents[resource])
                    mutate(document)
                    paths[resource].write_bytes(self.json_bytes(document))
                    paths["output"].write_bytes(b"retained prior embedding\r\n")
                    paths["layers_output"].write_bytes(b"retained prior layers\r\n")
                    self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
                    self.assertEqual(paths["output"].read_bytes(), b"retained prior embedding\r\n")
                    self.assertEqual(paths["layers_output"].read_bytes(), b"retained prior layers\r\n")

    def test_check_detects_stale_or_missing_layers_without_rewriting_either_output(self):
        with tempfile.TemporaryDirectory() as directory:
            paths, _, command = self.isolated_fixture(Path(directory))
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            original_embedding = paths["output"].read_bytes()
            stale_layers = paths["layers_output"].read_bytes() + b"\n"
            paths["layers_output"].write_bytes(stale_layers)
            self.assertNotEqual(subprocess.run(command + ["--check"], capture_output=True).returncode, 0)
            self.assertEqual(paths["output"].read_bytes(), original_embedding)
            self.assertEqual(paths["layers_output"].read_bytes(), stale_layers)
            paths["layers_output"].unlink()
            self.assertNotEqual(subprocess.run(command + ["--check"], capture_output=True).returncode, 0)
            self.assertEqual(paths["output"].read_bytes(), original_embedding)
            self.assertFalse(paths["layers_output"].exists())


if __name__ == "__main__":
    unittest.main()
