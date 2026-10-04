"""Data-configured offline prompt/parameter study of native semantic proposals."""
from __future__ import annotations
import argparse
from copy import deepcopy
from decimal import Decimal
import hashlib
from pathlib import Path
from zipfile import ZipFile

from . import openrouter_runner as safe

ROOT = Path(__file__).resolve().parents[3]


def read(path):
    return safe.parse_json(Path(path).read_bytes())


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(safe.canonical(value) + b'\n')


def digest_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def project_inputs(config_path, output):
    """Source-only text projection; no synthetic gold or reference graph is read."""
    config_path = Path(config_path).resolve()
    config = read(config_path)
    archives = {}
    for provider, relative in config['source_archives'].items():
        path = ROOT / relative
        with ZipFile(path) as archive:
            raw = archive.read('conversations.json')
        archives[provider] = (safe.parse_json(raw), digest_file(path),
                              hashlib.sha256(raw).hexdigest())
    cases = []
    for case_id in config['case_ids']:
        provider, conversation_id = case_id.split(':', 1)
        conversations, container_hash, member_hash = archives[provider]
        matches = [(i, row) for i, row in enumerate(conversations)
                   if row.get('id', row.get('uuid')) == conversation_id]
        if len(matches) != 1:
            raise ValueError('synthetic_conversation_identity')
        index, conversation = matches[0]
        records = []
        if provider == 'chatgpt':
            mapping, cursor = conversation['mapping'], conversation['current_node']
            visited = set()
            while cursor is not None:
                if cursor in visited or cursor not in mapping:
                    raise ValueError('source_branch_cycle_or_missing_node')
                visited.add(cursor)
                row = mapping[cursor]
                records.append((cursor, row.get('message'), f'/{index}/mapping/{cursor}/message'))
                cursor = row.get('parent')
            records.reverse()
        else:
            records = [(row['uuid'], row, f'/{index}/chat_messages/{i}')
                       for i, row in enumerate(conversation['chat_messages'])]
            if any('parent_message_uuid' in row for _, row, _ in records):
                mapping = {node: (row, pointer) for node, row, pointer in records}
                cursor, visited, branch = conversation['current_leaf_message_uuid'], set(), []
                while cursor is not None:
                    if cursor in visited or cursor not in mapping:
                        raise ValueError('source_branch_cycle_or_missing_node')
                    visited.add(cursor)
                    row, pointer = mapping[cursor]
                    branch.append((cursor, row, pointer))
                    # This export uses the empty string for the branch root.
                    cursor = row.get('parent_message_uuid') or None
                records = list(reversed(branch))
        observations = []
        for node, message, pointer in records:
            if message is None:
                continue
            fragments = ([(message['text'], pointer + '/text')] if provider == 'claude'
                         else [(part, pointer + f'/content/parts/{i}')
                               for i, part in enumerate(message['content']['parts']) if isinstance(part, str)])
            for text, location in fragments:
                locator = {'source': 'sha256:' + member_hash, 'member': 'conversations.json',
                           'json_pointer': location, 'byte_start': None, 'byte_len': None,
                           'time_start': None, 'time_end': None, 'line': None}
                observations.append({'id': 'ob_' + safe.digest([case_id, node, location, text])[:24],
                                     'unit': 'un_' + safe.digest(case_id)[:24], 'kind': 'sentence',
                                     'text': text, 'locator': locator, 'lang': 'und', 'date': '',
                                     'ordinal': len(observations), 'artifact_type': 'conversation',
                                     'speaker': message.get('sender', message.get('author', {}).get('role', '')),
                                     'attrs': {'node': node, 'branch': 'source_current_leaf',
                                               'provider': provider}})
        packet = {'schema': 'loom.source_packet/1', 'snapshot_id': 'synthetic:' + case_id,
                  'observations': observations, 'entities': [], 'claims': [],
                  'metadata': {'conversation_id': conversation_id,
                               'archive_sha256': container_hash, 'member_sha256': member_hash,
                               'projection': 'exact_text_parts_on_source_current_branch',
                               'nontext_metadata_are_not_model_input': True,
                               'chronology': 'explicit_parent_path_or_retained_source_array_order'}}
        cases.append({'id': case_id, 'source_packet': packet, 'packet_hash': safe.digest(packet),
                      'source_archive': config['source_archives'][provider]})
    result = {'schema': 'loom.native_semantic_study.inputs/1', 'population': config['population'],
              'reference_labels_loaded': False, 'cases': cases,
              'source_archives_sha256': {k: v[1] for k, v in archives.items()}}
    write(output, result)
    return result


