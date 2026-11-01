"""Sole-integrator opt-in check; NOT invoked by unittest discovery.

Uses an explicitly chosen library in a fresh scratch data directory. Upload,
materialize/read/reopen only; no model/provider action. Run through the authorized
integration runner after applying integration.patch, never as an author build.
"""
from copy import deepcopy
import base64
import ctypes
import json
from pathlib import Path
import sqlite3
import tempfile

from loom.tools.python_ast_basic import parse, source_parts_request


class Native:
    def __init__(self, library, directory):
        self.lib = ctypes.CDLL(str(library))
        self.lib.loom_init_ex.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
        self.lib.loom_init_ex.restype = ctypes.c_void_p
        self.lib.loom_shutdown.argtypes = [ctypes.c_void_p]
        self.lib.loom_free_string.argtypes = [ctypes.c_void_p]
        self.lib.loom_invoke.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
        self.lib.loom_invoke.restype = ctypes.c_void_p
        error = ctypes.c_void_p()
        options = json.dumps({'data_dir': str(directory), 'start_workers': False}).encode()
        self.context = self.lib.loom_init_ex(options, ctypes.byref(error))
        if not self.context:
            raise RuntimeError(self.take(error.value) if error.value else 'native_init_failed')

    def take(self, pointer):
        if not pointer:
            raise RuntimeError('native_null_result')
        try:
            return json.loads(ctypes.string_at(pointer).decode())
        finally:
            self.lib.loom_free_string(pointer)

    def invoke(self, action, request):
        return self.take(self.lib.loom_invoke(self.context, b'actions', action.encode(),
                         json.dumps(request, ensure_ascii=False).encode()))

    def close(self):
        if self.context:
            self.lib.loom_shutdown(self.context)
            self.context = None


def counts(directory):
    # Only this newly created synthetic data directory, never application data.
    with sqlite3.connect(f'file:{directory / "chatadhd.db"}?mode=ro', uri=True) as db:
        return [db.execute('SELECT count(*) FROM ' + table).fetchone()[0]
                for table in ('nodes', 'links', 'loom_sources', 'loom_provenance', 'loom_blobs')]


def check(library, scratch_root):
    raw = Path(__file__).with_name('fixtures').joinpath('zagniezdzenia.py').read_bytes()
    receipt = parse(raw, source_key='synthetic:python-ast-native', name='żółw.py')
    request = source_parts_request(receipt, raw)
    parent_edge = 'synthetic_python_parent'
    request['pack']['source_parts']['edges'] = {'part': parent_edge}
    with tempfile.TemporaryDirectory(prefix='python-ast-native-', dir=scratch_root) as temporary:
        directory = Path(temporary)
        native = Native(library, directory)
        try:
            upload = native.invoke('client.blob_begin', {})
            assert 'error' not in upload, upload
            appended = native.invoke('client.blob_append', {
                'upload_id': upload['upload_id'], 'data_base64': base64.b64encode(raw).decode()})
            assert 'error' not in appended, appended
            committed = native.invoke('client.blob_commit', {'upload_id': upload['upload_id'], 'mime': 'text/x-python'})
            assert committed['hash'] == receipt['original_sha256'], committed
            first = native.invoke('client.blob_text', request)
            assert 'error' not in first, first
            assert first['source_id'] == receipt['source']['id']
            assert first['source_sha256'] == receipt['original_sha256']
            assert first['coordinate'] == 'source_utf8_bytes'
            assert first['observations'] == receipt['observations']
            assert len(first['occurrence_nodes']) == len(receipt['declarations'])
            for ordinal, node in enumerate(first['occurrence_nodes']):
                read = native.invoke('client.blob_text_read', {'node_id': node})
                assert 'error' not in read, read
                assert read['text'] == receipt['observations'][ordinal]['text']
                assert read['provenance']['unit'] == receipt['units'][ordinal]
                assert read['provenance']['observation_refs'] == [receipt['observations'][ordinal]['id']]
            with sqlite3.connect(f'file:{directory / "chatadhd.db"}?mode=ro', uri=True) as db:
                for ordinal, declaration in enumerate(receipt['declarations']):
                    if declaration['parent'] is None:
                        continue
                    parent = first['occurrence_nodes'][declaration['parent']]
                    child = first['occurrence_nodes'][ordinal]
                    assert db.execute('SELECT count(*) FROM links WHERE src=? AND dst=? AND link_type=?',
                                      (child, parent, parent_edge)).fetchone()[0] == 1
            before = counts(directory)
            assert native.invoke('client.blob_text', request) == first
            assert counts(directory) == before
            # No rows or receipts are added by malformed requests, including
            # to an already cached source. Integrity precedes the cache return.
            for mutate in (lambda r: r.update(hash='0'*64),
                           lambda r: r['python_ast_receipt']['declarations'][2].update(parent=2),
                           lambda r: r['python_ast_receipt']['observations'][1].update(text='fabricated'),
                           lambda r: r['python_ast_receipt']['units'][1].update(id='un_wrong')):
                broken = deepcopy(request)
                mutate(broken)
                assert 'error' in native.invoke('client.blob_text', broken)
                assert counts(directory) == before
        finally:
            native.close()
        reopened = Native(library, directory)
        try:
            assert reopened.invoke('client.blob_text', request) == first
            for ordinal, node in enumerate(first['occurrence_nodes']):
                read = reopened.invoke('client.blob_text_read', {'node_id': node})
                assert read['text'] == receipt['observations'][ordinal]['text']
            assert counts(directory) == before
        finally:
            reopened.close()
    return {'native_executed': True, 'declarations': len(receipt['declarations']),
            'reopen': True, 'repeat_without_row_growth': True, 'parent_edges': True, 'invalid_requests_refused': 4}


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', required=True, type=Path)
    parser.add_argument('--scratch-root', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.library.resolve(), args.scratch_root.resolve()), sort_keys=True))


if __name__ == '__main__':
    main()
