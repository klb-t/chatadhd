#!/usr/bin/env python3
"""Offline checks for the open method-graph data pattern; no provider calls."""
from copy import deepcopy
import json
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker, ValidationError

root = Path(__file__).resolve().parents[1]
schema = json.loads((root / 'method-graph.schema.json').read_text())
fixture = json.loads((root / 'tests/method-graph-fixture.json').read_text())
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema, format_checker=FormatChecker())
checks = 1


def accepts(value):
    global checks
    validator.validate(value)
    checks += 1


def rejects(value):
    global checks
    try:
        validator.validate(value)
    except ValidationError:
        checks += 1
    else:
        raise AssertionError('invalid method graph accepted')


contract = fixture['contract']
accepts(contract)
accepts(contract['trace'])
accepts(fixture['binding_diff']['entities']['update'][0]['after']['attrs'])
open_data = deepcopy(contract)
open_data['vocabulary']['kinds']['future_role'] = 'caller:future-kind'
open_data['vocabulary']['predicates']['future_edge'] = 'caller:future-edge'
open_data['bindings']['future_role_id'] = 'caller:future-id'
open_data['definition_hashes']['future_definition'] = 'a' * 64
open_data['trace']['effective_parameters'] = [None, {'caller_option': -123456789}]
accepts(open_data)

lexical = deepcopy(contract)
for role in ('prompt_version_id', 'recipe_version_id', 'preset_version_id',
             'combination_version_id', 'model_identity_id', 'compiler_transform_id'):
    lexical['bindings'][role] = None
lexical['definition_hashes'] = {'method_version': contract['definition_hashes']['method_version']}
for field in ('model_identity_id', 'model', 'recipe_sha256', 'prompt_sha256',
              'preset_version_id', 'preset_sha256', 'combination_version_id', 'combination_sha256'):
    lexical['trace'][field] = None
lexical['trace']['execution_kind'] = 'caller:lexical-method'
lexical['trace']['response_provenance'] = 'caller:local-operation'
accepts(lexical)
accepts(lexical['trace'])
minimal = deepcopy(lexical)
minimal['bindings'] = {name: minimal['bindings'][name]
                       for name in ('method_identity_id', 'method_version_id', 'run_id')}
minimal['vocabulary']['kinds'] = {name: minimal['vocabulary']['kinds'][name]
                                for name in ('method', 'method_version', 'run')}
minimal['vocabulary']['predicates'] = {
    name: minimal['vocabulary']['predicates'][name]
    for name in ('version_of', 'requests_method_version', 'produced_in_run', 'produced_by_method_version')}
for field in ('model_identity_id', 'model', 'recipe_sha256', 'prompt_sha256',
              'preset_version_id', 'preset_sha256', 'combination_version_id', 'combination_sha256'):
    del minimal['trace'][field]
accepts(minimal)
accepts(minimal['trace'])

bad = deepcopy(contract)
del bad['bindings']['method_version_id']
rejects(bad)
bad = deepcopy(contract)
bad['definition_hashes']['method_version'] = 'invalid-hash'
rejects(bad)
bad = deepcopy(contract)
bad['definition_hashes']['method_version'] += '\n'
rejects(bad)
bad = deepcopy(contract['trace'])
bad['measurements']['accuracy'] = 'unmeasured'
rejects(bad)
bad = deepcopy(contract)
bad['schema'] = 'loom.method_graph/unknown'
rejects(bad)
print(f'method graph schema: {checks}/{checks} checks passed; model/lexical/open data and invalid contracts')