def prepare(config_path, output):
    config_path, output = Path(config_path).resolve(), Path(output)
    config = read(config_path)
    if config.get('schema') != 'loom.native_semantic_study.config/1':
        raise ValueError('semantic_study_config_schema')
    if output.exists():
        raise ValueError('new_preparation_required')
    paths = {name: config_path.parent / config[name] for name in ('inputs', 'recipes')}
    inputs, recipes = read(paths['inputs']), read(paths['recipes'])
    cases = {row['id']: row for row in inputs['cases']}
    if len(cases) != len(inputs['cases']) or len(set(config['case_ids'])) != len(config['case_ids']):
        raise ValueError('duplicate_case_identity')
    prompt_map = {row['id']: row for row in recipes['recipes']}
    if len(prompt_map) != len(recipes['recipes']):
        raise ValueError('duplicate_recipe_identity')
    requests, arms, methods = [], [], []
    for model in config['models']:
        for recipe_id in config['recipe_ids']:
            recipe = prompt_map[recipe_id]
            for variant in config['parameter_variants']:
                parameters = deepcopy(config['generation_defaults']) | deepcopy(variant['parameters'])
                if {'model', 'messages', 'provider'} & set(parameters):
                    raise ValueError('parameters_override_bound_identity')
                method = {'model': model['id'], 'provider': model['provider'],
                          'recipe_id': recipe_id, 'recipe_version': recipe['version'],
                          'prompt_sha256': hashlib.sha256(recipe['system_prompt'].encode()).hexdigest(),
                          'parameters': parameters,
                          'input_representation': config['native_api']['representation'],
                          'output_contract': config['native_api']['response_contract']}
                method_hash = safe.digest(method)
                method_id = 'semantic-' + method_hash[:24]
                rows = []
                for case_id in config['case_ids']:
                    case = cases[case_id]
                    packet = deepcopy(case['source_packet'])
                    packet_hash = safe.digest(packet)
                    if packet_hash != case['packet_hash']:
                        raise ValueError('source_packet_hash_mismatch')
                    body = {'model': model['id'], 'provider': deepcopy(model['provider']),
                            **parameters,
                            'messages': [{'role': 'system', 'content': recipe['system_prompt']},
                                         {'role': 'user', 'content': safe.canonical({'packet_hash': packet_hash,
                                                                                 'source_packet': packet}).decode()}]}
                    estimate = safe.estimate_reservation(body)
                    request_id = method_id + '-' + hashlib.sha256(case_id.encode()).hexdigest()[:16]
                    row = {'id': request_id, 'body': body,
                           'reservation_usd': estimate['minimum_reservation_usd']}
                    rows.append(row)
                    requests.append({'case_id': case_id, 'method_id': method_id,
                                     'method_hash': method_hash, 'request_hash': safe.digest(body),
                                     'packet_hash': packet_hash, 'request': row})
                arms.append({'id': method_id, 'method_hash': method_hash,
                             'request_ids': [r['id'] for r in rows],
                             'requests': rows, 'parameter_variant': variant['id'],
                             'pricing_evidence': deepcopy(config['historical_pricing_evidence']),
                             'execution_manifest_status': 'pending_separate_key_current_USD_cap_and_fresh_prices',
                             'programme_budget': deepcopy(config['programme_budget'])})
                methods.append({'id': method_id, 'version': config['version'],
                                'hash': method_hash, 'record': method,
                                'evidence_status': 'prepared_unmeasured'})
    if len({r['request']['id'] for r in requests}) != len(requests):
        raise ValueError('duplicate_request_identity')
    plan = {'schema': 'loom.native_semantic_study.preparation/1', 'version': config['version'],
            'population': inputs['population'], 'new_provider_calls': 0,
            'gold_loaded_by_preparation': False, 'native_runtime_wiring_proven': False,
            'semantic_reference_status': config['scoring']['semantic_reference_status'],
            'programme_budget': config['programme_budget'],
            'planned_requests': len(requests), 'case_count': len(config['case_ids']),
            'methods': methods, 'arms': arms,
            'historical_reservation_usd': str(sum((safe._money(r['request']['reservation_usd']) for r in requests), Decimal(0))),
            'EUR_USD_parity_assumed': False, 'requests': requests}
    write(output / 'prepared.json', plan)
    dependencies = [config_path, *paths.values(), Path(__file__)]
    freeze = {'schema': 'loom.native_semantic_study.freeze/1',
              'source_sha256': {str(p.relative_to(ROOT)): digest_file(p) for p in dependencies},
              'files_sha256': {'prepared.json': digest_file(output / 'prepared.json')},
              'before_provider_outputs': True, 'source_gold_loaded': False}
    write(output / 'freeze.json', freeze)
    return plan


def verify(output):
    output = Path(output)
    frozen = read(output / 'freeze.json')
    for root, field in ((ROOT, 'source_sha256'), (output, 'files_sha256')):
        for name, expected in frozen[field].items():
            path = (root / name).resolve()
            if not path.is_relative_to(root.resolve()) or digest_file(path) != expected:
                raise ValueError('native_semantic_study_drift')
    return read(output / 'prepared.json')


def materialize_manifest(prepared, arm_id, *, verified_usd_cap, fresh_pricing_evidence):
    """Pure manifest conversion; caller must prove EUR budget/key identity upstream.

    No currency conversion is fabricated. This function only creates a runner
    manifest after an explicit USD cap and fresh saved evidence are supplied.
    """
    plan = verify(prepared)
    arm = next((row for row in plan['arms'] if row['id'] == arm_id), None)
    if arm is None:
        raise ValueError('unknown_semantic_study_arm')
    manifest = {'schema': safe.MANIFEST_SCHEMA, 'experiment_id': arm_id,
                'budget_usd': str(safe._money(verified_usd_cap)),
                'max_requests': len(arm['requests']), 'requests': arm['requests'],
                'pricing_evidence': deepcopy(fresh_pricing_evidence)}
    safe.plan_manifest(manifest)
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    prep = commands.add_parser('prepare')
    prep.add_argument('--config', type=Path, required=True)
    prep.add_argument('--output', type=Path, required=True)
    projection = commands.add_parser('project-inputs')
    projection.add_argument('--config', type=Path, required=True)
    projection.add_argument('--output', type=Path, required=True)
    check = commands.add_parser('verify')
    check.add_argument('--prepared', type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command == 'project-inputs':
        result = project_inputs(args.config, args.output)
        print(safe.canonical({'cases': len(result['cases']), 'new_provider_calls': 0}).decode())
    else:
        result = prepare(args.config, args.output) if args.command == 'prepare' else verify(args.prepared)
        print(safe.canonical({'planned_requests': result['planned_requests'], 'new_provider_calls': 0,
                             'historical_reservation_usd': result['historical_reservation_usd']}).decode())


if __name__ == '__main__':
    main()
