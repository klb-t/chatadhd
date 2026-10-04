"""Run the ordinary import audit suite through the existing CTest discovery.

The suite lives beside archive_cost.py; this adapter adds no duplicate tests or
central CMake changes. Both preserved estimate compatibility and new raw audit
regressions run in every full CTest invocation.
"""
import importlib.util
from pathlib import Path
import sys
import unittest

EVAL = Path(__file__).resolve().parents[2] / "tools" / "eval"
sys.path.insert(0, str(EVAL))
spec = importlib.util.spec_from_file_location("archive_cost_regular_tests", EVAL / "test_archive_cost.py")
suite_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(suite_module)


def load_tests(loader, tests, pattern):
    return loader.loadTestsFromModule(suite_module)


if __name__ == "__main__":
    unittest.main()
