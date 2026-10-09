#!/usr/bin/env python3
"""Mechanics-only fixtures; these synthetic records are never quality data."""
import copy, hashlib, importlib.util, json, os, tempfile, unittest
from pathlib import Path
P=Path(__file__).with_name('retrieval-task-validate.py')
SPEC=importlib.util.spec_from_file_location('task_validator',P); M=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(M)
class TaskValidationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='thread7-task-validation-');self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.source=self.root/'source.json';messages=[{'message_id':str(i),'role':'user','created_at':i,'text':'mechanics fixture '+str(i)} for i in range(3)]
        self.source.write_text(json.dumps({'messages':messages}));e=[]
        for i in [0,1]:
            text=messages[i]['text'];e.append({'message_id':str(i),'pointer':f'/messages/{i}/text','role':'user','quote':text,'quote_span':[0,len(text)],'quote_sha256':M.sha(text.encode()),'message_text_sha256':M.sha(text.encode())})
        t={'task_id':'mechanics-only','source_path':str(self.source),'source_sha256':M.sha(self.source.read_bytes()),'family_id':'mechanics-family','split':'tuning','provider':'fixture','candidate_message_ids':['0','1','2'],'candidate_scope':{'mode':'family_full'},'expected_evidence':e,'absence_support':[],'answerability':'answerable','evidence_sets':[['0'],['1']],'forbidden_evidence':[{'message_id':'2'}],'independent_holdout':False,'category':'mechanics'}
        self.bundle={'tasks':[t]};self.task=t
    def run_bundle(self):
        p=self.root/'tasks.json';p.write_text(json.dumps(self.bundle));return M.validate(p)
    def test_accepts_alternative_evidence(self):self.assertEqual(self.run_bundle()['evidence_spans_checked'],2)
    def test_rejects_changed_source(self):
        self.source.write_text(self.source.read_text()+' ')
        with self.assertRaisesRegex(ValueError,'source_hash'):self.run_bundle()
    def test_rejects_misbound_pointer(self):
        self.task['expected_evidence'][0]['pointer']='/messages/1/text'
        with self.assertRaisesRegex(ValueError,'identity'):self.run_bundle()
    def test_rejects_interpretation_as_wrong_speaker(self):
        self.task['expected_evidence'][0]['role']='assistant'
        with self.assertRaisesRegex(ValueError,'identity'):self.run_bundle()
    def test_rejects_edited_quote(self):
        self.task['expected_evidence'][0]['quote']='new synthetic text'
        with self.assertRaisesRegex(ValueError,'quote'):self.run_bundle()
    def test_rejects_future_messages_in_prefix(self):
        self.task['candidate_scope']={'mode':'family_prefix','through_message_id':'1'}
        with self.assertRaisesRegex(ValueError,'scope'):self.run_bundle()
    def test_rejects_answerable_without_evidence(self):
        self.task['evidence_sets']=[]
        with self.assertRaisesRegex(ValueError,'missing_gold'):self.run_bundle()
    def test_rejects_positive_evidence_marked_forbidden(self):
        self.task['forbidden_evidence'].append({'message_id':'0'})
        with self.assertRaisesRegex(ValueError,'distractor'):self.run_bundle()
    def test_rejects_absence_with_positive_target(self):
        self.task['answerability']='unanswerable_in_source'
        with self.assertRaisesRegex(ValueError,'absence'):self.run_bundle()
    def test_rejects_false_independent_holdout_claim(self):
        self.task['independent_holdout']=True
        with self.assertRaisesRegex(ValueError,'independence'):self.run_bundle()
if __name__=='__main__':unittest.main(verbosity=2)
