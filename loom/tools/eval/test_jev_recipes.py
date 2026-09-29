"""T3 preparation and protocol checks; no model responses are generated."""
from __future__ import annotations
import ast
import copy
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jev_recipes as j
C = j.read(j.FIXTURE / 'corpus.json')
R = j.read(j.FIXTURE / 'recipes.json')
G = j.read(j.FIXTURE / 'gold.json')
B, I = j.build(C, R)

class Recipes(unittest.TestCase):
    def test_corpus_counts(self):
        self.assertEqual(len(C['cases']), 36)
        self.assertEqual(sum(c['language']=='pl' for c in C['cases']), 18)
        self.assertEqual(sum(c['split']=='validation' for c in C['cases']), 18)
        self.assertEqual({f:sum(c['family']==f for c in C['cases']) for f in ('relation','context','routing')}, {'relation':16,'context':12,'routing':8})

    def test_unique_source_states(self):
        self.assertEqual(len({j.digest(j.encode(c['state'])) for c in C['cases']}),36)

    def test_prepared_counts(self):
        self.assertEqual(len(B),208)
        self.assertEqual(I['body_count'],208)
        self.assertEqual(I['max_calls_if_all_succeed_once'],184)
        self.assertEqual(len(I['routing_episodes']),24)
        self.assertEqual(sum(r['family']!='routing' for r in I['records']),112)

    def test_no_enabled_execution_or_budget(self):
        self.assertIs(I['execution_enabled'],False)
        self.assertIsNone(I['authorized_cost_usd'])
        self.assertEqual(I['new_model_calls'],0)
        self.assertTrue(all(r['status']=='prepared_not_run' for r in I['records']))

    def test_reproducible_and_nonmutating(self):
        c,r=copy.deepcopy(C),copy.deepcopy(R)
        self.assertEqual(j.build(c,r),(B,I))
        self.assertEqual(c,C);self.assertEqual(r,R)

    def test_no_gold_argument(self):
        import inspect
        self.assertEqual(list(inspect.signature(j.build).parameters),['corpus','recipes'])

    def test_no_fixture_metadata_in_body(self):
        for raw in B.values():
            state=json.loads(raw)['state']
            self.assertFalse(set(state)&{'gold','split','family','case_id','rationale','authorship','tags'})

    def test_actual_representations_present(self):
        for c in C['cases']:
            if c['family']=='context':
                for s in c['state']['candidate_subgraphs']:
                    self.assertEqual(set(s['representations']),{'label','summary','full','raw'})
                    self.assertTrue(all(isinstance(v,str) and v for v in s['representations'].values()))

    def test_every_body_valid_and_hashed(self):
        for record in I['records']:
            raw=B[record['file']];j.validate_body(json.loads(raw))
            self.assertEqual(record['sha256'],j.digest(raw))
            self.assertEqual(record['bytes'],len(raw))

    def test_variant_state_and_criteria_identical(self):
        for c in C['cases']:
            if c['family']=='routing':continue
            bodies=[json.loads(B[f"requests/{c['id']}.{arm}.json"]) for arm in R['arms']]
            self.assertTrue(all(x['state']==bodies[0]['state'] for x in bodies))
            for qid in bodies[0]['questions']:
                self.assertTrue(all(x['questions'][qid]['criteria']==bodies[0]['questions'][qid]['criteria'] for x in bodies))
                vals=list(bodies[0]['questions'][qid]['instructions'].values())
                for body,arm in zip(bodies,R['arms']):
                    ins=body['questions'][qid]['instructions']
                    self.assertEqual(ins,'\n'.join(vals)) if arm=='string' else self.assertEqual(list(ins.values()),vals)

    def test_expressed_and_inferred_are_independent(self):
        for rec in I['records']:
            if rec['family']=='relation':
                questions=json.loads(B[rec['file']])['questions']
                self.assertEqual(set(questions),{'expressed','inferred'})
                self.assertTrue(all(q['type']=='noul' for q in questions.values()))
        pairs={(x['expressed'],x['inferred']) for cid,x in G['cases'].items() if cid.startswith('r')}
        self.assertTrue({(0,0),(0,1),(1,1)}<=pairs)

    def test_subgraph_queries_are_not_exclusive(self):
        for rec in I['records']:
            if rec['family']=='context':
                qs=json.loads(B[rec['file']])['questions']
                self.assertEqual(len(qs),7)
                self.assertEqual(sum(k.startswith('relevant_') and q['type']=='noul' for k,q in qs.items()),3)
                self.assertEqual(sum(k.startswith('detail_') and q['type']=='choice' for k,q in qs.items()),3)

    def test_detail_can_report_unavailable(self):
        self.assertEqual(set(R['detail_options']),{'omit','label','summary','full','raw','unavailable'})

    def test_nested_option_counts(self):
        for cid in [c['id'] for c in C['cases'] if c['family']=='routing']:
            sets=[]
            for n in R['routing_option_counts']:
                q=json.loads(B[f'requests/{cid}.object_meaningful.flat{n}.json'])['questions']['route']
                self.assertEqual(len(q['criteria']),n);sets.append(set(q['criteria']))
            self.assertTrue(sets[0]<sets[1]<sets[2])

    def test_children_partition_flat(self):
        for e in I['routing_episodes']:
            flat=json.loads(B['requests/'+e['flat_request_id']+'.json'])['questions']['route']['criteria']
            root=json.loads(B['requests/'+e['root_request_id']+'.json'])['questions']['route']['criteria']
            seen={}
            for group,rid in e['child_request_ids'].items():
                child=json.loads(B['requests/'+rid+'.json'])['questions']['route']['criteria']
                self.assertEqual(child,root[group]);self.assertFalse(set(child)&set(seen));seen.update(child)
            self.assertEqual(seen,flat)

    def test_routing_obeys_root_even_if_wrong(self):
        e=I['routing_episodes'][0];body=json.loads(B['requests/'+e['root_request_id']+'.json'])
        keys=list(body['questions']['route']['criteria'])
        for selected in keys:
            self.assertEqual(j.next_child(e,body,{k:float(k==selected) for k in keys}),e['child_request_ids'][selected])

    def test_tie_uses_declared_order(self):
        e=I['routing_episodes'][0];b=json.loads(B['requests/'+e['root_request_id']+'.json'])
        keys=list(b['questions']['route']['criteria'])
        self.assertEqual(j.next_child(e,b,{k:.5 for k in keys}),e['child_request_ids'][keys[0]])

    def test_invalid_root_does_not_fallback(self):
        e=I['routing_episodes'][0];b=json.loads(B['requests/'+e['root_request_id']+'.json'])
        a,c=list(b['questions']['route']['criteria'])
        for probs in (None,{}, {a:1}, {a:1,c:1},{a:float('nan'),c:0},{a:True,c:False},{a:-.1,c:1.1},{a:.8,c:.2,'alien':0}):
            self.assertIsNone(j.next_child(e,b,probs))

    def test_archive_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'r.zip';p.write_bytes(j.archive_bytes(B,I));self.assertEqual(j.unpack(p),(B,I))

    def test_unexpected_zip_member(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'r.zip'
            with ZipFile(p,'w') as z:z.writestr('../unsafe','x')
            with self.assertRaises(ValueError):j.unpack(p)

    def test_unsafe_record_name(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'r.zip'
            with ZipFile(p,'w') as z:
                z.writestr('request_index.json','{}')
                z.writestr('requests.jsonl',json.dumps({'file':'requests/../unsafe.json','body':json.loads(next(iter(B.values())))}))
            with self.assertRaises(ValueError):j.unpack(p)

    def test_duplicate_record(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'r.zip';line=json.dumps({'file':'requests/a.json','body':json.loads(next(iter(B.values())))})+'\n'
            with ZipFile(p,'w') as z:z.writestr('request_index.json','{}');z.writestr('requests.jsonl',line+line)
            with self.assertRaises(ValueError):j.unpack(p)

    def test_duplicate_case_rejected(self):
        c=copy.deepcopy(C);c['cases'].append(c['cases'][0])
        with self.assertRaises(ValueError):j.build(c,R)

    def test_duplicate_instruction_keys_rejected(self):
        r=copy.deepcopy(R);r['arms']['bad']=['x','x','y']
        with self.assertRaises(ValueError):j.build(C,r)

    def test_unknown_language_rejected(self):
        c=copy.deepcopy(C);c['cases'][0]['language']='unknown'
        with self.assertRaises(ValueError):j.build(c,R)

    def test_no_network_modules(self):
        tree=ast.parse(Path(j.__file__).read_text())
        names=[]
        for n in ast.walk(tree):
            if isinstance(n,ast.Import):names.extend(x.name.split('.')[0] for x in n.names)
            elif isinstance(n,ast.ImportFrom):names.append((n.module or '').split('.')[0])
        self.assertFalse(set(names)&{'socket','requests','http','urllib','subprocess'})
        with patch('socket.socket',side_effect=AssertionError('network prohibited')):self.assertEqual(j.build(C,R),(B,I))

    def test_freeze_and_archive_verified(self):
        self.assertEqual(j.verify(),[])

    def test_gold_is_not_changed_by_build(self):
        raw=(j.FIXTURE/'gold.json').read_bytes();j.build(C,R)
        self.assertEqual((j.FIXTURE/'gold.json').read_bytes(),raw)

    def test_cli_export(self):
        with tempfile.TemporaryDirectory() as d:
            dest=Path(d)/'export';cmd=[sys.executable,j.__file__,'--export-to',str(dest)]
            run=subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertEqual(len(list((dest/'requests').glob('*.json'))),208)
            self.assertTrue(all((dest/name).read_bytes()==raw for name,raw in B.items()))
            self.assertEqual(subprocess.run(cmd,capture_output=True).returncode,2)

    def test_freeze_tampering(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            manifests=[j.read(j.FIXTURE/n) for n in ('source_freeze.json','manifest.json')]
            paths=set().union(*(m['files'].keys() for m in manifests))
            for n in ('source_freeze.json','manifest.json'):paths.add((j.FIXTURE/j.Path(n)).relative_to(j.ROOT).as_posix())
            for p in paths:
                target=root/p;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(j.ROOT/p,target)
            p=root/'loom/tests/fixtures/eval/jev_recipes_v1/gold.json';p.write_bytes(p.read_bytes()+b' ')
            self.assertTrue(any('gold.json' in x for x in j.verify(root)))

if __name__=='__main__':unittest.main()
