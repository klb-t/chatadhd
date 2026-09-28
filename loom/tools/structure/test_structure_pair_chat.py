"""Offline protocol and strict classification scorer tests; no real labels/API."""
from copy import deepcopy
import hashlib
from pathlib import Path
import tempfile
import unittest
try:
    from . import structure_pair_chat as chat
except ImportError:
    import structure_pair_chat as chat
safe = chat.safe


def case(n=0):
    return {'case_id': 'mock-'+str(n), 'language': 'pl', 'state': {'text': '{"left":"żółty","right":"yellow"}'},
            'questions': {'q01': {'type': 'noul', 'instructions': 'Compare structure.',
                                 'criteria': {'true': 'Same.', 'false': 'Different.'}}}}


def response(content='{"q01":true}', **kw):
    obj = {'model': chat.MODEL, 'provider': 'OpenAI', 'choices': [{'finish_reason':'stop',
            'message': {'content':content}}], 'usage': {'cost':0.00001,'is_byok':False}}
    obj.update(kw)
    return safe.canonical(obj)


class PairTests(unittest.TestCase):
    def test_exact_input_strings(self):
        c = case()
        body = chat.body_for(c)
        decoded = safe.parse_json(body['messages'][1]['content'])
        self.assertEqual(decoded, {k:c[k] for k in ('state','questions')})
        self.assertNotIn('language', decoded)
        self.assertEqual(body['temperature'],0)
        self.assertEqual(body['max_tokens'],128)
        self.assertFalse(body['provider']['allow_fallbacks'])

    def test_boolean_only(self):
        self.assertIs(chat.parse_boolean(response()),True)
        self.assertIs(chat.parse_boolean(response('{"q01":false}')),False)
        for content in ('{"q01":1}', '{"q01":"true"}', '{"q01":null}', '{}',
                        '{"q01":true,"q01":false}', '{"q01":true,"why":"same"}',
                        '```json\n{"q01":true}\n```','true','NaN'):
            with self.subTest(content=content), self.assertRaises(safe.RunnerError):
                chat.parse_boolean(response(content))

    def test_refusal_truncation_and_identity(self):
        for change in ({'model':'other/model'},{'provider':'Other'},
                       {'choices':[{'finish_reason':'length','message':{'content':'{"q01":true}'}}]},
                       {'choices':[{'finish_reason':'stop','message':{'content':'{"q01":true}','refusal':'no'}}]},
                       {'error':{'message':'failed'}}):
            with self.subTest(change=change), self.assertRaises(safe.RunnerError):
                chat.parse_boolean(response(**change))

    def test_duplicate_envelope_refused(self):
        with self.assertRaises(safe.RunnerError):
            chat.parse_boolean(b'{"choices":[],"choices":[]}')

    def test_missing_affects_coverage_and_planned_accuracy(self):
        rows=[{'prediction':True,'label':1},{'prediction':False,'label':1},
              {'prediction':None,'label':0},{'prediction':False,'label':0}]
        m=chat.metrics(rows)
        self.assertEqual(m['coverage'],.75)
        self.assertEqual(m['accuracy_planned'],.5)
        self.assertEqual(m['accuracy_available'],2/3)
        self.assertEqual(m['fn'],1)

    def test_prepare_plan_and_score_missing(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            inputs=[case(i) for i in range(48)]
            snapshot={'data':{'endpoints':[{'tag':'openai','status':0,
                'supported_parameters':['max_tokens','temperature','response_format'],
                'pricing':{'prompt':'0.0000004','completion':'0.0000016'}}]}}
            chat.write_new(root/'inputs.json',inputs)
            chat.write_new(root/'snapshot.json',snapshot)
            p=chat.prepare(root/'inputs.json',root/'snapshot.json',root/'prep',safe._utc())
            self.assertEqual(p['request_count'],48)
            m=chat.read_json(root/'prep'/'manifest.json')
            chat.validate_manifest(m)
            (root/'run').mkdir()
            raw=response()
            (root/'run'/'mock-0.response.bin').write_bytes(raw)
            r=p['requests'][0]
            attempt={k:r[k] for k in ('id','request_hash','reservation_usd')}
            attempt.update(state='completed',http_status=200,response_file='mock-0.response.bin',
                response_sha256=hashlib.sha256(raw).hexdigest(),reported_cost_usd='.00001',elapsed_seconds=.1)
            ledger={'schema':'loom.openrouter_ledger/1','manifest_hash':p['manifest_hash'],'attempts':[attempt]}
            chat.write_new(root/'run'/'ledger.json',ledger)
            gold=[{'case_id':c['case_id'],'labels':{'q01':1}} for c in inputs]
            result=chat.score(m,root/'run',gold)
            self.assertEqual(result['overall']['available'],1)
            self.assertEqual(result['overall']['missing'],47)
            self.assertEqual(result['reported_cost_usd'],'0.00001')
            self.assertEqual(result['errors']['not_attempted'],47)
            with self.assertRaises(safe.RunnerError):
                chat.score(m,root/'run',gold[:-1])
            changed=deepcopy(m);changed['requests'][0]['body']['temperature']=.5
            with self.assertRaises(safe.RunnerError):
                chat.validate_manifest(changed)

if __name__=='__main__':unittest.main()
