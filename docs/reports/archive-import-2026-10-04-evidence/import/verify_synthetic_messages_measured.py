#!/usr/bin/env python3
"""Check an offline synthetic provider import against its fixture generator.

Requires a completed benchmark receipt with all database/source gates green.
Reads one conversation's rows at a time, verifies exact raw message/node JSON,
normalized text/roles/parent links, source pointers and wrapper sibling fields.
Does not rerun imports, mutate the database, or load the full source JSON.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sqlite3
import time


def sha256(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def generator_module():
    path = Path(__file__).with_name('generate_provider_export.py')
    spec = importlib.util.spec_from_file_location('synthetic_provider_generator', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, path


def verify(path, fixture, generator):
    conversations = fixture['conversations']
    messages = fixture['messages']
    if conversations <= 0 or messages % conversations:
        raise ValueError('fixture must have a positive constant message count per conversation')
    per_conversation = messages // conversations
    payload_site = fixture['payload_site']
    payload = 'x' * fixture['payload_bytes_per_message'] if payload_site != 'whitespace' else ''
    expected_wrapper = {'synthetic': True, 'before': {'marker': 'prefix'},
                        'after': {'marker': 'suffix', 'synthetic': True}}
    db = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
    counts = {'verified_conversations': 0, 'verified_raw_messages': 0,
              'verified_raw_nodes': 0, 'verified_normalized_messages': 0,
              'verified_parent_edges': 0, 'verified_root_messages': 0,
              'verified_message_pointers': 0, 'verified_wrapper_fields': 0,
              'normalized_text_characters': 0}
    source_indices = set()
    for conversation_id, title, metadata in db.execute('SELECT id,title,metadata FROM conversations'):
        ex = json.loads(metadata)['export']
        index = ex['source_index']
        if not isinstance(index, int) or not 0 <= index < conversations or index in source_indices:
            raise ValueError('invalid or duplicate source index')
        source_indices.add(index)
        expected = generator.conversation(index, per_conversation, payload, payload_site)
        expected_fields = {key: value for key, value in expected.items() if key != 'mapping'}
        pointer = f'/conversations/{index}' if fixture['shape'] == 'wrapper' else f'/{index}'
        container = 'conversations_wrapper' if fixture['shape'] == 'wrapper' else 'array'
        if (title != expected['title'] or ex['key'] != expected['id']
                or ex['fields'] != expected_fields or ex['current_node'] != expected['current_node']
                or ex['source_container'] != container or ex['json_pointer'] != pointer):
            raise ValueError(f'conversation fields/source pointer mismatch at {index}')
        if fixture['shape'] == 'wrapper':
            if ex.get('wrapper_fields') != expected_wrapper:
                raise ValueError(f'wrapper prefix/suffix metadata mismatch at {index}')
            counts['verified_wrapper_fields'] += 1
        seen_keys = set()
        for row in db.execute('SELECT id,parent_id,role,text,status,metadata FROM messages WHERE conv_id=?', (conversation_id,)):
            _, parent_id, role, text, status, message_metadata = row
            message_ex = json.loads(message_metadata)['export']
            key = message_ex['key']
            if key in seen_keys or key not in expected['mapping']:
                raise ValueError(f'unknown/duplicate message key at {index}')
            seen_keys.add(key)
            node = expected['mapping'][key]
            raw = node['message']
            if message_ex['raw'] != raw:
                raise ValueError(f'raw message JSON mismatch at {index}/{key}')
            if message_ex['node'] != {name: value for name, value in node.items() if name != 'message'}:
                raise ValueError(f'raw node/branch JSON mismatch at {index}/{key}')
            if (role != raw['author']['role'] or text != raw['content']['parts'][0] or status != 'active'):
                raise ValueError(f'normalized message mismatch at {index}/{key}')
            if message_ex.get('json_pointer') != pointer + f'/mapping/{key}/message':
                raise ValueError(f'message JSON pointer mismatch at {index}/{key}')
            if node['parent'] is None:
                if parent_id is not None:
                    raise ValueError(f'root message has unexpected parent at {index}/{key}')
                counts['verified_root_messages'] += 1
            else:
                parent = db.execute('SELECT conv_id,json_extract(metadata,\'$.export.key\') FROM messages WHERE id=?', (parent_id,)).fetchone()
                if parent != (conversation_id, node['parent']):
                    raise ValueError(f'normalized parent link mismatch at {index}/{key}')
                counts['verified_parent_edges'] += 1
            counts['verified_raw_messages'] += 1
            counts['verified_raw_nodes'] += 1
            counts['verified_normalized_messages'] += 1
            counts['verified_message_pointers'] += 1
            counts['normalized_text_characters'] += len(text)
        if seen_keys != set(expected['mapping']):
            raise ValueError(f'missing messages at {index}')
        counts['verified_conversations'] += 1
    db.close()
    if (source_indices != set(range(conversations)) or counts['verified_raw_messages'] != messages
            or counts['normalized_text_characters'] != fixture['normalized_text_characters']):
        raise ValueError('fixture coverage/Unicode character totals mismatch')
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('measurement', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    receipt = json.loads(args.measurement.read_text())
    if not receipt['database']['correct'] or not receipt['database']['checks'].get('source_blob'):
        parser.error('benchmark database/source preservation gates must first pass')
    generator, generator_path = generator_module()
    started = time.monotonic()
    result = {'schema': 'loom.synthetic_import_fidelity/1',
              'measurement_sha256': sha256(args.measurement),
              'fixture_source_sha256': receipt['fixture']['sha256'],
              'fixture_source_bytes': receipt['fixture']['source_bytes'],
              'fixture_payload_site': receipt['fixture']['payload_site'],
              'benchmark_binary_sha256': receipt['binary']['sha256'],
              'benchmark_source_commit': receipt['binary']['source_commit'],
              'verifier_sha256': sha256(Path(__file__)), 'generator_sha256': sha256(generator_path),
              'database': receipt['database']['path'],
              'source_byte_preservation': 'covered by independently hashed retained blob in benchmark receipt',
              'caveat': 'Synthetic generator contract only; no claim of real archive semantic accuracy. Whitespace padding does not model realistic content density.'}
    try:
        result['counts'] = verify(Path(receipt['database']['path']), receipt['fixture'], generator)
        result['correct'] = True
    except (ValueError, KeyError, TypeError, sqlite3.Error) as error:
        result.update(correct=False, error=str(error))
    result['elapsed_seconds'] = time.monotonic() - started
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['correct'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
