"""Scripted runtime mechanisms; these tests do not measure model quality or API access."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest

from loom.tools.structure.frontier_panel_v1 import adapter as a, runtime as rt
from loom.tools.structure import openrouter_runner as safe
from loom.tools.structure.agentic_graph_v1 import packet as codec


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.origin={'kind':'user','actor':'owner','model':None,'recipe_sha256':None,'response_sha256':None}
        self.packet=codec.make_packet(task={'scope':'whole_archive','goal':'find gaps'},origin=self.origin)
        self.presets=a.read(a.HERE/'runtime_presets.json')['presets']
        _,_,self.identities=a.load_presets()

    def request(self,kind,**changes):
        config=deepcopy(self.presets[kind]);config.update(changes)
        return rt.prepare_runtime(self.packet,config)

    def client_response(self,*,content='{}',calls=None):
        ident=self.identities['gpt61_sol']
        message={'role':'assistant','content':None if calls else content}
        if calls:message.update(tool_calls=calls,reasoning_details=[{'type':'reasoning.text','text':'preserved mechanism trace'}])
        return safe.canonical({'model':ident['model_aliases'][-1],'provider':ident['provider_aliases'][-1],
            'choices':[{'finish_reason':'tool_calls' if calls else 'stop','message':message}],
            'usage':{'cost':'.01','is_byok':False,'prompt_tokens':100,'completion_tokens':50}})

    @staticmethod
    def chat_tool(name='lookup'):
        return {'type':'function','function':{'name':name,'description':'Read supplied graph/source data',
                'parameters':{'type':'object','properties':{'id':{'type':'string'}},'required':['id']}}}

    @staticmethod
    def chat_call(ident='c1',name='lookup'):
        return {'id':ident,'type':'function','function':{'name':name,'arguments':'{"id":"source1"}'}}

    def managed_response(self,request,output,status='completed'):
        return safe.canonical({'model':request['config']['model'],'status':status,'output':output,
            'usage':{'input_tokens':100,'output_tokens':80,'output_tokens_details':{'reasoning_tokens':40}}})

    def test_local_runtime_uses_same_packet_and_zero_model_cost(self):
        request=self.request('local');self.assertEqual(request['body'],self.packet)
        with tempfile.TemporaryDirectory() as d:
            ledger=rt.execute_scripted(request,Path(d)/'run',lambda current:safe.canonical(codec.empty_diff(self.packet,proposal_id='d',origin=self.origin)))
            self.assertEqual(ledger['stopped_reason'],'completed');self.assertEqual(ledger['retained_budget_charge_usd'],'0')
            replay=rt.replay_scripted(Path(d)/'run');self.assertEqual(len(replay['outputs']),1)
            self.assertEqual(replay['network_calls'],0)

    def test_client_tool_loop_preserves_tools_reasoning_and_all_first_results(self):
        tools=[self.chat_tool()];request=self.request('client_loop',tools=tools,tool_permissions={'lookup':{'execute':True,'scope':'supplied_packet'}})
        calls=[];handled=[]
        def script(current):
            calls.append(current)
            if len(calls)==1:return self.client_response(calls=[self.chat_call()])
            return self.client_response(content='{"final":"candidate only"}')
        with tempfile.TemporaryDirectory() as d:
            ledger=rt.execute_scripted(request,Path(d)/'run',script,tool_handlers={'lookup':lambda args,context:handled.append(args) or {'raw':'retained'}})
            self.assertEqual(len(calls),2);self.assertEqual(handled,[{'id':'source1'}])
            self.assertEqual(calls[1]['body']['tools'],tools)
            self.assertEqual(calls[1]['body']['messages'][-1]['role'],'tool')
            assistant=calls[1]['body']['messages'][-2]
            self.assertEqual(assistant['reasoning_details'],[{'type':'reasoning.text','text':'preserved mechanism trace'}])
            self.assertEqual(assistant['tool_calls'],[self.chat_call()])
            self.assertEqual(ledger['retained_budget_charge_usd'],'0.02')
            replay=rt.replay_scripted(Path(d)/'run');self.assertEqual(len(replay['outputs']),2)
            self.assertEqual(replay['outputs'][-1]['final_content'],'{"final":"candidate only"}')

    def test_undeclared_tool_is_not_dispatched(self):
        request=self.request('client_loop',tools=[self.chat_tool()]);handled=[]
        with tempfile.TemporaryDirectory() as d:
            ledger=rt.execute_scripted(request,Path(d)/'run',lambda current:self.client_response(calls=[self.chat_call(name='erase')]),tool_handlers={'erase':lambda args,context:handled.append(args)})
            self.assertEqual(handled,[]);self.assertEqual(ledger['stopped_reason'],'stage_failure_no_retry')
            self.assertEqual(ledger['attempts'][0]['reported_cost_usd'],'0.01')

    def test_duplicate_tool_ids_fail_before_any_handler(self):
        request=self.request('client_loop',tools=[self.chat_tool()]);handled=[]
        with tempfile.TemporaryDirectory() as d:
            ledger=rt.execute_scripted(request,Path(d)/'run',lambda current:self.client_response(calls=[self.chat_call(),self.chat_call()]),tool_handlers={'lookup':lambda args,context:handled.append(args)})
            self.assertEqual(handled,[]);self.assertEqual(ledger['stopped_reason'],'stage_failure_no_retry')

    def test_managed_swarm_official_beta_shape_and_no_fixed_concurrency_ceiling(self):
        request=self.request('managed_swarm',max_concurrent_subagents=10000)
        self.assertEqual(request['headers']['OpenAI-Beta'],'responses_multi_agent=v1')
        self.assertEqual(request['body']['multi_agent'],{'enabled':True,'max_concurrent_subagents':10000})
        self.assertEqual(safe.parse_json(request['body']['input'][1]['content']),self.packet)
        self.assertFalse(request['account_availability_verified'])
        self.assertFalse(request['persistent_agents_api_session'])

    def test_hosted_swarm_cannot_pretend_to_have_mixed_model_subagents(self):
        with self.assertRaisesRegex(ValueError,'share_model_use_loom'):
            self.request('managed_swarm',subagent_models=['gpt','claude'])

    def test_managed_unsupported_beta_parameters_are_honest(self):
        with self.assertRaisesRegex(ValueError,'parameter_not_supported'):
            self.request('managed_swarm',reasoning={'effort':'low','summary':'auto'})
        with self.assertRaisesRegex(ValueError,'parameter_not_supported'):
            self.request('managed_swarm',max_tool_calls=100)

    def test_hosted_collaboration_actions_are_not_client_tools(self):
        request=self.request('managed_swarm');output=[
            {'type':'multi_agent_call','name':'spawn_agent','arguments':'{"task":"audit"}'},
            {'type':'message','agent':{'agent_name':'/root/child'},'phase':'final_answer','content':[{'type':'output_text','text':'child proposal'}]},
            {'type':'message','agent':{'agent_name':'/root'},'phase':'analysis','content':[{'type':'output_text','text':'intermediate'}]},
            {'type':'message','agent':{'agent_name':'/root'},'phase':'final_answer','content':[{'type':'output_text','text':'root synthesis'}]}]
        parsed=rt.parse_runtime_response(self.managed_response(request,output),request)
        self.assertEqual(parsed['pending_tools'],[]);self.assertEqual(parsed['final_content'],'root synthesis')
        self.assertEqual(parsed['continuation_items'],output)
        self.assertIsNone(parsed['reported_cost_usd'])

    def test_managed_function_loop_uses_function_call_output_and_retains_unknown_cost(self):
        tool={'type':'function','name':'lookup','parameters':{'type':'object','properties':{'id':{'type':'string'}},'required':['id']}}
        request=self.request('managed_agent',tools=[tool],tool_permissions={'lookup':{'execute':True,'scope':'supplied_packet'}});sent=[]
        def script(current):
            sent.append(current)
            if len(sent)==1:return self.managed_response(request,[{'type':'function_call','call_id':'c1','name':'lookup','arguments':'{"id":"a"}'}])
            return self.managed_response(request,[{'type':'message','content':[{'type':'output_text','text':'final proposal'}]}])
        with tempfile.TemporaryDirectory() as d:
            ledger=rt.execute_scripted(request,Path(d)/'run',script,tool_handlers={'lookup':lambda args,context:{'supplied':args}})
            self.assertEqual(len(sent),2)
            self.assertEqual(sent[1]['body']['input'][-1],{'type':'function_call_output','call_id':'c1','output':'{"supplied":{"id":"a"}}'})
            self.assertEqual(ledger['retained_budget_charge_usd'],'0.2')
            self.assertTrue(all(r['reported_cost_usd'] is None for r in ledger['attempts']))
            self.assertEqual(len(rt.replay_scripted(Path(d)/'run')['outputs']),2)

    def test_managed_in_progress_without_final_is_not_complete(self):
        request=self.request('managed_agent')
        parsed=rt.parse_runtime_response(self.managed_response(request,[],status='in_progress'),request)
        self.assertEqual(parsed['state'],'rejected')

    def test_configurable_no_step_limit_stops_on_declared_completion(self):
        request=self.request('local',max_steps=None,budget_usd='1000000000')
        with tempfile.TemporaryDirectory() as d:
            ledger=rt.execute_scripted(request,Path(d)/'run',lambda current:b'{}')
            self.assertEqual(ledger['stopped_reason'],'completed');self.assertEqual(len(ledger['attempts']),1)

    def test_ambiguous_callback_is_never_reissued(self):
        request=self.request('client_loop');counter=[]
        def broken(current):counter.append(1);raise RuntimeError('simulated transport interruption')
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'run';ledger=rt.execute_scripted(request,path,broken)
            self.assertEqual(counter,[1]);self.assertEqual(ledger['attempts'][0]['state'],'uncertain')
            with self.assertRaises(FileExistsError):rt.execute_scripted(request,path,broken)
            self.assertEqual(counter,[1]);self.assertEqual(ledger['retained_budget_charge_usd'],'0.1')

    def test_semantic_identity_failure_preserves_provider_cost(self):
        request=self.request('client_loop');value=safe.parse_json(self.client_response());value['model']='unobserved'
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'run';ledger=rt.execute_scripted(request,path,lambda current:safe.canonical(value))
            self.assertEqual(ledger['attempts'][0]['state'],'rejected')
            self.assertEqual(ledger['attempts'][0]['reported_cost_usd'],'0.01')
            self.assertEqual(ledger['retained_budget_charge_usd'],'0.01')
            self.assertEqual(rt.replay_scripted(path)['outputs'][0]['state'],'replay_rejected')

    def test_first_response_hash_tampering_rejected(self):
        request=self.request('local')
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'run';rt.execute_scripted(request,path,lambda current:b'{}')
            response=path/'call0001.response.bin';response.write_bytes(b'{"changed":true}')
            with self.assertRaisesRegex(a.IdentityIntegrityError,'response_hash_drift'):rt.replay_scripted(path)

    def test_initial_semantic_contract_content_hash_is_recomputed(self):
        request=self.request('local')
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'run';rt.execute_scripted(request,path,lambda current:b'{}')
            initial=a.read(path/'initial_request.json')
            initial['semantic_contract']='altered.domain/1'
            rt._persist(path/'initial_request.json',initial)
            with self.assertRaisesRegex(a.IdentityIntegrityError,'initial_request_drift'):rt.replay_scripted(path)

    def test_first_tool_result_survives_second_ambiguous_failure(self):
        request=self.request('client_loop',tools=[self.chat_tool('first'),self.chat_tool('second')],
            tool_permissions={'first':{'execute':True},'second':{'execute':True}})
        calls=[self.chat_call('c1','first'),self.chat_call('c2','second')];events=[]
        def first(args,context):events.append('first');return {'action_receipt':'retained'}
        def second(args,context):events.append('second');raise RuntimeError('interrupted tool')
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'run'
            ledger=rt.execute_scripted(request,path,lambda current:self.client_response(calls=calls),
                                       tool_handlers={'first':first,'second':second})
            self.assertEqual(events,['first','second']);self.assertEqual(len(ledger['attempts']),1)
            row=ledger['attempts'][0]
            self.assertEqual([t['state'] for t in row['tool_attempts']],['completed','uncertain'])
            self.assertEqual(a.read(path/row['tool_results_file'])[0]['output'],{'action_receipt':'retained'})
            self.assertEqual(ledger['stopped_reason'],'stage_failure_no_retry')
            replay=rt.replay_scripted(path)
            self.assertEqual([t['state'] for t in replay['outputs'][0]['tool_execution_receipts']],['completed','uncertain'])
            self.assertEqual(events,['first','second'])

    def test_unserializable_tool_return_retains_ambiguous_start_without_retry(self):
        request=self.request('client_loop',tools=[self.chat_tool()],tool_permissions={'lookup':{'execute':True}})
        executed=[]
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'run'
            ledger=rt.execute_scripted(request,path,lambda current:self.client_response(calls=[self.chat_call()]),
                                       tool_handlers={'lookup':lambda args,context:executed.append(1) or object()})
            self.assertEqual(executed,[1]);row=ledger['attempts'][0]
            self.assertEqual(row['tool_attempts'][0]['state'],'uncertain')
            self.assertEqual(a.read(path/row['tool_results_file']),[])
            self.assertEqual(rt.replay_scripted(path)['outputs'][0]['tool_execution_receipts'][0]['state'],'uncertain')
            self.assertEqual(executed,[1])

    def test_completed_tool_result_tampering_is_rejected(self):
        request=self.request('client_loop',tools=[self.chat_tool()],tool_permissions={'lookup':{'execute':True}},max_steps=1)
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'run'
            ledger=rt.execute_scripted(request,path,lambda current:self.client_response(calls=[self.chat_call()]),
                                       tool_handlers={'lookup':lambda args,context:{'fact':'original'}})
            tool=ledger['attempts'][0]['tool_attempts'][0]
            receipt=a.read(path/tool['result_file']);receipt['output']={'fact':'changed'}
            rt._persist(path/tool['result_file'],receipt)
            with self.assertRaisesRegex(a.IdentityIntegrityError,'tool_result_drift'):rt.replay_scripted(path)

    def test_rehashed_attempt_cannot_change_recorded_continuation(self):
        request=self.request('client_loop',tools=[self.chat_tool()],tool_permissions={'lookup':{'execute':True}});sent=[]
        def call(current):
            sent.append(current)
            return self.client_response(calls=[self.chat_call()]) if len(sent)==1 else self.client_response()
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'run';rt.execute_scripted(request,path,call,tool_handlers={'lookup':lambda args,context:{}})
            changed=a.read(path/'call0002.request.json');changed['semantic_contract']='altered.domain/1'
            changed['request_sha256']=safe.digest({k:v for k,v in changed.items() if k!='request_sha256'})
            rt._persist(path/'call0002.request.json',changed)
            ledger=a.read(path/'ledger.json');ledger['attempts'][1]['request_sha256']=safe.digest(changed)
            rt._persist(path/'ledger.json',ledger)
            with self.assertRaisesRegex(a.IdentityIntegrityError,'attempt_request_drift'):rt.replay_scripted(path)

    def test_runtime_extensions_do_not_require_core_branch_edits(self):
        def build(packet,config):value=rt._base(packet,config);value.update(body=packet);return value
        rt.register_runtime('custom_archive_solver',build,lambda raw,request:{'state':'completed'})
        config=deepcopy(self.presets['local']);config['kind']='custom_archive_solver'
        request=rt.prepare_runtime(self.packet,config)
        self.assertEqual(request['runtime_kind'],'custom_archive_solver')
        self.assertEqual(rt.parse_runtime_response(b'{}',request),{'state':'completed'})

    def test_zero_unknown_baseline_cost_is_not_fabricated(self):
        request=self.request('managed_agent');parsed=rt.parse_runtime_response(self.managed_response(request,[]),request)
        self.assertIsNone(parsed['reported_cost_usd']);self.assertEqual(parsed['cost_status'],'unknown')

    def test_shared_packet_and_tool_capability_do_not_grant_project_access(self):
        request=self.request('client_loop',tools=[self.chat_tool()]);handled=[]
        with tempfile.TemporaryDirectory() as d:
            ledger=rt.execute_scripted(request,Path(d)/'run',lambda current:self.client_response(calls=[self.chat_call()]),
                tool_handlers={'lookup':lambda args,context:handled.append((args,context))})
            self.assertEqual(handled,[])
            self.assertEqual(ledger['stopped_reason'],'stage_failure_no_retry')
            self.assertEqual(request['resource_scope']['project_access_grants'],[])
            self.assertTrue(request['tool_capability_is_not_execution_permission'])

    def test_executor_receives_explicit_scope_contract_and_permission(self):
        request=self.request('client_loop',tools=[self.chat_tool()],
            tool_permissions={'lookup':{'execute':True,'scope':'watchdog_public_dataset'}},
            semantic_contract='watchdog.research.protocol/1',resource_scope={'project':'WatchDog','resources':['public_dataset']})
        contexts=[];sent=[]
        def call(current):
            sent.append(current)
            return self.client_response(calls=[self.chat_call()]) if len(sent)==1 else self.client_response(content='{}')
        with tempfile.TemporaryDirectory() as d:
            ledger=rt.execute_scripted(request,Path(d)/'run',call,tool_handlers={'lookup':lambda args,context:contexts.append(context) or {}})
            self.assertEqual(ledger['stopped_reason'],'completed')
            self.assertEqual(contexts[0]['semantic_contract'],'watchdog.research.protocol/1')
            self.assertEqual(contexts[0]['resource_scope'],{'project':'WatchDog','resources':['public_dataset']})
            self.assertEqual(contexts[0]['permission']['scope'],'watchdog_public_dataset')


if __name__=='__main__':unittest.main()
