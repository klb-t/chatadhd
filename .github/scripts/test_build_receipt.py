"""Configuration receipts must identify the build without publishing its full cache."""
from pathlib import Path
import hashlib
import tempfile
import unittest

from write_build_receipt import cache_receipt


class CacheReceiptTests(unittest.TestCase):
    def receipt(self, raw, fields):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "CMakeCache.txt"
            path.write_bytes(raw)
            return cache_receipt(path, fields)

    def test_records_exact_cache_hash_and_only_declared_fields(self):
        raw = (b"// Generated cache\r\nCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++\r\n"
               b"LOOM_USE_SYSTEM_SQLITE:BOOL=OFF\r\n"
               b"UNRELATED_PRIVATE_VALUE:STRING=fabricated-sentinel\r\n")
        result = self.receipt(raw, ["CMAKE_CXX_COMPILER", "LOOM_USE_SYSTEM_SQLITE"])
        self.assertEqual(result["sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(result["bytes"], len(raw))
        self.assertEqual(result["fields"], {
            "CMAKE_CXX_COMPILER": {"type": "FILEPATH", "value": "/usr/bin/clang++"},
            "LOOM_USE_SYSTEM_SQLITE": {"type": "BOOL", "value": "OFF"}})
        self.assertNotIn("fabricated-sentinel", repr(result))

    def test_preserves_empty_flags_and_embedded_equals(self):
        result = self.receipt(b"FLAGS:STRING=\nOTHER_FLAGS:STRING=-DVALUE=7\n", ["FLAGS", "OTHER_FLAGS"])
        self.assertEqual(result["fields"]["FLAGS"]["value"], "")
        self.assertEqual(result["fields"]["OTHER_FLAGS"]["value"], "-DVALUE=7")

    def test_missing_or_duplicate_selected_field_cannot_claim_configuration(self):
        for raw in (b"OTHER:BOOL=ON\n", b"SELECTED:BOOL=ON\nSELECTED:BOOL=OFF\n",
                    b"SELECTED=ON\n"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                self.receipt(raw, ["SELECTED"])

    def test_empty_or_repeated_field_selection_is_rejected(self):
        for fields in ([], ["SELECTED", "SELECTED"]):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                self.receipt(b"SELECTED:BOOL=ON\n", fields)


if __name__ == "__main__":
    unittest.main()
