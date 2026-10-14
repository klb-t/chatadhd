"""The ROM catalogue pack is generated from the working tree; --check fails when it is stale."""
import subprocess
import sys
import unittest
from pathlib import Path

LOOM = Path(__file__).resolve().parents[2]


class RomCatalogueTest(unittest.TestCase):
    def test_pack_is_current(self):
        done = subprocess.run([sys.executable, str(LOOM / 'tools/gen_rom_catalogue.py'), '--check'],
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)


if __name__ == '__main__':
    unittest.main()
