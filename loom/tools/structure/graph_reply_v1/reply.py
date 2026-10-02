"""Compile one model-authored graph reply into an append-only GraphPacket diff.

The wire is a graph of equally-shaped response/part nodes. ``children`` are
ordered, lossless text partitions; the host computes all UTF-8 byte spans.
Model-proposed semantic links remain *draft data*, not native truth claims.
Only mechanically checked containment/turn topology becomes native Claims.
All graph operations here are projections; no canonical store is written.
"""
from __future__ import annotations

import base64
from copy import deepcopy
import hashlib

try:
    from ..agentic_graph_v1 import packet as graph
    from .. import openrouter_runner as safe
except ImportError:
    from agentic_graph_v1 import packet as graph
    import openrouter_runner as safe

REPLY_SCHEMA = 'loom.graph_reply/1'
COMPOSED_REPLY_SCHEMA = 'loom.graph_reply/2'
CAPTURE_SCHEMA = 'loom.graph_reply_capture/1'
COMPILATION_SCHEMA = 'loom.graph_reply_compilation/1'
COMPILER = 'loom.graph_reply_compiler/1'


class GraphReplyError(ValueError):
    """Fixed error code plus exact first-attempt capture, even on invalid JSON."""

    def __init__(self, code, capture=None):
        super().__init__(code)
        self.code = code
        self.capture = deepcopy(capture)


def _keys(value, expected, code):
    if not isinstance(value, dict) or set(value) != set(expected):
        raise GraphReplyError(code)


def _text(value, code):
    if not isinstance(value, str) or not value:
        raise GraphReplyError(code)
    try:
        value.encode('utf-8')
    except UnicodeError:
        raise GraphReplyError('graph_reply_invalid_unicode') from None


def capture_response(raw):
    """Retain actual input bytes before parsing, never a reserialized substitute.

    Bytes are preferred at a transport boundary. A supplied str has already
    been decoded by its caller; its exact UTF-8 representation is retained.
    """
    if isinstance(raw, str):
        try:
            raw = raw.encode('utf-8')
        except UnicodeError:
            raise GraphReplyError('graph_reply_invalid_unicode') from None
    if not isinstance(raw, bytes):
        raise GraphReplyError('graph_reply_raw_bytes_or_text_required')
    return {'schema': CAPTURE_SCHEMA, 'sha256': hashlib.sha256(raw).hexdigest(),
            'byte_len': len(raw), 'raw_base64': base64.b64encode(raw).decode('ascii')}


def _capture_bytes(capture):
    _keys(capture, {'schema', 'sha256', 'byte_len', 'raw_base64'}, 'graph_reply_capture_invalid')
    if capture['schema'] != CAPTURE_SCHEMA or not isinstance(capture['raw_base64'], str):
        raise GraphReplyError('graph_reply_capture_invalid')
    try:
        raw = base64.b64decode(capture['raw_base64'], validate=True)
    except (ValueError, UnicodeError):
        raise GraphReplyError('graph_reply_capture_invalid') from None
    if (type(capture['byte_len']) is not int or len(raw) != capture['byte_len'] or
            hashlib.sha256(raw).hexdigest() != capture['sha256']):
        raise GraphReplyError('graph_reply_capture_hash_drift')
    return raw


def _parse_capture(capture):
    raw = _capture_bytes(capture)
    try:
        # Explicit UTF-8 avoids json.loads(bytes)' implicit UTF-16/UTF-32 handling.
        return safe.parse_json(raw.decode('utf-8'))
    except RecursionError:
        raise GraphReplyError('graph_reply_resource_capability_exhausted', capture) from None
    except (UnicodeError, ValueError) as exc:
        code = str(exc) if isinstance(exc, safe.RunnerError) else 'graph_reply_invalid_utf8'
        raise GraphReplyError(code, capture) from None


