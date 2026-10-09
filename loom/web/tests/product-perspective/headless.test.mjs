import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from './loader.mjs';
const loader = await createModuleLoader();
const m = await loader.load('src/workspace/knowledge-perspective.ts');
const { NativeProfileSession } = await loader.load('src/api/onboarding-host.ts');
test.after(() => loader.close());
const entities = [{id:'a',kind:'entity',label:'A',confidence:1,evidence_class:'observed',attrs:{unknown:{keep:[2,3]}}},{id:'b',kind:'entity',label:'B',evidence_class:'inferred'},{id:'c',kind:'entity',label:'C',evidence_class:'unknown'}];
const claims = [{id:'ab',subject:'a',object:'b',predicate:'links',assessment:{evidence_class:'inferred'}},{id:'ac',subject:'a',object:'c',predicate:'links',assessment:{evidence_class:'observed'}}];
const resolution = () => ({components:m.perspectivePack.entries.map(entry=>({id:entry.key,value:structuredClone(entry.value),status:'effective',origin:{test:'native_fixture'}}))});
function fixture(options={}) {
 let reads=0;
 const source=m.createKnowledgePerspectiveSource({async query(what) {reads++; if(options.failure)throw new Error('offline');return {run:options.run??'r',items:structuredClone(what==='entities'?entities:claims),has_more:options.partial??false};}},'r',100);
 return {source,get reads(){return reads;},permission:{id:'explicit-local-test',canRead:ref=>ref.source===source.adapter.descriptor.id}};
}
const perspective=source=>({schema:'loom.graph_perspective/1',id:'test',components:{},focus:source.ref('a')});

test('actual product adapter selects before rendering and preserves source identity and unknown fields',async()=>{
 const f=fixture();const out=await m.selectKnowledgePerspective(perspective(f.source),resolution(),f.source,f.permission);
 assert.equal(f.reads,2);assert.equal(out.objects.length,3);assert.equal(out.graph.edges.length,2);
 assert.deepEqual(out.objects.find(item=>item.ref.selector==='a').properties.raw.attrs.unknown,{keep:[2,3]});
 assert.equal(out.analysis.length,0);assert.equal(out.complete,true);
 const rendered=m.perspectiveDataset(out);assert.deepEqual(rendered.entities.map(row=>row.id),out.objects.map(item=>item.key));
 assert.deepEqual(rendered.claims.map(row=>[row.subject,row.object]),out.graph.edges.map(edge=>[edge.src,edge.dst]));
 assert.equal(out.objects.find(item=>item.ref.selector==='b').confidence,undefined);
 assert.equal(rendered.entities[0].confidence,undefined);assert.equal(out.objects.find(item=>item.ref.selector==='a').properties.raw.confidence,1);
});
test('denied headless access performs zero real loader queries',async()=>{
 const f=fixture();const out=await m.selectKnowledgePerspective(perspective(f.source),resolution(),f.source,{id:'deny',canRead:()=>false});
 assert.equal(f.reads,0);assert.equal(out.objects[0].status,'denied');assert.equal(out.complete,false);
});
test('native effective data changes render budget without modifying source or analysis selection',async()=>{
 const f=fixture(), r=resolution();r.components.find(row=>row.id==='graph.perspective.budget').value.render=1;
 const out=await m.selectKnowledgePerspective(perspective(f.source),r,f.source,f.permission);
 assert.equal(out.objects.length,1);assert.ok(out.omissions.some(item=>item.reason==='render_budget'));
 assert.deepEqual(out.analysis,[]);assert.deepEqual((await f.source.load()).entities,entities);
});
test('required native exclusion is an error, never restored by a hand-written default',async()=>{
 const f=fixture(), r=resolution();const budget=r.components.find(row=>row.id==='graph.perspective.budget');budget.status='excluded';delete budget.value;
 await assert.rejects(m.selectKnowledgePerspective(perspective(f.source),r,f.source,f.permission),/budget_query_invalid/);assert.equal(f.reads,0);
});
test('prefix, unavailable and wrong-version statuses remain explicit even with no traversal',async()=>{
 const f=fixture({partial:true}),r=resolution();r.components.find(row=>row.id==='graph.perspective.traversal').value.hops=0;
 const out=await m.selectKnowledgePerspective(perspective(f.source),r,f.source,f.permission);
 assert.equal(out.complete,false);assert.ok(out.omissions.some(item=>item.detail==='knowledge_prefix_limited'));
 for(const options of [{failure:true},{run:'different'}]){
  const failed=fixture(options);const result=await m.selectKnowledgePerspective(perspective(failed.source),resolution(),failed.source,failed.permission);
  assert.equal(result.objects[0].status,'unavailable');assert.equal(result.complete,false);
 }
 const p=perspective(f.source);p.focus.snapshot='other-version';const result=await m.selectKnowledgePerspective(p,resolution(),f.source,f.permission);
 assert.equal(result.objects[0].reason,'knowledge_run_version_unavailable');
});
test('abort before selection dispatch is observable and retry uses same contract',async()=>{
 const f=fixture(),controller=new AbortController();controller.abort();
 await assert.rejects(m.selectKnowledgePerspective(perspective(f.source),resolution(),f.source,f.permission,controller.signal),/abort/i);assert.equal(f.reads,0);
 const retried=await m.selectKnowledgePerspective(perspective(f.source),resolution(),f.source,f.permission);assert.equal(retried.complete,true);
});
test('unsupported structure/resolution/render grouping never exposes a working-looking control',async()=>{
 for(const [key,value] of [['structure','future'],['resolution','future'],['visual',[{id:'g',dimension:'relevance',match:{},style:{group:'future'},explanation:'future'}]]]) {
  const f=fixture(),r=resolution();r.components.find(row=>row.id===`graph.perspective.${key}`).value=value;
  await assert.rejects(m.selectKnowledgePerspective(perspective(f.source),r,f.source,f.permission),/unavailable/);assert.equal(f.reads,0);
 }
});
test('native profile pack installer rejects writes before a real snapshot is loaded',async()=>{
 let calls=0;const session=new NativeProfileSession('test',{async onboarding(){calls++;throw new Error('not used');}});
 await assert.rejects(session.installDefaultEntries(m.perspectivePack),/Reload/);assert.equal(calls,0);
});
