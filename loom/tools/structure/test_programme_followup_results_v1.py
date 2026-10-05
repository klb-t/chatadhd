"""Fabricated model captures only; no actual results, credentials or transport."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

try:
    from . import programme_followup_results_v1 as subject
except ImportError:
    import programme_followup_results_v1 as subject

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / 'docs/research/model_research_2026-10-04'
SOURCE = BASE / 'followup-frontier-reply/final-verified-lineage'


def fixture(kind='graph_reply'):
    stage = 'stage4' if kind == 'graph_reply' else 'stage3'
    original = (SOURCE / stage / 'manifest.json').read_bytes()
    config = (SOURCE / 'config.json').read_bytes()
    references = (SOURCE / stage / 'references.json').read_bytes() if stage == 'stage3' else None
    policy = subject.wire.parse_json((BASE / 'followup-results' / (stage + '-policy.json')).read_bytes())
    rows = subject.wire.parse_json(original)['requests']
    normalized = {'schema':'loom.programme_results/1','stage_id':'arbitrary-stage-name',
                  'manifest_sha256':subject.sha(b'fabricated-adapted-manifest'),
                  'planned_operations':len(rows),'planned_operation_ids':[], 'requests':[], 'responses':[], 'rows':[]}
    for index, row in enumerate(rows):
        operation_id = 'invented-operation:' + str(index)
        body = subject.at(row, policy['prepared']['body_path'])
        body_hash = subject.digest(body)
        response_hash = subject.sha(('invented-http-envelope:' + str(index)).encode())
        if kind == 'graph_reply':
            arm = row['arm']
            content = {'schema':arm['wire_schema'],'base_packet_sha256':row['base_packet']['packet_id'],
                       'response_id':'invented-root','nodes':[{'id':'invented-root','text':' Żółć i 😀. ',
                       'role':'response','children':[]}], 'links':[]}
            if arm['representation'] == 'text_plus_json':
                content['text'] = ' Żółć i 😀. '
        else:
            if row['track'] == 'graph_completion':
                diff = subject.frontier.codec.empty_diff(row['input']['packet'], proposal_id='invented-proposal:' + str(index),
                    origin={'kind':'system','actor':'fabricated-test','model':None,'recipe_sha256':None,'response_sha256':None})
                content = {'diff':diff, 'transformation_report':{'loss':[],'augmentation':[]}}
            else:
                content = {'patterns':[], 'transformation_report':{'loss':[],'augmentation':[]}}
        normalized['planned_operation_ids'].append(operation_id)
        normalized['requests'].append({'operation_id':operation_id,'request_sha256':body_hash,
            'model_id':body['model'],'provider_id':body['provider']['only'][0], 'body':deepcopy(body),
            'metadata':{'prepared_request_id':row['id'],'source_manifest_sha256':subject.sha(original)}})
        normalized['responses'].append({'operation_id':operation_id,'response_sha256':response_hash,
            'projection':{'model':'fabricated/observed-model', 'provider':'invented-observed-provider',
                'choices':[{'message':{'role':'assistant','content':subject.wire.canonical(content).decode()},'finish_reason':'stop'}]}})
        normalized['rows'].append({'operation_id':operation_id,'request_sha256':body_hash,
            'manifest_sha256':normalized['manifest_sha256'],'response_sha256':response_hash,
            'state':'completed','completed':True,'billing_replay_verified':True,'response_ledger_bound':True,
            'http_status':200,'actual_cost_usd':'0.012','latency_seconds':0.25,
            'response_model':'fabricated/observed-model','requested_model':body['model']})
    return normalized, original, config, policy, references


def run(inputs):
    normalized, original, config, policy, references = inputs
    return subject.evaluate(subject.wire.canonical(normalized), original, config, policy, references_raw=references)


def content(inputs, index=0):
    return inputs[0]['responses'][index]['projection']['choices'][0]['message']['content']


def setcontent(inputs, value, index=0):
    inputs[0]['responses'][index]['projection']['choices'][0]['message']['content'] = value


class Results(unittest.TestCase):
    def test_valid_graph_all_slots_exact_utf8_packet_diff_spans(self):
        inputs = fixture(); before = deepcopy(inputs)
        result = run(inputs)
        self.assertEqual(result['all_choices_valid_requests'],12)
        self.assertEqual(result['planned_requests'],12)
        self.assertFalse(result['native_cpp_execution'])
        self.assertFalse(result['billing_or_identity_verification_performed_here'])
        self.assertEqual(inputs,before)
        choice = result['rows'][0]['choices'][0]
        self.assertEqual(choice['first_content_utf8'],content(inputs))
        self.assertEqual(choice['first_content_sha256'],subject.sha(content(inputs).encode()))
        self.assertEqual(choice['first_content_hex'],content(inputs).encode().hex())
        self.assertIn('candidate_packet',choice)
        self.assertIn('diff',choice['compilation'])
        self.assertIn('spans',choice)
        self.assertIsNone(result['semantic_accuracy'])
        self.assertIsNone(result['winner'])
        self.assertEqual(choice['response_text'],' Żółć i 😀. ')

    def test_frontier_recorded_content_uses_pinned_reference_scorer(self):
        result = run(fixture('frontier_comparison'))
        self.assertEqual(result['all_choices_valid_requests'],12)
        self.assertFalse(result['rows'][0]['choices'][0]['reference_agreement'])
        self.assertFalse(result['rows'][0]['choices'][0]['reference_agreement_is_semantic_judgement'])
        self.assertEqual(result['rows'][0]['choices'][0]['instrument_report']['response_origin'],'recorded')
        self.assertEqual(result['rows'][0]['choices'][0]['bound_diff']['origin']['model'],'fabricated/observed-model')

    def test_pattern_unused_diff_does_not_add_new_validation_rule(self):
        inputs=fixture('frontier_comparison')
        rows=subject.wire.parse_json(inputs[1])['requests']
        index=next(i for i,row in enumerate(rows) if row['track']=='pattern_discovery')
        parsed=subject.wire.parse_json(content(inputs,index));parsed['diff']={'unused':'extra-field'}
        setcontent(inputs,subject.wire.canonical(parsed).decode(),index)
        result=run(inputs)['rows'][index]['choices'][0]
        self.assertTrue(result['mechanical_validity'])
        self.assertNotIn('candidate_packet',result)

    def test_effective_policy_backend_and_adapter_source_retained(self):
        inputs=fixture();result=run(inputs)
        self.assertEqual(result['effective_policy'],inputs[3])
        self.assertEqual(result['adapter_sha256'],subject.sha(Path(subject.__file__).read_bytes()))
        self.assertEqual(result['evaluator_backend'],'pinned_python_reference')

    def test_required_instrument_dependency_cannot_be_omitted(self):
        inputs=fixture();del inputs[3]['instrument_files_sha256']['loom/tools/structure/agentic_graph_v1/packet.py']
        with self.assertRaisesRegex(ValueError,'instrument_dependency_binding_missing'):run(inputs)

    def test_actually_executed_followup_loader_pin_cannot_be_omitted(self):
        inputs=fixture();del inputs[3]['instrument_files_sha256']['loom/tools/structure/frontier_reply_followup_v1.py']
        with self.assertRaisesRegex(ValueError,'instrument_dependency_binding_missing'):run(inputs)

    def test_malformed_first_does_not_abort_later_valid_slots(self):
        inputs=fixture();setcontent(inputs,'{unclosed')
        result=run(inputs)
        self.assertEqual(result['all_choices_valid_requests'],11)
        self.assertEqual(len(result['rows']),12)
        self.assertFalse(result['rows'][0]['all_choices_mechanically_valid'])
        self.assertTrue(result['rows'][1]['all_choices_mechanically_valid'])
        self.assertEqual(result['rows'][0]['choices'][0]['first_content_utf8'],'{unclosed')

    def test_fences_and_nonjson_are_retained_without_repair(self):
        for text in ('```json\n{}\n```','ordinary answer',''):
            with self.subTest(text=text):
                inputs=fixture();setcontent(inputs,text)
                choice=run(inputs)['rows'][0]['choices'][0]
                self.assertFalse(choice['mechanical_validity'])
                self.assertEqual(choice['first_content_utf8'],text)
                self.assertEqual(choice['first_content_sha256'],subject.sha(text.encode()))

    def test_duplicate_keys_strict_per_choice(self):
        inputs=fixture();setcontent(inputs,'{"schema":"x","schema":"y"}')
        result=run(inputs)
        self.assertEqual(result['rows'][0]['choices'][0]['failure']['code'],'duplicate_json_key')
        self.assertEqual(result['all_choices_valid_requests'],11)

    def test_every_alternative_including_repeated_content_retained(self):
        inputs=fixture();choices=inputs[0]['responses'][0]['projection']['choices']
        choices.extend(deepcopy(choices)*2)
        result=run(inputs)['rows'][0]
        self.assertEqual(result['choice_count'],3)
        self.assertTrue(result['all_choices_mechanically_valid'])
        self.assertEqual(list(result['repeated_content_sha256_counts'].values()),[3])
        self.assertEqual([c['choice_index'] for c in result['choices']],[0,1,2])

    def test_valid_first_bad_later_alternative_invalidates_request(self):
        inputs=fixture();choices=inputs[0]['responses'][0]['projection']['choices']
        choices.append({'message':{'content':'broken'}})
        result=run(inputs)['rows'][0]
        self.assertTrue(result['choices'][0]['mechanical_validity'])
        self.assertFalse(result['choices'][1]['mechanical_validity'])
        self.assertFalse(result['all_choices_mechanically_valid'])

    def test_missing_planned_response_is_explicit_and_denominator_preserved(self):
        inputs=fixture();inputs[0]['responses'].pop(0)
        result=run(inputs)
        self.assertEqual(result['planned_requests'],12)
        self.assertEqual(len(result['rows']),12)
        self.assertIn('missing_planned_response',result['rows'][0]['errors'])
        self.assertEqual(result['rows'][0]['choice_count'],0)
        self.assertEqual(result['all_choices_valid_requests'],11)

    def test_missing_planned_request_invalidates_global_integrity(self):
        inputs=fixture();inputs[0]['requests'].pop(0)
        result=run(inputs)
        self.assertEqual(len(result['rows']),12)
        self.assertIn('missing_planned_request',result['rows'][0]['errors'])
        self.assertFalse(result['input_integrity_valid'])
        self.assertEqual(result['all_choices_valid_requests'],0)
        self.assertEqual(result['locally_all_choices_valid_requests'],11)

    def test_duplicate_response_records_are_not_dropped(self):
        inputs=fixture();inputs[0]['responses'].append(deepcopy(inputs[0]['responses'][0]))
        row=run(inputs)['rows'][0]
        self.assertEqual(len(row['responses']),2)
        self.assertEqual(row['choice_count'],2)
        self.assertIn('duplicate_operation_response',row['errors'])
        self.assertFalse(row['all_choices_mechanically_valid'])

    def test_reusing_one_physical_operation_for_two_plans_rejected(self):
        inputs=fixture();inputs[0]['requests'][1]['operation_id']=inputs[0]['requests'][0]['operation_id']
        result=run(inputs)
        self.assertIn('duplicate_physical_operation_identity',result['input_integrity_errors'])
        self.assertFalse(result['rows'][0]['all_choices_mechanically_valid'])
        self.assertFalse(result['rows'][1]['all_choices_mechanically_valid'])
        self.assertEqual(result['all_choices_valid_requests'],0)

    def test_wrong_body_hash_and_source_manifest_binding_keep_content(self):
        for field in ('body','request_sha256','source_manifest_sha256'):
            with self.subTest(field=field):
                inputs=fixture()
                if field=='body':inputs[0]['requests'][0]['body']['max_tokens']+=1
                elif field=='request_sha256':inputs[0]['requests'][0][field]='0'*64
                else:inputs[0]['requests'][0]['metadata'][field]='0'*64
                row=run(inputs)['rows'][0]
                self.assertIn('request_body_or_source_binding_mismatch',row['errors'])
                self.assertEqual(row['choices'][0]['first_content_utf8'],content(inputs))
                self.assertFalse(row['all_choices_mechanically_valid'])

    def test_response_receipt_hash_binding_is_required(self):
        inputs=fixture();inputs[0]['responses'][0]['response_sha256']='0'*64
        choice=run(inputs)['rows'][0]['choices'][0]
        self.assertIn('response_row_hash_binding_mismatch',choice['binding_errors'])
        self.assertFalse(choice['mechanical_validity'])

    def test_none_hashes_cannot_bind(self):
        inputs=fixture();inputs[0]['responses'][0]['response_sha256']=None;inputs[0]['rows'][0]['response_sha256']=None
        self.assertFalse(run(inputs)['rows'][0]['all_choices_mechanically_valid'])

    def test_wrong_source_bytes_fail_before_instrument(self):
        inputs=fixture();inputs=list(inputs);inputs[1]+=b' '
        with self.assertRaisesRegex(ValueError,'source_bytes_hash_mismatch'):run(inputs)

    def test_wrong_instrument_pin_rejected(self):
        inputs=fixture();name=next(iter(inputs[3]['instrument_files_sha256']))
        inputs[3]['instrument_files_sha256'][name]='0'*64
        with self.assertRaisesRegex(ValueError,'instrument_source_hash_mismatch'):run(inputs)

    def test_wrong_reply_base_packet_hash_not_repaired(self):
        inputs=fixture();parsed=subject.wire.parse_json(content(inputs));parsed['base_packet_sha256']='0'*64
        setcontent(inputs,subject.wire.canonical(parsed).decode())
        choice=run(inputs)['rows'][0]['choices'][0]
        self.assertEqual(choice['failure']['code'],'reply_base_packet_binding_mismatch')
        self.assertEqual(choice['first_content_utf8'],content(inputs))

    def test_invalid_graph_tree_is_failure_per_choice(self):
        inputs=fixture();parsed=subject.wire.parse_json(content(inputs));parsed['nodes'][0]['children']=['absent']
        setcontent(inputs,subject.wire.canonical(parsed).decode())
        self.assertFalse(run(inputs)['rows'][0]['all_choices_mechanically_valid'])

    def test_text_json_projection_declared_loss_and_exact_text_equality(self):
        inputs=fixture();index=next(i for i,r in enumerate(subject.wire.parse_json(inputs[1])['requests']) if r['arm']['representation']=='text_plus_json')
        good=run(inputs)['rows'][index]['choices'][0]
        self.assertTrue(good['mechanical_validity'])
        self.assertFalse(good['compiler_input_is_original_content'])
        self.assertTrue(good['transformation_report']['loss'])
        parsed=subject.wire.parse_json(content(inputs,index));parsed['text']='Different answer.'
        setcontent(inputs,subject.wire.canonical(parsed).decode(),index)
        bad=run(inputs)['rows'][index]['choices'][0]
        self.assertEqual(bad['failure']['code'],'text_annotation_mismatch')
        self.assertEqual(bad['first_content_utf8'],content(inputs,index))

    def test_text_projection_requires_component_in_arm(self):
        inputs=list(fixture());doc=subject.wire.parse_json(inputs[1]);index=next(i for i,r in enumerate(doc['requests']) if r['arm']['representation']=='text_plus_json')
        row=doc['requests'][index];row['arm']['components']=[]
        row['request_sha256']=subject.digest({k:v for k,v in row.items() if k!='request_sha256'})
        inputs[1]=subject.wire.canonical(doc)
        inputs[3]['sources']['manifest']['sha256']=subject.sha(inputs[1])
        for request in inputs[0]['requests']:request['metadata']['source_manifest_sha256']=subject.sha(inputs[1])
        result=run(inputs)['rows'][index]['choices'][0]
        self.assertEqual(result['failure']['code'],'text_projection_not_declared_in_arm')

    def test_ambiguous_nonstring_content_retained(self):
        inputs=fixture();value=[{'type':'text','text':'invented'}];setcontent(inputs,value)
        choice=run(inputs)['rows'][0]['choices'][0]
        self.assertEqual(choice['choice']['message']['content'],value)
        self.assertEqual(choice['failure']['code'],'ambiguous_or_nonstring_content')

    def test_lone_surrogate_failure_still_exports_later_valid_results(self):
        inputs=fixture();setcontent(inputs,'\ud800')
        raw=subject.json.dumps(inputs[0],ensure_ascii=True).encode('ascii')
        result=subject.evaluate(raw,inputs[1],inputs[2],inputs[3])
        self.assertEqual(result['all_choices_valid_requests'],11)
        choice=result['rows'][0]['choices'][0]
        self.assertEqual(choice['choice']['message']['content'],'\ud800')
        self.assertEqual(choice['failure']['type'],'UnicodeEncodeError')
        self.assertIsNone(choice['first_content_sha256'])
        encoded=subject.serialize_report(result)
        self.assertIn(b'\\ud800',encoded)
        self.assertEqual(subject.wire.parse_json(encoded)['rows'][0]['choices'][0]['choice']['message']['content'],'\ud800')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);args=[]
            for name,value in {'normalized':raw,'source-manifest':inputs[1],'config':inputs[2],
                               'policy':subject.wire.canonical(inputs[3])}.items():
                path=root/(name+'.json');path.write_bytes(value);args.extend(['--'+name,str(path)])
            output=root/'result.json';args.extend(['--output',str(output)])
            self.assertEqual(subject.main(args),0)
            self.assertEqual(subject.wire.parse_json(output.read_bytes())['all_choices_valid_requests'],11)

    def test_ambiguous_content_envelope_rejected(self):
        inputs=fixture();inputs[0]['responses'][0]['projection']['answers']={}
        choice=run(inputs)['rows'][0]['choices'][0]
        self.assertIn('ambiguous_content_envelope',choice['binding_errors'])
        self.assertFalse(choice['mechanical_validity'])

    def test_missing_observed_model_never_falls_back_to_configured_model(self):
        inputs=fixture();del inputs[0]['responses'][0]['projection']['model'];del inputs[0]['rows'][0]['response_model']
        choice=run(inputs)['rows'][0]['choices'][0]
        self.assertIn('observed_model_identity_missing',choice['binding_errors'])
        self.assertFalse(choice['mechanical_validity'])

    def test_billing_latency_http_flags_retained_separate_from_mechanics(self):
        inputs=fixture();inputs[0]['rows'][0].update(billing_replay_verified=False,http_status=502,actual_cost_usd=None,completed=False)
        row=run(inputs)['rows'][0]
        self.assertTrue(row['all_choices_mechanically_valid'])
        self.assertFalse(row['billing_or_identity_verification_performed_here'])
        self.assertEqual(row['billing_and_resource_rows'],[inputs[0]['rows'][0]])

    def test_global_denominator_and_unmapped_response_qualified_count_zero(self):
        inputs=fixture();inputs[0]['planned_operations']=13
        inputs[0]['responses'].append({'operation_id':'unplanned','projection':{'choices':[]}})
        result=run(inputs)
        self.assertEqual(result['locally_all_choices_valid_requests'],12)
        self.assertEqual(result['all_choices_valid_requests'],0)
        self.assertEqual(len(result['unmapped_responses']),1)

    def test_evaluator_selection_is_data_not_stage_id(self):
        inputs=fixture();inputs[0]['stage_id']='frontier-comparison-looking-label'
        self.assertEqual(run(inputs)['evaluator_kind'],'graph_reply')

    def test_cli_uses_public_files_and_refuses_output_overwrite(self):
        inputs=fixture()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);names={'normalized':subject.wire.canonical(inputs[0]),'source-manifest':inputs[1],
                'config':inputs[2],'policy':subject.wire.canonical(inputs[3])}
            args=[]
            for name,raw in names.items():
                path=root/(name+'.json');path.write_bytes(raw);args.extend(['--'+name,str(path)])
            output=root/'result.json';args.extend(['--output',str(output)])
            self.assertEqual(subject.main(args),0)
            self.assertEqual(subject.wire.parse_json(output.read_bytes())['all_choices_valid_requests'],12)
            with self.assertRaises(FileExistsError):subject.main(args)


if __name__=='__main__':
    unittest.main()