def validate_reply(reply, packet, *, resource_limits=None):
    """Validate wire shape, complete text tree and context-bound references.

    No text repairs, whitespace normalization, Markdown unwrapping, node
    selection or unbound semantic-link interpretation is performed.
    """
    graph.validate_packet(packet, resource_limits=resource_limits)
    try:
        graph.validate_json_resources(reply, resource_limits)
    except (UnicodeError, ValueError) as exc:
        raise GraphReplyError('graph_reply_invalid_unicode' if isinstance(exc, UnicodeError) else str(exc)) from None
    _keys(reply, {'schema', 'base_packet_sha256', 'response_id', 'nodes', 'links'}, 'graph_reply_shape_invalid')
    if reply['schema'] not in (REPLY_SCHEMA, COMPOSED_REPLY_SCHEMA):
        raise GraphReplyError('graph_reply_schema_invalid')
    if reply['base_packet_sha256'] != packet['packet_id']:
        raise GraphReplyError('graph_reply_stale_base')
    _text(reply['response_id'], 'graph_reply_response_identity_required')
    if not isinstance(reply['nodes'], list) or not reply['nodes']:
        raise GraphReplyError('graph_reply_nodes_required')
    nodes = {}
    for node in reply['nodes']:
        _keys(node, {'id', 'text', 'role', 'children'}, 'graph_reply_node_shape_invalid')
        for key in ('id', 'role'):
            _text(node[key], 'graph_reply_node_' + key + '_required')
        if reply['schema'] == REPLY_SCHEMA or node['text'] is not None:
            _text(node['text'], 'graph_reply_node_text_required')
        if node['id'] in nodes:
            raise GraphReplyError('graph_reply_duplicate_node_id')
        children = node['children']
        if not isinstance(children, list):
            raise GraphReplyError('graph_reply_children_array_required')
        for child in children:
            _text(child, 'graph_reply_child_identity_required')
        if len(children) != len(set(children)):
            raise GraphReplyError('graph_reply_duplicate_child')
        if node['text'] is None and not children:
            raise GraphReplyError('graph_reply_null_leaf_text_invalid')
        nodes[node['id']] = node
    root = reply['response_id']
    if root not in nodes or nodes[root]['role'] != 'response':
        raise GraphReplyError('graph_reply_response_root_invalid')
    parents = {}
    for node in nodes.values():
        for child in node['children']:
            if child not in nodes:
                raise GraphReplyError('graph_reply_unknown_child')
            if child in parents:
                raise GraphReplyError('graph_reply_multiple_parents')
            parents[child] = node['id']
        if (reply['schema'] == REPLY_SCHEMA and node['children'] and
                ''.join(nodes[c]['text'] for c in node['children']) != node['text']):
            raise GraphReplyError('graph_reply_partition_mismatch')
    if root in parents:
        raise GraphReplyError('graph_reply_root_has_parent')
    # An iterative walk handles caller-selected depth without a codec hard cap.
    pending, seen = [root], set()
    while pending:
        ident = pending.pop()
        if ident in seen:
            raise GraphReplyError('graph_reply_cycle')
        seen.add(ident)
        pending.extend(nodes[ident]['children'])
    if seen != set(nodes):
        raise GraphReplyError('graph_reply_disconnected_or_cyclic_nodes')
    if reply['schema'] == COMPOSED_REPLY_SCHEMA:
        _rendered_texts(reply)
    if not isinstance(reply['links'], list):
        raise GraphReplyError('graph_reply_links_array_required')
    existing = {entity['id'] for entity in packet['entities']}
    all_existing = {graph.record_id(name, item) for name in graph.COLLECTIONS for item in packet[name]}
    if set(nodes) & all_existing:
        raise GraphReplyError('graph_reply_local_context_identity_collision')
    seen_links = set()
    for link in reply['links']:
        _keys(link, {'from', 'predicate', 'to'}, 'graph_reply_link_shape_invalid')
        for key in ('from', 'predicate', 'to'):
            _text(link[key], 'graph_reply_link_' + key + '_required')
        if link['from'] not in nodes:
            raise GraphReplyError('graph_reply_unknown_link_source')
        if link['to'] not in existing and link['to'] not in nodes:
            raise GraphReplyError('graph_reply_unknown_context_entity')
        key = (link['from'], link['predicate'], link['to'])
        if key in seen_links:
            raise GraphReplyError('graph_reply_duplicate_link')
        seen_links.add(key)
    return reply


