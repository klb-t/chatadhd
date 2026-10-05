import collections,copy,hashlib,importlib.util,itertools,json,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parent
REPO=next(x for x in ROOT.parents if (x/'loom/tools/structure/research_programme_manifest.py').exists())
sys.path.insert(0,str(REPO/'loom/tools/structure'))
import research_programme_manifest as manifests
spec=importlib.util.spec_from_file_location('express_prepare',ROOT/'prepare.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
class PreparationTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.out=Path(self.temp.name)/'prepared';self.manifest=p.prepare(ROOT/'design.json',self.out);self.design=json.loads((ROOT/'design.json').read_text())
 def test_exact_full_crossing_without_duplicate_operations(self):
  ops=self.manifest['operations'];self.assertEqual(len(ops),128);self.assertEqual(len({x['operation_id'] for x in ops}),128)
  cw=json.loads((self.out/'crosswalk.json').read_text());actual=collections.Counter((x['strategy_id'],x['temperature'],x['max_tokens'],x['preference_id'],x['query_id']) for x in cw)
  expected=collections.Counter(itertools.product([x['id'] for x in self.design['prompt_strategies']],self.design['temperatures'],self.design['max_tokens'],[x['id'] for x in self.design['user_preferences']],self.design['selection']['query_ids']))
  self.assertEqual(actual,expected)
 def test_exact_source_text_same_for_every_configuration_and_no_gold_rationale(self):
  mpath=(ROOT/self.design['source']['prepared_manifest_file']).resolve();m=json.loads(mpath.read_text());expected={}
  for op in m['operations']:
   if op['metadata']['arm_id']=='j_active':expected[op['metadata']['context_preparation']['case_id']]=json.loads((mpath.parent/op['request_file']).read_text())['state']['text']
  gold=json.loads((ROOT/self.design['source']['gold_file']).read_text());rationales=[j['rationale'] for c in gold['cases'] for j in c['judgments']]
  for op in self.manifest['operations']:
   b=json.loads((self.out/op['request_file']).read_text());self.assertEqual(b['messages'][1]['content'],expected[op['metadata']['query_id']]);joined=' '.join(x['content'] for x in b['messages'])
   self.assertTrue(all(r not in joined for r in rationales));self.assertNotIn(self.design['source']['gold_sha256'],joined)
 def test_pinned_loader_accepts_and_bounds_cover_actual_body(self):
  loaded=manifests.load_operations(self.out/'manifest.json');self.assertEqual(len(loaded),128)
  for op in loaded:
   self.assertGreaterEqual(op['units_upper_bounds']['prompt'],len(op['request_bytes']));self.assertEqual(op['units_upper_bounds']['completion'],op['request_body']['max_tokens']);self.assertEqual(op['request_body']['provider'],{'only':['openai'],'allow_fallbacks':False})
 def test_corrupted_body_rejected_by_actual_runner_loader(self):
  op=self.manifest['operations'][0];f=self.out/op['request_file'];f.write_bytes(f.read_bytes()+b' ')
  with self.assertRaises(manifests.ManifestError):manifests.load_operations(self.out/'manifest.json')
 def test_changed_source_raw_hash_rejected_before_materialization(self):
  with self.assertRaisesRegex(ValueError,'source_hash_mismatch'):p.read_bound(ROOT/'design.json','0'*64)
 def test_replay_exact_and_collision_rejected(self):
  before={str(x.relative_to(self.out)):x.read_bytes() for x in self.out.rglob('*') if x.is_file()};p.prepare(ROOT/'design.json',self.out)
  after={str(x.relative_to(self.out)):x.read_bytes() for x in self.out.rglob('*') if x.is_file()};self.assertEqual(before,after)
  target=self.out/'manifest.json';target.write_bytes(b'{}')
  with self.assertRaisesRegex(ValueError,'immutable_artifact_collision'):p.prepare(ROOT/'design.json',self.out)
 def test_parameter_and_prompt_hash_bindings(self):
  cs={x['configuration_id']:x for x in json.loads((self.out/'configurations.json').read_text())}
  for op in self.manifest['operations']:
   c=cs[op['metadata']['configuration_id']];body=json.loads((self.out/op['request_file']).read_text());self.assertEqual(body['temperature'],c['temperature']);self.assertEqual(body['max_tokens'],c['max_tokens']);self.assertEqual(op['metadata']['prompt_sha256'],p.digest(body['messages'][0]['content'].encode()))
if __name__=='__main__':unittest.main(verbosity=2)
