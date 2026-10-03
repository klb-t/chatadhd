"""Mocked Jev decisions/billing/replay tests: no secret or paid requests."""
from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
try:
    from . import jev_live_pilot as pilot
except ImportError:
    import jev_live_pilot as pilot


def config():
    return {'schema':'loom.jev_pilot_request/1','enabled':True,'experiment_id':'jev-unit',
            'model':pilot.MODEL,'budget_usd':2,'batch_cap_usd':'.10','max_requests':64}


def inputs(n=2):
    return [{'case_id':'case-'+str(i),'language':'en','state':{'text':'All A are B. A(x). Therefore B(x).'},
             'questions':{'q01':{'type':'noul','instructions':'Is this modus ponens?',
                                'criteria':{'true':'The implication supports the conclusion.','false':'It does not.'}}}}
            for i in range(n)]


def catalog():
    return {'data':{'endpoints':[{'tag':'typesafe','status':0,'name':'TypeSafe | typesafe/jev-1.13-20260917',
                                  'pricing':{'prompt':'0.000000042','completion':'0','discount':0}}]}}


class HTTP:
    def __init__(self, probabilities=(.9,.8), byok=False, failure=None, metadata_404=0):
        self.calls=[]; self.probabilities=list(probabilities); self.byok=byok
        self.failure=failure; self.metadata_404=metadata_404
    def __call__(self, method,path,body,key):
        self.calls.append((method,path,body))
        if path == pilot.ENDPOINT:
            return 200,pilot.safe.canonical(catalog())
        if path == '/api/v1/key':
            return 200,pilot.safe.canonical({'data':{'limit':2,'limit_remaining':1.5,'limit_reset':None,
                'is_management_key':False,'include_byok_in_limit':False,'byok_usage':0}})
        if method == 'POST':
            if self.failure:
                raise self.failure
            return 200,pilot.safe.canonical({'id':'gen-dec-unit','model':'typesafe/jev-1.13-20260917','provider':'TypeSafe',
                'answers':{'q01':{'type':'noul','noul':self.probabilities.pop(0)}},
                'usage':{'input_tokens':100,'output_tokens':10,'cost':.0000042}})
        if self.metadata_404:
            self.metadata_404-=1
            return 404,b'{}'
        return 200,pilot.safe.canonical({'data':{'id':'gen-dec-unit','model':'typesafe/jev-1.13-20260917',
            'provider_name':'TypeSafe','api_type':'decisions','is_byok':self.byok,'total_cost':.0000042,
            'private_unrelated_account_field':'must-not-be-persisted'}})


class JevTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name); self.run_dir=self.root/'run'
    def prep(self, count=2, http=None):
        return pilot.prepare(config(),inputs(count),self.root/'prepared',transport_fn=http or HTTP())
    def run_pilot(self,m,http):
        return pilot.run(m,self.run_dir,transport_fn=http,key_loader=lambda:'fixture-secret')

    def test_prepare_frozen_questions_no_gold_and_caps(self):
        m=self.prep()
        self.assertEqual(m['total_reservation_usd'],'0.002')
        self.assertEqual(m['requests'][0]['body']['questions'],inputs()[0]['questions'])
        self.assertNotIn('language',m['requests'][0]['body'])
        self.assertEqual(m['model_aliases'],[pilot.MODEL,'typesafe/jev-1.13-20260917'])
        pilot.validate_manifest(m)
        m['requests'][0]['body']['state']['text']+='changed'
        with self.assertRaisesRegex(pilot.safe.RunnerError,'request_invalid'):
            pilot.validate_manifest(m)

    def test_price_body_and_question_invalid_before_paid_requests(self):
        for mutate in (lambda b:b['provider']['only'].append('other'),
                       lambda b:b['questions']['q01'].update(type='choice'),
                       lambda b:b['state'].update(text='x'*20000)):
            body={'model':pilot.MODEL,'state':inputs()[0]['state'],'questions':inputs()[0]['questions'],
                  'provider':{'only':['typesafe'],'allow_fallbacks':False,'max_price':{'prompt':'.042','completion':'0'}}}
            # Exact serialization cap must match the prepared form.
            body['provider']['max_price']['prompt']='0.042'
            mutate(body)
            with self.assertRaises(pilot.safe.RunnerError): pilot.validate_body(body)
        value=catalog();value['data']['endpoints'][0]['pricing']['prompt']='.000000043'
        with self.assertRaisesRegex(pilot.safe.RunnerError,'price_exceeds'):
            pilot.endpoint_identity(value)

    def test_success_billing_each_call_and_no_sensitive_metadata(self):
        m=self.prep();http=HTTP();result=self.run_pilot(m,http)
        self.assertEqual([r['state'] for r in result['attempts']],['completed','completed'])
        self.assertEqual(result['reported_cost_usd'],'0.0000084')
        self.assertEqual(len([x for x in http.calls if x[0]=='POST']),2)
        self.assertEqual(len([x for x in http.calls if 'generation?' in x[1]]),2)
        for path in self.run_dir.iterdir():
            self.assertNotIn(b'fixture-secret',path.read_bytes())
            self.assertNotIn(b'must-not-be-persisted',path.read_bytes())
        def forbidden(*args):self.fail('Completed resume cannot call any endpoint')
        self.assertEqual(pilot.run(m,self.run_dir,transport_fn=forbidden,key_loader=forbidden),result)

    def test_byok_or_missing_billing_stops_after_one_and_survives_resume(self):
        for byok in (True,):
            with self.subTest(byok=byok),tempfile.TemporaryDirectory() as folder:
                m=self.prep() if not (self.root/'prepared').exists() else pilot.read_json(self.root/'prepared/manifest.json')
                http=HTTP(byok=byok)
                result=pilot.run(m,folder,transport_fn=http,key_loader=lambda:'fixture-secret')
                self.assertEqual(result['stopped_reason'],'jev_generation_byok_detected')
                self.assertEqual(len(result['attempts']),1)
                self.assertTrue((Path(folder)/'case-0.response.bin').exists())
                def forbidden(*args):self.fail('Stop must prevent further paid requests')
                self.assertEqual(pilot.run(m,folder,transport_fn=forbidden,key_loader=forbidden),result)

    def test_generation_404_retry_is_read_only_bounded(self):
        m=self.prep(1);http=HTTP(metadata_404=3)
        with patch.object(pilot.time,'sleep'):
            result=self.run_pilot(m,http)
        self.assertNotIn('stopped_reason',result)
        self.assertEqual(result['attempts'][0]['generation_billing']['audit_status'],'unavailable')
        self.assertEqual(len([x for x in http.calls if x[0]=='POST']),1)
        self.assertEqual(len([x for x in http.calls if 'generation?' in x[1]]),1)

    def test_interrupt_started_is_not_retried_or_advanced(self):
        m=self.prep();http=HTTP(failure=KeyboardInterrupt())
        with self.assertRaises(KeyboardInterrupt): self.run_pilot(m,http)
        def forbidden(*args):self.fail('Interrupted billing must block every further request')
        result=pilot.run(m,self.run_dir,transport_fn=forbidden,key_loader=forbidden)
        self.assertEqual(result['stopped_reason'],'jev_interrupted_billing_unknown')
        self.assertEqual(len(result['attempts']),1)

    def test_score_known_probabilities_missing_denominator_and_tamper_detection(self):
        m=self.prep();self.run_pilot(m,HTTP(probabilities=(.9,.1)))
        gold=[{'case_id':'case-0','language':'en','split':'development','family_id':'a','variant':'base',
               'base_case_id':'case-0','labels':{'q01':1},'expected_changed_questions':[]},
              {'case_id':'case-1','language':'en','split':'validation','family_id':'a','variant':'foil',
               'base_case_id':'case-0','labels':{'q01':0},'expected_changed_questions':['q01']}]
        score=pilot.score(m,self.run_dir,gold)
        self.assertEqual(score['overall']['accuracy_planned'],1)
        self.assertAlmostEqual(score['overall']['brier_available'],.01)
        self.assertEqual(score['comparisons']['foil']['foil_changed_decision_fraction'],1)
        absent=pilot.metrics([{'probability':None,'label':1},{'probability':.9,'label':1}])
        self.assertEqual(absent['accuracy_planned'],.5)
        self.assertEqual(absent['coverage'],.5)
        self.assertEqual(absent['accuracy_available'],1)
        (self.run_dir/'case-0.response.bin').write_bytes(b'tamper')
        with self.assertRaisesRegex(pilot.safe.RunnerError,'hash_mismatch'):
            pilot.score(m,self.run_dir,gold)

    def test_transport_rejects_arbitrary_host_and_generation_injection(self):
        for path in ('https://example.invalid', '/api/v1/generation?id=abc&leak=yes','/api/v1/chat/completions'):
            with self.assertRaisesRegex(pilot.safe.RunnerError,'invalid_jev_route'):
                pilot.transport('GET',path,None,'fixture-secret')

    def test_rejected_answers_keep_known_cost_and_over_reservation_stops_first(self):
        m=self.prep()
        for cost, expected in ((.0000042,'jev_answer_inventory_invalid'),(.002,'jev_cost_exceeds_reservation'),
                               (None,'invalid_money')):
            with self.subTest(cost=cost),tempfile.TemporaryDirectory() as folder:
                original=HTTP()
                def send(method,path,body,key):
                    status,raw=original(method,path,body,key)
                    if method=='POST':
                        value=pilot.safe.parse_json(raw)
                        value['answers']={}
                        value['usage']['cost']=cost
                        raw=pilot.safe.canonical(value)
                    return status,raw
                result=pilot.run(m,folder,transport_fn=send,key_loader=lambda:'fixture-secret')
                self.assertEqual(result['stopped_reason'],expected)
                self.assertEqual(len(result['attempts']),1)
                self.assertEqual(result['cost_accounting_complete'],cost is not None)
                self.assertEqual(result['attempts_with_unknown_cost'],int(cost is None))
                if cost is not None:
                    self.assertEqual(pilot.safe._money(result['reported_cost_usd']),pilot.safe._money(cost))
                else:
                    self.assertNotIn('reported_cost_usd',result['attempts'][0])

if __name__=='__main__': unittest.main()
