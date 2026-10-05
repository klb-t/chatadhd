"""Offline hash-bound context substitution; method and population live in JSON DATA."""
from copy import deepcopy
import argparse
import hashlib
import itertools
from pathlib import Path
import tempfile

from loom.tools.structure import method_graph_export_v1 as graph
from loom.tools.structure import openrouter_runner as wire
from loom.tools.structure import research_programme_manifest as manifests

SCHEMA = 'loom.context_preparation/1'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def unique(values, reason):
    if not isinstance(values, list) or not values or any(not isinstance(x, str) or not x for x in values) or len(values) != len(set(values)):
        raise ValueError(reason)
    return values


def assign(value, pointer, replacement):
    """Replace one existing location using the existing JSON-pointer semantics."""
    graph.pointer(value, pointer)
    if pointer == '':
        return deepcopy(replacement)
    parent_path, _, leaf = pointer.rpartition('/')
    parent = graph.pointer(value, parent_path)
    key = leaf.replace('~1', '/').replace('~0', '~')
    parent[int(key) if isinstance(parent, list) else key] = deepcopy(replacement)
    return value


def render(expression, variables):
    if isinstance(expression, dict):
        if '$from' in expression:
            wire._keys(expression, {'$from', 'pointer', 'encoding'})
            result = graph.pointer(variables[expression['$from']], expression['pointer'])
            if expression['encoding'] == 'value':
                return deepcopy(result)
            if expression['encoding'] == 'canonical_json':
                return wire.canonical(result).decode('utf-8')
            if expression['encoding'] == 'sha256':
                return wire.digest(result)
            raise ValueError('context_expression_encoding_invalid')
        return {key: render(value, variables) for key, value in expression.items()}
    if isinstance(expression, list):
        return [render(value, variables) for value in expression]
    return deepcopy(expression)


