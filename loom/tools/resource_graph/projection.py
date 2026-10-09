"""Small projection builder using existing native entity/claim/observation contracts."""
from __future__ import annotations
import hashlib
import json
from copy import deepcopy
from pathlib import Path
from loom.tools.structure.agentic_graph_v1 import packet as codec

PROFILE_PATH = Path(__file__).resolve().parents[2] / 'data/resource_graph/projection.json'
ORIGIN = {'kind': 'system', 'actor': 'resource_graph', 'model': None, 'recipe_sha256': None, 'response_sha256': None}


def profile():
    return json.loads(PROFILE_PATH.read_text())


def ident(*parts):
    return 'rg_' + codec.digest(list(parts))


def entity(key, kind, label, attrs, *, parent='', evidence='derived'):
    return {'id': key, 'kind': kind, 'canonical_key': key, 'label': label, 'labels': {}, 'aliases': [],
            'parent': parent, 'first_seen': '', 'last_seen': '', 'evidence_class': evidence,
            'origin': 'system', 'confidence': 1.0, 'status': 'active', 'attrs': deepcopy(attrs)}


def relation(subject, predicate, obj, *, version, attrs=None):
    return {'id': ident(subject, predicate, obj, version), 'subject': subject, 'predicate': predicate,
            'object': obj, 'value': None,
            'qualifiers': {'valid_from': '', 'valid_to': '', 'version': version, 'branch': '', 'scope': '', 'lang': '', 'extra': attrs or {}},
            'assessment': {'basis': {'support': [], 'derivation': {'operator': 'resource_projection', 'operator_version': 1, 'morphism': '', 'depth': 0}},
                'evidence_class': 'derived', 'origin': 'system', 'confidence': 1.0,
                'premises': {'claims': [], 'principles': [], 'assumptions': []},
                'counter': {'observations': [], 'claims': []}, 'status': 'active',
                'consequences': {'claims': [], 'predictions': [], 'checks': []},
                'open': {'slots': [], 'questions': [], 'fill_query': None},
                'expected_property': None, 'check_state': 'n/a', 'alternatives': []}}


def observation(resource, pointer, value, *, revision, ordinal=0):
    text = json.dumps(value, ensure_ascii=False, sort_keys=True, allow_nan=False)
    selector = {'source': resource['logical_id'], 'member': '!/'.join(resource['members']), 'json_pointer': pointer,
                'byte_start': None, 'byte_len': None, 'time_start': None, 'time_end': None, 'line': None}
    return {'observation': {'id': ident(resource['logical_id'], revision, pointer, 'observation'),
                'unit': resource['logical_id'], 'kind': 'field', 'text': text, 'locator': selector,
                'lang': '', 'date': '', 'ordinal': ordinal, 'artifact_type': 'resource', 'speaker': '',
                'attrs': {'serialized_value': True, 'source_version': resource.get('source_version'),
                          'content_sha256': resource.get('content_sha256')}},
            'known_at': None, 'text_sha256': hashlib.sha256(text.encode()).hexdigest()}


def make_packet(entities, claims=(), sources=(), *, task=None):
    return codec.make_packet(entities=entities, claims=claims, sources=sources, task=task,
                             origin=ORIGIN, known_at=None)
