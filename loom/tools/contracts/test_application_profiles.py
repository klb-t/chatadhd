"""Offline declarative-profile contract checks; no application or model parity claim."""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest
from unittest.mock import patch

from jsonschema import Draft202012Validator, FormatChecker

try:
    from .validate import ContractValidator, SCHEMA_DIR, read_json
except ImportError:
    from validate import ContractValidator, SCHEMA_DIR, read_json


PROFILE_DIR = Path(__file__).resolve().parents[2] / 'web' / 'src' / 'profiles' / 'data'


class ApplicationProfiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema = read_json(SCHEMA_DIR / 'application_profile.schema.json')
        cls.contracts = ContractValidator()
        cls.validator = Draft202012Validator(cls.schema, registry=cls.contracts.registry,
                                             format_checker=cls.contracts.format_checker)
        cls.base = read_json(PROFILE_DIR / 'loom-default.json')

    def assert_invalid(self, document):
        self.assertTrue(list(self.validator.iter_errors(document)))

    def test_schema_and_all_actual_builtins_are_valid(self):
        Draft202012Validator.check_schema(self.schema)
        paths = sorted(PROFILE_DIR.glob('*.json'))
        self.assertEqual(len(paths), 4)
        for path in paths:
            with self.subTest(path=path.name):
                self.validator.validate(read_json(path))

    def test_existing_validator_does_not_gain_optional_uri_dependency(self):
        # jsonschema conditionally omits URI checkers without additional deps.
        # New UI profiles must not stop every existing projection from loading.
        available = {name: checker for name, checker in FormatChecker.checkers.items()
                     if name not in ('uri', 'uri-reference', 'iri', 'iri-reference')}
        with patch.object(FormatChecker, 'checkers', available):
            validator = ContractValidator()
            for example in read_json(SCHEMA_DIR / 'examples' / 'valid.json'):
                with self.subTest(example=example['name']):
                    self.assertFalse(validator.validate(example['document']))

    def test_target_and_revision_axes_are_independent_open_data(self):
        document = deepcopy(self.base)
        document['target'] = {'application_id': 'unknown-vendor/app', 'version': 'historical-7', 'platform': 'new-platform'}
        document['profile_revision'] = 2
        self.validator.validate(document)
        document['target']['version'] = None
        self.validator.validate(document)
        document['target'].pop('version')
        self.assert_invalid(document)

    def test_execution_settings_or_scripts_cannot_be_profile_data(self):
        for field, value in [('script', 'alert(1)'), ('model', 'other-model'),
                             ('context', {'scope': 'entire-history'}),
                             ('request_adapter', {'system_prompt': 'hidden'})]:
            document = deepcopy(self.base)
            document[field] = value
            with self.subTest(field=field):
                self.assert_invalid(document)
        document = deepcopy(self.base)
        document['actions'][0]['script'] = 'fetch("https://example.org")'
        self.assert_invalid(document)
        document = deepcopy(self.base)
        document['presentation']['tokens']['background'] = 'url(https://example.org)'
        self.assert_invalid(document)

    def test_url_syntax_is_explicit_without_optional_format(self):
        for url in ['javascript:alert(1)', 'https://example.org/a b', 'https://example.org/\n']:
            document = deepcopy(self.base)
            document['evidence']['sources'][0]['url'] = url
            with self.subTest(url=url):
                self.assert_invalid(document)

    def test_verified_status_requires_known_target_sources_and_no_gaps(self):
        document = deepcopy(self.base)
        document['evidence']['status'] = 'verified'
        document['evidence']['gaps'] = []
        self.validator.validate(document)
        for mutate in [lambda p: p['target'].update(version=None),
                       lambda p: p['evidence'].update(sources=[]),
                       lambda p: p['evidence'].update(gaps=['Unverified workflow'])]:
            changed = deepcopy(document)
            mutate(changed)
            self.assert_invalid(changed)

    def test_widths_have_representation_validation_and_no_product_ceiling(self):
        document = deepcopy(self.base)
        document['presentation']['content_width'] = 10 ** 12
        document['presentation']['sidebar']['width'] = 10 ** 12
        self.validator.validate(document)
        document['presentation']['content_width'] = 0
        self.assert_invalid(document)

    def test_session_definition_validates_checked_persistence_shape(self):
        validator = Draft202012Validator({'$ref': self.schema['$id'] + '#/$defs/session'},
                                          registry=self.contracts.registry)
        document = {
            'schema': 'loom.application_profile_session/1',
            'profile': {'id': self.base['id'], 'profile_revision': 1, 'target': self.base['target'],
                        'definition': json.dumps(self.base, sort_keys=True, separators=(',', ':'))},
            'workflows': {'conversation-review': 'ready'}, 'sequence': 0, 'history': [],
        }
        validator.validate(document)
        document['profile'].pop('definition')
        self.assertTrue(list(validator.iter_errors(document)))


if __name__ == '__main__':
    unittest.main()
