#!/usr/bin/env python3
"""Reproduce two known W1 acceptance-storage failures; not a correctness gate.

No key, provider, or HTTP transport is configured. Authentication failure occurs
after acceptance. All generated files and native rows are retained. Set
LOOM_LIBRARY to the shared library under audit and choose an evidence directory.
"""
import argparse
import copy
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--data-parent', type=Path, required=True)
    args = parser.parse_args()
    library = Path(os.environ['LOOM_LIBRARY']).resolve()
    if args.results.exists():
        raise RuntimeError('Refusing to overwrite existing evidence: ' + str(args.results))
    args.data_parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='w1-acceptance-audit-', dir=args.data_parent))
    lib = C.CDLL(str(library))
    signatures = {
        'loom_init_ex': ([C.c_char_p, C.c_void_p], C.c_void_p),
        'loom_shutdown': ([C.c_void_p], None),
        'loom_free_string': ([C.c_void_p], None),
        'loom_set_config_json': ([C.c_void_p, C.c_char_p], C.c_int),
        'loom_create_conversation': ([C.c_void_p, C.c_char_p], C.c_void_p),
        'loom_get_messages': ([C.c_void_p, C.c_char_p], C.c_void_p),
        'loom_get_message': ([C.c_void_p, C.c_char_p], C.c_void_p),
        'loom_update_message': ([C.c_void_p, C.c_char_p, C.c_char_p], C.c_int),
        'loom_chat_ex': ([C.c_void_p, C.c_char_p, C.c_void_p, C.c_void_p], C.c_void_p),
    }
    for name, (params, result) in signatures.items():
        getattr(lib, name).argtypes = params
        getattr(lib, name).restype = result

    def encoded(value):
        return json.dumps(value, ensure_ascii=False).encode('utf-8')

    def read(pointer):
        assert pointer, 'null C ABI JSON result'
        try:
            return json.loads(C.string_at(pointer))
        finally:
            lib.loom_free_string(pointer)

    def opened(directory):
        ctx = lib.loom_init_ex(encoded({'data_dir': str(directory), 'start_workers': False}), None)
        assert ctx, 'runtime initialization failed'
        assert lib.loom_set_config_json(ctx, encoded({
            'auto_title': False, 'semantic_analysis': False, 'system_prompt': '', 'stream': False,
        })) == 0
        return ctx

    results = {
        'schema': 'w1.acceptance_boundary_audit/1',
        'measurement': 'known_failure_reproduction_not_correctness_gate',
        'library': str(library),
        'library_sha256': hashlib.sha256(library.read_bytes()).hexdigest(),
        'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'retained_synthetic_data_dir': str(root),
        'scenarios': [],
        'distinct_scenarios': 2,
        'provider_calls': 0,
        'remote_calls': 0,
        'deletions': 0,
        'accounting_basis': 'fresh directories, no key installed, every accepted send returns auth before transport',
    }
    for mutation in ['metadata_relayout', 'move_accepting_row']:
        directory = root / mutation
        ctx = opened(directory)
        try:
            conv = read(lib.loom_create_conversation(ctx, b'synthetic durable acceptance'))['id']
            base = {'conv_id': conv, 'stream': False, 'include_memory': False,
                    'include_graph_memory': False, 'trace_context': False}

            def messages():
                return read(lib.loom_get_messages(ctx, conv.encode()))

            seed = read(lib.loom_chat_ex(ctx, encoded(dict(base,
                message='Synthetic source: use report format.')), None, None))
            assert seed['error']['code'] == 'auth', seed
            assert len(messages()) == 1
            source = messages()[0]
            spec = {
                'schema': 'loom.active_task_spec/1',
                'product_ref': {'kind': 'product', 'id': 'p-a'},
                'goal_id': 'report', 'knowledge_run': None,
                'scope': {'conversation_id': conv, 'branch_id': 'native:active', 'task_id': 'report'},
                'version': 1, 'previous_product_ref': None,
                'known_at': '2026-09-30T22:00:00Z', 'representation': 'derived_product',
                'materializer': {'id': 'audit-synthetic', 'version': '1'},
                'history_event_ids': ['e'],
                'source_refs': [{'event_id': 'e', 'locator': {'source': 'synthetic'},
                                 'known_at': '2026-09-30T21:00:00Z', 'quote': source['text']}],
                'statements': [{'id': 'goal', 'kind': 'goal', 'status': 'active',
                                'text': 'Use report format.', 'source_event_ids': ['e'],
                                'claim_ids': [], 'conditions': [], 'supersedes': []}],
                'compiled_instruction': {'text': 'UNTRUSTED', 'source_map': [
                    {'span': {'byte_start': 0, 'byte_len': 9}, 'statement_ids': ['goal']}]},
            }
            bindings = {'e': {'message_id': source['id'],
                              'text_sha256': hashlib.sha256(source['text'].encode()).hexdigest()}}

            def send(product):
                return read(lib.loom_chat_ex(ctx, encoded(dict(base,
                    message='Audit current turn', active_task_spec=product,
                    active_task_bindings=bindings)), None, None))

            first = send(spec)
            assert first['error']['code'] == 'auth', first
            assert len(messages()) == 2
            accepted = next(m for m in messages() if 'active_task' in m['metadata'])
            competitor = copy.deepcopy(spec)
            competitor['product_ref']['id'] = 'p-competing-root'
            before = send(competitor)
            assert before['error']['code'] == 'invalid_argument', before
            assert len(messages()) == 2
            if mutation == 'metadata_relayout':
                patch = {'metadata': {'preserved_original_metadata': accepted['metadata'],
                                      'client_annotation': 'synthetic relayout'}}
            else:
                destination = read(lib.loom_create_conversation(ctx, b'synthetic move destination'))['id']
                patch = {'conv_id': destination}
            assert lib.loom_update_message(ctx, accepted['id'].encode(), encoded(patch)) == 0
            lib.loom_shutdown(ctx)
            ctx = None
            ctx = opened(directory)
            preserved = read(lib.loom_get_message(ctx, accepted['id'].encode()))
            retained_metadata = (preserved['metadata']['preserved_original_metadata']
                                 if mutation == 'metadata_relayout' else preserved['metadata'])
            assert retained_metadata == accepted['metadata']
            n_before = len(messages())
            after = send(competitor)
            assert after['error']['code'] == 'auth', after
            updated = messages()
            assert len(updated) == n_before + 1
            new = next(m for m in updated if m.get('metadata', {}).get('active_task', {})
                       .get('supplied_spec', {}).get('product_ref', {}).get('id') == 'p-competing-root')
            unchanged_source = read(lib.loom_get_message(ctx, source['id'].encode())) == source
            assert unchanged_source
            results['scenarios'].append({
                'scenario': mutation, 'conversation_id': conv, 'source_message_id': source['id'],
                'original_accepting_message_id': accepted['id'],
                'competing_accepting_message_id': new['id'],
                'seed_error': seed['error'], 'original_acceptance_error': first['error'],
                'before_mutation_error': before['error'], 'after_restart_error': after['error'],
                'new_competing_root_persisted': True, 'original_acceptance_preserved': True,
                'source_unchanged': unchanged_source,
                'original_scope_rows_before_second_acceptance': n_before,
                'original_scope_rows_after_second_acceptance': len(updated),
                'new_message_count': len(updated) - n_before,
            })
        finally:
            if ctx:
                lib.loom_shutdown(ctx)
    results['reproduced_scenarios'] = len(results['scenarios'])
    rendered = json.dumps(results, indent=2, ensure_ascii=False) + '\n'
    args.results.parent.mkdir(parents=True, exist_ok=True)
    with args.results.open('x', encoding='utf-8') as output:
        output.write(rendered)
    print(rendered, end='')


if __name__ == '__main__':
    main()
