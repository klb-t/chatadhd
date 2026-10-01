"""Offline mechanism tests; synthetic transport outcomes are not model-quality data."""
from copy import deepcopy
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from loom.tools.structure.frontier_panel_v1 import adapter as a, bounded_runner as client
from loom.tools.structure import graph_panel_live as panel, openrouter_runner as safe
from loom.tools.structure import graph_panel_score_run as integrity


class FrontierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.presets, cls.configs, cls.identities = a.load_presets()
        cls.recipes = a.load_recipes(); cls.cases = panel.load_dev_inputs()

    def one_manifest(self, *, track='supplied_edge_judgment', model='gpt6_luna', recipe='baseline', execution=None, config=None, n=1):
        cfg = deepcopy(config or self.configs[model])
        rows = a.dev_rows(self.cases[:1], track, recipe, cfg, self.identities[model], self.recipes)[:n]
        ex = deepcopy(execution or self.presets['default_execution'])
        return a.manifest_for(rows, cfg, model, track, recipe, 'all24', ex, 1)

    @staticmethod
    def key_raw(limit='2'):
        return safe.canonical({'data': {'limit':limit, 'limit_remaining':limit,
            'limit_reset':None, 'is_management_key':False, 'include_byok_in_limit':False, 'byok_usage':'0'}})

    def response(self, manifest, index=0, *, cost='.00001', is_byok=False, finish='stop', content=None, model=None, provider=None):
        m=manifest['metadata']; identity=self.identities[m['model_key']]; row=manifest['requests'][index]
        if content is None: content={'query_id':row['id'],'label':'unknown'}
        return safe.canonical({'model':model or identity['model_aliases'][-1],
            'provider':provider or identity['provider_aliases'][-1],
            'choices':[{'finish_reason':finish,'message':{'content':content if isinstance(content,str) else safe.canonical(content).decode()}}],
            'usage':{'cost':cost,'is_byok':is_byok,'prompt_tokens':10,'completion_tokens':10,
                     'completion_tokens_details':{'reasoning_tokens':4}}})

    def scripted_run(self, manifest, directory, *, response=None, http_status=200, key_limit='2'):
        posts=[]
        def transport(method,path,body,key):
            self.assertEqual(key,'offline-test-credential')
            if method=='GET': return 200,self.key_raw(key_limit)
            posts.append(body)
            return http_status, response if response is not None else self.response(manifest, len(posts)-1)
        # The public price snapshot is immutable research evidence. A mocked
        # transport test runs at that snapshot's time; wall-clock aging must
        # still block real requests through the unchanged production runner.
        snapshot_time = safe._timestamp(manifest['pricing_evidence'][0]['retrieved_at'])
        with patch.object(safe.time, 'time', return_value=snapshot_time):
            ledger=client.run_manifest(manifest,directory,transport_fn=transport,key_loader=lambda:'offline-test-credential')
        return ledger,posts

    def test_snapshot_freshness_guard_boundaries_before_transport(self):
        manifest = self.one_manifest()
        snapshot_time = safe._timestamp(manifest['pricing_evidence'][0]['retrieved_at'])
        for age, allowed in ((-301, False), (-300, True), (0, True), (86400, True), (86401, False)):
            with self.subTest(age=age), tempfile.TemporaryDirectory() as directory:
                calls = []
                def transport(method, path, body, key):
                    calls.append(method)
                    return (200, self.key_raw() if method == 'GET' else self.response(manifest))
                with patch.object(safe.time, 'time', return_value=snapshot_time + age):
                    if allowed:
                        result = client.run_manifest(manifest, directory, transport_fn=transport,
                                                     key_loader=lambda: 'offline-test-credential')
                        self.assertEqual(result['attempts'][0]['state'], 'completed')
                        self.assertEqual(calls, ['GET', 'POST'])
                    else:
                        with self.assertRaisesRegex(safe.RunnerError, 'pricing_evidence_stale'):
                            client.run_manifest(manifest, directory, transport_fn=transport,
                                                key_loader=lambda: 'offline-test-credential')
                        self.assertEqual(calls, [])

    def test_public_snapshots_have_exact_provider_and_date_aliases(self):
        self.assertEqual(len(self.configs),5)
        for key,identity in self.identities.items():
            self.assertIn(self.configs[key]['model'],identity['model_aliases'])
            self.assertEqual(len(identity['model_aliases']),2)
            self.assertEqual(identity['endpoint']['status'],0)
            self.assertTrue(identity['catalog_model'].get('reasoning'))

    def test_mandatory_reasoning_is_model_conditioned(self):
        for key in ('gpt61_sol','sonnet55','gemini31_pro','gemini38_flash'):
            config=deepcopy(self.configs[key]);config['reasoning']={'enabled':False}
            with self.assertRaisesRegex(ValueError,'requires_reasoning'):
                a.validate_reasoning(config,self.identities[key])
        config=deepcopy(self.configs['gpt6_luna']);config['reasoning']={'enabled':False}
        a.validate_reasoning(config,self.identities['gpt6_luna'])

    def test_high_reasoning_is_configurable(self):
        config=deepcopy(self.configs['gpt61_sol']);config['reasoning']={'effort':'high','exclude':True}
        body=a.body_for({},'assisted_extraction','baseline',config,self.identities['gpt61_sol'],self.recipes)
        self.assertEqual(body['reasoning'],config['reasoning'])

    def test_unobserved_reasoning_effort_rejected(self):
        config=deepcopy(self.configs['gemini31_pro']);config['reasoning']={'effort':'max'}
        with self.assertRaisesRegex(ValueError,'unobserved_reasoning_effort'):
            a.validate_reasoning(config,self.identities['gemini31_pro'])

    def test_no_fake_precise_gemini_reasoning_cap(self):
        config=deepcopy(self.configs['gemini31_pro']);config['reasoning']={'max_tokens':200}
        with self.assertRaisesRegex(ValueError,'does_not_advertise_precise'):
            a.validate_reasoning(config,self.identities['gemini31_pro'])

    def test_every_declared_recipe_matches_own_hash(self):
        self.assertEqual(len(self.recipes['tracks']['graph_packet']),8)
        for track,recipes in self.recipes['tracks'].items():
            for value in recipes.values():
                self.assertEqual(value['system_sha256'],hashlib.sha256(value['system'].encode()).hexdigest())
                self.assertIn('JSON',value['system'])

    def test_free_payload_is_source_only(self):
        rows=a.dev_rows(self.cases[:1],'free_source_extraction','baseline',self.configs['gpt6_luna'],self.identities['gpt6_luna'],self.recipes)
        payload=json.loads(rows[0]['body']['messages'][1]['content'])
        self.assertEqual(set(payload),{'id','source_id','turns'})
        self.assertNotIn('node_inventory',payload)
        self.assertNotIn('judgment_queries',payload)

    def test_judge_physically_excludes_future_turns(self):
        rows=a.dev_rows(self.cases[:1],'supplied_edge_judgment','baseline',self.configs['gpt6_luna'],self.identities['gpt6_luna'],self.recipes)
        for row in rows:
            payload=json.loads(row['body']['messages'][1]['content'])
            self.assertTrue(all(panel._time(t['known_at'])<=panel._time(payload['query']['as_of']) for t in payload['turns']))

    def test_selection_balance_and_complete_denominators(self):
        selected=a.selected_cases(self.presets,'balanced8')
        self.assertEqual(len(selected),8)
        self.assertEqual(sum(c['language']=='en' for c in selected),4)
        self.assertEqual(sum(c['language']=='pl' for c in selected),4)
        selected=a.selected_cases(self.presets,'balanced12')
        self.assertEqual(sum(c['language']=='en' for c in selected),6)

    def test_prepare_does_not_read_gold_or_validation(self):
        with tempfile.TemporaryDirectory() as d, patch.object(panel,'load_dev_gold',side_effect=AssertionError('gold_read')):
            out=a.prepare_dev(Path(d)/'prepared',model_key='gpt6_luna',track='free_source_extraction',selection='screen6')
            self.assertEqual(out['planned_requests'],6);self.assertEqual(out['paid_calls'],0)
            self.assertEqual([r for b in out['batches'] for r in b['request_ids']],out['case_ids'])
            self.assertTrue(all(Decimal(b['reservation_usd'])<=Decimal('.1') for b in out['batches']))

    def test_plan_keeps_actual_wire_hash_reasoning_and_reservation(self):
        manifest=self.one_manifest(model='gpt61_sol')
        plan=a.plan_manifest(manifest)
        self.assertEqual(plan['requests'][0]['request_hash'],safe.digest(manifest['requests'][0]['body']))
        self.assertEqual(manifest['requests'][0]['body']['reasoning'],{'effort':'low','exclude':False})
        self.assertGreater(Decimal(plan['requests'][0]['reservation_usd']),Decimal('0'))

    def test_original_runner_still_rejects_enabled_reasoning(self):
        manifest=self.one_manifest(model='gpt61_sol')
        with self.assertRaisesRegex(safe.RunnerError,'reasoning_must_be_explicitly_disabled'):
            safe.plan_manifest(manifest)
        a.plan_manifest(manifest)

    def test_provider_snapshot_and_manifest_alias_claims_cannot_be_forged(self):
        config=deepcopy(self.configs['gpt61_sol']);config['endpoint_sha256']='0'*64
        with self.assertRaises(a.IdentityIntegrityError): a.public_identity(config)
        manifest=self.one_manifest();manifest['metadata']['public_model_aliases'].append('openai/gpt-6-luna-20990101')
        with self.assertRaisesRegex(a.IdentityIntegrityError,'public_identity_or_pricing'):
            a.plan_manifest(manifest)

    def test_underreservation_rejected(self):
        manifest=self.one_manifest();manifest['requests'][0]['reservation_usd']='0'
        with self.assertRaisesRegex(ValueError,'below_full_allowance'): a.plan_manifest(manifest)

    def test_batch_cap_is_configurable_not_global_reset(self):
        manifest=self.one_manifest();manifest['metadata']['execution']['batch_reservation_cap_usd']='0.0000001'
        with self.assertRaisesRegex(ValueError,'reservations_exceeded'): a.plan_manifest(manifest)
        manifest['metadata']['execution']['batch_reservation_cap_usd']='1'
        a.plan_manifest(manifest)
        self.assertEqual(manifest['budget_usd'],'2')

    def test_billion_dollar_budget_configuration_and_key_gate(self):
        ex=deepcopy(self.presets['default_execution']);ex['budget_usd']='1000000000'
        manifest=self.one_manifest(execution=ex)
        self.assertEqual(a.plan_manifest(manifest)['budget_usd'],'1000000000')
        with tempfile.TemporaryDirectory() as d:
            ledger,posts=self.scripted_run(manifest,Path(d),key_limit='1000000000')
            self.assertEqual(len(posts),1)
            self.assertEqual(ledger['key_check']['limit_usd'],'1000000000')

    def test_accounting_rejects_negative_nan_or_infinite_money(self):
        for value in (-1,'NaN','Infinity','-Infinity',True):
            with self.assertRaises(safe.RunnerError): a.money(value)

    def test_scripted_transport_preserves_exact_reasoning_body_and_first_response(self):
        manifest=self.one_manifest(model='gpt61_sol')
        before_sha=a.sha(safe.__file__);before_plan=safe.plan_manifest
        with tempfile.TemporaryDirectory() as d:
            ledger,posts=self.scripted_run(manifest,Path(d))
            self.assertEqual(posts,[safe.canonical(manifest['requests'][0]['body'])])
            self.assertEqual(ledger['attempts'][0]['reported_cost_usd'],'0.00001')
            first=(Path(d)/ledger['attempts'][0]['response_file']).read_bytes()
            self.assertEqual(hashlib.sha256(first).hexdigest(),ledger['attempts'][0]['response_sha256'])
            untouched=lambda *args: (_ for _ in ()).throw(AssertionError('terminal_attempt_reissued'))
            resumed=client.run_manifest(manifest,d,transport_fn=untouched,key_loader=untouched)
            self.assertEqual(resumed['attempts'],ledger['attempts'])
        self.assertEqual(a.sha(safe.__file__),before_sha);self.assertIs(safe.plan_manifest,before_plan)

    def test_missing_cost_stops_without_reissuing_http_error(self):
        manifest=self.one_manifest(n=2)
        with tempfile.TemporaryDirectory() as d:
            ledger,posts=self.scripted_run(manifest,Path(d),response=safe.canonical({'error':{'code':400}}),http_status=400)
            self.assertEqual(len(posts),1)
            self.assertEqual(ledger['stopped_reason'],'byok_billing_unknown_stop')
            self.assertEqual(ledger['attempts'][0]['state'],'http_error')
            resumed=client.run_manifest(manifest,d,transport_fn=lambda *args: self.fail('retry'),key_loader=lambda:self.fail('key_retry'))
            self.assertEqual(len(resumed['attempts']),1)

    def test_byok_tripwire_and_cost_overrun_stop(self):
        manifest=self.one_manifest(n=2)
        for raw,reason in [(self.response(manifest,is_byok=True),'uncapped_byok_detected_stop'),
                           (self.response(manifest,cost='1'),'reported_cost_exceeded_reservation')]:
            with tempfile.TemporaryDirectory() as d:
                ledger,posts=self.scripted_run(manifest,Path(d),response=raw)
                self.assertEqual(len(posts),1);self.assertEqual(ledger['stopped_reason'],reason)

    def test_raw_ledger_cost_mismatch_is_hard_error(self):
        manifest=self.one_manifest();attempt={'reported_cost_usd':'.2'}
        with self.assertRaises(integrity.BillingIntegrityError):
            a.response_content(self.response(manifest),attempt,self.identities['gpt6_luna'])

    def test_wrong_response_model_or_provider_is_hard_error(self):
        manifest=self.one_manifest();attempt={'reported_cost_usd':'.00001'}
        for raw in (self.response(manifest,model='openai/gpt-6-luna-20990101'),self.response(manifest,provider='Unknown')):
            with self.assertRaises(a.IdentityIntegrityError): a.response_content(raw,attempt,self.identities['gpt6_luna'])

    def test_replay_missing_results_retains_all_planned_queries(self):
        manifest=self.one_manifest(n=4)
        with tempfile.TemporaryDirectory() as d:
            directory=Path(d);manifest_path=directory/'prepared.json';panel.write_new(manifest_path,manifest)
            ledger,posts=self.scripted_run(manifest,directory/'run',response=safe.canonical({'error':{'code':400}}),http_status=400)
            compiled,summary,cases=a.replay(manifest_path,directory/'run')
            self.assertEqual(len(compiled),4);self.assertEqual(summary['attempted_requests'],1)
            self.assertEqual(summary['missing_cost_attempts'],1);self.assertEqual(summary['compiled_complete'],0)
            self.assertTrue(all(r['state']=='unavailable' for r in compiled))

    def test_rich_payload_is_not_reduced_by_generic_body_builder(self):
        payload={'schema':'loom.graph_packet/1','claims':[{'qualifiers':{'x':'y'},'alternatives':['a'],'open_questions':['q']}],
                 'sources':[{'observation':{'text':'source'},'known_at':'2026-01-01T00:00:00Z'}], 'history':[{'old':'retained'}]}
        body=a.body_for(payload,'graph_packet','critique_graph',self.configs['gpt61_sol'],self.identities['gpt61_sol'],self.recipes)
        self.assertEqual(safe.parse_json(body['messages'][1]['content']),payload)

    def test_resource_limits_bound_private_transport_scope_only(self):
        manifest=self.one_manifest();manifest['metadata']['execution']['limits']['timeout_seconds']=120
        manifest['metadata']['execution']['limits']['max_response_bytes']=12345678
        scope=client.runner_scope(manifest)
        self.assertEqual(scope['TIMEOUT_SECONDS'],120);self.assertEqual(scope['MAX_RESPONSE_BYTES'],12345678)
        self.assertEqual(safe.TIMEOUT_SECONDS,60);self.assertEqual(safe.MAX_RESPONSE_BYTES,2097152)

    def test_packet_response_preview_binds_instrument_time_not_source_time(self):
        from loom.tools.structure.agentic_graph_v1 import packet as codec
        origin={'kind':'user','actor':'owner','model':None,'recipe_sha256':None,'response_sha256':None}
        packet=codec.make_packet(task={'scope':'whole_archive'},origin=origin,known_at='2026-01-01T00:00:00Z')
        body=a.packet_body(packet,model_key='gpt61_sol',recipe='propose_definitions')
        self.assertEqual(safe.parse_json(body['messages'][1]['content']),packet)
        model_origin={'kind':'model','actor':'untrusted_model','model':'claimed-model','recipe_sha256':None,'response_sha256':None}
        diff=codec.empty_diff(packet,proposal_id='p1',origin=model_origin,known_at=None)
        diff['definitions']['add']=[{'id':'new_structure','kind':'user_defined_thought_operator',
            'description':'candidate pattern','examples':[],'origin':model_origin,'attrs':{'content_truth':'unverified'}}]
        manifest=self.one_manifest(model='gpt61_sol');raw=self.response(manifest,content=diff)
        attempt={'reported_cost_usd':'.00001','response_sha256':hashlib.sha256(raw).hexdigest(),
                 'state':'completed','http_status':200,'finished_at':'2026-09-30T15:00:00Z'}
        result=a.packet_response_preview(raw,attempt,packet,{'body':body},model_key='gpt61_sol',instrument_actor='measured-run')
        self.assertEqual(result['raw_proposed_diff'],diff)
        self.assertIsNone(result['raw_proposed_diff']['known_at'])
        bound=result['instrument_bound_diff']
        self.assertEqual(bound['known_at'],attempt['finished_at'])
        self.assertEqual(bound['definitions'],diff['definitions'])
        self.assertEqual(bound['origin']['response_sha256'],attempt['response_sha256'])
        self.assertEqual(result['native_record_mutations_from_binding'],0)
        candidate=result['preview']['candidate_packet']
        self.assertEqual(candidate['provenance']['definitions']['new_structure']['known_at'],attempt['finished_at'])
        policy={'schema':'loom.graph_packet_apply_policy/1','acceptance':'auto','allow_source_tombstones':False}
        applied,receipt=codec.apply_diff(packet,bound,policy)
        self.assertEqual(applied,candidate)
        self.assertEqual(codec.invert_application(receipt,applied),packet)
        self.assertFalse(result['canonical_store_written'])

    def test_packet_response_rejects_unmeasured_timestamp_and_stale_diff(self):
        from loom.tools.structure.agentic_graph_v1 import packet as codec
        origin={'kind':'user','actor':'owner','model':None,'recipe_sha256':None,'response_sha256':None}
        packet=codec.make_packet(origin=origin)
        body=a.packet_body(packet,model_key='gpt61_sol')
        diff=codec.empty_diff(packet,proposal_id='p1',origin=origin)
        manifest=self.one_manifest(model='gpt61_sol');raw=self.response(manifest,content=diff)
        attempt={'reported_cost_usd':'.00001','response_sha256':hashlib.sha256(raw).hexdigest(),'state':'completed','http_status':200}
        with self.assertRaisesRegex(a.IdentityIntegrityError,'availability_time'):
            a.packet_response_preview(raw,attempt,packet,{'body':body},model_key='gpt61_sol',instrument_actor='run')
        diff['base_packet_sha256']='0'*64;raw=self.response(manifest,content=diff);attempt['response_sha256']=hashlib.sha256(raw).hexdigest()
        attempt['finished_at']='2026-09-30T15:00:00Z'
        with self.assertRaisesRegex(ValueError,'stale_or_incompatible'):
            a.packet_response_preview(raw,attempt,packet,{'body':body},model_key='gpt61_sol',instrument_actor='run')

    def test_configured_large_response_is_replayable(self):
        manifest=self.one_manifest()
        content=safe.canonical({'query_id':manifest['requests'][0]['id'],'label':'unknown'}).decode()+' '*(2200000)
        raw=self.response(manifest,content=content)
        self.assertGreater(len(raw),safe.MAX_RESPONSE_BYTES)
        with tempfile.TemporaryDirectory() as d:
            directory=Path(d);path=directory/'prepared.json';panel.write_new(path,manifest)
            self.scripted_run(manifest,directory/'run',response=raw)
            compiled,summary,cases=a.replay(path,directory/'run')
            self.assertEqual(summary['compiled_complete'],1)
            self.assertEqual(compiled[0]['label'],'unknown')

    def test_configured_million_cost_is_replayable(self):
        ex=deepcopy(self.presets['default_execution']);ex['budget_usd']='1000000000';ex['batch_reservation_cap_usd']='3000000'
        manifest=self.one_manifest(execution=ex);manifest['requests'][0]['reservation_usd']='3000000'
        raw=self.response(manifest,cost='1000001')
        with tempfile.TemporaryDirectory() as d:
            directory=Path(d);path=directory/'prepared.json';panel.write_new(path,manifest)
            ledger,posts=self.scripted_run(manifest,directory/'run',response=raw,key_limit='1000000000')
            self.assertEqual(ledger['attempts'][0]['reported_cost_usd'],'1000001')
            compiled,summary,cases=a.replay(path,directory/'run')
            self.assertEqual(summary['reported_cost_usd'],'1000001')
            self.assertEqual(summary['compiled_complete'],1)

    def test_all_public_model_presets_build_with_exact_provider_price_precision(self):
        self.assertEqual(a.money('0.0000000416666666666667'),Decimal('0.0000000416666666666667'))
        for key,config in self.configs.items():
            for track in a.TRACKS[:-1]:
                rows=a.dev_rows(self.cases[:1],track,'baseline',config,self.identities[key],self.recipes)
                self.assertTrue(all(a.money(r['reservation_usd'])>0 for r in rows))
                self.assertTrue(all(r['body']['model']==config['model'] for r in rows))

    def test_supplied_packet_prepares_own_manifest_without_archive_import_or_gold(self):
        from loom.tools.structure.agentic_graph_v1 import packet as codec
        origin={'kind':'user','actor':'owner','model':None,'recipe_sha256':None,'response_sha256':None}
        packet=codec.make_packet(task={'scope':'whole_archive'},origin=origin)
        with tempfile.TemporaryDirectory() as d, patch.object(panel,'load_dev_gold',side_effect=AssertionError('gold_read')):
            result=a.prepare_packet(Path(d)/'packet',packet,model_key='gpt6_luna')
            manifest=a.read(Path(d)/'packet'/'manifest.json')
            self.assertEqual(manifest['metadata']['split'],'owner_archive')
            self.assertEqual(safe.parse_json(manifest['requests'][0]['body']['messages'][1]['content']),packet)
            self.assertEqual(result['paid_calls'],0);self.assertFalse(result['source_imported_by_this_method'])
            a.plan_manifest(manifest)

    def test_provider_supported_output_budget_above_legacy_limit_is_configurable(self):
        config=deepcopy(self.configs['gpt61_sol']);config['max_tokens']['supplied_edge_judgment']=32768
        ex=deepcopy(self.presets['default_execution']);ex['batch_reservation_cap_usd']='2'
        manifest=self.one_manifest(model='gpt61_sol',config=config,execution=ex)
        plan=a.plan_manifest(manifest)
        self.assertEqual(manifest['requests'][0]['body']['max_tokens'],32768)
        self.assertEqual(plan['requests'][0]['request_hash'],safe.digest(manifest['requests'][0]['body']))


if __name__=='__main__': unittest.main()
