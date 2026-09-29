"""T5 mechanism checks. No browser, native graph or model is exercised."""
from copy import deepcopy
import concurrent.futures
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from workspace_ref import Workspace, WorkspaceError, EXAMPLE_PATH, same, encoded

FIXTURE=json.loads(EXAMPLE_PATH.read_text())
PRESET=json.loads((EXAMPLE_PATH.parent/'workspace_presets_v1.json').read_text())['presets'][0]


def nodes(doc):
    stack=[doc['root']]
    while stack:
        node=stack.pop();yield node
        stack.extend(node.get('children',[]))


def node(doc,id):return next(x for x in nodes(doc) if x['id']==id)


def event(owner='history',parameter='selection',value='e_new',id='event-1',scope='analysis',**kwargs):
    return dict(id=id,scope=scope,writes=[dict(owner=owner,parameter=parameter,value=value)],**kwargs)


def edge(id,src,dst,op='identity',args=None,scope='analysis',enabled=True):
    return dict(id=id,source=dict(owner=src[0],parameter=src[1]),target=dict(owner=dst[0],parameter=dst[1]),
                transform=dict(op=op,args=args or {}),scope=scope,enabled=enabled)


class WorkspaceTests(unittest.TestCase):
    def setUp(self):self.doc=deepcopy(FIXTURE);self.ws=Workspace(self.doc)

    def error(self,code,fn,*args,**kwargs):
        with self.assertRaises(WorkspaceError) as cm:fn(*args,**kwargs)
        self.assertEqual(cm.exception.code,code)

    def test_five_graphs_plus_table_and_cards(self):
        renderers=[n['renderer'] for n in nodes(self.doc) if n['kind']=='view']
        self.assertEqual(renderers.count('graph'),5)
        self.assertIn('table',renderers);self.assertIn('cards',renderers)
        self.assertIn('tabs',[n.get('container_kind') for n in nodes(self.doc)])

    def test_shared_selection_and_frozen_reference(self):
        report=self.ws.dispatch(event())
        for name in ['workbench','history','architecture','evidence','context','table','cards']:
            self.assertEqual(self.ws.value(name,'selection'),'e_new')
        self.assertEqual(self.ws.value('reference','selection'),'none')
        self.assertIn({'binding':'select-to-reference','status':'frozen'},report['trace'])

    def test_depth_only_changes_bound_views(self):
        self.ws.dispatch(event(parameter='depth',value=3))
        self.assertEqual(self.ws.value('architecture','depth'),3)
        self.assertEqual(self.ws.value('context','depth'),1)
        self.assertEqual(self.ws.value('history','detail'),'summary')

    def test_detail_remains_local(self):
        self.ws.dispatch(event(parameter='detail',value='raw'))
        self.assertEqual(self.ws.value('history','detail'),'raw')
        self.assertEqual(self.ws.value('architecture','detail'),'summary')

    def test_wildcard_time_follows_review_scope(self):
        self.ws.dispatch(event('workbench','time',[20,40],scope='review'))
        self.assertEqual(self.ws.value('table','time'),[20,40])
        self.assertEqual(self.ws.value('reference','time'),[0,100])

    def test_other_scope_does_not_change_selection(self):
        report=self.ws.dispatch(event(scope='review'))
        self.assertEqual(self.ws.value('history','selection'),'e_new')
        self.assertEqual(self.ws.value('table','selection'),'none')
        self.assertTrue(any(x['status']=='different_scope' for x in report['trace']))

    def test_detach_then_reconnect_is_explicit(self):
        self.ws.bind_enabled('select-to-architecture',False)
        self.ws.dispatch(event())
        self.assertEqual(self.ws.value('architecture','selection'),'none')
        self.ws.bind_enabled('select-to-architecture',True)
        self.assertEqual(self.ws.value('architecture','selection'),'none')
        self.ws.dispatch(event(id='resync'))
        self.assertEqual(self.ws.value('architecture','selection'),'e_new')

    def test_thaw_does_not_implicitly_catch_up(self):
        self.ws.dispatch(event())
        self.ws.freeze('reference',False)
        self.assertEqual(self.ws.value('reference','selection'),'none')
        self.ws.dispatch(event(id='resync'))
        self.assertEqual(self.ws.value('reference','selection'),'e_new')

    def test_frozen_container_blocks_descendants(self):
        self.ws.freeze('tabs')
        self.ws.dispatch(event('workbench'))
        self.assertEqual(self.ws.value('history','selection'),'none')
        self.assertEqual(self.ws.value('architecture','selection'),'none')
        self.assertEqual(self.ws.value('table','selection'),'e_new')
        self.error('direct_write_frozen',self.ws.dispatch,event(id='bad'))

    def test_direct_reference_write_fails_atomically(self):
        before=self.ws.document()
        self.error('direct_write_frozen',self.ws.dispatch,event('reference'))
        self.assertEqual(self.ws.document(),before)

    def test_freeze_workspace(self):
        self.ws.freeze('workbench')
        self.error('direct_write_frozen',self.ws.dispatch,event())

    def test_type_error_rolls_back_all_writes(self):
        e=event();e['writes'].append(dict(owner='history',parameter='depth',value=True))
        before=self.ws.document();rev=self.ws.revision
        self.error('parameter_type',self.ws.dispatch,e)
        self.assertEqual(self.ws.document(),before);self.assertEqual(self.ws.revision,rev)

    def test_bounds_error(self):self.error('parameter_bounds',self.ws.dispatch,event(parameter='opacity',value=1.1))
    def test_enum_error(self):self.error('parameter_enum',self.ws.dispatch,event(parameter='detail',value='ultra'))
    def test_unknown_parameter(self):self.error('parameter_missing',self.ws.dispatch,event(parameter='wat'))
    def test_nonfinite(self):self.error('not_finite_json',self.ws.dispatch,event(value=float('nan')))
    def test_boolean_not_integer(self):self.assertFalse(same(True,1))
    def test_numeric_encoding_is_explicit(self):self.assertFalse(same(1,1.0))
    def test_key_order_does_not_matter(self):self.assertTrue(same({'x':1,'y':2},{'y':2,'x':1}))
    def test_non_json_object(self):self.error('not_finite_json',encoded,{1:'x'})

    def test_identity_cycle_converges(self):
        report=self.ws.dispatch(event())
        self.assertEqual(report['status'],'committed')
        self.assertLess(len(report['trace']),20)

    def test_inconsistent_cycle_rejected_atomically(self):
        self.doc['bindings'] += [edge('return-depth',('architecture','depth'),('history','depth'),'affine',{'offset':1})]
        ws=Workspace(self.doc);before=ws.document()
        self.error('conflicting_proposals',ws.dispatch,event(parameter='depth',value=3))
        self.assertEqual(ws.document(),before)

    def test_diamond_equal_coalesces(self):
        self.doc['bindings'].append(edge('diamond',('architecture','selection'),('cards','selection')))
        ws=Workspace(self.doc);ws.dispatch(event())
        self.assertEqual(ws.value('cards','selection'),'e_new')

    def test_diamond_disagree_does_not_pick_last(self):
        self.doc['bindings'].append(edge('diamond',('architecture','selection'),('cards','selection'),'lookup',{'pairs':[{'from':'e_new','to':'other'}]}))
        for reverse in [False,True]:
            if reverse:self.doc['bindings'].reverse()
            ws=Workspace(self.doc);before=ws.document()
            self.error('conflicting_proposals',ws.dispatch,event())
            self.assertEqual(ws.document(),before)

    def test_order_of_binding_array_is_irrelevant(self):
        reverse=deepcopy(self.doc);reverse['bindings'].reverse()
        a=self.ws.dispatch(event());b=Workspace(reverse).dispatch(event())
        self.assertEqual(a,b)

    def test_conflicting_seeds(self):
        e=event();e['writes'].append(dict(owner='table',parameter='selection',value='other'))
        self.error('conflicting_proposals',self.ws.dispatch,e)
        self.assertEqual(self.ws.value('history','selection'),'none')

    def test_equal_seeds(self):
        e=event();e['writes'].append(dict(owner='table',parameter='selection',value='e_new'))
        self.ws.dispatch(e);self.assertEqual(self.ws.value('cards','selection'),'e_new')

    def test_frozen_target_cannot_relay(self):
        self.doc['bindings']=[edge('into-ref',('history','selection'),('reference','selection')),
                              edge('out-ref',('reference','selection'),('table','selection'))]
        ws=Workspace(self.doc);ws.dispatch(event())
        self.assertEqual(ws.value('table','selection'),'none')

    def test_resource_bound_rolls_back(self):
        self.doc['limits']['max_proposals']=1
        ws=Workspace(self.doc);before=ws.document()
        self.error('proposal_budget_exceeded',ws.dispatch,event())
        self.assertEqual(ws.document(),before)

    def test_affine(self):
        self.doc['bindings']=[edge('scale',('history','depth'),('architecture','depth'),'affine',{'factor':2,'offset':1})]
        ws=Workspace(self.doc);ws.dispatch(event(parameter='depth',value=3))
        self.assertEqual(ws.value('architecture','depth'),7)

    def test_transform_target_bounds_rolls_back(self):
        self.doc['bindings']=[edge('scale',('history','depth'),('architecture','depth'),'affine',{'factor':100})]
        ws=Workspace(self.doc);before=ws.document()
        self.error('parameter_bounds',ws.dispatch,event(parameter='depth',value=3))
        self.assertEqual(ws.document(),before)

    def test_overflow_rejected(self):
        self.doc['bindings']=[edge('huge',('history','opacity'),('architecture','opacity'),'affine',{'factor':1e308})]
        self.doc['root']['extensions']={}
        node(self.doc,'history')['parameters']['opacity']['constraints']={}
        ws=Workspace(self.doc)
        self.error('not_finite_json',ws.dispatch,event(parameter='opacity',value=1e308))

    def test_lookup(self):
        self.doc['bindings']=[edge('lookup',('history','selection'),('architecture','selection'),'lookup',{'pairs':[{'from':'old_project','to':'new_component'}]})]
        ws=Workspace(self.doc);ws.dispatch(event(value='old_project'))
        self.assertEqual(ws.value('architecture','selection'),'new_component')
        self.assertEqual(ws.document()['data_ref'],'graph:fixture@1')

    def test_ambiguous_lookup(self):
        self.doc['bindings']=[edge('lookup',('history','selection'),('architecture','selection'),'lookup',{'pairs':[{'from':'e_new','to':'a'},{'from':'e_new','to':'b'}]})]
        ws=Workspace(self.doc)
        self.error('transform_ambiguous_lookup',ws.dispatch,event())

    def test_missing_lookup_no_invented_mapping(self):
        self.doc['bindings']=[edge('lookup',('history','selection'),('architecture','selection'),'lookup',{'pairs':[]})]
        ws=Workspace(self.doc);self.error('transform_missing_lookup',ws.dispatch,event())

    def test_unknown_transform_no_eval(self):
        self.doc['bindings'][0]['transform']['op']='__import__("os").system'
        self.error('transform_unavailable',Workspace,self.doc)

    def test_custom_trusted_transform(self):
        self.doc['bindings']=[edge('uppercase',('history','selection'),('architecture','selection'),'custom_upper')]
        ws=Workspace(self.doc,transforms={'custom_upper':lambda value,args:value.upper()})
        ws.dispatch(event(value='letter'));self.assertEqual(ws.value('architecture','selection'),'LETTER')

    def test_transform_cannot_replace_builtin(self):self.error('transform_registration',Workspace,self.doc,transforms={'identity':lambda v,a:v})

    def test_transform_failure_omits_payload(self):
        self.doc['bindings']=[edge('bad',('history','selection'),('architecture','selection'),'custom_bad')]
        def bad(v,a):raise RuntimeError('PRIVATE_VALUE')
        ws=Workspace(self.doc,transforms={'custom_bad':bad})
        self.error('transform_failure',ws.dispatch,event())
        self.assertEqual(ws.value('history','selection'),'none')

    def test_invalid_identity_args(self):
        self.doc['bindings'][0]['transform']['args']={'unused':True}
        self.error('transform_arguments',Workspace(self.doc).dispatch,event())

    def test_views_queries_and_original_are_not_mutated(self):
        before=deepcopy(self.doc)
        self.ws.dispatch(event())
        self.assertEqual(before,self.doc)
        for n in nodes(self.ws.document()):
            if n['kind']=='view':self.assertEqual(n['query'],node(before,n['id'])['query'])

    def test_value_and_export_are_detached_copies(self):
        a=self.ws.value('history','time');a.append(999)
        doc=self.ws.document();node(doc,'history')['parameters']['selection']['value']='bad'
        self.assertEqual(self.ws.value('history','time'),[0,100])
        self.assertEqual(self.ws.value('history','selection'),'none')

    def test_input_event_is_not_mutated(self):
        e=event();before=deepcopy(e);self.ws.dispatch(e);self.assertEqual(e,before)

    def test_document_roundtrip_preserves_values_detach_and_freeze(self):
        self.ws.dispatch(event());self.ws.bind_enabled('select-to-table',False);self.ws.freeze('tabs')
        snap=self.ws.document();other=Workspace(json.loads(json.dumps(snap)))
        self.assertEqual(other.document(),snap)
        other.dispatch(event('workbench',value='second'))
        self.assertEqual(other.value('table','selection'),'e_new')
        self.assertEqual(other.value('history','selection'),'e_new')

    def test_replayed_event_has_no_effect(self):
        e=event();self.ws.dispatch(e);rev=self.ws.revision
        self.ws.bind_enabled('select-to-cards',False)
        r=self.ws.dispatch(e)
        self.assertEqual(r['status'],'replayed');self.assertEqual(r['changes'],[])
        self.assertEqual(self.ws.revision,rev+1)

    def test_event_id_collision(self):
        self.ws.dispatch(event());self.error('event_id_collision',self.ws.dispatch,event(value='other'))

    def test_stale_revision(self):
        self.ws.dispatch(event());self.error('stale_revision',self.ws.dispatch,event(id='e2',expected_revision=0))

    def test_failed_event_can_be_corrected(self):
        self.error('parameter_bounds',self.ws.dispatch,event(parameter='depth',value=99))
        self.ws.dispatch(event(parameter='depth',value=2))
        self.assertEqual(self.ws.value('history','depth'),2)

    def test_noop_propagates_but_does_not_increment_revision(self):
        r=self.ws.dispatch(event(value='none'));self.assertEqual(r['changes'],[])
        self.assertTrue(r['trace']);self.assertEqual(self.ws.revision,0)

    def test_concurrent_revision_guard(self):
        def send(i):
            try:return self.ws.dispatch(event(id=f'e{i}',value=f'v{i}',expected_revision=0))['status']
            except WorkspaceError as e:return e.code
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results=list(pool.map(send,range(4)))
        self.assertEqual(results.count('committed'),1);self.assertEqual(results.count('stale_revision'),3)

    def test_interface_preset_keeps_all_other_profiles_and_parameters(self):
        before=self.ws.profiles();view=self.ws.document()['root']
        self.ws.apply_preset(PRESET,components=['interface'])
        after=self.ws.profiles()
        self.assertEqual(after['interface'],PRESET['components']['interface'])
        for k in before:
            if k!='interface':self.assertEqual(before[k],after[k])
        self.assertEqual(self.ws.document()['root'],view)

    def test_multiple_explicit_profile_components(self):
        self.ws.apply_preset(PRESET,components=['interface','context'])
        self.assertEqual(self.ws.profiles()['context'],PRESET['components']['context'])
        self.assertEqual(self.ws.profiles()['tools'],FIXTURE['profiles']['tools'])

    def test_preset_selection_must_be_explicit(self):
        with self.assertRaises(TypeError):self.ws.apply_preset(PRESET)
        self.error('preset_selection',self.ws.apply_preset,PRESET,components=[])

    def test_invalid_preset_is_atomic(self):
        p=deepcopy(PRESET);p['components']['context']['version']=3
        before=self.ws.profiles();self.error('profile_shape',self.ws.apply_preset,p,components=['interface','context'])
        self.assertEqual(before,self.ws.profiles())

    def test_preset_unknown_component(self):self.error('profile_missing',self.ws.apply_preset,PRESET,components=['unknown'])
    def test_profiles_detached_copy(self):
        p=self.ws.profiles();p['tools']['config'].clear()
        self.assertEqual(self.ws.capability('local_lookup'),'equivalent')

    def test_missing_tools_do_not_pretend_to_work(self):
        self.assertEqual(self.ws.capability('unlisted'),'unavailable')
        self.assertEqual(self.ws.capability('web'),'unavailable')
        self.ws.apply_preset(PRESET,components=['tools'])
        self.assertEqual(self.ws.capability('browser'),'unavailable')

    def test_arbitrary_container_kind_preserved(self):
        self.doc['root']['container_kind']='adaptive-owner-custom'
        self.assertEqual(Workspace(self.doc).document()['root']['container_kind'],'adaptive-owner-custom')

    def test_duplicate_node(self):
        node(self.doc,'table')['id']='history';self.error('duplicate_node_id',Workspace,self.doc)
    def test_duplicate_binding(self):
        self.doc['bindings'].append(deepcopy(self.doc['bindings'][0]));self.error('duplicate_binding_id',Workspace,self.doc)
    def test_missing_endpoint(self):
        self.doc['bindings'][0]['target']['owner']='missing';self.error('binding_endpoint_missing',Workspace,self.doc)
    def test_missing_scope(self):
        self.doc['bindings'][0]['scope']='missing';self.error('binding_scope_missing',Workspace,self.doc)
    def test_missing_frozen(self):
        self.doc['initial_frozen']=['missing'];self.error('frozen_node_missing',Workspace,self.doc)
    def test_missing_active_tab(self):
        node(self.doc,'tabs')['layout']['active_tab']='missing';self.error('active_tab_missing',Workspace,self.doc)
    def test_scope_star_reserved(self):
        self.doc['scopes'].append('*');self.error('wildcard_is_not_event_scope',Workspace,self.doc)
    def test_unknown_schema_field(self):
        self.doc['profile']='typo';self.error('workspace_schema',Workspace,self.doc)
    def test_bad_event_shape(self):self.error('event_shape',self.ws.dispatch,dict(id='x'))
    def test_bad_event_scope(self):self.error('event_scope_or_writes',self.ws.dispatch,event(scope='*'))
    def test_bad_revision_type(self):self.error('revision_type',self.ws.dispatch,event(expected_revision=True))
    def test_freeze_invalid(self):self.error('freeze_target',self.ws.freeze,'missing')
    def test_toggle_invalid(self):self.error('binding_toggle',self.ws.bind_enabled,'missing',False)

    def test_offline_execution(self):
        with patch('socket.socket',side_effect=AssertionError('network forbidden')):
            self.assertEqual(Workspace(self.doc).dispatch(event())['status'],'committed')

    def test_malformed_capability_declaration_is_unavailable(self):
        for malformed in [[], 'native', None, {'web': ['native']}]:
            self.doc['profiles']['tools']['config']['capabilities']=malformed
            self.assertEqual(Workspace(self.doc).capability('web'),'unavailable')

    def test_bad_capability_name(self):self.error('capability_name',self.ws.capability,[])

    def test_custom_transform_receives_copies(self):
        self.doc['bindings']=[edge('mutating',('history','time'),('architecture','time'),'custom_copy')]
        def fn(value,args):value.append(123);args['mutated']=True;return value
        ws=Workspace(self.doc,transforms={'custom_copy':fn});ev=event(parameter='time',value=[1,2])
        ws.dispatch(ev)
        self.assertEqual(ws.value('history','time'),[1,2])
        self.assertEqual(ws.value('architecture','time'),[1,2,123])
        self.assertEqual(ev['writes'][0]['value'],[1,2])
        self.assertEqual(ws.document()['bindings'][0]['transform']['args'],{})

    def test_random_identity_graphs_are_order_invariant(self):
        import random
        rng=random.Random(29)
        names=['history','architecture','evidence','context','table','cards']
        for trial in range(20):
            doc=deepcopy(self.doc);doc['bindings']=[]
            for i in range(25):
                a,b=rng.sample(names,2)
                doc['bindings'].append(edge(f'b{i}',(a,'selection'),(b,'selection')))
            before=Workspace(doc).dispatch(event())
            rng.shuffle(doc['bindings'])
            after=Workspace(doc).dispatch(event())
            self.assertEqual(before,after)

    def test_cli_demo_and_duplicate_key_rejection(self):
        script=Path(__file__).with_name('workspace_ref.py')
        result=subprocess.run([sys.executable,str(script),'--demo'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(len(json.loads(result.stdout)['event_results']),2)
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'bad.json';p.write_text('{"id":1,"id":2}')
            result=subprocess.run([sys.executable,str(script),str(p)],capture_output=True,text=True)
            self.assertEqual(result.returncode,1)
            self.assertEqual(json.loads(result.stdout)['error'],'input_error')


if __name__=='__main__':unittest.main()
