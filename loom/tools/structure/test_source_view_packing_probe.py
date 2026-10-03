"""Authored offline mechanism counterexamples; no live/validation/gold access."""
from copy import deepcopy
from decimal import Decimal
import hashlib
from pathlib import Path
import tempfile
import unittest

try:
    from . import source_view_packing_probe as probe
except ImportError:
    import source_view_packing_probe as probe

safe = probe.safe
jev = probe.jev


def question(words='support'):
    return {'type': 'noul', 'instructions': words,
        'criteria': {'true': 'explicit source support', 'false': 'no explicit source support'}}


def snapshot():
    return {'data': {'id': jev.MODEL, 'endpoints': [{'tag': 'typesafe', 'status': 0,
        'model_id': 'typesafe/jev-1.13-20260917',
        'pricing': {'prompt': '0.000000042', 'completion': '0'}}]}}


def source_manifests(ids=('q1', 'q2', 'q3', 'q4')):
    result = {}
    for arm in probe.ARMS:
        rows = []
        for ident in ids:
            state = {'text': safe.canonical({'case_id': 'authored', 'query': {'id': ident},
                'turns': [{'id': 't1', 'text': 'authored source premise'}]}).decode()}
            questions = {'q01': question(), 'q02': question('historical refute' if arm == probe.ARMS[0] else 'active refute')}
            body = {'model': jev.MODEL, 'state': state, 'questions': questions,
                'provider': {'only': ['typesafe'], 'allow_fallbacks': False,
                    'max_price': {'prompt': '0.042', 'completion': '0'}}}
            rows.append({'id': ident, 'language': 'en', 'body': body, 'request_hash': safe.digest(body)})
        result[arm] = {'requests': rows}
    return result


def prediction(ident, probability):
    return {'query_id': ident, 'state': 'completed', 'support_noul': probability,
        'refute_noul': .1, 'label': 'unknown'}


class SelectionAndPreparationTests(unittest.TestCase):
    def setUp(self):
        self.policy = probe.read(probe.POLICY)
        self.old = [prediction('q'+str(i), .1) for i in range(1, 6)]
        self.new = [prediction('q'+str(i), p) for i,p in enumerate((.3, .2, .2, .1, .2),1)]

    def test_selection_decimal_ties_and_exact_stable_control(self):
        chosen=probe.select_queries(self.old,self.new)
        self.assertEqual([r['query_id'] for r in chosen],['q1','q2','q3','q4'])
        self.assertEqual(chosen[0]['absolute_q01_delta'],'0.2')
        self.assertEqual(chosen[-1]['selection_role'],'exact_q01_stable_control')

    def test_input_order_and_irrelevant_labels_do_not_change_selection(self):
        selected=probe.select_queries(self.old,self.new)
        changed=deepcopy(self.new)
        for row in changed:row['label']='supported';row['refute_noul']=.99
        self.assertEqual(probe.select_queries(list(reversed(self.old)),list(reversed(changed))),selected)

    def test_selection_refuses_unavailable_or_invalid_numeric_probabilities(self):
        for value in (True,float('nan'),float('inf'),-.1,1.1,None,'0.1'):
            new=deepcopy(self.new);new[0]['support_noul']=value
            with self.subTest(value=value),self.assertRaises(probe.ProbeIntegrityError):probe.select_queries(self.old,new)
        new=deepcopy(self.new);new[0]['state']='unavailable'
        with self.assertRaises(probe.ProbeIntegrityError):probe.select_queries(self.old,new)

    def test_selection_refuses_duplicate_missing_queries_and_no_control(self):
        for new in (self.new+[self.new[0]],self.new[:-1],[prediction('q'+str(i),.2) for i in range(1,6)]):
            with self.assertRaises(probe.ProbeIntegrityError):probe.select_queries(self.old,new)

    def test_only_three_changed_plus_one_control_are_selected(self):
        new=deepcopy(self.old);new[0]['support_noul']=.2;new[1]['support_noul']=.3
        with self.assertRaises(probe.ProbeIntegrityError):probe.select_queries(self.old,new)

    def test_build_retains_inner_ids_state_and_q01_in_all_24_requests(self):
        selected=probe.select_queries(self.old,self.new);original=source_manifests()
        rows,bindings=probe.build_inputs(selected,original,self.policy)
        self.assertEqual(len(rows),24);self.assertEqual(len({r['case_id'] for r in rows}),24)
        for row,b in zip(rows,bindings):
            state=safe.parse_json(row['state']['text'])
            self.assertEqual(state['query']['id'],b['original_query_id'])
            self.assertNotEqual(row['case_id'],state['query']['id'])
            self.assertEqual(row['questions']['q01'],question())
            self.assertEqual(set(row['questions']),{'q01'} if b['condition']=='C' else {'q01','q02'})
        self.assertEqual([b['measurement'] for b in bindings],[1]*12+[2]*12)
        for b in bindings[:12]:
            repeated=next(x for x in bindings[12:] if x['original_query_id']==b['original_query_id'] and x['condition']==b['condition'])
            self.assertEqual(b['expected_body_sha256'],repeated['expected_body_sha256'])

    def test_existing_runner_accepts_declared_replicates_without_any_post(self):
        rows,_=probe.build_inputs(probe.select_queries(self.old,self.new),source_manifests(),self.policy)
        calls=[]
        def stub(method,path,body,key):
            calls.append((method,path,body,key));return 200,safe.canonical(snapshot())
        req={'schema':'loom.jev_pilot_request/1','enabled':True,'experiment_id':'authored-packing-mechanism',
            'model':jev.MODEL,'budget_usd':'2','batch_cap_usd':'0.10','max_requests':24}
        with tempfile.TemporaryDirectory() as tmp:
            manifest=jev.prepare(req,rows,Path(tmp)/'prepared',transport_fn=stub)
            jev.validate_manifest(manifest)
        self.assertEqual(calls,[('GET',jev.ENDPOINT,None,None)])
        self.assertEqual(manifest['total_reservation_usd'],'0.024')
        self.assertEqual(len({r['request_hash'] for r in manifest['requests']}),12)

    def test_build_rejects_changed_q01_state_or_inner_identity(self):
        for kind in ('q01','state','inner'):
            manifests=source_manifests()
            if kind=='q01':manifests[probe.ARMS[1]]['requests'][0]['body']['questions']['q01']=question('other')
            elif kind=='state':manifests[probe.ARMS[1]]['requests'][0]['body']['state']['text']='other state'
            else:
                for arm in probe.ARMS:manifests[arm]['requests'][0]['body']['state']['text']=safe.canonical({'query':{'id':'wrong'}}).decode()
            with self.subTest(kind=kind),self.assertRaises(probe.ProbeIntegrityError):
                probe.build_inputs(probe.select_queries(self.old,self.new),manifests,self.policy)


class ReplayTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.directory=Path(self.temp.name);self.run=self.directory/'run';self.run.mkdir()
        self.snapshot=snapshot()
        req={'schema':'loom.jev_pilot_request/1','enabled':True,'experiment_id':'authored-probe-replay',
            'model':jev.MODEL,'budget_usd':'2','batch_cap_usd':'0.10','max_requests':2}
        inputs=[{'case_id':'physical_'+str(i),'language':'en','state':{'text':'authored source'},'questions':{'q01':question()}} for i in (1,2)]
        self.manifest=jev.prepare(req,inputs,self.directory/'prepared',transport_fn=lambda *args:(200,safe.canonical(self.snapshot)))
        self.ledger={'schema':'loom.jev_ledger/1','manifest_hash':safe.digest(self.manifest),'attempts':[],'reported_cost_usd':'0.00002'}
        for request in self.manifest['requests']:
            env={'id':'gen_'+request['id'],'model':'typesafe/jev-1.13-20260917','provider':'TypeSafe',
                'usage':{'cost':.00001,'input_tokens':10,'output_tokens':5,'is_byok':False},
                'answers':{'q01':{'type':'noul','noul':.3}}}
            raw=safe.canonical(env);path=request['id']+'.response.bin';(self.run/path).write_bytes(raw)
            parsed=jev.parse_response(raw,request,self.manifest['model_aliases'])
            self.ledger['attempts'].append({'id':request['id'],'request_hash':request['request_hash'],'reservation_usd':request['reservation_usd'],
                'state':'completed','http_status':200,'response_file':path,'response_sha256':hashlib.sha256(raw).hexdigest(),**parsed})
        self.persist()

    def persist(self):
        (self.run/'ledger.json').write_bytes(safe.canonical(self.ledger))

    def mutate_raw(self,change):
        a=self.ledger['attempts'][0];path=self.run/a['response_file'];v=safe.parse_json(path.read_bytes());change(v)
        raw=safe.canonical(v);path.write_bytes(raw);a['response_sha256']=hashlib.sha256(raw).hexdigest();self.persist()

    def test_gold_free_single_question_replay_and_complete_accounting(self):
        rows,summary=probe.replay_probabilities(self.manifest,self.run,self.snapshot)
        self.assertEqual([r['probabilities'] for r in rows],[{'q01':.3},{'q01':.3}])
        self.assertEqual(summary['planned'],2);self.assertEqual(summary['completed'],2)
        self.assertEqual(summary['reported_cost_usd'],'0.00002')
        self.assertEqual(summary['attempts_with_unknown_cost'],0)

    def test_raw_ledger_billing_mismatch_is_fatal_even_for_rejected_attempt(self):
        for state in ('completed','rejected'):
            self.ledger['attempts'][0]['state']=state;self.ledger['attempts'][0]['reported_cost_usd']='0.000001';self.persist()
            with self.subTest(state=state),self.assertRaises(probe.replay.BillingIntegrityError):
                probe.replay_probabilities(self.manifest,self.run,self.snapshot)

    def test_total_ledger_billing_mismatch_is_fatal(self):
        self.ledger['reported_cost_usd']='0.000001';self.persist()
        with self.assertRaises(probe.replay.BillingIntegrityError):probe.replay_probabilities(self.manifest,self.run,self.snapshot)

    def test_endpoint_hash_identity_price_and_alias_changes_are_fatal(self):
        for change in ('id','tag','price','alias'):
            snap=deepcopy(self.snapshot)
            if change=='id':snap['data']['id']='other-model'
            elif change=='tag':snap['data']['endpoints'][0]['tag']='other'
            elif change=='price':snap['data']['endpoints'][0]['pricing']['prompt']='1'
            else:snap['data']['endpoints'][0]['model_id']='typesafe/jev-1.13-20260918'
            manifest=deepcopy(self.manifest);manifest['endpoint_snapshot_hash']=safe.digest(snap)
            with self.subTest(change=change),self.assertRaises((ValueError,safe.RunnerError)):
                probe.replay_probabilities(manifest,self.run,snap)

    def test_probability_ledger_drift_is_fatal(self):
        self.ledger['attempts'][0]['probabilities']['q01']=.8;self.persist()
        with self.assertRaises(probe.ProbeIntegrityError):probe.replay_probabilities(self.manifest,self.run,self.snapshot)

    def test_raw_answer_inventory_and_provider_drift_are_fatal(self):
        self.mutate_raw(lambda v:v.update(provider='other'))
        with self.assertRaises(safe.RunnerError):probe.replay_probabilities(self.manifest,self.run,self.snapshot)

    def test_extra_question_drift_is_fatal(self):
        self.mutate_raw(lambda v:v['answers'].update(q02={'type':'noul','noul':.1}))
        with self.assertRaises(safe.RunnerError):probe.replay_probabilities(self.manifest,self.run,self.snapshot)

    def test_byte_hash_drift_is_fatal(self):
        path=self.run/self.ledger['attempts'][0]['response_file'];path.write_bytes(path.read_bytes()+b' ')
        with self.assertRaises(safe.RunnerError):probe.replay_probabilities(self.manifest,self.run,self.snapshot)

    def test_unattempted_query_preserves_planned_denominator_and_no_fabricated_answer(self):
        self.ledger['attempts']=self.ledger['attempts'][:1];self.ledger['reported_cost_usd']='0.00001';self.persist()
        rows,summary=probe.replay_probabilities(self.manifest,self.run,self.snapshot)
        self.assertEqual(summary['planned'],2);self.assertEqual(summary['completed'],1);self.assertEqual(summary['unavailable'],1)
        self.assertIsNone(rows[1]['probabilities']);self.assertEqual(rows[1]['state'],'unavailable')

    def test_completed_without_receipt_or_http200_is_fatal(self):
        self.ledger['attempts'][0]['http_status']=400;self.persist()
        with self.assertRaises(probe.ProbeIntegrityError):probe.replay_probabilities(self.manifest,self.run,self.snapshot)


