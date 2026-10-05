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


class FirstResponseScoringTests(unittest.TestCase):
 """Only controlled synthetic envelopes; no provider output or network is read."""
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
  self.out=Path(self.temp.name)/'prepared';self.manifest=p.prepare(ROOT/'design.json',self.out)
  self.design_raw=(ROOT/'design.json').read_bytes();self.design=json.loads(self.design_raw)
  self.manifest_path=self.out/'manifest.json';self.manifest_hash=p.digest(self.manifest_path.read_bytes())
  _,_,self.sources=p._scoring_sources(ROOT/'design.json',p.digest(self.design_raw))
  self.bundle={'schema':'loom.programme_results/1','programme_id':self.manifest['programme_id'],'stage_id':self.manifest['stage_id'],
   'manifest_sha256':self.manifest_hash,'planned_operation_ids':[op['operation_id'] for op in self.manifest['operations']],
   'planned_operations':len(self.manifest['operations']),'saved_row_count':len(self.manifest['operations']),'rows':[],'responses':[],'requests':[]}
  for index,op in enumerate(self.manifest['operations']):
   q=self.sources[op['metadata']['query_id']];turn=q['visible'][0]
   value={'label':q['gold_label'],'evidence':[{'turn_id':turn['id'],'quote':turn['text']}],'explanation':'Controlled test event summary.','counterarguments':[]}
   content=json.dumps(value,ensure_ascii=False);generation='controlled-fake-'+str(index);rh=p.digest((generation+content).encode())
   meta={key:op['metadata'].get(key) for key in ('arm_id','prepared_request_id','source_manifest_sha256')}
   self.bundle['requests'].append({key:copy.deepcopy(op[key]) for key in ('operation_id','route_id','request_sha256','model_id','provider_id')})
   self.bundle['requests'][-1].update(metadata=meta,body=json.loads((self.out/op['request_file']).read_text()))
   self.bundle['rows'].append({'operation_id':op['operation_id'],'manifest_sha256':self.manifest_hash,'request_sha256':op['request_sha256'],'metadata':meta,
    'requested_model':op['model_id'],'requested_provider':op['provider_id'],'response_model':'fake-observed-model','observed_model':'fake-observed-model',
    'observed_provider':'fake-observed-provider','generation_id':generation,'response_sha256':rh,'generation_sha256':None,'http_status':200,
    'response_available':True,'response_ledger_bound':True,'exact_sent_request_capture_verified':True,'billing_verified':False,'billing_replay_verified':False,
    'actual_cost_usd':None,'state':'pending_billing'})
   self.bundle['responses'].append({'operation_id':op['operation_id'],'response_sha256':rh,'generation_sha256':None,
    'projection':{'id':generation,'model':'fake-observed-model','choices':[{'message':{'role':'assistant','content':content},'finish_reason':'stop'}]}})
 def score(self,bundle=None,design_path=None):
  bundle_raw=p.canonical(self.bundle if bundle is None else bundle)
  path=ROOT/'design.json' if design_path is None else design_path
  return p.score_first_responses(path,self.manifest_path,bundle_raw,expected_design_sha256=p.digest(path.read_bytes()),
   expected_manifest_sha256=self.manifest_hash,expected_bundle_sha256=p.digest(bundle_raw),observed_on='2026-10-05')
 def test_all_declared_queries_and_no_preference_success_inferred(self):
  result=self.score();self.assertEqual(result['planned_operations'],128);self.assertEqual(len(result['configurations']),16)
  self.assertTrue(all(c['planned_queries']==8 and c['correct_labels']==8 for c in result['configurations']))
  self.assertTrue(all(c['label_match_all_planned']==1 and c['total_cost_usd'] is None for c in result['configurations']))
  self.assertTrue(all(r['preference_adherence_manual'] is None and r['semantic_grounding_manual'] is None for r in result['records']))
  self.assertTrue(all(r['produced_by']['prompt_sha256'] and r['produced_by']['requested_model']=='openai/gpt-4.1-mini' for r in result['records']))
  self.assertTrue(all(c['observable_preference_features']['no_automatic_adherence_score'] for c in result['configurations']))
  self.assertEqual(result['new_model_calls'],0);self.assertTrue(result['first_response_only'])
 def test_missing_row_retained_and_not_wrong_observed_label(self):
  self.bundle['rows'].pop(0);self.bundle['responses'].pop(0);self.bundle['saved_row_count']-=1
  result=self.score();group=result['configurations'][0];record=result['records'][0]
  self.assertEqual(group['planned_queries'],8);self.assertEqual(group['correct_labels'],7);self.assertEqual(group['label_match_all_planned'],7/8)
  self.assertEqual(group['label_match_valid_schema'],1);self.assertEqual(group['observed_wrong_labels'],0)
  self.assertEqual(group['missing_semantic_results'],1);self.assertIsNone(record['strict_json_valid']);self.assertFalse(record['available'])
 def test_duplicate_or_unknown_first_rows_are_rejected(self):
  self.bundle['rows'].append(copy.deepcopy(self.bundle['rows'][0]));self.bundle['saved_row_count']+=1
  with self.assertRaisesRegex(p.ScoringError,'duplicate_or_unknown_inventory_identity'):self.score()
 def test_duplicate_capture_and_generation_are_rejected(self):
  for field in ('generation_id','response_sha256'):
   with self.subTest(field=field):
    b=copy.deepcopy(self.bundle);b['rows'][1][field]=b['rows'][0][field]
    if field=='generation_id':b['responses'][1]['projection']['id']=b['rows'][0][field]
    else:b['responses'][1][field]=b['rows'][0][field]
    with self.assertRaisesRegex(p.ScoringError,'duplicate'):self.score(b)
 def test_public_request_and_order_drift_are_rejected(self):
  b=copy.deepcopy(self.bundle);b['requests'][0]['body']['messages'][1]['content']='changed synthetic source'
  with self.assertRaisesRegex(p.ScoringError,'public_request_changed'):self.score(b)
  b=copy.deepcopy(self.bundle);b['planned_operation_ids'].reverse()
  with self.assertRaisesRegex(p.ScoringError,'normalized_bundle_binding_changed'):self.score(b)
 def test_gold_exact_raw_hash_is_mandatory(self):
  changed=Path(self.temp.name)/'changed-gold.json';goldpath=(ROOT/self.design['source']['gold_file']).resolve()
  changed.write_bytes(goldpath.read_bytes()+b' ')
  d=copy.deepcopy(self.design)
  for name in ('inputs_file','prepared_manifest_file'):d['source'][name]=str((ROOT/d['source'][name]).resolve())
  d['source']['gold_file']=str(changed);design_path=Path(self.temp.name)/'changed-design.json';design_path.write_bytes(p.canonical(d))
  with self.assertRaisesRegex(p.ScoringError,'artifact_hash_changed'):self.score(design_path=design_path)
 def test_strict_json_no_repair_duplicate_nonfinite_fences(self):
  valid={'label':'unknown','evidence':[],'explanation':'test','counterarguments':[]}
  invalid=['{"label":"unknown","label":"supported"}',json.dumps(valid)+' trailing',
   '```json\n'+json.dumps(valid)+'\n```','{"label":"unknown","evidence":[],"explanation":NaN,"counterarguments":[]}',
   '{"nested":1e999}']
  for content in invalid:
   with self.subTest(content=content):
    result=p.score_content(content,[],'unknown');self.assertFalse(result['strict_json_valid']);self.assertIsNone(result['gold_label_match'])
  with self.assertRaisesRegex(p.ScoringError,'duplicate_json_key'):p.strict_json('{"nested":{"x":1,"x":2}}')
 def test_exact_four_field_schema_rejects_missing_extra_and_wrong_types(self):
  valid={'label':'unknown','evidence':[],'explanation':'test','counterarguments':[]}
  bad=[{**valid,'extra':1},{k:v for k,v in valid.items() if k!='explanation'},
   {**valid,'evidence':{}},{**valid,'counterarguments':[{}]},{**valid,'label':'conflicting'},
   {**valid,'evidence':[{'turn_id':'t','quote':'','extra':1}]}]
  for value in bad:
   with self.subTest(value=value):
    result=p.score_content(json.dumps(value),[],'unknown');self.assertTrue(result['strict_json_valid']);self.assertFalse(result['schema_valid'])
 def test_contiguous_repeated_quotes_valid_and_future_or_invented_quotes_fail(self):
  turns=[{'id':'visible','text':'repeat repeat'}]
  value={'label':'supported','evidence':[{'turn_id':'visible','quote':'repeat'},{'turn_id':'future','quote':'future'},{'turn_id':'visible','quote':'invented'}],
   'explanation':'test','counterarguments':[]}
  result=p.score_content(json.dumps(value),turns,'supported')
  self.assertEqual(result['grounded_quote_count'],1);self.assertEqual(result['quote_grounding_ratio'],1/3)
  self.assertEqual(result['evidence_quotes'][0]['source_occurrences'],2);self.assertTrue(result['evidence_quotes'][0]['exact_source_quote'])
  self.assertFalse(result['evidence_quotes'][0]['unique_source_occurrence'])
 def test_unknown_empty_evidence_ratio_null_not_automatic_fail(self):
  value={'label':'unknown','evidence':[],'explanation':'No explicit commitment in the supplied view.','counterarguments':[]}
  result=p.score_content(json.dumps(value),[],'unknown');self.assertTrue(result['gold_label_match'])
  self.assertIsNone(result['quote_grounding_ratio']);self.assertIsNone(result['all_quotes_grounded'])
 def test_truncation_and_invalid_json_are_separate(self):
  value={'label':'unknown','evidence':[],'explanation':'test','counterarguments':[]}
  result=p.score_content(json.dumps(value),[],'unknown','length');self.assertTrue(result['schema_valid']);self.assertTrue(result['finish_reason_length'])
  result=p.score_content('{"label":',[],'unknown','stop');self.assertTrue(result['incomplete_json']);self.assertFalse(result['finish_reason_length'])
  result=p.score_content('```bad```',[],'unknown','length');self.assertFalse(result['incomplete_json']);self.assertTrue(result['finish_reason_length'])
 def test_transport_and_envelope_availability_independent_of_json(self):
  b=copy.deepcopy(self.bundle);b['rows'][0]['http_status']=503
  result=self.score(b)['records'][0];self.assertFalse(result['available']);self.assertTrue(result['valid_provider_envelope']);self.assertIsNone(result['strict_json_valid'])
  b=copy.deepcopy(self.bundle);b['responses'][0]['projection']['choices'][0]['message']['content']='invalid-json'
  result=self.score(b)['records'][0];self.assertTrue(result['available']);self.assertFalse(result['strict_json_valid']);self.assertFalse(result['schema_valid'])
  b=copy.deepcopy(self.bundle);b['responses'][0]['projection']['choices'][0]['message']['content']=42
  result=self.score(b)['records'][0];self.assertFalse(result['valid_provider_envelope']);self.assertFalse(result['available'])
 def test_verified_cost_crosschecked_and_unknown_cost_stays_null(self):
  row=self.bundle['rows'][0];capture=self.bundle['responses'][0];gh=p.digest(b'controlled fake billing')
  row.update(billing_verified=True,billing_replay_verified=True,is_byok=False,billing_mode='credits',generation_sha256=gh,actual_cost_usd='0.001',reported_cost_usd='0.001')
  capture['generation_sha256']=gh;capture['projection']['usage']={'cost':0.001,'is_byok':False}
  capture['generation_projection']={'id':row['generation_id'],'model':row['observed_model'],'provider_name':row['observed_provider'],'total_cost':'0.001','is_byok':False}
  result=self.score();self.assertEqual(result['records'][0]['actual_cost_usd'],'0.001');self.assertIsNone(result['records'][1]['actual_cost_usd'])
  self.assertEqual(result['configurations'][0]['known_verified_cost_usd'],'0.001');self.assertEqual(result['configurations'][0]['unknown_cost_operations'],7)
  capture['generation_projection']['total_cost']='0.002'
  with self.assertRaisesRegex(p.ScoringError,'verified_cost_changed'):self.score()
 def test_no_attempts_are_unattempted_not_unknown_charges_or_observed_quality(self):
  self.bundle['rows']=[];self.bundle['responses']=[];self.bundle['saved_row_count']=0
  result=self.score();self.assertEqual(result['first_rows'],0)
  for group in result['configurations']:
   self.assertEqual(group['planned_queries'],8);self.assertEqual(group['unattempted_operations'],8)
   self.assertEqual(group['unknown_cost_operations'],0);self.assertEqual(group['total_cost_usd'],'0')
   self.assertIsNone(group['label_match_all_planned']);self.assertEqual(group['correct_labels'],0)
  metrics=p.shared_metrics_by_configuration(result,source_id='controlled_fake_score')
  first=metrics[result['configurations'][0]['configuration_id']]
  self.assertIsNone(first['source_gold_label_match_all_planned']['value'])
  self.assertEqual(first['source_gold_label_match_all_planned']['method'],'unavailable')
  self.assertEqual(first['source_gold_label_match_all_planned']['denominator'],8)
  self.assertEqual(first['total_cost_usd']['value'],0);self.assertEqual(first['unknown_cost_operations']['value'],0)
 def test_portable_metrics_follow_model_profile_schema_and_preserve_missing_cost(self):
  import jsonschema
  result=self.score();metrics=p.shared_metrics_by_configuration(result,source_id='controlled_fake_score')
  schema=json.loads((REPO/'docs/contracts/model_profile.schema.json').read_text())['$defs']['profile']['properties']['metrics']
  for group in metrics.values():jsonschema.validate(group,schema)
  first=metrics[result['configurations'][0]['configuration_id']]
  agreement=first['source_gold_label_match_all_planned']
  self.assertEqual((agreement['value'],agreement['numerator'],agreement['denominator'],agreement['method']),(1,8,8,'counted_ratio'))
  self.assertIsNone(first['total_cost_usd']['value']);self.assertEqual(first['total_cost_usd']['method'],'unavailable')
  self.assertEqual(first['unknown_cost_operations']['value'],8)
  self.assertEqual(first['source_gold_label_match_all_planned']['evidence'],{'source_id':'controlled_fake_score','location':'/configurations/0/label_match_all_planned'})
  self.assertIsNone(first['preference_adherence_manual']['value'])


if __name__=='__main__':unittest.main(verbosity=2)
