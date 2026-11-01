"""Own packaged host-reader acceptance; synthetic bytes, explicit library/data only."""
import argparse
import base64
from copy import deepcopy
import ctypes
import hashlib
import json
from pathlib import Path
import sqlite3


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
        self.context = self.lib.loom_init_ex(json.dumps({'data_dir': str(directory), 'start_workers': False}).encode(), ctypes.byref(error))
        assert self.context, 'native_init_failed'

    def invoke(self, action, request):
        pointer = self.lib.loom_invoke(self.context, b'actions', action.encode(), json.dumps(request).encode())
        assert pointer
        try:
            return json.loads(ctypes.string_at(pointer))
        finally:
            self.lib.loom_free_string(pointer)

    def ok(self, action, request):
        result = self.invoke(action, request)
        assert 'error' not in result, result
        return result

    def upload(self, raw):
        upload = self.ok('client.blob_begin', {})['upload_id']
        self.ok('client.blob_append', {'upload_id': upload, 'data_base64': base64.b64encode(raw).decode()})
        result = self.ok('client.blob_commit', {'upload_id': upload, 'mime': 'text/plain'})
        assert result['hash'] == hashlib.sha256(raw).hexdigest()
        return result['hash']

    def close(self):
        if self.context:
            self.lib.loom_shutdown(self.context); self.context = None


def counts(root):
    with sqlite3.connect(f'file:{root / "chatadhd.db"}?mode=ro', uri=True) as db:
        return {t: db.execute('SELECT count(*) FROM '+t).fetchone()[0]
                for t in ('nodes', 'links', 'loom_sources', 'loom_provenance', 'loom_blobs')}


