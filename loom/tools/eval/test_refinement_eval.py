"""Evaluator/fixture self-consistency and adversarial mutations, NOT model scores."""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from refinement_fixture import (clean_source, load_cases, materialize_case, render_statements,
                                verify_manifest)
from refinement_eval import evaluate_case, evaluate_cases, label_metrics, normalized

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / 'loom/tests/fixtures/eval/refinement_v1'
DEV = load_cases(FIXTURE/'dev.jsonl')
VALIDATION = load_cases(FIXTURE/'validation.jsonl')
CASES = {c['id']: c for c in DEV+VALIDATION}


def prediction(cid):
    g = materialize_case(CASES[cid])
    return {k: g[k] for k in ('case_id','specs','turn_labels')}


def rerender(p):
    for s in p['specs']:
        s['compiled_instruction'] = render_statements(s['statements'])
    return p


def codes(result):
    return {x['code'] for x in result['findings']}


class Refinement(unittest.TestCase):
    def score(self, cid, p):
        return evaluate_case(CASES[cid], p)

    def test_counts_languages_domains(self):
        self.assertEqual((len(DEV), len(VALIDATION)), (20,20))
        self.assertEqual(sum(len(c['tasks']) for c in CASES.values()),44)
        self.assertEqual(sum(c['language']=='pl' for c in CASES.values()),20)
        for domain in ('letter','code','text','analysis'):
            self.assertEqual(sum(c['domain']==domain for c in CASES.values()),10)

    def test_unique_source_blocks(self):
        texts=[json.dumps(clean_source(c)['turns'],ensure_ascii=False) for c in CASES.values()]
        self.assertEqual(len(texts),len(set(texts)))

    def test_manifest_is_unchanged(self):
        self.assertEqual(verify_manifest(FIXTURE),[])

    def test_expanded_gold_hashes(self):
        manifest=json.loads((FIXTURE/'manifest.json').read_text())
        for name,data in [('dev',DEV),('validation',VALIDATION)]:
            content=''.join(json.dumps(materialize_case(c),ensure_ascii=False,separators=(',',':'))+'\n' for c in data).encode()
            self.assertEqual(hashlib.sha256(content).hexdigest(),manifest['expanded_gold'][name]['sha256'])

    def test_materializer_hash_frozen(self):
        expected=json.loads((FIXTURE/'manifest.json').read_text())['materializer_sha256']
        self.assertEqual(hashlib.sha256(Path(__file__).with_name('refinement_fixture.py').read_bytes()).hexdigest(),expected)

    def test_gold_does_not_leak_to_source(self):
        for c in CASES.values():
            source=clean_source(c)
            self.assertEqual(set(source),{'id','language','domain','turns'})
            for t in source['turns']:
                self.assertEqual(set(t),{'role','text'})

    def test_all_acts_are_represented(self):
        from refinement_fixture import ACTS
        acts={a for c in CASES.values() for t in c['turns'] for label in t['labels'] for a in label['acts']}
        self.assertEqual(acts,ACTS)

    def test_removing_exception_is_not_saving_tokens(self):
        p=prediction('d06'); sp=p['specs'][0]
        sp['statements']=[s for s in sp['statements'] if s['kind']!='exception']
        r=self.score('d06',rerender(p))
        self.assertIn('clause.reference_omission',codes(r))
        self.assertEqual(r['exception_coverage'],{'matched':0,'total':1,'ratio':0})

    def test_exception_scope_cannot_disappear(self):
        p=prediction('v05'); p['specs'][0]['statements'][-1]['conditions']=[]
        self.assertIn('clause.condition_mismatch',codes(self.score('v05',rerender(p))))

    def test_rejected_variant_cannot_return(self):
        p=prediction('d03'); p['specs'][0]['statements'][-1]['status']='active'
        self.assertIn('clause.rejected_or_superseded_return',codes(self.score('d03',rerender(p))))

    def test_conflict_not_last_wins(self):
        p=prediction('d02')
        for s in p['specs'][0]['statements']:
            if s['status']=='contested': s['status']='active'
        self.assertIn('clause.unjustified_resolution',codes(self.score('d02',rerender(p))))

    def test_dislike_does_not_license_arbitrary_fix(self):
        for cid in ('d05','v08'):
            p=prediction(cid); sp=p['specs'][0]
            s=copy.deepcopy(sp['statements'][-1]); s.update(id='made_up',kind='format',text='Use exactly seven bullet points.')
            sp['statements'].append(s)
            self.assertIn('clause.unsupported_correction',codes(self.score(cid,rerender(p))))

    def test_question_is_not_permission(self):
        p=prediction('d09'); sp=p['specs'][0]
        s=copy.deepcopy(sp['statements'][-1]); s.update(id='permission',kind='required_information',text='Add the new API parameter.')
        sp['statements'].append(s)
        self.assertIn('clause.unsupported_correction',codes(self.score('d09',rerender(p))))

    def test_executor_only_not_product_content(self):
        p=prediction('d01'); p['specs'][0]['statements'][-1]['kind']='required_information'
        self.assertIn('clause.executor_context_leak',codes(self.score('d01',rerender(p))))

    def test_parallel_tasks_not_merged(self):
        p=prediction('v20'); p['specs']=p['specs'][:1]
        self.assertIn('prediction.missing_task',codes(self.score('v20',p)))

    def test_wrong_task_scope(self):
        p=prediction('d08'); p['specs'][0]['scope']['branch_id']='other'
        self.assertIn('prediction.scope',codes(self.score('d08',p)))

    def test_renamed_statement_ids_are_not_semantic_errors(self):
        p=prediction('d07')
        for s in p['specs'][0]['statements']:
            s['id']='renamed_'+s['id']; s['supersedes']=['renamed_'+x for x in s['supersedes']]
        self.assertEqual(self.score('d07',rerender(p))['status'],'pass')

    def test_unknown_paraphrase_needs_review_not_hallucination_label(self):
        p=prediction('d01'); p['specs'][0]['statements'][0]['text']='Uprzejmie potwierdź otrzymanie przesyłki.'
        r=self.score('d01',rerender(p))
        self.assertEqual(r['status'],'needs_review')
        self.assertEqual(r['findings'],[])
        self.assertEqual(r['reference_coverage']['matched'],3)

    def test_reviewed_paraphrase_alignment(self):
        p=prediction('d01'); p['specs'][0]['statements'][0]['text']='Uprzejmie potwierdź otrzymanie przesyłki.'
        p['alignment']={'reviewer':'fixture test author','method':'manual test alignment',
                        'pairs':[{'task_id':'main','candidate_id':'g','gold_id':'g'}]}
        r=self.score('d01',rerender(p))
        self.assertEqual(r['status'],'pass')
        self.assertEqual(r['alignment']['pairs'],1)

    def test_alignment_cannot_invent_candidate(self):
        p=prediction('d01');p['alignment']={'reviewer':'test','method':'manual','pairs':[{'task_id':'main','candidate_id':'missing','gold_id':'g'}]}
        self.assertIn('prediction.alignment_format',codes(self.score('d01',p)))

    def test_conflicting_exact_alignment_rejected(self):
        p=prediction('d01');p['alignment']={'reviewer':'test','method':'manual','pairs':[{'task_id':'main','candidate_id':'g','gold_id':'r'}]}
        self.assertIn('prediction.alignment_conflict',codes(self.score('d01',p)))

    def test_duplicate_alignment_not_double_credit(self):
        p=prediction('d01'); sp=p['specs'][0]
        s=copy.deepcopy(sp['statements'][0]);s['id']='duplicate';sp['statements'].append(s)
        r=self.score('d01',rerender(p))
        self.assertIn('clause.duplicate_alignment',codes(r))
        self.assertLessEqual(r['reference_coverage']['matched'],r['reference_coverage']['total'])

    def test_instruction_divergence_not_silently_accepted(self):
        p=prediction('d01'); p['specs'][0]['compiled_instruction']['text']+='\nAlso include unrelated claims.'
        r=self.score('d01',p)
        self.assertEqual(r['status'],'needs_review')
        self.assertIn('instruction.requires_semantic_review',{x['code'] for x in r['review_items']})

    def test_tampered_quote_detected(self):
        p=prediction('d01');p['specs'][0]['source_refs'][0]['quote']='fabricated exact quote'
        self.assertIn('prediction.source_quote',codes(self.score('d01',p)))

    def test_tampered_locator_detected(self):
        p=prediction('d01');p['specs'][0]['source_refs'][0]['locator']['json_pointer']='/turns/999'
        self.assertIn('prediction.source_reference',codes(self.score('d01',p)))

    def test_future_knowledge_rejected(self):
        p=prediction('d01');p['specs'][0]['source_refs'][0]['known_at']='2026-01-01T00:00:00Z'
        r=self.score('d01',p)
        self.assertIn('prediction.contract',codes(r))

    def test_foreign_history_detected(self):
        p=prediction('d01');p['specs'][0]['history_event_ids'].append('other.t0')
        self.assertIn('prediction.foreign_history',codes(self.score('d01',p)))

    def test_no_substring_entailment(self):
        self.assertNotEqual(normalized('Not all'),normalized('None'))
        p=prediction('d12');p['specs'][0]['statements'][0]['text']='Wszystkie obserwacje wspierają hipotezę.'
        r=self.score('d12',rerender(p))
        self.assertNotEqual(r['status'],'pass')
        self.assertTrue(r['review_items'])

    def test_no_identifier_case_folding(self):
        self.assertNotEqual(normalized('Config'),normalized('config'))

    def test_no_label_prediction_is_unavailable(self):
        p=prediction('d01');p.pop('turn_labels')
        self.assertIsNone(self.score('d01',p)['turn_label_metrics'])

    def test_extra_label_is_false_positive(self):
        p=prediction('d01');p['turn_labels'][0]['acts'].append('exception')
        self.assertEqual(self.score('d01',p)['turn_label_metrics']['fp'],1)

    def test_missing_labels_not_perfect(self):
        p=prediction('d01');p['turn_labels']=[]
        m=self.score('d01',p)['turn_label_metrics']
        self.assertEqual(m['recall'],0)
        self.assertIsNone(m['precision'])

    def test_duplicate_label_rows_are_invalid(self):
        p=prediction('d01');p['turn_labels'].append(copy.deepcopy(p['turn_labels'][0]))
        self.assertIn('prediction.label_format',codes(self.score('d01',p)))

    def test_missing_predictions_not_excluded(self):
        r=evaluate_cases(DEV,[])
        self.assertEqual(r['missing_predictions'],20)
        self.assertEqual(r['case_status_counts'],{'fail':20})
        self.assertEqual(r['reference_coverage']['ratio'],0)
        self.assertIsNone(r['semantic_accuracy'])

    def test_duplicate_predictions_raise(self):
        p=prediction('d01')
        with self.assertRaises(ValueError): evaluate_cases(DEV,[p,p])

    def test_foreign_prediction_raise(self):
        with self.assertRaises(ValueError): evaluate_cases(DEV,[prediction('v01')])

    def test_materializer_does_not_alias_fixture_labels(self):
        original=copy.deepcopy(CASES['d01'])
        p=prediction('d01');p['turn_labels'][0]['acts'].append('exception')
        self.assertEqual(CASES['d01'],original)

    def test_no_input_mutation(self):
        p=prediction('d02');original=copy.deepcopy(p)
        self.score('d02',p);self.assertEqual(original,p)

    def test_network_never_used(self):
        with patch('socket.socket',side_effect=AssertionError('network forbidden')):
            self.assertEqual(self.score('d01',prediction('d01'))['status'],'pass')

    def test_cli_requires_validation_optin(self):
        script=Path(__file__).with_name('refinement_eval.py')
        with tempfile.TemporaryDirectory() as folder:
            pred=Path(folder)/'pred.json';pred.write_text('{"predictions":[]}')
            args=[sys.executable,str(script),str(FIXTURE/'validation.jsonl'),str(pred)]
            r=subprocess.run(args,capture_output=True,text=True)
            self.assertEqual(r.returncode,2,r.stderr)
            r=subprocess.run(args+['--include-validation'],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            self.assertEqual(json.loads(r.stdout)['missing_predictions'],20)

    def test_freeze_detects_change(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)
            for name in ('manifest.json','PLAN.md','dev.jsonl','validation.jsonl'):
                (path/name).write_bytes((FIXTURE/name).read_bytes())
            with (path/'dev.jsonl').open('a') as f:f.write(' ')
            self.assertIn('dev.jsonl',verify_manifest(path))


def make_self_check(case):
    def check(self):
        r=self.score(case['id'],prediction(case['id']))
        self.assertEqual(r['status'],'pass',r)
        self.assertEqual(r['reference_coverage']['ratio'],1)
        self.assertEqual(r['turn_label_metrics']['fp'],0)
        self.assertEqual(r['turn_label_metrics']['fn'],0)
    return check


for case in CASES.values():
    setattr(Refinement,'test_gold_contract_'+case['id'],make_self_check(case))

if __name__=='__main__': unittest.main()