def _rendered_texts(reply):
    """Render validated ordered trees by iterative postorder, preserving bytes.

    V1 stores text on every node; V2 additionally permits null on composites.
    Null is representation omission, not missing content: every leaf still has
    exact nonempty source text. This function does not mutate the wire object.
    """
    nodes = {node['id']: node for node in reply['nodes']}
    if reply['schema'] == REPLY_SCHEMA:
        return {ident: node['text'] for ident, node in nodes.items()}
    pending, rendered = [(reply['response_id'], False)], {}
    while pending:
        ident, children_ready = pending.pop()
        node = nodes[ident]
        if not children_ready and node['children']:
            pending.append((ident, True))
            pending.extend((child, False) for child in reversed(node['children']))
            continue
        if node['children']:
            text = ''.join(rendered[child] for child in node['children'])
            if node['text'] is not None and node['text'] != text:
                raise GraphReplyError('graph_reply_partition_mismatch')
            rendered[ident] = text
        else:
            rendered[ident] = node['text']
    return rendered


def _spans(reply, rendered_texts=None):
    nodes = {node['id']: node for node in reply['nodes']}
    rendered = _rendered_texts(reply) if rendered_texts is None else rendered_texts
    pending = [(reply['response_id'], None, 0, 0, 0)]
    spans = {}
    while pending:
        ident, parent, ordinal, byte_start, char_start = pending.pop()
        node = nodes[ident]
        spans[ident] = {'byte_start': byte_start, 'byte_len': len(rendered[ident].encode('utf-8')),
                        'char_start': char_start, 'char_len': len(rendered[ident]),
                        'parent_id': parent, 'ordinal': ordinal}
        child_byte, child_char = byte_start, char_start
        children = []
        for index, child in enumerate(node['children']):
            children.append((child, ident, index, child_byte, child_char))
            child_byte += len(rendered[child].encode('utf-8'))
            child_char += len(rendered[child])
        pending.extend(reversed(children))
    return spans


def _entity(ident, kind, label, attrs, *, parent='', origin='system'):
    return {'id': ident, 'kind': kind, 'canonical_key': ident, 'label': label,
            'labels': {}, 'aliases': [], 'parent': parent, 'first_seen': '', 'last_seen': '',
            'evidence_class': 'derived', 'origin': origin, 'confidence': 1.0,
            'status': 'active', 'attrs': attrs}


def _locator(source, member, pointer, byte_start, byte_len):
    return {'source': source, 'member': member, 'json_pointer': pointer,
            'byte_start': byte_start, 'byte_len': byte_len,
            'time_start': None, 'time_end': None, 'line': None}


def _source(ident, unit, text, member, artifact_type, known_at, attrs):
    raw = text.encode('utf-8'); sha = hashlib.sha256(raw).hexdigest()
    return {'observation': {'id': ident, 'unit': unit, 'kind': 'utterance', 'text': text,
                            'locator': _locator('sha256:' + sha, member, '', 0, len(raw)),
                            'lang': '', 'date': known_at or '', 'ordinal': 0,
                            'artifact_type': artifact_type, 'speaker': 'assistant', 'attrs': attrs},
            'known_at': known_at, 'text_sha256': sha}


def _structural_claim(ident, subject, predicate, obj, *, request_id, source, quote):
    return {'id': ident, 'subject': subject, 'predicate': predicate, 'object': obj, 'value': None,
            'qualifiers': {'valid_from': '', 'valid_to': '', 'version': '', 'branch': '',
                           'scope': 'graph_reply_structure', 'lang': '',
                           'extra': {'request_id': request_id, 'confidence_scope': 'structure_only',
                                     'content_verification': 'unverified'}},
            'assessment': {'basis': {'support': [{'observation': source['observation']['id'],
                                                 'locator': deepcopy(source['observation']['locator']),
                                                 'quote': quote, 'extractor': COMPILER, 'quality': 1.0}],
                                    'derivation': {'operator': COMPILER, 'operator_version': 1,
                                                   'morphism': '', 'depth': 0}},
                           'evidence_class': 'derived', 'origin': 'system', 'confidence': 1.0,
                           'premises': {'claims': [], 'principles': [], 'assumptions': []},
                           'counter': {'observations': [], 'claims': []}, 'status': 'active',
                           'consequences': {'claims': [], 'predictions': [], 'checks': []},
                           'open': {'slots': [], 'questions': [], 'fill_query': None},
                           'expected_property': None, 'check_state': 'n/a', 'alternatives': []}}


