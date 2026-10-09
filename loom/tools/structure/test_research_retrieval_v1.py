"""Controlled mechanics only; fixtures are not research quality evidence."""
import json
from pathlib import Path
import tempfile
import unittest

from loom.tools.structure import research_retrieval_v1 as rr

EVALUATION = {'ranking_cutoffs':[1,5,10], 'ndcg_cutoffs':[10], 'paired_reference_method':'m', 'paired_metric':'context_evidence_recall'}


def record(ident, text, parents=(), children=()):
    return {'message_id':ident, 'node_id':ident, 'text':text, 'role':'user',
            'parent_ids':list(parents), 'child_ids':list(children), 'created_at':ident}


def config(methods):
    return {'methods':[{'id':ident,'operator':op,'parameters':params,'selection':{'min_score':0.0}}
                       for ident,op,params in methods]}


class RetrievalMechanicsTests(unittest.TestCase):
    def test_existing_bm25_algebra_is_reused(self):
        docs=[record('a','rare rare'),record('b','other')]
        engine=rr.Engine(docs,config([('one','bm25',{'k1':1.2,'b':0.75})]))
        scores,_=engine.score({'query':'rare'})
        self.assertEqual(scores['one'],rr.explore.bm25('rare',[d['text'] for d in docs]))

    def test_plugin_method_does_not_need_engine_method_allowlist(self):
        engine=rr.Engine([record('a','x')],config([('caller_name','caller_operator',{})]))
        engine.register('caller_operator',lambda task,params,prior:[7.0])
        self.assertEqual(engine.score({'query':'x'})[0]['caller_name'],[7.0])

    def test_unavailable_operator_is_not_fake_execution(self):
        with self.assertRaisesRegex(ValueError,'unavailable_operator'):
            rr.Engine([],config([('x','missing',{})])).score({'query':'x'})

    def test_utf8_context_byte_budget_includes_json_and_separators(self):
        docs=[record('a','żółć'),record('b','東京')]
        exact=len(rr.canonical([rr.context_record(d) for d in docs]))
        full=rr.select_context(docs,exact,2)
        self.assertEqual(full['bytes'],exact)
        self.assertEqual(full['record_count'],2)
        limited=rr.select_context(docs,exact-1,2)
        self.assertEqual(limited['record_count'],1)
        self.assertIsNone(limited['model_tokens'])

    def test_nonfitting_whole_record_skipped_without_silent_truncation(self):
        docs=[record('large','z'*1000),record('short','small')]
        result=rr.select_context(docs,100,10)
        self.assertEqual([r['message_id'] for r in result['records']],['short'])
        self.assertEqual(result['omitted'][0]['reason'],'whole_record_exceeds_remaining_byte_budget')
        self.assertEqual(result['records'][0]['text'],'small')

    def test_record_cap_separate_from_byte_cap(self):
        result=rr.select_context([record('a','x'),record('b','x')],10000,1)
        self.assertEqual(result['record_count'],1)
        self.assertEqual(result['omitted'][0]['reason'],'record_budget')

    def test_graph_uses_only_native_edges_not_adjacent_array_rows(self):
        docs=[record('a','one',children=['b']),record('b','two',parents=['a']),record('c','third')]
        method=('graph','graph_distance',{'directions':['child_ids'],'max_hops':2,'decay':0.5,'unanchored':'none'})
        scores,_=rr.Engine(docs,config([method])).score({'query':'', 'anchor_message_id':'a'})
        self.assertEqual(scores['graph'],[1.0,0.5,0.0])

    def test_graph_missing_parent_remains_unresolved(self):
        docs=[record('a','one',parents=['external'])]
        method=('graph','graph_distance',{'directions':['parent_ids'],'max_hops':2,'decay':0.5,'unanchored':'none'})
        scores,_=rr.Engine(docs,config([method])).score({'query':'', 'anchor_message_id':'a'})
        self.assertEqual(scores['graph'],[1.0])

    def test_observed_parent_reverse_edges_allow_sibling_navigation(self):
        docs=[record('parent','x'),record('left','x',parents=['parent']),record('right','x',parents=['parent'])]
        params={'directions':['parent_ids','child_ids'],'max_hops':2,'decay':0.5,'unanchored':'none','adjacency_policy':'observed_parent_reciprocal'}
        scores,_=rr.Engine(docs,config([('graph','graph_distance',params)])).score({'query':'','anchor_message_id':'left'})
        self.assertEqual(scores['graph'],[0.5,1.0,0.25])
        params['adjacency_policy']='declared_fields'
        legacy,_=rr.Engine(docs,config([('graph','graph_distance',params)])).score({'query':'','anchor_message_id':'left'})
        self.assertEqual(legacy['graph'],[0.5,1.0,0.0])

    def test_reverse_parent_edges_do_not_invent_external_nodes(self):
        docs=[record('left','x',parents=['external']),record('right','x',parents=['external'])]
        params={'directions':['parent_ids','child_ids'],'max_hops':3,'decay':0.5,'unanchored':'none','adjacency_policy':'observed_parent_reciprocal'}
        scores,_=rr.Engine(docs,config([('graph','graph_distance',params)])).score({'query':'','anchor_message_id':'left'})
        self.assertEqual(scores['graph'],[1.0,0.0])

    def test_reused_scores_do_not_execute_operator_again(self):
        engine=rr.Engine([record('a','x')],config([('cached','explode',{})]))
        def fail(task,params,prior):raise AssertionError('reexecuted')
        engine.register('explode',fail)
        scores,measurement=engine.score({'query':'x'},{'cached':([3.0],{'operator_seconds':0.1,'python_peak_allocated_bytes':7})})
        self.assertEqual(scores['cached'],[3.0])
        self.assertTrue(measurement['cached']['operator_replayed_not_executed'])

    def test_reuse_rejects_changed_seed_dependency(self):
        methods=config([('base','bm25',{}),('graph','graph_distance',{'seed_method':'base'})])['methods']
        with self.assertRaisesRegex(ValueError,'changed_upstream'):
            rr.validate_reuse_dependencies(methods,{'graph'})

    def test_reuse_rejects_transitively_changed_component_dependency(self):
        methods=config([('base','token_cosine',{}),
            ('middle','weighted_rrf',{'components':[{'method':'base'}]}),
            ('outer','weighted_rrf',{'components':[{'method':'middle'}]})])['methods']
        with self.assertRaisesRegex(ValueError,'changed_upstream'):
            rr.validate_reuse_dependencies(methods,{'outer','middle'})

    def test_reuse_accepts_only_complete_unchanged_dependency_closure(self):
        methods=config([('base','token_cosine',{}),
            ('middle','weighted_rrf',{'components':[{'method':'base'}]}),
            ('outer','weighted_rrf',{'components':[{'method':'middle'}]})])['methods']
        rr.validate_reuse_dependencies(methods,{'base','middle','outer'})

    def test_reuse_cyclic_dependency_fails_closed(self):
        methods=config([('first','graph_distance',{'seed_method':'second'}),
                        ('second','graph_distance',{'seed_method':'first'})])['methods']
        with self.assertRaisesRegex(ValueError,'cyclic_method_dependency'):
            rr.validate_reuse_dependencies(methods,{'first','second'})

    def test_unknown_anchor_is_rejected(self):
        method=('graph','graph_distance',{'directions':['parent_ids'],'max_hops':2,'decay':0.5,'unanchored':'none'})
        with self.assertRaisesRegex(ValueError,'anchor_not'):
            rr.Engine([record('a','x')],config([method])).score({'query':'','anchor_message_id':'z'})

    def test_unknown_unanchored_policy_cannot_silently_return_empty(self):
        method=('graph','graph_distance',{'directions':['parent_ids'],'max_hops':2,'decay':0.5,'unanchored':'typo'})
        with self.assertRaisesRegex(ValueError,'unsupported_unanchored'):
            rr.Engine([record('a','x')],config([method])).score({'query':''})

    def test_evaluation_cutoffs_are_caller_data(self):
        docs=[record('a','x')]
        task={'expected_evidence':[{'message_id':'a'}],'answerability':'answerable'}
        metrics=rr.evaluate(task,docs,rr.select_context(docs,1000,1),
                            {'ranking_cutoffs':[2],'ndcg_cutoffs':[3]})
        self.assertEqual(metrics['precision_at_2'],0.5)
        self.assertEqual(metrics['ndcg_at_3'],1.0)
        self.assertNotIn('precision_at_1',metrics)

    def test_composition_includes_dependency_cost_not_only_fusion(self):
        methods=[('a','token_cosine',{}),('b','weighted_rrf',{'rank_constant':60,'components':[{'method':'a','weight':1,'min_score':0}]})]
        _,timings=rr.Engine([record('a','x')],config(methods)).score({'query':'x'})
        self.assertEqual(timings['b']['dependency_operator_count'],2)
        self.assertGreaterEqual(timings['b']['method_total_seconds'],timings['a']['operator_seconds'])

    def test_required_evidence_recall_and_wrong_version_are_separate(self):
        docs=[record('old','old instruction'),record('new','new instruction')]
        task={'expected_evidence':[{'message_id':'new','required':True,'quote':'new instruction'}],
              'evidence_sets':[['new']], 'forbidden_evidence':[{'message_id':'old','reason':'wrong_version'}],
              'answerability':'answerable'}
        metrics=rr.evaluate(task,docs,rr.select_context(docs,10000,10),EVALUATION)
        self.assertEqual(metrics['context_evidence_recall'],1)
        self.assertEqual(metrics['annotated_version_distractors_selected'],1)
        self.assertEqual(metrics['reciprocal_rank'],0.5)
        self.assertEqual(metrics['context_precision'],0.5)

    def test_alternative_evidence_set_can_satisfy_recall(self):
        docs=[record('b','text')]
        task={'expected_evidence':[{'message_id':'a','quote':'alternative','required':False},{'message_id':'b','quote':'text','required':False}], 'evidence_sets':[['a'],['b']],
              'answerability':'answerable'}
        metrics=rr.evaluate(task,docs,rr.select_context(docs,1000,2),EVALUATION)
        self.assertEqual(metrics['context_evidence_recall'],1)
        self.assertEqual(metrics['required_quote_retention'],1)

    def test_unknown_answerability_cannot_silently_produce_null(self):
        task={'expected_evidence':[],'answerability':'typo'}
        with self.assertRaisesRegex(ValueError,'answerability_contract'):
            rr.evaluate(task,[],rr.select_context([],1000,2),EVALUATION)

    def test_unknown_distractor_reason_cannot_silently_produce_null(self):
        task={'expected_evidence':[],'answerability':'answerable','forbidden_evidence':[{'message_id':'a','reason':'typo'}]}
        with self.assertRaisesRegex(ValueError,'distractor_reason'):
            rr.evaluate(task,[],rr.select_context([],1000,2),EVALUATION)

    def test_unanswerable_has_null_gold_recall_not_fabricated_quality(self):
        task={'expected_evidence':[], 'evidence_sets':[], 'answerability':'unanswerable_in_source'}
        metrics=rr.evaluate(task,[],rr.select_context([],1000,2),EVALUATION)
        self.assertIsNone(metrics['context_evidence_recall'])
        self.assertTrue(metrics['no_answer_retrieval_abstained'])
        self.assertIsNone(metrics['answer_correctness'])

    def test_empty_selected_precision_is_unknown(self):
        task={'expected_evidence':[{'message_id':'a'}],'answerability':'answerable'}
        self.assertIsNone(rr.evaluate(task,[],rr.select_context([],1000,2),EVALUATION)['context_precision'])

    def test_budget_ceiling_distinguishes_impossible_whole_gold(self):
        docs=[record('a','x'*1000),record('b','small')]
        task={'expected_evidence':[{'message_id':'a'},{'message_id':'b'}], 'evidence_sets':[['a','b']]}
        result=rr.budget_ceiling(task,docs,{'max_bytes':100,'max_records':10})
        self.assertEqual(result['oracle_context_evidence_recall_ceiling'],0.5)
        self.assertFalse(result['gold_complete_evidence_fits_budget'])

    def test_alternative_gold_budget_ceiling_does_not_require_all_duplicates(self):
        docs=[record('a','x'*1000),record('b','small')]
        task={'evidence_sets':[['a'],['b']]}
        result=rr.budget_ceiling(task,docs,{'max_bytes':100,'max_records':10})
        self.assertEqual(result['oracle_context_evidence_recall_ceiling'],1)
        self.assertTrue(result['gold_complete_evidence_fits_budget'])

    def test_family_not_question_is_weight_unit(self):
        rows=[{'split':'tuning','method':'m','budget_id':'b','family_id':f,'task_id':str(i),
               'metrics':{'context_evidence_recall':score}}
              for i,(f,score) in enumerate([('large',1)]*9+[('small',0)])]
        result=rr.aggregate(rows,EVALUATION)['summaries'][0]['metrics']['context_evidence_recall']
        self.assertEqual(result['family_macro_mean'],0.5)
        self.assertEqual(result['families_observed'],2)
        self.assertEqual(result['tasks_observed'],10)

    def test_frozen_input_mutation_is_detected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            rr.write_new(root/'a.json',{'frozen':True})
            rr.write_new(root/'FREEZE.json',{'files':[{'path':'a.json','sha256':rr.sha((root/'a.json').read_bytes())}]})
            rr.verify_freeze(root/'FREEZE.json')
            (root/'a.json').write_text('{}')
            with self.assertRaisesRegex(ValueError,'frozen_input_mismatch'):
                rr.verify_freeze(root/'FREEZE.json')

    def test_native_anthropic_alternative_text_survives_projection(self):
        value={'messages':[{'message_id':'a','node_id':'a','role':'user','parent_ids':[],
                           'child_ids':[],'created_at':1,'source_pointer':'/x',
                           'native_message':{'text':'alternate','content':[{'type':'text','text':'block'}]}}],
               'family_id':'family'}
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'source.json';rr.write_new(path,value)
            _,records,_=rr.load_source(path)
            self.assertEqual(records[0]['text'],'block\nalternate')

    def test_score_and_record_inventory_cannot_diverge(self):
        with self.assertRaisesRegex(ValueError,'score_inventory'):
            rr.ranked([record('a','x')],[])

    def test_ranking_rejects_nan_and_infinity(self):
        for value in (float('nan'),float('inf')):
            with self.assertRaisesRegex(ValueError,'score_inventory'):
                rr.ranked([record('a','x')],[value])


if __name__=='__main__':
    unittest.main()