def run(library, root, fixture):
    root.mkdir(mode=0o700)  # never reuse a partial fixture
    raw = fixture.read_bytes()
    native = Native(library, root)
    checks = []
    try:
        request = {'hash': native.upload(raw), 'source_key': 'synthetic:host-csv', 'name': 'żółć.csv', 'format': 'csv'}
        receipt = native.ok('client.blob_text', request)
        assert receipt['source_sha256'] == hashlib.sha256(raw).hexdigest()
        assert receipt['coordinate'] == 'source_utf8_bytes'
        assert receipt['warnings'][0]['code'] == 'csv_ragged_rows'
        assert len(receipt['occurrence_nodes']) == 16  # five rows, eleven cells
        reads = [native.ok('client.blob_text_read', {'node_id': n}) for n in receipt['occurrence_nodes']]
        row2 = next(r for r in reads if r['provenance']['unit']['kind'] == 'csv_row' and r['provenance']['unit']['attrs']['row'] == 2)
        cell23 = next(r for r in reads if r['provenance']['unit']['kind'] == 'csv_cell' and
            r['provenance']['unit']['attrs']['csv_locator']['rows'] == [2,2] and
            r['provenance']['unit']['attrs']['csv_locator']['columns'] == [3,3])
        assert cell23['text'] == '"pierwsza\r\ndruga; część"'
        assert cell23['provenance']['unit']['attrs']['physical_lines'] == [2,3]
        assert cell23['provenance']['unit']['attrs']['decoded']['value'] == 'pierwsza\r\ndruga; część'
        assert cell23['provenance']['unit']['attrs']['decoded']['evidence_class'] == 'derived'
        for read in reads:
            start,end = read['provenance']['byte_range']
            assert read['text'].encode() == raw[start:end]
        checks.append('csv_rows_cells_raw_BOM_CRLF_and_derived_values')
        selector = cell23['provenance']['unit']['attrs']['csv_locator']
        selection = native.ok('client.blob_text_read', {'node_id': row2['node_id'], 'csv_selection': selector})
        assert selection['cells'][0]['text'] == cell23['text']
        rectangular = dict(selector, kind='range', rows=[2,5], columns=[2,3])
        selection = native.ok('client.blob_text_read', {'node_id': row2['node_id'], 'csv_selection': rectangular})
        assert selection['missing'] == [{'row':4,'column':2},{'row':4,'column':3},{'row':5,'column':3}]
        for broken in [dict(selector, rows=[4,4]), dict(selector, method_sha256='0'*64), dict(selector, original_sha256='0'*64), dict(selector, columns=[3,999])]:
            assert 'error' in native.invoke('client.blob_text_read', {'node_id': row2['node_id'], 'csv_selection': broken})
        checks.append('csv_selection_replayed_missing_and_hash_bounds_refusal')
        before = counts(root)
        assert native.ok('client.blob_text', request) == receipt
        assert counts(root) == before
        for field in ('csv_receipt','python_ast_receipt','profile','reader','pack','script','python','options'):
            broken = dict(request);broken[field] = {'approved':True}
            assert native.invoke('client.blob_text', broken)['error']['message'] == 'host_reader_request_override_refused'
            assert counts(root) == before
        assert 'error' in native.invoke('client.blob_text', dict(request, hash='0'*64))
        assert counts(root) == before
        checks.append('idempotence_and_forged_client_receipt_config_refused_before_mutation')
        malformed = dict(request, hash=native.upload(b'"unterminated'), source_key='synthetic:bad')
        before = counts(root)
        assert 'error' in native.invoke('client.blob_text', malformed)
        assert counts(root) == before
        checks.append('malformed_csv_no_partial_graph')
        # Two identical rows, separate occurrences (and row vs sole-cell spans).
        identical = dict(request, hash=native.upload(b'x\r\nx\r\n'), source_key='synthetic:duplicates')
        dup = native.ok('client.blob_text', identical)
        assert len(set(dup['occurrence_nodes'])) == 4
        checks.append('identical_text_and_row_cell_occurrences_distinct')
        code = 'raise RuntimeError("never executed")\r\nclass Żółw:\r\n    def go(self): return "gęś"\r\n'.encode()
        pyrequest = {'hash':native.upload(code),'source_key':'synthetic:host-python','name':'żółw.py','format':'python_ast'}
        ast = native.ok('client.blob_text', pyrequest)
        assert len(ast['occurrence_nodes']) == 3
        ast_reads = [native.ok('client.blob_text_read', {'node_id': n}) for n in ast['occurrence_nodes']]
        assert ast_reads[2]['text'] == 'def go(self): return "gęś"'
        assert ast_reads[2]['provenance']['structural_evidence_class'] == 'derived'
        checks.append('python_host_generation_no_source_execution_and_native_parts')
        # Test corruption only in this newly created synthetic blob store.
        saved_hash = receipt['host_reader']['receipt_blob']
        matches = [p for p in root.rglob('*') if p.is_file() and p.name == saved_hash]
        if not matches: # content-address store can split the first two hash digits
            matches = [p for p in root.rglob('*') if p.is_file() and p.name == saved_hash[2:] and p.parent.name == saved_hash[:2]]
        assert len(matches)==1, 'receipt_blob_path_missing'
        path = matches[0]; original = path.read_bytes()
        def replace_blob(value):
            temporary = path.with_name(path.name+'.synthetic-fault')
            temporary.write_bytes(value); temporary.replace(path)
        try:
            replace_blob(original+b' ')
            assert 'error' in native.invoke('client.blob_text_read', {'node_id':cell23['node_id']})
            assert 'error' in native.invoke('client.blob_text', request)
        finally:replace_blob(original)
        checks.append('persisted_receipt_hash_corruption_refused')
        with sqlite3.connect(root/'chatadhd.db') as db:
            original_meta = db.execute('SELECT metadata FROM nodes WHERE id=?', (cell23['node_id'],)).fetchone()[0]
            changed = json.loads(original_meta)
            del changed['method']['host_reader']
            # Even recomputing the method digest cannot turn a hosted occurrence
            # into an unchecked generic text occurrence under its existing ID.
            changed['method']['format'] = 'text'
            canonical = json.dumps(changed['method'], ensure_ascii=False, sort_keys=True, separators=(',',':'))
            changed['method_sha256'] = hashlib.sha256(canonical.encode()).hexdigest()
            db.execute('UPDATE nodes SET metadata=? WHERE id=?', (json.dumps(changed), cell23['node_id']));db.commit()
            try:
                assert 'error' in native.invoke('client.blob_text_read', {'node_id':cell23['node_id']})
            finally:
                db.execute('UPDATE nodes SET metadata=? WHERE id=?', (original_meta,cell23['node_id']));db.commit()
        checks.append('erased_host_binding_cannot_downgrade_existing_occurrence')
    finally:
        native.close()
    reopened = Native(library, root)
    try:
        assert reopened.ok('client.blob_text', request) == receipt
        assert reopened.ok('client.blob_text_read', {'node_id':cell23['node_id']}) == cell23
        assert reopened.ok('client.blob_text_read', {'node_id':ast_reads[2]['node_id']}) == ast_reads[2]
        checks.append('native_close_reopen_csv_and_python')
    finally:reopened.close()
    return {'passed_groups':checks,'count':len(checks),'runtime_pass':True,'ui_pass':False,
            'original_sha256':hashlib.sha256(raw).hexdigest(),'csv_receipt':receipt,'selected_cell':cell23['node_id']}


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--fixture',type=Path,required=True);a=p.parse_args()
    print(json.dumps(run(a.library.resolve(),a.data.resolve(),a.fixture.resolve()),ensure_ascii=False,indent=2))
