#!/usr/bin/env python3
"""Offline research only: validate a Desktop Entry and marshal an OpenURI call.

No launch, D-Bus connection, registration, user data lookup or GUI interaction.
Requires the already installed desktop-file-validate and PyGObject/GLib/GIO.
Temporary files stay under this directory and are removed on exit.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import gi

gi.require_version("GioUnix", "2.0")
from gi.repository import GioUnix, GLib


HERE = Path(__file__).resolve().parent
ENTRY = HERE / "org.example.ChatADHDProbe.desktop"


class DesktopContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.validator = shutil.which("desktop-file-validate")
        if cls.validator is None:
            raise RuntimeError("desktop-file-validate is required; no validation was performed")
        cls.source = ENTRY.read_text(encoding="utf-8")

    def validate(self, source):
        with tempfile.TemporaryDirectory(prefix="contract-", dir=HERE) as root:
            entry = Path(root) / ENTRY.name
            entry.write_text(source, encoding="utf-8")
            return subprocess.run(
                [self.validator, str(entry)],
                capture_output=True, text=True, check=False, timeout=10,
            )

    def test_desktop_entry_passes_external_validator(self):
        result = self.validate(self.source)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_external_validator_rejects_unknown_and_multiple_field_codes(self):
        for replacement in ("%Z", "%u %U"):
            with self.subTest(exec_field=replacement):
                result = self.validate(self.source.replace("/usr/bin/true %u", "/usr/bin/true " + replacement))
                self.assertNotEqual(result.returncode, 0)

    def test_entry_without_required_name_is_rejected(self):
        source = "\n".join(line for line in self.source.splitlines() if not line.startswith("Name")) + "\n"
        result = self.validate(source)
        self.assertNotEqual(result.returncode, 0)

    def test_parsing_without_exec_does_not_prove_launchability(self):
        # Both installed parsers accept this variant. An adapter must separately
        # check its launch mechanism (Exec or an implemented D-Bus activation).
        source = self.source.replace("Exec=/usr/bin/true %u\n", "")
        self.assertEqual(self.validate(source).returncode, 0)
        with tempfile.TemporaryDirectory(prefix="contract-", dir=HERE) as root:
            entry = Path(root) / ENTRY.name
            entry.write_text(source, encoding="utf-8")
            app = GioUnix.DesktopAppInfo.new_from_filename(str(entry))
            self.assertIsNotNone(app)
            self.assertFalse(app.get_executable())

    def test_gio_reads_explicit_fixture_without_launching(self):
        app = GioUnix.DesktopAppInfo.new_from_filename(str(ENTRY))
        self.assertIsNotNone(app)
        self.assertTrue(app.supports_uris())
        self.assertEqual(app.get_executable(), "/usr/bin/true")
        self.assertEqual(app.get_commandline(), "/usr/bin/true %u")
        self.assertTrue(app.get_nodisplay())

    def test_absolute_uri_is_validated_and_original_bytes_are_marshaled(self):
        # Synthetic URIs only. Shell-looking characters remain data; no command is run.
        samples = (
            "loom-probe://graph/node_1?label=za%C5%BC%C3%B3%C5%82%C4%87#view",
            "https://example.invalid/a%20b?x=%25u&y=$(id);echo#view",
        )
        for raw in samples:
            with self.subTest(uri=raw):
                parsed = GLib.Uri.parse(raw, GLib.UriFlags.NONE)
                self.assertIsNotNone(parsed.get_scheme())
                # OpenURI(parent_window s, uri s, options a{sv}); no bus or proxy.
                packet = GLib.Variant("(ssa{sv})", (
                    "", raw, {
                        "handle_token": GLib.Variant("s", "loom_probe_1"),
                        "ask": GLib.Variant("b", True),
                    },
                ))
                parent, uri, options = packet.unpack()
                self.assertEqual(parent, "")
                self.assertEqual(uri.encode("utf-8"), raw.encode("utf-8"))
                self.assertIs(options["ask"], True)

    def test_relative_uri_is_rejected_by_glib(self):
        with self.assertRaises(GLib.Error):
            GLib.Uri.parse("relative/path", GLib.UriFlags.NONE)

    def test_wrong_open_uri_argument_type_is_rejected(self):
        with self.assertRaises(TypeError):
            GLib.Variant("(ssa{sv})", ("", 42, {}))


if __name__ == "__main__":
    unittest.main(verbosity=2)
