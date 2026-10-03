"""Versioned graph exchange/diff *projection*, never a second graph store.

Native Entity, Observation and Claim JSON records are copied without changing
their meaning. Instrument provenance sits beside the native records. Diff apply
is reversible, keeps removed/replaced records in an append-only history, and
does not assert that model acceptance establishes content truth.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import math

from .. import openrouter_runner as safe

PACKET_SCHEMA = 'loom.graph_packet/1'
DIFF_SCHEMA = 'loom.graph_packet_diff/1'
COLLECTIONS = ('definitions', 'entities', 'claims', 'sources')
ORIGINS = {'archive', 'repo', 'user', 'external_authority', 'model_knowledge', 'system'}
EVIDENCE = {'observed', 'derived', 'inferred', 'extrapolated', 'absent', 'user'}
STATUSES = {'active', 'contested', 'superseded', 'rejected'}
LOCATOR_FIELDS = {'source', 'member', 'json_pointer', 'byte_start', 'byte_len', 'time_start', 'time_end', 'line'}
CLAIM_FIELDS = {'id', 'subject', 'predicate', 'object', 'value', 'qualifiers', 'assessment'}
ASSESSMENT_FIELDS = {'basis', 'evidence_class', 'origin', 'confidence', 'premises', 'counter',
                     'status', 'consequences', 'open', 'expected_property', 'check_state', 'alternatives'}
ENTITY_FIELDS = {'id', 'kind', 'canonical_key', 'label', 'labels', 'aliases', 'parent', 'first_seen',
                 'last_seen', 'evidence_class', 'origin', 'confidence', 'status', 'attrs'}
OBSERVATION_FIELDS = {'id', 'unit', 'kind', 'text', 'locator', 'lang', 'date', 'ordinal', 'artifact_type', 'speaker', 'attrs'}
EXPECTED_PREDICATES = {'all', 'any', 'not', 'nonempty', 'in_enum', 'in_class', 'not_in', 'subset_of',
                       'version_gt', 'version_gte', 'version_between', 'date_between', 'co_mentioned_with',
                       'consistent_with', 'available_on', 'exists_symbol', 'matches', 'not_contradicted_by', 'ordered_by_date'}


def digest(value):
    return safe.digest(value)


def validate_json_resources(value, limits=None):
    """JSON validity plus optional caller-selected resource limits.

    None is unrestricted by this codec. Legacy transport caps are not universal
    graph/whole-archive limits. Actual allocation/serialization failures remain
    capability errors; a caller may choose bounded presets in its method data.
    """
    limits = {} if limits is None else limits
    allowed = {'max_nodes', 'max_depth', 'max_string_bytes', 'max_integer_bits'}
    if (not isinstance(limits, dict) or set(limits) - allowed or
            any(v is not None and (type(v) is not int or v < 1) for v in limits.values())):
        raise ValueError('graph_packet_resource_policy_invalid')
    pending, nodes, string_bytes = [(value, 0)], 0, 0
    while pending:
        item, depth = pending.pop(); nodes += 1
        if limits.get('max_nodes') is not None and nodes > limits['max_nodes']:
            raise ValueError('graph_packet_configured_node_resource_limit')
        if limits.get('max_depth') is not None and depth > limits['max_depth']:
            raise ValueError('graph_packet_configured_depth_resource_limit')
        if isinstance(item, dict):
            if any(not isinstance(k, str) for k in item):
                raise ValueError('graph_packet_json_object_key_invalid')
            pending.extend((x, depth + 1) for pair in item.items() for x in pair)
        elif isinstance(item, list):
            pending.extend((x, depth + 1) for x in item)
        elif isinstance(item, str):
            string_bytes += len(item.encode('utf-8'))
            if limits.get('max_string_bytes') is not None and string_bytes > limits['max_string_bytes']:
                raise ValueError('graph_packet_configured_string_resource_limit')
        elif type(item) is int:
            if limits.get('max_integer_bits') is not None and item.bit_length() > limits['max_integer_bits']:
                raise ValueError('graph_packet_configured_integer_resource_limit')
        elif type(item) is float:
            if not math.isfinite(item):
                raise ValueError('graph_packet_nonfinite_json_number')
        elif item is not None and type(item) is not bool:
            raise ValueError('graph_packet_json_value_invalid')
    return value


def _text(value, code, *, empty=False):
    if not isinstance(value, str) or (not empty and not value):
        raise ValueError(code)


def _texts(value, code):
    if not isinstance(value, list) or any(not isinstance(x, str) for x in value):
        raise ValueError(code)


def _number(value, code, *, unit=False):
    if type(value) not in (int, float) or not math.isfinite(value) or (unit and not 0 <= value <= 1):
        raise ValueError(code)


def _timestamp(value):
    if value is not None:
        safe._timestamp(value)


def validate_origin(origin):
    safe._keys(origin, {'kind', 'actor', 'model', 'recipe_sha256', 'response_sha256'})
    if origin['kind'] not in {'recorded', 'model', 'user', 'system'}:
        raise ValueError('invalid_instrument_origin_kind')
    _text(origin['actor'], 'instrument_actor_required')
    for name in ('model', 'recipe_sha256', 'response_sha256'):
        if origin[name] is not None:
            _text(origin[name], 'instrument_origin_value_invalid')
    if origin['kind'] == 'model' and not origin['model']:
        raise ValueError('model_instrument_identity_required')
    for name in ('recipe_sha256', 'response_sha256'):
        if origin[name] is not None and (len(origin[name]) != 64 or any(c not in '0123456789abcdef' for c in origin[name])):
            raise ValueError('instrument_hash_invalid')
    return origin


def validate_locator(locator):
    safe._keys(locator, LOCATOR_FIELDS)
    for key in ('source', 'member', 'json_pointer'):
        _text(locator[key], 'native_locator_string_invalid', empty=True)
    for key in ('byte_start', 'byte_len', 'line'):
        if locator[key] is not None and (type(locator[key]) is not int or locator[key] < 0):
            raise ValueError('native_locator_integer_invalid')
    for key in ('time_start', 'time_end'):
        if locator[key] is not None:
            _number(locator[key], 'native_locator_time_invalid')
    if locator['time_start'] is not None and locator['time_end'] is not None and locator['time_end'] < locator['time_start']:
        raise ValueError('native_locator_time_order_invalid')
    return locator


def validate_source(source):
    safe._keys(source, {'observation', 'known_at', 'text_sha256'})
    observation = source['observation']
    safe._keys(observation, OBSERVATION_FIELDS)
    for key in ('id', 'unit'):
        _text(observation[key], 'native_observation_identity_required')
    for key in ('text', 'lang', 'date', 'artifact_type', 'speaker'):
        _text(observation[key], 'native_observation_string_invalid', empty=True)
    if observation['kind'] not in {'sentence', 'list_item', 'heading', 'code_block', 'utterance', 'table_row', 'field'}:
        raise ValueError('native_observation_kind_invalid')
    if type(observation['ordinal']) is not int or not isinstance(observation['attrs'], dict):
        raise ValueError('native_observation_metadata_invalid')
    validate_locator(observation['locator'])
    if not observation['locator']['source']:
        raise ValueError('source_observation_locator_required')
    _timestamp(source['known_at'])
    if source['text_sha256'] != hashlib.sha256(observation['text'].encode('utf-8')).hexdigest():
        raise ValueError('source_text_hash_drift')
    return source


def validate_entity(entity):
    safe._keys(entity, ENTITY_FIELDS)
    for key in ('id', 'kind', 'canonical_key'):
        _text(entity[key], 'native_entity_identity_required')
    for key in ('label', 'parent', 'first_seen', 'last_seen'):
        _text(entity[key], 'native_entity_string_invalid', empty=True)
    if (not isinstance(entity['labels'], dict) or any(not isinstance(k, str) or not isinstance(v, str) for k, v in entity['labels'].items())
            or not isinstance(entity['attrs'], dict) or not isinstance(entity['aliases'], list)):
        raise ValueError('native_entity_metadata_invalid')
    if entity['evidence_class'] not in EVIDENCE or entity['origin'] not in ORIGINS or entity['status'] not in STATUSES:
        raise ValueError('native_entity_assessment_enum_invalid')
    _number(entity['confidence'], 'native_entity_confidence_invalid', unit=True)
    for alias in entity['aliases']:
        safe._keys(alias, {'key', 'surface', 'lang', 'method', 'count', 'confidence'})
        _text(alias['key'], 'native_alias_key_required')
        for key in ('surface', 'lang', 'method'):
            _text(alias[key], 'native_alias_string_invalid', empty=True)
        if type(alias['count']) is not int:
            raise ValueError('native_alias_count_invalid')
        _number(alias['confidence'], 'native_alias_confidence_invalid', unit=True)
    return entity


def validate_claim(claim):
    """Exact Claim.to_json shape and native local Claim/Assessment invariants.

    Native KnowledgeStore performs additional cross-claim/pack checks on import;
    passing this projection codec does not stand in for that operation.
    """
    safe._keys(claim, CLAIM_FIELDS)
    for key in ('id', 'subject', 'predicate'):
        _text(claim[key], 'native_claim_identity_required')
    _text(claim['object'], 'native_claim_object_invalid', empty=True)
    q, assessment = claim['qualifiers'], claim['assessment']
    safe._keys(q, {'valid_from', 'valid_to', 'version', 'branch', 'scope', 'lang', 'extra'})
    for key in set(q) - {'extra'}:
        _text(q[key], 'native_qualifier_string_invalid', empty=True)
    if not isinstance(q['extra'], dict):
        raise ValueError('native_qualifier_extra_invalid')
    safe._keys(assessment, ASSESSMENT_FIELDS)
    if (assessment['evidence_class'] not in EVIDENCE or assessment['origin'] not in ORIGINS or
            assessment['status'] not in STATUSES or assessment['check_state'] not in {'pending', 'holds', 'violated', 'n/a'}):
        raise ValueError('native_claim_assessment_enum_invalid')
    _number(assessment['confidence'], 'native_claim_confidence_invalid', unit=True)
    basis = assessment['basis']
    safe._keys(basis, {'support', 'derivation'})
    if not isinstance(basis['support'], list):
        raise ValueError('native_claim_support_array_required')
    for support in basis['support']:
        safe._keys(support, {'observation', 'locator', 'quote', 'extractor', 'quality'})
        _text(support['observation'], 'native_support_observation_required')
        for key in ('quote', 'extractor'):
            _text(support[key], 'native_support_string_invalid', empty=True)
        validate_locator(support['locator'])
        _number(support['quality'], 'native_support_quality_invalid', unit=True)
    derivation = basis['derivation']
    if derivation is not None:
        safe._keys(derivation, {'operator', 'operator_version', 'morphism', 'depth'})
        _text(derivation['operator'], 'native_derivation_operator_required')
        _text(derivation['morphism'], 'native_derivation_morphism_invalid', empty=True)
        if (type(derivation['operator_version']) is not int or derivation['operator_version'] < 1 or
                type(derivation['depth']) is not int or derivation['depth'] < 0):
            raise ValueError('native_derivation_numbers_invalid')
    for key, fields in (('premises', {'claims', 'principles', 'assumptions'}),
                        ('counter', {'observations', 'claims'}),
                        ('consequences', {'claims', 'predictions', 'checks'})):
        safe._keys(assessment[key], fields)
        for value in assessment[key].values():
            _texts(value, 'native_claim_reference_array_invalid')
    safe._keys(assessment['open'], {'slots', 'questions', 'fill_query'})
    _texts(assessment['open']['slots'], 'native_open_slots_invalid')
    _texts(assessment['open']['questions'], 'native_open_questions_invalid')
    if not isinstance(assessment['alternatives'], list):
        raise ValueError('native_alternatives_array_required')
    for alternative in assessment['alternatives']:
        safe._keys(alternative, {'object', 'value', 'score'})
        _text(alternative['object'], 'native_alternative_object_invalid', empty=True)
        _number(alternative['score'], 'native_alternative_score_invalid')
        if not alternative['object'] and alternative['value'] is None:
            raise ValueError('native_alternative_object_or_value_required')
    expected = assessment['expected_property']
    if expected is not None:
        safe._keys(expected, {'expr', 'rationale', 'confirm_if', 'refute_if'})
        if not isinstance(expected['expr'], dict) or expected['expr'].get('op') not in EXPECTED_PREDICATES:
            raise ValueError('native_expected_expression_invalid')
        _text(expected['rationale'], 'native_expected_rationale_invalid', empty=True)
        _texts(expected['confirm_if'], 'native_expected_confirm_invalid')
        _texts(expected['refute_if'], 'native_expected_refute_invalid')
    evidence = assessment['evidence_class']
    if evidence == 'observed' and not basis['support']:
        raise ValueError('native_observed_support_required')
    if evidence == 'absent' and basis['support']:
        raise ValueError('native_absent_support_forbidden')
    if evidence in {'derived', 'inferred', 'extrapolated'} and derivation is None:
        raise ValueError('native_derived_operator_required')
    if evidence == 'inferred' and (expected is None or assessment['check_state'] == 'n/a'):
        raise ValueError('native_inferred_expected_check_required')
    if assessment['check_state'] != 'n/a' and expected is None:
        raise ValueError('native_check_expected_property_required')
    has_object, has_value = bool(claim['object']), claim['value'] is not None
    if evidence == 'absent':
        if has_object or has_value:
            raise ValueError('native_absent_object_value_forbidden')
    elif has_object == has_value:
        raise ValueError('native_claim_object_xor_value_required')
    return claim


def validate_definition(definition):
    safe._keys(definition, {'id', 'kind', 'description', 'examples', 'origin', 'attrs'})
    for key in ('id', 'kind'):
        _text(definition[key], 'definition_identity_required')
    _text(definition['description'], 'definition_description_invalid', empty=True)
    if not isinstance(definition['examples'], list) or not isinstance(definition['attrs'], dict):
        raise ValueError('definition_examples_or_attrs_invalid')
    validate_origin(definition['origin'])
    return definition


VALIDATORS = {'definitions': validate_definition, 'entities': validate_entity, 'claims': validate_claim, 'sources': validate_source}


def record_id(collection, record):
    return record['observation']['id'] if collection == 'sources' else record['id']


def _indexes(packet):
    return {name: {record_id(name, item): item for item in packet[name]} for name in COLLECTIONS}


def _packet_payload(packet):
    return {key: value for key, value in packet.items() if key != 'packet_id'}


def _validate_packet_content(packet):
    safe._keys(packet, {'schema', 'packet_id', *COLLECTIONS, 'task', 'provenance', 'history'})
    if packet['schema'] != PACKET_SCHEMA or packet['packet_id'] != digest(_packet_payload(packet)):
        raise ValueError('graph_packet_content_identity_invalid')
    if not isinstance(packet['task'], dict) or not isinstance(packet['history'], list):
        raise ValueError('graph_packet_task_or_history_invalid')
    safe._keys(packet['provenance'], set(COLLECTIONS))
    all_ids = set()
    for name in COLLECTIONS:
        if not isinstance(packet[name], list) or not isinstance(packet['provenance'][name], dict):
            raise ValueError('graph_packet_collection_invalid')
        ids = set()
        for record in packet[name]:
            VALIDATORS[name](record)
            ident = record_id(name, record)
            if ident in ids or ident in all_ids:
                raise ValueError('graph_packet_duplicate_or_colliding_record_id')
            ids.add(ident); all_ids.add(ident)
            if ident not in packet['provenance'][name]:
                raise ValueError('graph_packet_record_provenance_missing')
            metadata = packet['provenance'][name][ident]
            safe._keys(metadata, {'known_at', 'origin', 'record_sha256'})
            _timestamp(metadata['known_at']); validate_origin(metadata['origin'])
            if metadata['record_sha256'] != digest(record):
                raise ValueError('graph_packet_record_provenance_hash_drift')
        if set(packet['provenance'][name]) != ids:
            raise ValueError('graph_packet_extra_provenance_id')
    indexes = _indexes(packet)
    entities, claims, sources = indexes['entities'], indexes['claims'], indexes['sources']
    for entity in entities.values():
        if entity['parent'] and entity['parent'] not in entities:
            raise ValueError('graph_packet_unknown_entity_parent')
    for claim in claims.values():
        if claim['subject'] not in entities or (claim['object'] and claim['object'] not in entities):
            raise ValueError('graph_packet_unknown_claim_entity')
        # Referenced claims must be in this packet. Other native object kinds
        # (principles/checks/etc.) remain opaque IDs for native import checks.
        references = (claim['assessment']['premises']['claims'] + claim['assessment']['counter']['claims'] +
                      claim['assessment']['consequences']['claims'])
        if any(ref not in claims for ref in references):
            raise ValueError('graph_packet_unknown_claim_reference')
        for support in claim['assessment']['basis']['support']:
            source = sources.get(support['observation'])
            if source is None:
                raise ValueError('graph_packet_missing_support_observation')
            observation = source['observation']
            if not support['quote'] or support['quote'] not in observation['text']:
                raise ValueError('graph_packet_support_quote_mismatch')
            original, located = observation['locator'], support['locator']
            if located != original:
                # Native Support can carry a narrower absolute byte span inside
                # the observation. Preserve that locator and verify exact UTF-8
                # bytes instead of forcing the full observation's locator.
                if any(located[k] != original[k] for k in ('source', 'member', 'json_pointer', 'time_start', 'time_end')):
                    raise ValueError('graph_packet_support_locator_mismatch')
                if (original['byte_start'] is None or located['byte_start'] is None or located['byte_len'] is None):
                    raise ValueError('graph_packet_support_subspan_unverifiable')
                start = located['byte_start'] - original['byte_start']
                raw = observation['text'].encode('utf-8')
                stop = start + located['byte_len']
                if start < 0 or stop > len(raw) or raw[start:stop] != support['quote'].encode('utf-8'):
                    raise ValueError('graph_packet_support_subspan_quote_mismatch')
        for reference in claim['assessment']['premises']['claims']:
            premise = claims[reference]['assessment']
            if premise['evidence_class'] in {'absent', 'extrapolated'}:
                raise ValueError('native_absent_or_extrapolated_premise_forbidden')
            derivation = claim['assessment']['basis']['derivation']
            premise_derivation = premise['basis']['derivation']
            if derivation and premise_derivation and derivation['morphism'] and premise_derivation['morphism']:
                raise ValueError('native_transfer_chain_forbidden')
    return packet


def _validate_history(packet):
    """Replay each retained change backwards and forwards to the hash-bound head.

    These are mechanical consistency checks, not an authentication claim about
    an untrusted imported packet. An external source/commit/manifest anchor is
    needed to establish that an entire internally consistent history is genuine.
    """
    current = deepcopy(packet)
    while current['history']:
        event = current['history'][-1]
        safe._keys(event, {'application_id', 'base_packet_id', 'diff', 'changes', 'previous_task', 'previous_order', 'result_origin'})
        if event['application_id'] != digest({k: v for k, v in event.items() if k != 'application_id'}):
            raise ValueError('graph_packet_history_hash_drift')
        validate_origin(event['result_origin'])
        safe._keys(event['previous_order'], set(COLLECTIONS))
        if not isinstance(event['changes'], list) or not isinstance(event['previous_task'], dict):
            raise ValueError('graph_packet_history_changes_invalid')
        indexes = deepcopy(_indexes(current)); provenance = deepcopy(current['provenance']); touched = set()
        for change in reversed(event['changes']):
            safe._keys(change, {'collection', 'action', 'record_id', 'before', 'after', 'before_provenance', 'after_provenance'})
            name, ident = change['collection'], change['record_id']
            if name not in COLLECTIONS or change['action'] not in {'add', 'update', 'remove'} or (name, ident) in touched:
                raise ValueError('graph_packet_history_change_identity_invalid')
            touched.add((name, ident))
            if indexes[name].get(ident) != change['after'] or provenance[name].get(ident) != change['after_provenance']:
                raise ValueError('graph_packet_history_after_state_mismatch')
            before = change['before']
            if before is None:
                if change['action'] != 'add' or change['before_provenance'] is not None:
                    raise ValueError('graph_packet_history_missing_before_record')
                del indexes[name][ident]; del provenance[name][ident]
            else:
                VALIDATORS[name](before)
                if record_id(name, before) != ident or change['action'] == 'add':
                    raise ValueError('graph_packet_history_before_identity_invalid')
                metadata = change['before_provenance']
                safe._keys(metadata, {'known_at', 'origin', 'record_sha256'})
                validate_origin(metadata['origin']); _timestamp(metadata['known_at'])
                if metadata['record_sha256'] != digest(before):
                    raise ValueError('graph_packet_history_before_provenance_hash_drift')
                indexes[name][ident] = deepcopy(before); provenance[name][ident] = deepcopy(metadata)
        parent = deepcopy(current)
        parent['history'].pop(); parent['task'] = deepcopy(event['previous_task']); parent['provenance'] = provenance
        for name in COLLECTIONS:
            order = event['previous_order'][name]
            if not isinstance(order, list) or len(order) != len(set(order)) or set(order) != set(indexes[name]):
                raise ValueError('graph_packet_history_previous_order_invalid')
            parent[name] = [indexes[name][ident] for ident in order]
        parent['packet_id'] = digest(_packet_payload(parent))
        if parent['packet_id'] != event['base_packet_id']:
            raise ValueError('graph_packet_history_parent_hash_mismatch')
        _validate_packet_content(parent)
        _validate_diff_structure(event['diff'], parent)
        replayed, replayed_event = _candidate(parent, event['diff'], validate_result=False)
        if replayed != current or replayed_event != event:
            raise ValueError('graph_packet_history_forward_replay_mismatch')
        current = parent


def validate_packet(packet, *, resource_limits=None):
    validate_json_resources(packet, resource_limits)
    _validate_packet_content(packet)
    _validate_history(packet)
    return packet


def make_packet(*, definitions=(), entities=(), claims=(), sources=(), task=None, origin, known_at=None):
    validate_origin(origin); _timestamp(known_at)
    packet = {'schema': PACKET_SCHEMA, 'definitions': deepcopy(list(definitions)), 'entities': deepcopy(list(entities)),
              'claims': deepcopy(list(claims)), 'sources': deepcopy(list(sources)), 'task': deepcopy(task or {}),
              'provenance': {name: {} for name in COLLECTIONS}, 'history': []}
    for name in COLLECTIONS:
        for item in packet[name]:
            packet['provenance'][name][record_id(name, item)] = {
                'known_at': item['known_at'] if name == 'sources' else known_at,
                'origin': deepcopy(origin), 'record_sha256': digest(item)}
    packet['packet_id'] = digest(packet)
    return validate_packet(packet)


def empty_diff(packet, *, proposal_id, origin, known_at=None):
    validate_packet(packet); validate_origin(origin); _timestamp(known_at)
    return {'schema': DIFF_SCHEMA, 'base_packet_sha256': packet['packet_id'], 'proposal_id': proposal_id,
            'origin': deepcopy(origin), 'known_at': known_at,
            **{name: {'add': [], 'update': [], 'remove': []} for name in COLLECTIONS},
            'task': None, 'annotations': {'critique': [], 'questions': [], 'operations': []}}


def _validate_diff_structure(diff, packet):
    safe._keys(diff, {'schema', 'base_packet_sha256', 'proposal_id', 'origin', 'known_at', *COLLECTIONS, 'task', 'annotations'})
    if diff['schema'] != DIFF_SCHEMA or diff['base_packet_sha256'] != packet['packet_id']:
        raise ValueError('graph_diff_stale_or_incompatible_base')
    _text(diff['proposal_id'], 'graph_diff_proposal_identity_required')
    validate_origin(diff['origin']); _timestamp(diff['known_at'])
    indexes = _indexes(packet)
    touched = set()
    for name in COLLECTIONS:
        edits = diff[name]
        safe._keys(edits, {'add', 'update', 'remove'})
        if any(not isinstance(edits[key], list) for key in edits):
            raise ValueError('graph_diff_collection_arrays_required')
        for action in ('add', 'update', 'remove'):
            for entry in edits[action]:
                if action == 'add':
                    VALIDATORS[name](entry); ident = record_id(name, entry)
                    if ident in indexes[name]:
                        raise ValueError('graph_diff_add_existing_record')
                else:
                    safe._keys(entry, {'id', 'before_sha256', 'after'} if action == 'update' else {'id', 'before_sha256', 'reason'})
                    ident = entry['id']; _text(ident, 'graph_diff_record_identity_required')
                    if ident not in indexes[name] or entry['before_sha256'] != digest(indexes[name][ident]):
                        raise ValueError('graph_diff_record_compare_and_swap_failed')
                    if action == 'update':
                        VALIDATORS[name](entry['after'])
                        if record_id(name, entry['after']) != ident:
                            raise ValueError('graph_diff_update_cannot_change_record_id')
                        if name == 'sources' and entry['after'] != indexes[name][ident]:
                            raise ValueError('graph_diff_raw_source_update_requires_new_record')
                        if name == 'claims' and any(entry['after'][k] != indexes[name][ident][k]
                                for k in ('subject', 'predicate', 'object', 'value', 'qualifiers')):
                            raise ValueError('graph_diff_claim_content_change_requires_new_record_id')
                        if name == 'entities' and any(entry['after'][k] != indexes[name][ident][k]
                                for k in ('kind', 'canonical_key')):
                            raise ValueError('graph_diff_entity_identity_change_requires_new_record_id')
                    else:
                        _text(entry['reason'], 'graph_diff_tombstone_reason_required')
                key = (name, ident)
                if key in touched:
                    raise ValueError('graph_diff_duplicate_record_edit')
                touched.add(key)
    if diff['task'] is not None:
        safe._keys(diff['task'], {'before_sha256', 'after'})
        if diff['task']['before_sha256'] != digest(packet['task']) or not isinstance(diff['task']['after'], dict):
            raise ValueError('graph_diff_task_compare_and_swap_failed')
    safe._keys(diff['annotations'], {'critique', 'questions', 'operations'})
    if any(not isinstance(x, list) for x in diff['annotations'].values()):
        raise ValueError('graph_diff_annotations_arrays_required')
    # Descriptive intents are open data. They do not silently perform merges,
    # splits, alias rewrites, type registration, truth promotion or relinking.
    for operation in diff['annotations']['operations']:
        safe._keys(operation, {'kind', 'inputs', 'outputs', 'reason'})
        _text(operation['kind'], 'graph_diff_operation_kind_required')
        _texts(operation['inputs'], 'graph_diff_operation_inputs_invalid')
        _texts(operation['outputs'], 'graph_diff_operation_outputs_invalid')
        _text(operation['reason'], 'graph_diff_operation_reason_invalid', empty=True)
    return diff


def validate_diff(diff, packet, *, resource_limits=None):
    validate_packet(packet, resource_limits=resource_limits); validate_json_resources(diff, resource_limits)
    return _validate_diff_structure(diff, packet)


def _candidate(packet, diff, *, validate_result=True):
    indexes = deepcopy(_indexes(packet)); provenance = deepcopy(packet['provenance']); changes = []
    for name in COLLECTIONS:
        for action in ('add', 'update', 'remove'):
            for edit in diff[name][action]:
                ident = record_id(name, edit) if action == 'add' else edit['id']
                before = indexes[name].get(ident)
                before_provenance = provenance[name].get(ident)
                after = deepcopy(edit) if action == 'add' else deepcopy(edit['after']) if action == 'update' else None
                after_provenance = None
                if after is None:
                    del indexes[name][ident]; del provenance[name][ident]
                else:
                    indexes[name][ident] = after
                    after_provenance = {'known_at': after['known_at'] if name == 'sources' else diff['known_at'],
                                        'origin': deepcopy(diff['origin']), 'record_sha256': digest(after)}
                    provenance[name][ident] = after_provenance
                changes.append({'collection': name, 'action': action, 'record_id': ident,
                                'before': deepcopy(before), 'after': deepcopy(after),
                                'before_provenance': deepcopy(before_provenance),
                                'after_provenance': deepcopy(after_provenance)})
    result = deepcopy(packet)
    for name in COLLECTIONS:
        # Retain original ordering; newly added records retain declared order.
        result[name] = list(indexes[name].values())
    result['provenance'] = provenance
    if diff['task'] is not None:
        result['task'] = deepcopy(diff['task']['after'])
    event = {'base_packet_id': packet['packet_id'], 'diff': deepcopy(diff), 'changes': changes,
             'previous_task': deepcopy(packet['task']),
             'previous_order': {name: [record_id(name, item) for item in packet[name]] for name in COLLECTIONS},
             'result_origin': deepcopy(diff['origin'])}
    event['application_id'] = digest(event)
    result['history'].append(event)
    result['packet_id'] = digest(_packet_payload(result))
    if validate_result:
        validate_packet(result)
    return result, event


def preview_diff(packet, diff):
    validate_diff(diff, packet)
    candidate, event = _candidate(packet, diff)
    return {'schema': 'loom.graph_packet_preview/1', 'base_packet_id': packet['packet_id'],
            'diff_sha256': digest(diff), 'candidate_packet': candidate, 'changes': deepcopy(event['changes']),
            'annotations': deepcopy(diff['annotations']), 'canonical_store_written': False,
            'acceptance_establishes_content_truth': False}


def apply_diff(packet, diff, policy, *, explicitly_accepted=False):
    safe._keys(policy, {'schema', 'acceptance', 'allow_source_tombstones'})
    if (policy['schema'] != 'loom.graph_packet_apply_policy/1' or policy['acceptance'] not in {'preview', 'auto'} or
            type(policy['allow_source_tombstones']) is not bool or type(explicitly_accepted) is not bool):
        raise ValueError('graph_packet_apply_policy_invalid')
    preview = preview_diff(packet, diff)
    if diff['sources']['remove'] and not policy['allow_source_tombstones']:
        raise ValueError('source_projection_tombstones_disabled_by_policy')
    accepted = policy['acceptance'] == 'auto' or explicitly_accepted
    candidate = preview['candidate_packet']
    receipt = {'schema': 'loom.graph_packet_application/1', 'accepted': accepted,
               'policy': deepcopy(policy), 'explicitly_accepted': explicitly_accepted,
               'before_packet': deepcopy(packet), 'diff': deepcopy(diff),
               'candidate_packet_sha256': candidate['packet_id'],
               'after_packet_sha256': candidate['packet_id'] if accepted else packet['packet_id'],
               'canonical_store_written': False, 'acceptance_establishes_content_truth': False}
    receipt['receipt_sha256'] = digest(receipt)
    return (candidate if accepted else deepcopy(packet)), receipt


def invert_application(receipt, current_packet):
    """Restore the exact projection only while the receipt is the current head.

    The immutable receipt still contains the proposal, old/new values and origin;
    callers keep it when choosing to restore the prior projection.
    """
    safe._keys(receipt, {'schema', 'accepted', 'policy', 'explicitly_accepted', 'before_packet', 'diff',
                        'candidate_packet_sha256', 'after_packet_sha256', 'canonical_store_written',
                        'acceptance_establishes_content_truth', 'receipt_sha256'})
    if (receipt['schema'] != 'loom.graph_packet_application/1' or
            receipt['receipt_sha256'] != digest({k: v for k, v in receipt.items() if k != 'receipt_sha256'})):
        raise ValueError('graph_packet_application_receipt_drift')
    validate_packet(current_packet)
    if current_packet['packet_id'] != receipt['after_packet_sha256']:
        raise ValueError('graph_packet_inverse_requires_current_application_head')
    before = validate_packet(receipt['before_packet'])
    recomputed, _ = apply_diff(before, receipt['diff'], receipt['policy'], explicitly_accepted=receipt['explicitly_accepted'])
    if recomputed != current_packet:
        raise ValueError('graph_packet_application_replay_mismatch')
    return deepcopy(before)


def encode_packet(packet):
    validate_packet(packet)
    return safe.canonical(packet)


def decode_packet(raw):
    return validate_packet(safe.parse_json(raw))