class CellSummaryTests(unittest.TestCase):
    def setUp(self):
        self.bindings=[];self.rows=[]
        for condition,values in [('A',[.1,.3]),('B',[.2,.4]),('C',[.15,.35])]:
            for measurement,value in enumerate(values,1):
                ident=condition+str(measurement)
                self.bindings.append({'request_id':ident,'original_query_id':'authored_q','condition':condition,'measurement':measurement})
                self.rows.append({'request_id':ident,'state':'completed','probabilities':{'q01':value}})

    def test_decimal_mean_range_and_contrasts_descriptive_only(self):
        r=probe.summarize_cells(self.bindings,self.rows)
        self.assertEqual([c['q01_mean_available'] for c in r['cells']],['0.2','0.3','0.25'])
        self.assertEqual([c['q01_range_available'] for c in r['cells']],['0.2']*3)
        self.assertEqual(r['contrasts'][0]['B_minus_A_q01_mean'],'0.1')
        self.assertFalse(r['causal_packing_claim']);self.assertTrue(r['no_gold_accuracy_computed'])

    def test_partial_cell_keeps_missing_measurement_and_abstains_from_contrast(self):
        self.rows[1].update(state='unavailable',probabilities=None)
        r=probe.summarize_cells(self.bindings,self.rows)
        self.assertEqual(r['cells'][0]['available_measurements'],1)
        self.assertEqual(r['cells'][0]['planned_measurements'],2)
        self.assertEqual(len(r['cells'][0]['measurements']),2)
        self.assertNotIn('B_minus_A_q01_mean',r['contrasts'][0])

    def test_duplicate_rows_or_measurement_numbers_are_rejected(self):
        with self.assertRaises(probe.ProbeIntegrityError):probe.summarize_cells(self.bindings,self.rows+[self.rows[0]])
        self.bindings[1]['measurement']=1
        with self.assertRaises(probe.ProbeIntegrityError):probe.summarize_cells(self.bindings,self.rows)


