"""Project source-recorded actor metadata from hash-bound native observations.

This active, offline adapter accepts the declared synthetic OpenAI wrapper
contract. Historical actor_projection_v1 runners remain frozen and unchanged.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re

DEFAULT_POLICY = Path(__file__).with_name('actor_projection_v1') / 'actor_projection_policy_v2.json'


class AmbiguousPointer(ValueError):
    """A referenced object member occurs more than once in the raw JSON."""


class InvalidPointer(ValueError):
    """A pointer or the source structure cannot identify the requested value."""


class _Object(dict):
    def __init__(self, pairs):
        super().__init__()
        self.duplicates = set()
        for key, value in pairs:
            if key in self:
                self.duplicates.add(key)
            self[key] = value


def pointer_tokens(pointer):
    """Decode RFC6901 tokens, without normalizing invalid escape sequences."""
    if not isinstance(pointer, str) or (pointer and not pointer.startswith('/')):
        raise InvalidPointer('invalid_json_pointer')
    if re.search(r'~(?![01])', pointer):
        raise InvalidPointer('invalid_json_pointer_escape')
    return [] if not pointer else [part.replace('~1', '/').replace('~0', '~')
                                   for part in pointer[1:].split('/')]


def resolve_pointer(document, pointer):
    value = document
    for key in pointer_tokens(pointer):
        if isinstance(value, dict):
            if isinstance(value, _Object) and key in value.duplicates:
                raise AmbiguousPointer('duplicate_json_member')
            value = value[key]
        elif isinstance(value, list):
            if not re.fullmatch(r'0|[1-9][0-9]*', key):
                raise InvalidPointer('invalid_json_array_index')
            # Compare decimal strings before conversion: arbitrary source/policy
            # tokens can exceed Python's integer-string conversion limit.
            maximum = str(len(value) - 1)
            if not value or len(key) > len(maximum) or (len(key) == len(maximum) and key > maximum):
                raise IndexError('json_array_index_out_of_range')
            value = value[int(key)]
        else:
            raise InvalidPointer('invalid_source_structure')
    return value


def _reject_constant(value):
    raise ValueError('non_json_numeric_constant:' + value)


def _read(document, pointer):
    try:
        return 'present', resolve_pointer(document, pointer)
    except AmbiguousPointer:
        return 'ambiguous_source_fields', None
    except InvalidPointer:
        return 'invalid_source_structure', None
    except (KeyError, IndexError):
        return 'missing', None


def _agree(reads):
    for state in ('ambiguous_source_fields', 'invalid_source_structure'):
        if any(kind == state for kind, _ in reads):
            return {'state': state, 'value': None}
    values = [value for _, value in reads if value is not None]
    if any(not isinstance(value, str) or not value.strip() for value in values):
        return {'state': 'invalid_type_or_empty', 'value': None}
    try:
        for value in values:
            value.encode('utf-8')
    except UnicodeError:
        return {'state': 'invalid_unicode', 'value': None}
    present = set(values)
    if len(present) > 1:
        return {'state': 'conflicting_source_fields', 'value': None}
    return {'state': 'resolved' if present else 'unknown',
            'value': next(iter(present)) if present else None}


def validate_policy(policy):
    if policy['supported_source_format'] != 'frozen_synthetic_openai_wrapper':
        raise ValueError('unsupported_source_format')
    if policy['conflict_policy'] != 'abstain_per_field' or policy['missing_policy'] != 'unknown_per_field':
        raise ValueError('unsupported_actor_projection_resolution_policy')
    if (policy['role_fallback_for_actor'] or policy['materialize_native_graph'] or
            policy['cross_source_identity_merge'] != 'forbidden'):
        raise ValueError('unsupported_actor_projection_identity_policy')
    if (policy['identity_semantics'] != 'literal_source_actor_label_not_verified_world_identity' or
            policy['actor_epistemic_status'] != 'source_recorded'):
        raise ValueError('unsupported_actor_projection_evidence_semantics')
    parts = policy['allowed_content_parts']
    if not isinstance(parts, list) or not parts or any(type(index) is not int or index < 0 for index in parts):
        raise ValueError('invalid_actor_projection_content_part_policy')
    for name in ('actor_paths_relative_to_message', 'source_id_paths_relative_to_message',
                 'source_id_paths_relative_to_document', 'turn_id_paths_relative_to_message'):
        paths = policy[name]
        if not isinstance(paths, list) or not paths:
            raise ValueError('invalid_actor_projection_field_paths:' + name)
        for pointer in paths:
            pointer_tokens(pointer)
    for name in ('source_known_at_path_relative_to_message', 'transport_role_path_relative_to_message'):
        pointer_tokens(policy[name])


def _project(observation, raw_sha, document, decode_error, policy):
    result = {'observation_id': observation.get('id'), 'state': 'unavailable',
              'epistemic_kind': 'source_assertion', 'identity_semantics': policy['identity_semantics'],
              'actor_epistemic_status': policy['actor_epistemic_status']}
    locator = observation.get('locator')
    if not isinstance(locator, dict):
        return result | {'reason': 'missing_or_invalid_native_locator'}
    if locator.get('source') != 'sha256:' + raw_sha:
        return result | {'reason': 'raw_source_hash_mismatch'}
    pointer = locator.get('json_pointer')
    if not isinstance(pointer, str):
        return result | {'reason': 'missing_or_invalid_source_pointer'}
    try:
        parts = pointer_tokens(pointer)
    except InvalidPointer:
        return result | {'reason': 'invalid_source_pointer'}
    if (len(parts) != 6 or parts[0] != 'mapping' or parts[2:5] != ['message', 'content', 'parts'] or
            parts[5] not in {str(index) for index in policy['allowed_content_parts']}):
        return result | {'reason': 'unsupported_source_pointer_format'}
    if decode_error:
        return result | {'reason': 'source_binding_decode_failure'}
    message_pointer = '/'.join(pointer.split('/')[:4])
    try:
        if not isinstance(resolve_pointer(document, '/mapping'), dict):
            raise InvalidPointer('mapping_must_be_object')
        message = resolve_pointer(document, message_pointer)
        if not isinstance(message, dict):
            raise InvalidPointer('message_must_be_object')
        content_parts = resolve_pointer(document, message_pointer + '/content/parts')
        if not isinstance(content_parts, list):
            raise InvalidPointer('content_parts_must_be_array')
        text = resolve_pointer(document, pointer)
        start, length = locator['byte_start'], locator['byte_len']
        if not isinstance(text, str) or type(start) is not int or type(length) is not int or start < 0 or length < 0:
            return result | {'reason': 'invalid_text_or_utf8_bounds'}
        encoded = text.encode('utf-8')
        if start + length > len(encoded):
            return result | {'reason': 'utf8_span_outside_source_text'}
        for boundary in (start, start + length):
            if boundary < len(encoded) and encoded[boundary] & 0xC0 == 0x80:
                return result | {'reason': 'invalid_utf8_span_boundary'}
        if encoded[start:start + length].decode('utf-8') != observation.get('text'):
            return result | {'reason': 'observation_quote_mismatch'}
    except AmbiguousPointer:
        return result | {'reason': 'ambiguous_source_binding'}
    except (ValueError, TypeError, KeyError, IndexError, UnicodeError):
        return result | {'reason': 'source_binding_decode_failure'}

    def field(name):
        return [_read(message, path) for path in policy[name]]

    actor = _agree(field('actor_paths_relative_to_message'))
    source_id = _agree(field('source_id_paths_relative_to_message') +
                       [_read(document, path) for path in policy['source_id_paths_relative_to_document']])
    turn_id = _agree([('present', parts[1])] + field('turn_id_paths_relative_to_message'))
    known_at = _agree([_read(message, policy['source_known_at_path_relative_to_message'])])
    if known_at['state'] == 'resolved':
        epoch_state, epoch_value = _read(message, '/create_time')
        if epoch_state in ('ambiguous_source_fields', 'invalid_source_structure'):
            known_at = {'state': epoch_state, 'value': None}
        else:
            try:
                if type(epoch_value) not in (int, float) or not math.isfinite(epoch_value):
                    raise ValueError('invalid_epoch')
                original = datetime.fromisoformat(known_at['value'].replace('Z', '+00:00'))
                observed = datetime.fromisoformat(observation['date'].replace('Z', '+00:00'))
                epoch = datetime.fromtimestamp(epoch_value, timezone.utc)
                if original.tzinfo is None or original != observed or original != epoch:
                    known_at = {'state': 'conflicting_source_fields', 'value': None}
            except (ValueError, TypeError, KeyError, OverflowError, AttributeError, OSError):
                known_at = {'state': 'invalid_date', 'value': None}
    role = _agree([_read(message, policy['transport_role_path_relative_to_message'])])
    return result | {'state': 'bound_source_projection', 'fields': {
        'source_actor_label': actor, 'source_id': source_id, 'source_turn_id': turn_id,
        'source_known_at_statement': known_at, 'transport_role': role},
        'source_sha256': raw_sha, 'message_pointer': message_pointer,
        'native_speaker': observation.get('speaker'), 'native_graph_modified': False}


def project_many(observations, raw, policy):
    """Decode/hash one immutable source once and project its observations in order."""
    validate_policy(policy)
    if not isinstance(raw, bytes):
        raise TypeError('raw_source_must_be_bytes')
    observations = list(observations)
    if any(not isinstance(observation, dict) for observation in observations):
        raise ValueError('observations_must_be_objects')
    raw_sha = hashlib.sha256(raw).hexdigest()
    document, decode_error = None, False
    try:
        document = json.loads(raw, object_pairs_hook=_Object, parse_constant=_reject_constant)
    except (ValueError, UnicodeError, RecursionError):
        decode_error = True
    return [_project(observation, raw_sha, document, decode_error, policy) for observation in observations]


def project(observation, raw, policy):
    return project_many([observation], raw, policy)[0]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw-source', type=Path, required=True)
    parser.add_argument('--observations', type=Path, required=True,
                        help='JSON observation array or native_snapshot.json')
    parser.add_argument('--policy', type=Path, default=DEFAULT_POLICY)
    parser.add_argument('--output', type=Path, required=True, help='New output file; existing files are not overwritten')
    args = parser.parse_args(argv)
    observations = json.loads(args.observations.read_bytes())
    if isinstance(observations, dict):
        observations = observations['bodies']['loom_kb_observations']
    if not isinstance(observations, list):
        parser.error('observations must be an array or a native snapshot')
    raw = args.raw_source.read_bytes()
    policy_raw = args.policy.read_bytes()
    projections = project_many(observations, raw, json.loads(policy_raw))
    output = {'schema': 'loom.research.source_actor_projection/1',
              'source_sha256': hashlib.sha256(raw).hexdigest(),
              'policy_sha256': hashlib.sha256(policy_raw).hexdigest(),
              'observations': len(projections), 'projections': projections,
              'native_graph_modified': False, 'semantic_identity_accuracy_measured': False}
    with args.output.open('x', encoding='utf-8') as stream:
        stream.write(json.dumps(output, ensure_ascii=True, sort_keys=True, indent=2) + '\n')


if __name__ == '__main__':
    main()
