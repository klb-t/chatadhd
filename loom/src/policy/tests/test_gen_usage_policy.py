"""Offline generator regressions; compile emitted literals to verify bytes."""

import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "gen_usage_policy.py"
spec = importlib.util.spec_from_file_location("gen_usage_policy", SCRIPT)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class GeneratorTests(unittest.TestCase):
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
            (root / "preset.inc").write_bytes(generated)
            (root / "probe.cpp").write_text(
                '#include <string_view>\n#include <cstdio>\n'
                '#include "preset.inc"\n'
                'int main() { if (kUsagePolicyPresetSource.empty()) return 2; '
                'for (auto chunk : kUsagePolicyPresetChunks) '
                'if (std::fwrite(chunk.data(), 1, chunk.size(), stdout) != chunk.size()) return 1; '
                'return 0; }\n',
                encoding="utf-8",
            )
            outputs = []
            for compiler in compilers:
                with self.subTest(compiler=compiler):
                    flags = ["-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Woverlength-strings"]
                    version = subprocess.check_output([compiler, "--version"]).decode("utf-8", errors="replace")
                    if "Free Software Foundation" in version:
                        # Octal source must preserve UTF-8 bytes even when the
                        # compiler's execution charset differs from UTF-8.
                        flags.append("-fexec-charset=ISO-8859-1")
                    compiled = subprocess.run(
                        [compiler, *flags, "probe.cpp", "-o", "probe"], cwd=root, capture_output=True,
                    )
                    self.assertEqual(compiled.returncode, 0, compiled.stderr.decode("utf-8", errors="replace"))
                    outputs.append(subprocess.check_output([str(root / "probe")]))
            for output in outputs:
                self.assertEqual(output, outputs[0])
            return outputs[0]

    def test_exact_unicode_bom_crlf_and_large_source_bytes(self):
        document = {"unknown_extension": "zażółć 🧠 " * 15000, "fraction": 10.0}
        text = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
        raw = b"\xef\xbb\xbf" + text.replace("\n", "\r\n").encode("utf-8")
        generated = generator.generate(raw)
        self.assertEqual(generated, generator.generate(raw))
        self.assertEqual(self.emitted_bytes(generated), raw)

    def test_delimiter_like_and_hostile_text_remain_inert_bytes(self):
        hostile = ')UP00000000000000"; }; int main() { return 99; } /* \\001 字 🧠'
        raw = json.dumps({"text": hostile}, ensure_ascii=False).encode("utf-8")
        generated = generator.generate(raw)
        self.assertNotIn(hostile.encode("utf-8"), generated)
        self.assertEqual(self.emitted_bytes(generated), raw)

    def test_nonpolicy_objects_and_unbounded_finite_integers_are_not_restricted(self):
        raw = json.dumps({"extension": {"large_integer": 2 ** 4000, "nullable": None}}).encode("ascii")
        self.assertEqual(self.emitted_bytes(generator.generate(raw)), raw)

    def test_invalid_source_never_overwrites_an_existing_artifact(self):
        invalid_sources = [
            b"{broken", b"[]", b'{"x":NaN}', b'{"x":Infinity}',
            b'{"nested":[1e400]}', b'{"x":"\xff"}',
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "input.pack", root / "preset.inc"
            for raw in invalid_sources:
                with self.subTest(raw=raw):
                    source.write_bytes(raw)
                    output.write_bytes(b"retained prior artifact\r\n")
                    result = subprocess.run(
                        [sys.executable, str(SCRIPT), "--source", str(source), "--output", str(output)],
                        capture_output=True,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(output.read_bytes(), b"retained prior artifact\r\n")

    def test_check_compares_exact_bytes_and_does_not_rewrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "input.pack", root / "preset.inc"
            source.write_bytes(b'{"extension":true}\n')
            command = [sys.executable, str(SCRIPT), "--source", str(source), "--output", str(output)]
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(subprocess.run(command + ["--check"], capture_output=True).returncode, 0)
            stale = output.read_bytes() + b"\n"
            output.write_bytes(stale)
            checked = subprocess.run(command + ["--check"], capture_output=True)
            self.assertNotEqual(checked.returncode, 0)
            self.assertEqual(output.read_bytes(), stale)
            output.unlink()
            self.assertNotEqual(subprocess.run(command + ["--check"], capture_output=True).returncode, 0)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