class FrozenPlanReplayTests(unittest.TestCase):
    def test_24_receipt_end_to_end_frozen_plan_no_real_transport(self):
        policy=probe.read(probe.POLICY)
        selected=[{'query_id':'q'+str(i)} for i in range(1,5)]
        inputs,bindings=probe.build_inputs(selected,source_manifests(),policy)
        request={'schema':'loom.jev_pilot_request/1','enabled':True,'experiment_id':policy['experiment_id'],
            'model':jev.MODEL,'budget_usd':'2','batch_cap_usd':'0.10','max_requests':24}
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);plan=base/'plan';plan.mkdir()
            for name,value in [('policy.json',policy),('selection.json',{'request_bindings':bindings}),
                    ('inputs.json',inputs),('request.json',request),('FIRST_MECHANISM_RESULTS.json',{'authored':True})]:
                (plan/name).write_bytes(safe.canonical(value))
            (plan/'PROTOCOL.md').write_text('Authored offline mechanism only.\n')
            probe.freeze_plan(plan)
            prepared=base/'prepared'
            manifest=jev.prepare(request,inputs,prepared,transport_fn=lambda *args:(200,safe.canonical(snapshot())))
            run=base/'run';run.mkdir();attempts=[]
            for req in manifest['requests']:
                env={'id':'gen_'+req['id'],'model':'typesafe/jev-1.13-20260917','provider':'TypeSafe',
                    'usage':{'cost':.00001,'input_tokens':10,'output_tokens':5,'is_byok':False},
                    'answers':{q:{'type':'noul','noul':.3} for q in req['body']['questions']}}
                raw=safe.canonical(env);filename=req['id']+'.response.bin';(run/filename).write_bytes(raw)
                attempts.append({'id':req['id'],'request_hash':req['request_hash'],'reservation_usd':req['reservation_usd'],
                    'state':'completed','http_status':200,'response_file':filename,'response_sha256':hashlib.sha256(raw).hexdigest(),
                    **jev.parse_response(raw,req,manifest['model_aliases'])})
            (run/'ledger.json').write_bytes(safe.canonical({'schema':'loom.jev_ledger/1',
                'manifest_hash':safe.digest(manifest),'attempts':attempts,'reported_cost_usd':'0.00024'}))
            report=probe.score_plan(plan,prepared,run,base/'scored')
            self.assertEqual(report['accounting']['completed'],24)
            self.assertEqual(report['accounting']['reported_cost_usd'],'0.00024')
            self.assertEqual(len(report['cells']),12)
            self.assertTrue(all(c['complete_cell'] for c in report['cells']))
            self.assertTrue(all(c['B_minus_A_q01_mean']=='0' for c in report['contrasts']))
            (plan/'inputs.json').write_bytes(b'[]')
            with self.assertRaises(probe.ProbeIntegrityError):probe.score_plan(plan,prepared,run,base/'rejected')


if __name__=='__main__':unittest.main()
