"""Offline descriptor compiler. Format knowledge is data; authority is host-owned.

No network requests, imports of provider code, shell evaluation or graph writes.
Compiled receipts are plans for existing adapters, never native graph identities.
"""
from dataclasses import dataclass
from hashlib import sha256
import json
import re

SCHEMA = 'loom.unknown_structure.descriptor/1'


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('utf-8')


def digest(value):
    return sha256(value).hexdigest()


def closed(value, keys):
    if not isinstance(value, dict) or set(value) != set(keys):
        raise ValueError('unexpected_or_missing_contract_key')


def public_spec_url(format_id, catalog):
    # Caller supplies a public format ID only. Never concatenate filenames,
    # sample content, UIDs, arbitrary signatures, queries or URL parameters.
    if type(format_id) is not str or format_id not in catalog['formats']:
        raise ValueError('public_format_id_required')
    return catalog['formats'][format_id]['spec']['url']


def spec_evidence(format_id, body, catalog):
    entry = catalog['formats'].get(format_id)
    if entry is None or not isinstance(body, bytes):
        raise ValueError('public_format_id_required')
    pinned = entry['spec'].get('content_sha256')
    if pinned is not None and digest(body) != pinned:
        raise ValueError('public_spec_content_conflict')
    text = body.decode('utf-8', errors='replace').lower()
    if not all(token.lower() in text for token in entry['spec_match_tokens']):
        raise ValueError('public_spec_mismatch')
    # Token match is bibliographic evidence, not permission to execute content.
    return {'sha256': digest(body), 'match': 'bibliographic_tokens_only', 'confidence': None}


@dataclass(frozen=True)
class Compiled:
    payload: bytes
    sha256: str

    def plan(self):
        return json.loads(self.payload)


def compile_descriptor(data, catalog):
    closed(data, ('schema', 'id', 'version', 'format_id', 'spec', 'provider', 'layers', 'mapping', 'gaps'))
    if data['schema'] != SCHEMA or type(data['id']) is not str or not re.fullmatch(r'[a-z][a-z0-9_.-]{0,79}', data['id']):
        raise ValueError('descriptor_identity_invalid')
    if type(data['version']) is not int or data['version'] < 1:
        raise ValueError('descriptor_version_invalid')
    public_spec_url(data['format_id'], catalog)
    entry = catalog['formats'][data['format_id']]
    if data['spec'] != entry['spec']:
        raise ValueError('public_spec_mismatch')
    if data['provider'] not in entry['providers']:
        raise ValueError('provider_format_mismatch')
    provider = catalog['providers'].get(data['provider'])
    if not provider:
        raise ValueError('provider_unavailable')
    if data['mapping'] != provider['mapping']:
        raise ValueError('mapping_contract_mismatch')
    if (not isinstance(data['layers'], list) or not data['layers'] or
            any(type(x) is not str or x not in entry['layers'] for x in data['layers']) or
            len(set(data['layers'])) != len(data['layers'])):
        raise ValueError('structural_layer_invalid')
    if not isinstance(data['gaps'], list) or any(type(x) is not str for x in data['gaps']):
        raise ValueError('gaps_invalid')
    payload = canonical(data)
    return Compiled(payload, digest(payload))


def compare(previous, current):
    a, b = previous.plan(), current.plan()
    if previous.sha256 == current.sha256:
        return 'reuse'
    if a['id'] != b['id']:
        return 'independent'
    if b['version'] <= a['version']:
        return 'conflict'
    return 'new_version'


def resume(receipt, compiled, original, output):
    closed(receipt, ('descriptor_sha256', 'original_sha256', 'output_sha256'))
    if receipt['descriptor_sha256'] != compiled.sha256 or receipt['original_sha256'] != digest(original):
        raise ValueError('resume_input_conflict')
    if not re.fullmatch('[0-9a-f]{64}', receipt['output_sha256']):
        raise ValueError('resume_output_hash_invalid')
    if digest(output) != receipt['output_sha256']:
        raise ValueError('resume_output_hash_mismatch')
    return receipt['output_sha256']