def compile_reply(packet, raw, *, request_id, turn_id, model, actor='graph_reply_model',
                  recipe_sha256=None, parent_turn_id=None, known_at=None, resource_limits=None):
    """Capture → strictly validate → deterministically compile, without mutation.

    Host-supplied turn ids represent one accepted response attempt each. A retry
    uses a distinct turn id; replay/stale writes are rejected through packet CAS.
    ``parent_turn_id`` denotes an existing compiled turn ENTITY id, not a model
    string that can rewrite branch history. Semantic correction links preserve
    old entities and do not automatically supersede them.
    """
    capture = capture_response(raw)
    try:
        return _compile(packet, capture, request_id=request_id, turn_id=turn_id, model=model,
                        actor=actor, recipe_sha256=recipe_sha256, parent_turn_id=parent_turn_id,
                        known_at=known_at, resource_limits=resource_limits)
    except RecursionError:
        raise GraphReplyError('graph_reply_resource_capability_exhausted', capture) from None
    except (ValueError, UnicodeError) as exc:
        code = exc.code if isinstance(exc, GraphReplyError) else str(exc)
        raise GraphReplyError(code, capture) from None


def _compile(packet, capture, *, request_id, turn_id, model, actor, recipe_sha256,
             parent_turn_id, known_at, resource_limits=None):
    graph.validate_packet(packet, resource_limits=resource_limits)
    for value in (request_id, turn_id, model, actor):
        _text(value, 'graph_reply_host_identity_required')
    model_origin = {'kind': 'model', 'actor': actor, 'model': model,
                    'recipe_sha256': recipe_sha256, 'response_sha256': capture['sha256']}
    graph.validate_origin(model_origin)
    graph._timestamp(known_at)
    if parent_turn_id is not None:
        _text(parent_turn_id, 'graph_reply_parent_turn_identity_required')
        parent = next((e for e in packet['entities'] if e['id'] == parent_turn_id), None)
        if parent is None or parent['kind'] != 'conversation_turn':
            raise GraphReplyError('graph_reply_unknown_parent_turn')
    reply = validate_reply(_parse_capture(capture), packet, resource_limits=resource_limits)
    rendered = _rendered_texts(reply)
    spans = _spans(reply, rendered)
    seed = graph.digest({'request_id': request_id, 'turn_id': turn_id})
    turn_entity_id = 'gr_turn_' + seed
    namespace = graph.digest({'base_packet_sha256': packet['packet_id'], 'request_id': request_id,
                              'turn_id': turn_id, 'response_sha256': capture['sha256']})
    node_ids = {node['id']: 'gr_node_' + graph.digest({'namespace': namespace, 'local_id': node['id']})
                for node in reply['nodes']}
    raw_source_id, display_source_id = 'gr_raw_' + namespace, 'gr_display_' + namespace
    response_text = rendered[reply['response_id']]
    raw_text = _capture_bytes(capture).decode('utf-8')
    raw_source = _source(raw_source_id, turn_entity_id, raw_text, 'response.json',
                         'graph_reply_wire', known_at,
                         {'request_id': request_id, 'model_origin': model_origin,
                          'observation_scope': 'exact_model_output_bytes_not_world_truth'})
    display_source = _source(display_source_id, turn_entity_id, response_text, 'response.txt',
                             'graph_reply_display', known_at,
                             {'request_id': request_id, 'derived_from': raw_source_id,
                              'transform': ('json_decode_root_text' if reply['schema'] == REPLY_SCHEMA
                                            else 'compose_ordered_leaf_text'), 'model_origin': model_origin,
                              'observation_scope': 'decoded_model_output_not_world_truth'})
    compiler_origin = {'kind': 'system', 'actor': COMPILER, 'model': model,
                       'recipe_sha256': recipe_sha256, 'response_sha256': capture['sha256']}
    diff = graph.empty_diff(packet, proposal_id='gr_proposal_' + namespace,
                            origin=compiler_origin, known_at=known_at)
    diff['sources']['add'] = [raw_source, display_source]
    diff['entities']['add'].append(_entity(turn_entity_id, 'conversation_turn', turn_id,
        {'turn_id': turn_id, 'request_id': request_id, 'parent_turn_id': parent_turn_id,
         'response_entity_id': node_ids[reply['response_id']], 'raw_source_id': raw_source_id,
         'display_source_id': display_source_id, 'model_origin': model_origin,
         'confidence_scope': 'identity/structure_only', 'content_verification': 'unverified'}))
    for node in reply['nodes']:
        span = spans[node['id']]
        parent = node_ids[span['parent_id']] if span['parent_id'] is not None else turn_entity_id
        links = [{**deepcopy(link), 'resolved_from': node_ids[link['from']],
                  'resolved_to': node_ids.get(link['to'], link['to']),
                  'target_scope': 'local' if link['to'] in node_ids else 'context',
                  'status': 'draft', 'content_verification': 'unverified',
                  'origin': deepcopy(model_origin), 'context_packet_sha256': packet['packet_id']}
                 for link in reply['links'] if link['from'] == node['id']]
        attrs = {'local_id': node['id'], 'text': rendered[node['id']], 'role': node['role'],
             'children': [node_ids[child] for child in node['children']],
             'model_links': links, 'source_observation_id': display_source_id,
             'source_span': deepcopy(span), 'turn_entity_id': turn_entity_id,
             'model_origin': deepcopy(model_origin), 'content_verification': 'unverified',
             'confidence_scope': 'identity/structure_only'}
        if reply['schema'] == COMPOSED_REPLY_SCHEMA:
            attrs.update({'wire_text': node['text'], 'response_text': rendered[node['id']]})
        diff['entities']['add'].append(_entity(node_ids[node['id']], 'graph_reply_node',
            rendered[node['id']], attrs, parent=parent, origin='model_knowledge'))
    edges = [(turn_entity_id, 'has_response', node_ids[reply['response_id']])]
    edges.extend((node_ids[node['id']], 'contains_part', node_ids[child])
                 for node in reply['nodes'] for child in node['children'])
    if parent_turn_id is not None:
        edges.append((turn_entity_id, 'reply_to_turn', parent_turn_id))
    for subject, predicate, obj in edges:
        ident = 'gr_structure_' + graph.digest({'namespace': namespace, 'subject': subject,
                                               'predicate': predicate, 'object': obj})
        diff['claims']['add'].append(_structural_claim(ident, subject, predicate, obj,
            request_id=request_id, source=display_source, quote=response_text))
    # Full native packet preview catches collisions and cross-record invariants.
    graph.preview_diff(packet, diff)
    compilation = {'schema': COMPILATION_SCHEMA, 'base_packet_sha256': packet['packet_id'],
                   'raw_capture': deepcopy(capture), 'host': {'request_id': request_id, 'turn_id': turn_id,
                   'model': model, 'actor': actor, 'recipe_sha256': recipe_sha256,
                   'parent_turn_id': parent_turn_id, 'known_at': known_at},
                   'response_text': response_text, 'spans': spans, 'node_ids': node_ids,
                   'turn_entity_id': turn_entity_id, 'diff': diff,
                   'canonical_store_written': False, 'acceptance_establishes_content_truth': False}
    compilation['compilation_sha256'] = graph.digest(compilation)
    return compilation


