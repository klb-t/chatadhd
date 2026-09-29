"""Regression for environments where jsonschema silently omits RFC3339 checks."""
import unittest
from unittest.mock import patch
from jsonschema import FormatChecker
from validate import ContractValidator

class DependencyGuards(unittest.TestCase):
    def test_missing_rfc3339_checker_refuses_validation_environment(self):
        available = {key: value for key, value in FormatChecker.checkers.items() if key != 'date-time'}
        with patch.object(FormatChecker, 'checkers', available):
            with self.assertRaisesRegex(RuntimeError, 'format checker unavailable'):
                ContractValidator()

    def test_required_formats_remain_active(self):
        checker = ContractValidator().format_checker
        self.assertFalse(checker.conforms('2026-02-31T00:00:00Z', 'date-time'))
        self.assertTrue(checker.conforms('2024-02-29T00:00:00Z', 'date-time'))

if __name__ == '__main__': unittest.main()