def build(config_path):
    path = Path(config_path)
    raw = path.read_bytes()
    config = wire.parse_json(raw)
    wire._keys(config, {'schema', 'programme_id', 'stage_id', 'selection', 'units_policy',
        'inputs', 'expected_case_ids', 'methods', 'order_dimensions', 'prepared'})
    if config['schema'] != SCHEMA:
        raise ValueError('context_config_schema_invalid')
    manifests._identity(config['programme_id']); manifests._identity(config['stage_id'])
    cases = unique(config['expected_case_ids'], 'context_case_inventory_invalid')
    if sorted(config['order_dimensions']) != ['case', 'method']:
        raise ValueError('context_order_invalid')
    wire._keys(config['prepared'], {'header', 'rows_key'}, {'row_digest_field'})
    prepared_document = render(config['prepared']['header'], {'programme_id': config['programme_id'], 'stage_id': config['stage_id']})
    if not isinstance(prepared_document, dict) or config['prepared']['rows_key'] in prepared_document:
        raise ValueError('context_prepared_header_invalid')
    namespace = prepared_document.get('experiment_id')
    manifests._identity(namespace)
    row_digest_field = config['prepared'].get('row_digest_field')
    if row_digest_field is not None and (not isinstance(row_digest_field, str) or not row_digest_field):
        raise ValueError('context_row_digest_field_invalid')
    sources = {}

    def bound(binding):
        wire._keys(binding, {'file', 'sha256'}, {'pointer'})
        target = (path.parent / binding['file']).resolve()
        content = target.read_bytes()
        if sha(content) != binding['sha256']:
            raise ValueError('context_source_snapshot_changed')
        key = binding['file']
        if key in sources and sources[key] != binding['sha256']:
            raise ValueError('context_source_binding_conflict')
        sources[key] = binding['sha256']
        return graph.pointer(wire.parse_json(content), binding.get('pointer', ''))

    selection = bound(config['selection'])
    units_policy = bound(config['units_policy'])
    inputs = {}
    for binding in config['inputs']:
        wire._keys(binding, {'key', 'file', 'sha256', 'rows_pointer', 'rows_mode', 'id_pointer', 'value_pointer'}, {'allowed_row_keys'})
        document = bound({key: binding[key] for key in ('file', 'sha256')})
        rows = graph.pointer(document, binding['rows_pointer'])
        if binding['rows_mode'] == 'single':
            rows = [rows]
        elif binding['rows_mode'] != 'array' or not isinstance(rows, list):
            raise ValueError('context_input_rows_invalid')
        index = inputs.setdefault(binding['key'], {})
        for row in rows:
            if 'allowed_row_keys' in binding and (not isinstance(row, dict) or set(row) != set(binding['allowed_row_keys'])):
                raise ValueError('context_input_keys_invalid')
            identity = graph.pointer(row, binding['id_pointer'])
            if not isinstance(identity, str) or identity in index:
                raise ValueError('context_input_case_duplicate')
            index[identity] = graph.pointer(row, binding['value_pointer'])
    methods = config['methods']
    if not isinstance(methods, list) or not methods:
        raise ValueError('context_methods_required')
    method_ids = unique([row['method_id'] for row in methods], 'context_method_inventory_invalid')
    prepared_methods = {}
    for method in methods:
        wire._keys(method, {'method_id', 'selection_pointer', 'selected_configuration_id', 'input_keys',
            'template', 'body_pointer', 'minimum_reservation', 'updates', 'preserve_body_pointers', 'row_template'})
        selected = graph.pointer(selection, method['selection_pointer'])
        admitted = method['selected_configuration_id'] in selected if isinstance(selected, list) else method['selected_configuration_id'] == selected
        if not admitted:
            raise ValueError('context_method_not_selected')
        unique(method['input_keys'], 'context_method_inputs_invalid')
        for key in method['input_keys']:
            if key not in inputs or set(inputs[key]) != set(cases):
                raise ValueError('context_input_case_inventory_mismatch')
        template = bound(method['template'])
        body = graph.pointer(template, method['body_pointer'])
        if not isinstance(body, dict):
            raise ValueError('context_template_body_invalid')
        floor = bound(method['minimum_reservation'])
        manifests._quantity(floor)
        updates = method['updates']
        if not isinstance(updates, list) or not updates:
            raise ValueError('context_updates_required')
        unique([entry['pointer'] for entry in updates], 'context_update_pointer_duplicate')
        for entry in updates:
            wire._keys(entry, {'pointer', 'value'})
            try:
                graph.pointer(body, entry['pointer'])
            except (KeyError, IndexError, TypeError, ValueError):
                raise ValueError('context_update_pointer_invalid') from None
        for pointer in method['preserve_body_pointers']:
            graph.pointer(body, pointer)
        prepared_methods[method['method_id']] = (method, template, body, floor)
    dimensions = {'case': cases, 'method': method_ids}
    rows, witnesses, floor_by_id = [], [], {}
    for pair in itertools.product(*(dimensions[key] for key in config['order_dimensions'])):
        cell = dict(zip(config['order_dimensions'], pair))
        case_id, method_id = cell['case'], cell['method']
        method, template, template_body, floor = prepared_methods[method_id]
        context = {key: inputs[key][case_id] for key in method['input_keys']}
        request_id = 'new.' + wire.digest([sha(raw), case_id, method_id])
        variables = {'input': context, 'template': template, 'case_id': case_id, 'method_id': method_id,
            'request_id': request_id, 'minimum_reservation_usd': floor,
            'programme_id': config['programme_id'], 'stage_id': config['stage_id']}
        body = deepcopy(template_body)
        for entry in method['updates']:
            body = assign(body, entry['pointer'], render(entry['value'], variables))
        for pointer in method['preserve_body_pointers']:
            if wire.canonical(graph.pointer(body, pointer)) != wire.canonical(graph.pointer(template_body, pointer)):
                raise ValueError('context_preserved_body_field_changed')
        variables.update(body=body, input_sha256=wire.digest(context), request_sha256=wire.digest(body))
        row = render(method['row_template'], variables)
        if row_digest_field is not None:
            row[row_digest_field] = wire.digest({key: value for key, value in row.items() if key != row_digest_field})
        rows.append(row)
        floor_by_id[namespace + '.' + request_id] = manifests._quantity(floor)
        witnesses.append({'request_id': request_id, 'case_id': case_id, 'method_id': method_id,
            'template_sha256': wire.digest(template), 'template_body_sha256': wire.digest(template_body),
            'input_sha256': wire.digest(context), 'request_sha256': wire.digest(body),
            'minimum_reservation_usd': floor, 'context_update_pointers': [entry['pointer'] for entry in method['updates']],
            'preserved_body_pointer_sha256': {pointer: wire.digest(graph.pointer(body, pointer)) for pointer in method['preserve_body_pointers']}})
    prepared_document[config['prepared']['rows_key']] = rows
    prepared_raw = wire.canonical(prepared_document) + b'\n'
    transport_document = deepcopy(prepared_document)
    transport_document['requests'] = rows
    transport_raw = wire.canonical(transport_document) + b'\n'
    transport_file = 'source-prepared.json' if config['prepared']['rows_key'] == 'requests' else 'transport-prepared.json'
    with tempfile.TemporaryDirectory(prefix='context-preparation-offline-') as temporary:
        directory = Path(temporary) / namespace
        directory.mkdir()
        source = directory / transport_file; source.write_bytes(transport_raw)
        result = manifests.adapt_prepared_manifest(source, directory / 'paid', programme_id=config['programme_id'],
            stage_id=config['stage_id'], units_policy=units_policy)
        for operation in result['operations']:
            if operation['operation_id'] not in floor_by_id or manifests._quantity(operation.get('minimum_reservation_usd')) < floor_by_id[operation['operation_id']]:
                raise ValueError('context_minimum_reservation_below_template')
        for operation, witness in zip(result['operations'], witnesses):
            if operation['operation_id'] != namespace + '.' + witness['request_id'] or operation['request_sha256'] != witness['request_sha256']:
                raise ValueError('context_prepared_row_omits_or_changes_bound_body')
            operation['metadata']['context_preparation'] = {
                'case_id': witness['case_id'], 'method_id': witness['method_id'],
                'input_sha256': witness['input_sha256'], 'template_body_sha256': witness['template_body_sha256'],
                'request_body_sha256': witness['request_sha256'], 'preparation_config_sha256': sha(raw)}
        if len(result['operations']) != len(rows):
            raise ValueError('context_operation_inventory_mismatch')
        result['metadata']['source_manifests'][0]['file'] = transport_file
        artifacts = {p.relative_to(directory / 'paid').as_posix(): p.read_bytes()
            for p in (directory / 'paid').rglob('*') if p.is_file()}
    artifacts['manifest.json'] = wire.canonical(result) + b'\n'
    artifacts['source-prepared.json'] = prepared_raw
    if transport_file != 'source-prepared.json':
        artifacts[transport_file] = transport_raw
    artifacts['preparation.json'] = wire.canonical({'schema': 'loom.context_preparation.receipt/1',
        'producer_sha256': sha(Path(__file__).read_bytes()), 'config_sha256': sha(raw), 'sources_sha256': sources,
        'planned_operations': len(rows), 'expected_case_ids': cases, 'method_ids': method_ids,
        'order_dimensions': config['order_dimensions'], 'bindings': witnesses,
        'source_prepared_sha256': sha(prepared_raw), 'manifest_sha256': sha(artifacts['manifest.json']),
        'transport_projection_file': transport_file, 'transport_projection_sha256': sha(transport_raw),
        'paid_calls': 0, 'automatic_input_or_gold_discovery': False, 'billing_bound_guaranteed': False,
        'status': 'offline_prepared_pending_coordinator_freeze_and_current_quotes'}) + b'\n'
    return result, artifacts


def prepare(config_path, output_dir):
    manifest, artifacts = build(config_path)
    directory = Path(output_dir)
    for name, raw in artifacts.items():
        target = directory / name
        if target.exists() and target.read_bytes() != raw:
            raise ValueError('context_output_already_exists')
    for name, raw in artifacts.items():
        manifests._write_exact(directory / name, raw)
    manifests.validate_manifest(manifest, base_dir=directory)
    return manifest


def verify(config_path, output_dir):
    manifest, artifacts = build(config_path)
    directory = Path(output_dir)
    if any(not (directory / name).is_file() or (directory / name).read_bytes() != raw for name, raw in artifacts.items()):
        raise ValueError('context_preparation_replay_changed')
    manifests.validate_manifest(manifest, base_dir=directory)
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['prepare', 'verify'])
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    result = (prepare if args.command == 'prepare' else verify)(args.config, args.output)
    print(wire.canonical({'planned_operations': len(result['operations']), 'paid_calls': 0}).decode())


if __name__ == '__main__':
    main()