def validate_compilation(compilation, packet):
    """Recompile from preserved bytes/host inputs; hash alone is not sufficient."""
    _keys(compilation, {'schema', 'base_packet_sha256', 'raw_capture', 'host', 'response_text',
                       'spans', 'node_ids', 'turn_entity_id', 'diff', 'canonical_store_written',
                       'acceptance_establishes_content_truth', 'compilation_sha256'},
          'graph_reply_compilation_shape_invalid')
    if compilation['schema'] != COMPILATION_SCHEMA:
        raise GraphReplyError('graph_reply_compilation_schema_invalid')
    if compilation['base_packet_sha256'] != packet['packet_id']:
        raise GraphReplyError('graph_reply_compilation_stale_base')
    if compilation['compilation_sha256'] != graph.digest({k: v for k, v in compilation.items()
                                                        if k != 'compilation_sha256'}):
        raise GraphReplyError('graph_reply_compilation_hash_drift')
    _keys(compilation['host'], {'request_id', 'turn_id', 'model', 'actor', 'recipe_sha256',
                               'parent_turn_id', 'known_at'}, 'graph_reply_host_shape_invalid')
    expected = _compile(packet, compilation['raw_capture'], **compilation['host'])
    if expected != compilation:
        raise GraphReplyError('graph_reply_compilation_replay_mismatch')
    for name in graph.COLLECTIONS:
        if compilation['diff'][name]['update'] or compilation['diff'][name]['remove']:
            raise GraphReplyError('graph_reply_append_only_required')
    return compilation


def apply_compiled_reply(packet, compilation, policy, *, explicitly_accepted=False):
    """Apply/preview verified additions using existing GraphPacket CAS/policy."""
    validate_compilation(compilation, packet)
    return graph.apply_diff(packet, compilation['diff'], policy,
                            explicitly_accepted=explicitly_accepted)
