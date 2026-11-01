"""One actual API gate: pinned ZIP tree preview, integrity refusal, no mutations.

Only fresh synthetic data. No provider/model calls, renderer or full import matrix.
"""
import hashlib
import io
import json
import os
from pathlib import Path
import sqlite3
import zipfile
from server_flow_harness import HttpFlowCase


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def archive(entries):
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_STORED) as z:
        for name, raw in entries.items():
            item = zipfile.ZipInfo(name, date_time=(2026, 11, 1, 0, 0, 0))
            z.writestr(item, raw)
    return out.getvalue()


class ArchiveProjectionPreview(HttpFlowCase):
    def post(self, body):
        r = self.client.post(self.srv.base + '/api/client/blob/text', json=body, timeout=60)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertNotIn('error', r.json())
        return r.json()

    def snapshot(self):
        dbs = list((self.root / 'guests').glob('*/chatadhd.db'))
        self.assertEqual(len(dbs), 1)
        self.database = dbs[0]
        tables = {}
        with sqlite3.connect('file:' + str(self.database) + '?mode=ro', uri=True) as db:
            for table in ('nodes', 'links', 'loom_sources', 'loom_provenance'):
                rows = db.execute('SELECT * FROM ' + table + ' ORDER BY id').fetchall()
                tables[table] = {'count': len(rows), 'sha256': sha(json.dumps(rows, ensure_ascii=False).encode())}
        blobs = {str(p.relative_to(self.database.parent)): sha(p.read_bytes())
                 for p in (self.database.parent / 'blobs').rglob('*') if p.is_file()}
        cfg = self.client.get(self.srv.base + '/api/config', timeout=20)
        self.assertEqual(cfg.status_code, 200)
        return {'tables': tables, 'blobs': blobs, 'config_sha256': sha(cfg.content)}

    def test_archive_preview_and_refusals_without_mutation(self):
        evidence = Path(os.environ['LOOM_FLOW_EVIDENCE_DIR'])
        text = 'Ten sam żółw.\n'.encode()
        nested = archive({'głębiej/plik.txt': text})
        raw = archive({'a/same.txt': text, 'b/same.txt': text,
                       'a/nested.zip': nested, 'opaque.bin': b'\x00opaque\xff'})
        r = self.client.post(self.srv.base + '/api/client/blob/upload',
                             files={'file': ('synthetic.zip', raw, 'application/zip')}, timeout=30)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json()['hash'], sha(raw))
        made = self.post({'hash': sha(raw), 'source_key': 'synthetic:archive-preview',
                          'name': 'synthetic.zip', 'format': 'zip'})
        child = next(c for c in made['children'] if c.get('method', {}).get('format') == 'text')
        chosen = child['occurrence_nodes'][0]
        self.patch({'context_execution': {'unified': {'enabled': True, 'include_knowledge': False,
                    'legacy_seed_ids': [chosen], 'legacy_relation_hops': 0}}, 'synthetic_preserve': 'keep'})
        before = self.snapshot()
        request = {'operation': 'projection_check', 'projection_node': made['projection_node'], 'selected_nodes': []}
        preview = self.post(request)
        self.assertEqual(self.snapshot(), before, 'preview changed persisted state')
        self.assertFalse(preview['selection_changed'])
        self.assertEqual(preview['current_replay'], 'not_exercised')
        view = preview['archive_preview']
        self.assertTrue(view['manifest_verified'])
        self.assertFalse(view['reproject_supported'])
        self.assertEqual(view['root']['sha256'], sha(raw))
        self.assertEqual(view['mapping'], {'status': 'not_exercised', 'candidates': [],
                                         'reason': 'archive_tree_migration_not_implemented'})
        rows = {r['locator']['member']: r for r in view['structure_nodes']}
        for name in ('a', 'b'):
            self.assertEqual(rows[name]['parent'], view['root']['node_id'])
            self.assertEqual(rows[name + '/same.txt']['parent'], rows[name]['node_id'])
            self.assertEqual(rows[name + '/same.txt']['sha256'], sha(text))
        self.assertNotEqual(rows['a/same.txt']['source_id'], rows['b/same.txt']['source_id'])
        self.assertNotEqual(rows['a/same.txt']['node_id'], rows['b/same.txt']['node_id'])
        children = {c['locator']['member']: c for c in view['children']}
        self.assertEqual(children['opaque.bin']['current_replay'], 'not_exercised')
        self.assertEqual(children['opaque.bin']['reason'], 'format_not_admitted')
        for path in ('a/same.txt', 'b/same.txt'):
            c = children[path]
            self.assertEqual(c['member_node'], rows[path]['node_id'])
            self.assertEqual(c['original_sha256'], sha(text))
            self.assertEqual(c['review']['current_replay'], 'compatible')
            # Existing non-ZIP response shape remains compatible with the strict UI validator.
            self.assertEqual(set(c['review']), {'schema', 'old_projection', 'old_receipt_sha256',
                'saved_integrity', 'current_replay', 'refusals', 'selection_scope', 'selection_changed'})
        deep = children['a/nested.zip']['review']['archive_preview']
        self.assertEqual(deep['root']['sha256'], sha(nested))
        self.assertEqual(deep['children'][0]['original_sha256'], sha(text))
        self.assertEqual(deep['children'][0]['projection']['method']['format'], 'text')
        refusals = []
        for body, error in [({**request, 'operation': 'projection_reproject',
                              'expected_receipt_sha256': preview['old_receipt_sha256']},
                             'archive_projection_upgrade_unsupported'),
                            ({**request, 'selected_nodes': [chosen]}, 'projection_selection_not_owned'),
                            ({**request, 'archive_manifest': {}}, 'projection_override_refused')]:
            r = self.client.post(self.srv.base + '/api/client/blob/text', json=body, timeout=30)
            self.assertGreaterEqual(r.status_code, 400)
            self.assertIn(error, r.text)
            self.assertEqual(self.snapshot(), before)
            refusals.append({'status': r.status_code, 'code': error})
        self.restart()
        self.assertEqual(self.post(request), preview)
        self.assertEqual(self.snapshot(), before, 'reopen/preview changed graph or selector')
        original = self.client.get(self.srv.base + '/api/client/blob/' + sha(raw) + '/raw', timeout=20)
        self.assertEqual(original.content, raw)
        # Deliberate corruption of this NEW synthetic closed DB, not a product workaround.
        self.srv.close()
        with sqlite3.connect(self.database) as db:
            damaged = json.loads(db.execute('SELECT metadata FROM nodes WHERE id=?',
                                           (made['projection_node'],)).fetchone()[0])
            damaged['structure_nodes'][0]['parent'] = 'not-the-archive-root'
            db.execute('UPDATE nodes SET metadata=? WHERE id=?',
                       (json.dumps(damaged), made['projection_node']))
        self.restart()
        corrupt_before = self.snapshot()
        r = self.client.post(self.srv.base + '/api/client/blob/text', json=request, timeout=30)
        self.assertGreaterEqual(r.status_code, 400)
        self.assertIn('file_graph_invalid_receipt', r.text)
        self.assertEqual(self.snapshot(), corrupt_before, 'refusal repaired or mutated the invalid tree')
        self.assertEqual(self.provider.calls, [])
        (evidence / 'archive-preview.json').write_text(json.dumps(preview, ensure_ascii=False, indent=2) + '\n')
        (evidence / 'archive-preview-result.json').write_text(json.dumps({
            'status': 'PASS', 'fixture_sha256': sha(raw), 'before': before,
            'after_preview_reopen': before, 'refusals': refusals,
            'corrupt_receipt_refused_without_mutation': True, 'provider_calls': 0,
            'UI_browser': 'NOT_RUN; existing ZIP UI remains disabled',
            'ZIP_reproject': 'unsupported'}, indent=2) + '\n')
