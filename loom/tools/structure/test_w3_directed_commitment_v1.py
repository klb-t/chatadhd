"""Discover the scoped W3 mechanism suite from the existing CTest research gate."""
from pathlib import Path
import sys
import unittest

ROOT = str(Path(__file__).resolve().parents[3])
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)


def load_tests(loader, standard_tests, pattern):
    return loader.loadTestsFromNames([
        'loom.tools.structure.w3_directed_commitment_v1.test_experiment',
        'loom.tools.structure.w3_directed_commitment_v1.test_independent',
    ])


if __name__ == '__main__':
    unittest.main()
