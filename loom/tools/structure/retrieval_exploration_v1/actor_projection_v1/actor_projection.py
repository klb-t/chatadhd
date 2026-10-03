"""Raw-source actor projection; never edits native graph or invents identities."""
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import resource
import time
from pathlib import Path
import sys
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'native_source_free_v1'))
import native_panel_v2 as panel
from evaluate_native_v3 import resolve_pointer


def value_at(document, pointer):
    try:
        return resolve_pointer(document, pointer)
    except (KeyError, IndexError, TypeError, ValueError):
        return None


def agree(values):
    if any(value is not None and (not isinstance(value, str) or not value.strip()) for value in values):
        return {'state': 'invalid_type_or_empty', 'value': None}
    present = set(value for value in values if value is not None)
    if len(present) > 1:
        return {'state': 'conflicting_source_fields', 'value': None}
    return {'state': 'resolved' if present else 'unknown', 'value': next(iter(present)) if present else None}


def project(observation, raw, policy):
    if policy['conflict_policy'] != 'abstain_per_field' or policy['missing_policy'] != 'unknown_per_field':
        raise ValueError('unsupported_actor_projection_resolution_policy')
    if policy['role_fallback_for_actor'] or policy['materialize_native_graph'] or policy['cross_source_identity_merge'] != 'forbidden':
        raise ValueError('unsupported_actor_projection_identity_policy')
    result = {'observation_id': observation.get('id'), 'state': 'unavailable',
        'epistemic_kind': 'source_assertion', 'identity_semantics': policy['identity_semantics'],
        'actor_epistemic_status': policy['actor_epistemic_status']}
    locator = observation.get('locator')
    if not isinstance(locator, dict):
        return result | {'reason': 'missing_or_invalid_native_locator'}
    raw_sha = hashlib.sha256(raw).hexdigest()
    if locator.get('source') != 'sha256:' + raw_sha:
        return result | {'reason': 'raw_source_hash_mismatch'}
    pointer = locator.get('json_pointer')
    if not isinstance(pointer, str):
        return result | {'reason': 'missing_or_invalid_source_pointer'}
    parts = pointer.split('/')
    if len(parts) != 7 or parts[1] != 'mapping' or parts[3:6] != ['message', 'content', 'parts']:
        return result | {'reason': 'unsupported_source_pointer_format'}
    try:
        document = json.loads(raw)
        text = resolve_pointer(document, locator['json_pointer'])
        start, length = locator['byte_start'], locator['byte_len']
        if not isinstance(text, str) or type(start) is not int or type(length) is not int or start < 0 or length < 0:
            return result | {'reason': 'invalid_text_or_utf8_bounds'}
        if start + length > len(text.encode()):
            return result | {'reason': 'utf8_span_outside_source_text'}
        if text.encode()[start:start + length].decode() != observation.get('text'):
            return result | {'reason': 'observation_quote_mismatch'}
        message_pointer = '/mapping/' + parts[2] + '/message'
        message = resolve_pointer(document, message_pointer)
    except (ValueError, TypeError, KeyError, IndexError, UnicodeDecodeError):
        return result | {'reason': 'source_binding_decode_failure'}
    actor = agree([value_at(message, path) for path in policy['actor_paths_relative_to_message']])
    source_id = agree([value_at(message, path) for path in policy['source_id_paths_relative_to_message']] +
        [value_at(document, path) for path in policy['source_id_paths_relative_to_document']])
    turn_id = agree([parts[2].replace('~1', '/').replace('~0', '~')] +
        [value_at(message, path) for path in policy['turn_id_paths_relative_to_message']])
    source_known_at = agree([value_at(message, policy['source_known_at_path_relative_to_message'])])
    if source_known_at['state'] == 'resolved':
        try:
            original = datetime.fromisoformat(source_known_at['value'].replace('Z', '+00:00'))
            observed = datetime.fromisoformat(observation['date'].replace('Z', '+00:00'))
            epoch = datetime.fromtimestamp(message['create_time'], timezone.utc)
            if original.tzinfo is None or original != observed or original != epoch:
                source_known_at = {'state': 'conflicting_source_fields', 'value': None}
        except (ValueError, TypeError, KeyError, OverflowError, AttributeError):
            source_known_at = {'state': 'invalid_date', 'value': None}
    transport_role = agree([value_at(message, policy['transport_role_path_relative_to_message'])])
    fields = {'source_actor_label': actor, 'source_id': source_id, 'source_turn_id': turn_id,
        'source_known_at_statement': source_known_at, 'transport_role': transport_role}
    return result | {'state': 'bound_source_projection', 'fields': fields,
        'source_sha256': raw_sha, 'message_pointer': message_pointer,
        'native_speaker': observation.get('speaker'), 'native_graph_modified': False}


def run():
    wall_start, cpu_start = time.perf_counter(), time.process_time()
    policy = json.loads((HERE / 'actor_projection_policy.json').read_text())
    before = json.loads((HERE / 'freeze_before_outputs.json').read_text())
    for path, expected in before['files_sha256'].items():
        if panel.sha(panel.ROOT / path) != expected:
            raise ValueError('actor_projection_freeze_drift')
    ledger = json.loads((panel.HERE / 'first_run_ledger2.json').read_text())
    projections, counts = [], Counter()
    for row in ledger['rows']:
        counts['planned_case_arms'] += 1
        if row['state'] != 'completed':
            counts['unavailable_case_arms'] += 1
            continue
        raw = (panel.ROOT / row['input_path']).read_bytes()
        snapshot = json.loads((panel.ROOT / row['output_directory'] / 'native_snapshot.json').read_text())
        for observation in snapshot['bodies']['loom_kb_observations']:
            projection = project(observation, raw, policy)
            projections.append({'case_id': row['case_id'], 'arm': row['arm']} | projection)
            counts['native_observations'] += 1
            counts['bound_source_projections'] += projection['state'] == 'bound_source_projection'
            fields = projection.get('fields', {})
            counts['all_declared_fields_resolved'] += bool(fields) and all(v['state'] == 'resolved' for v in fields.values())
            for field, value in projection.get('fields', {}).items():
                counts[field + ':' + value['state']] += 1
    result = {'schema': 'loom.research.native_actor_projection_first/1', 'counts': dict(counts),
        'projections': projections, 'posthoc_dev_source_projection': True,
        'native_runs': 0, 'native_graph_modified': False, 'gold_read': False,
        'validation_read': False, 'paid_calls': 0, 'semantic_identity_accuracy_measured': False,
        'runtime': {'wall_seconds': time.perf_counter() - wall_start,
            'cpu_seconds': time.process_time() - cpu_start,
            'process_peak_rss_kib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
            'scope': 'freeze_checks_source_reads_and_projection_excluding_python_import_startup'}}
    panel.write_new(HERE / 'first_projection.json', result)
    print(json.dumps(result['counts']))


if __name__ == '__main__':
    run()
