import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from './test-loader.mjs';

const loader = await createModuleLoader();
const {compilePerspective,isSupportedColor} = await loader.load('src/graph-perspectives/plan.ts');
const {selectPerspective} = await loader.load('src/graph-perspectives/select.ts');
const {createNavigation,navigate,goBack,goForward,changeNavigationStructure,referenceKey,objectKey} = await loader.load('src/graph-perspectives/navigation.ts');
const {exportPerspective,importPerspective} = await loader.load('src/graph-perspectives/persistence.ts');
const {initialWorkspace} = await loader.load('src/workspace/state.ts');
const {compareObjectVersions} = await loader.load('src/graph-perspectives/versions.ts');
const {createPacketAdapter} = await loader.load('src/graph-perspectives/adapters.ts');
test.after(() => loader.close());
const ref = (selector, snapshot) => ({source:'fixture',selector,...(snapshot === undefined ? {} : {snapshot})});
const capabilities = ['structure','resolution','traversal','temporal','evidence','visual','budget','localResolution','presentation'].map(target => ({id:target,label:target,target,status:'supported'}));
function compile(values = {}, changes = {}) {
 const defaults = {structure:'dependencies',resolution:'summary',traversal:{relations:['input'],direction:'both',hops:3},temporal:{compareSnapshots:[]},evidence:[],visual:[],budget:{query:100,render:100,page:3},localResolution:[]};
 const effective = {...defaults,...values};
 const perspective = {schema:'loom.graph_perspective/1',id:'test',components:{},focus:ref('root'),...changes};
 return compilePerspective(perspective,{components:Object.entries(effective).map(([id,value])=>({id,value,status:'effective',origin:{layer:'native_fixture'}}))},capabilities);
}
const allow = {id:'read-test',canRead:()=>true};
function source({edges=[['root','a','input'],['a','b','input'],['root','comment','comment'],['root','version','version']],statuses={},transform,capabilities: extraCapabilities=[]}={}) {
 const reads=[], queries=[];
 const adapter = {
  descriptor:{id:'fixture',label:'Fixture',structures:[{id:'dependencies',label:'Dependencies',relations:['input']},{id:'history',label:'History',relations:['comment','version']}],capabilities:extraCapabilities},
  async resolve(address) {
   reads.push(structuredClone(address));
   const object={ref:structuredClone(address),label:address.selector,kind:address.selector==='root'?'operation':'record',status:statuses[address.selector]??'available',evidence:'source',properties:{value:address.snapshot==='old'?1:2}};
   return transform ? transform(object) : object;
  },
  async neighbors(address,request) {
   queries.push(structuredClone(request));
   const all=edges.filter(([a,b,kind])=>request.relations.includes(kind)&&(request.direction!=='incoming'&&a===address.selector||request.direction!=='outgoing'&&b===address.selector)).map(([a,b,kind],i)=>({id:`${i}:${a}:${b}:${kind}`,from:ref(a,address.snapshot),to:ref(b,address.snapshot),kind,evidence:'source',basis:{fixture:true}}));
   const offset=Number(request.cursor??0), rows=all.slice(offset,offset+request.limit);
   return {relations:rows,...(offset+rows.length<all.length?{nextCursor:String(offset+rows.length)}:{}),total:all.length};
  }
 };
 return {adapter,reads,queries};
}

test('native suppression is permanent input to compiler, not a fallback request',()=>{
 const p=compile();
 for(const status of ['disabled','excluded','proposal','missing']){
  const rows=p.explanation.filter(x=>capabilities.some(d=>d.id===x.id)).map(x=>x.id==='structure'?{...x,status}:x);
  const output=compilePerspective(p.sourcePerspective,{components:rows},capabilities);
  assert.ok(output.errors.includes('structure_not_effective'));
  assert.equal(output.values.structure,undefined);
  assert.equal(output.explanation.find(x=>x.id==='structure').status,status);
 }
});
test('compile explains explicit navigation and validates budgets independently',()=>{
 const p=compile({budget:{query:4,render:2,page:1}});
 assert.deepEqual([p.queryBudget,p.renderBudget,p.pageSize],[4,2,1]);
 assert.ok(p.explanation.some(x=>x.reason==='explicit_navigation_focus'));
 assert.ok(compile({budget:{query:4,render:0,page:1}}).errors.includes('budget_render_invalid'));
});
test('malformed match/style fields fail while unknown predicates stay preserved and unsupported',()=>{
 assert.ok(compile({localResolution:[{id:'bad',match:{distance:-1},resolution:'raw'}]}).errors.length);
 assert.ok(compile({visual:[{id:'bad',dimension:'time',match:{},style:{color:5},explanation:'fixture'}]}).errors.length);
 const p=compile({localResolution:[{id:'future',match:{futurePredicate:true},resolution:'raw'}]});
 assert.equal(p.unsupported[0].reason,'match_field_unsupported');
 assert.equal(p.localResolution[0].match.futurePredicate,true);
});
test('on-demand address resolves before source rendering and bounded traversal is canonical-data immutable',async()=>{
 const s=source(), p=compile({}, {focus:ref('a')});
 const before=JSON.stringify(p);
 const out=await selectPerspective(p,[s.adapter],allow);
 assert.equal(s.reads[0].selector,'a');
 assert.deepEqual(new Set(out.objects.map(x=>x.ref.selector)),new Set(['root','a','b']));
 assert.equal(JSON.stringify(p),before);
 assert.equal(out.analysis.length,0);
 assert.equal(out.objects[0].ref.selector,'a');
 assert.ok(out.objects.every(x=>x.confidence===undefined));
});
test('same resolution gives distinct dependency vs comment/version structures',async()=>{
 const s=source();
 const a=await selectPerspective(compile(),[s.adapter],allow);
 const b=await selectPerspective(compile({structure:'history',traversal:{relations:['comment','version'],direction:'outgoing',hops:1}}),[s.adapter],allow);
 assert.deepEqual(a.objects.map(x=>x.resolution),['summary','summary','summary']);
 assert.deepEqual(b.objects.map(x=>x.resolution),['summary','summary','summary']);
 assert.deepEqual(new Set(b.objects.map(x=>x.ref.selector)),new Set(['root','comment','version']));
 assert.equal(a.objects[0].canonicalKey,b.objects[0].canonicalKey);
});
test('local detail and aggregation are independent of chosen relation structure',async()=>{
 const s=source({edges:[['root','a','input'],['root','b','input'],['root','c','input']]});
 const out=await selectPerspective(compile({localResolution:[{id:'records',match:{kind:'record'},resolution:'aggregate',aggregate:{id:'remaining',label:'Remaining records'}},{id:'input',match:{selectors:['a']},resolution:'detail'},{id:'focus',match:{focus:true},resolution:'raw'}]}),[s.adapter],allow);
 assert.equal(out.objects.find(x=>x.ref.selector==='root').resolution,'raw');
 assert.equal(out.objects.find(x=>x.ref.selector==='a').resolution,'detail');
 assert.equal(out.objects.find(x=>x.kind==='aggregate').aggregateMembers.length,2);
 assert.equal(out.omissions.filter(x=>x.reason==='aggregate').length,2);
 assert.ok(out.graph.edges.every(edge=>out.graph.nodes.some(n=>n.id===edge.src)&&out.graph.nodes.some(n=>n.id===edge.dst)));
});
test('unknown local predicate never silently broadens a rule',async()=>{
 const out=await selectPerspective(compile({localResolution:[{id:'future',match:{futurePredicate:true},resolution:'raw'}]}),[source().adapter],allow);
 assert.ok(out.objects.every(x=>x.resolution==='summary'));
});
test('visual relevance, time and uncertainty remain separately explained; parser does not gain confidence',async()=>{
 const visual=[{id:'r',dimension:'relevance',match:{distance:1},style:{blur:1},explanation:'Adjacent context'},{id:'t',dimension:'time',match:{snapshot:'old'},style:{opacity:0.4},explanation:'Historical snapshot'},{id:'u',dimension:'uncertainty',match:{evidence:'unknown'},style:{group:'unassessed'},explanation:'No assessment'}];
 const out=await selectPerspective(compile({visual,temporal:{snapshot:'old',compareSnapshots:[]}}),[source().adapter],allow);
 const a=out.objects.find(x=>x.ref.selector==='a');
 assert.deepEqual(a.visualReasons.map(x=>x.dimension),['relevance','time']);
 assert.equal(a.visual.opacity,0.4); assert.equal(a.confidence,undefined);
});
test('render and query budgets disclose omitted data and pending adapter cursors',async()=>{
 const s=source({edges:Array.from({length:30},(_,i)=>['root',`n${i}`,'input'])});
 const out=await selectPerspective(compile({budget:{query:5,render:2,page:2}}),[s.adapter],allow);
 assert.equal(out.objects.length,2); assert.ok(s.reads.length<=5);
 assert.ok(out.omissions.some(x=>x.reason==='render_budget'));
 assert.ok(out.omissions.some(x=>x.reason==='query_budget'));
 assert.ok(out.continuation.some(x=>x.cursor)); assert.equal(out.complete,false);
});
test('denied, unloaded, unavailable and unknown objects remain references without pretend empty data',async()=>{
 for(const status of ['unloaded','unavailable','unknown']){
  const out=await selectPerspective(compile(),[source({statuses:{root:status}}).adapter],allow);
  assert.equal(out.objects[0].status,status); assert.equal(out.complete,false);
 }
 const s=source(); const out=await selectPerspective(compile(),[s.adapter],{id:'denied',canRead:()=>false});
 assert.equal(out.objects[0].status,'denied'); assert.equal(s.reads.length,0);
 assert.equal(out.objects[0].properties,undefined);
 const missing=await selectPerspective(compile(),[],allow); assert.equal(missing.objects[0].status,'unloaded');
});
test('analysis membership is independent of view filtering and access permission',async()=>{
 const analysis={selected:[ref('offscreen')],policyRef:'analysis-policy',linkedToPerspective:false};
 const out=await selectPerspective(compile({evidence:['inferred']},{analysis}),[source().adapter],allow);
 assert.equal(out.objects.length,1); assert.deepEqual(out.analysis,[ref('offscreen')]); assert.equal(out.permissionId,allow.id);
});
test('snapshot comparison preserves identity and active focus with render budget one',async()=>{
 const out=await selectPerspective(compile({temporal:{snapshot:'new',compareSnapshots:['old']},budget:{query:20,render:1,page:4}}),[source().adapter],allow);
 assert.equal(out.objects[0].ref.snapshot,'new');
 assert.ok(out.omissions.some(x=>x.ref?.snapshot==='old'));
 const both=await selectPerspective(compile({temporal:{snapshot:'new',compareSnapshots:['old']}}),[source().adapter],allow);
 const roots=both.objects.filter(x=>x.ref.selector==='root');
 assert.equal(roots.length,2); assert.equal(roots[0].canonicalKey,roots[1].canonicalKey); assert.notEqual(roots[0].key,roots[1].key);
 assert.ok(both.relations.every(e=>e.from.snapshot===e.to.snapshot));
});
test('adapter cannot silently return another snapshot',async()=>{
 const out=await selectPerspective(compile({temporal:{snapshot:'new'}}),[source({transform:x=>({...x,ref:{...x.ref,snapshot:'old'}})}).adapter],allow);
 assert.equal(out.objects[0].status,'unavailable'); assert.match(out.objects[0].reason,/adapter_changed_snapshot/);
});
test('navigation history distinguishes representation, identity and related objects reversibly',()=>{
 const start=createNavigation(ref('root'),'physical');
 const structure=changeNavigationStructure(start,'logical');
 assert.equal(objectKey(start.current.ref),objectKey(structure.current.ref));
 const representation=navigate(structure,{ref:{...ref('root'),representation:'record'},structure:'logical'});
 assert.notEqual(referenceKey(structure.current.ref),referenceKey(representation.current.ref));
 assert.equal(objectKey(structure.current.ref),objectKey(representation.current.ref));
 assert.deepEqual(goBack(representation),{current:structure.current,back:structure.back,forward:[representation.current]});
 assert.deepEqual(goForward(goBack(representation)),representation);
 assert.equal(goBack(goBack(representation)).current.structure,'physical');
});
test('unknown options and native disable/exclusion actions survive roundtrip unchanged',()=>{
 const p=compile().sourcePerspective;
 p.components.future={nested:['unchanged']}; p.futureEnvelope={v:2}; p.layerActions=[{op:'disable',key:'visual'},{op:'exclude',key:'future'}];
 const result=importPerspective(exportPerspective(p));
 assert.deepEqual(result.perspective,p); assert.ok(result.warnings.includes('unknown_field_preserved:futureEnvelope'));
});
test('legacy workspace is validated and preserved for native resolution, not silently assigned defaults',()=>{
 const workspace=initialWorkspace(['graph']); workspace.extraFuture={retained:true};
 const result=importPerspective(JSON.stringify(workspace));
 assert.equal(result.perspective.workspace.schema,'loom.workbench/2');
 assert.deepEqual(result.perspective.legacyWorkspace.extraFuture,{retained:true});
 assert.deepEqual(result.perspective.components,{});
 assert.ok(result.warnings.includes('legacy_parameters_require_native_resolution'));
});
test('new adapter structure needs descriptors and mapping only, not core recompilation',async()=>{
 const s=source({edges:[['root','frequency','spectral_band']]});
 s.adapter.descriptor.structures.push({id:'spectral',label:'Spectral view',relations:['spectral_band']});
 const out=await selectPerspective(compile({structure:'spectral',traversal:{relations:['spectral_band'],direction:'outgoing',hops:1}}),[s.adapter],allow);
 assert.deepEqual(out.objects.map(x=>x.ref.selector),['root','frequency']);
 assert.equal(s.queries[0].structure,'spectral');
});
test('custom capability is forwarded and remains unsupported unless adapter declares it',async()=>{
 const descriptor={id:'custom',label:'Custom',target:'spectralFilter',status:'supported'};
 const p=compile();
 const plan=compilePerspective(p.sourcePerspective,{components:[...p.explanation.filter(x=>capabilities.some(d=>d.id===x.id)),{id:'custom',value:{band:5},status:'effective'}]},[...capabilities,descriptor]);
 assert.equal(plan.unsupported[0].reason,'adapter_capability_pending');
 const s=source({capabilities:[descriptor]});
 const out=await selectPerspective(plan,[s.adapter],allow);
 assert.deepEqual(s.queries[0].parameters.spectralFilter,{band:5});
 assert.equal(out.plan.unsupported.length,0);
});

test('resolved canonical identity is rechecked against permissions before exposing contents',async()=>{
 const s=source({transform:x=>({...x,ref:{...x.ref,canonicalId:'restricted'}})});
 const out=await selectPerspective(compile(),[s.adapter],{id:'canonical-policy',canRead:r=>r.canonicalId!=='restricted'});
 assert.equal(out.objects[0].status,'denied'); assert.equal(out.objects[0].properties,undefined); assert.equal(s.queries.length,0);
});
test('unavailable context cannot be disguised as an available aggregate',async()=>{
 const s=source({statuses:{a:'unavailable',b:'unloaded'}});
 const out=await selectPerspective(compile({localResolution:[{id:'all',match:{kind:'record'},resolution:'aggregate',aggregate:{id:'rest',label:'Rest'}}]}),[s.adapter],allow);
 assert.equal(out.objects.find(x=>x.ref.selector==='a').status,'unavailable');
 assert.equal(out.objects.some(x=>x.aggregateMembers?.some(r=>r.selector==='a')),false);
});

test('adapter-normalized representations do not duplicate objects reached through comparison and edges',async()=>{
 const s=source({edges:[['root','root','input']],transform:x=>({...x,ref:{...x.ref,representation:'record'}})});
 const original=s.adapter.neighbors;
 s.adapter.neighbors=async(address,request,context)=>{
  const page=await original(address,request,context);
  page.relations=page.relations.map(e=>({...e,from:{...e.from,representation:'record'},to:{...e.to,representation:'record'}}));
  if(address.snapshot==='old') page.relations.push({id:'version',from:{...address},to:{...address,snapshot:'new',representation:'record'},kind:'input',evidence:'source'});
  return page;
 };
 const out=await selectPerspective(compile({temporal:{snapshot:'old',compareSnapshots:['new']}}),[s.adapter],allow);
 assert.equal(out.objects.length,2);
 assert.equal(new Set(out.objects.map(x=>x.key)).size,out.objects.length);
 assert.equal(out.graph.nodes.length,2);
});

test('denied relation endpoints never expose adapter relation metadata',async()=>{
 const s=source({edges:[['root','secret','input']]});
 const original=s.adapter.neighbors;
 s.adapter.neighbors=async(...args)=>{ const page=await original(...args); page.relations.forEach(e=>e.basis={hidden:'must-never-appear'}); return page; };
 const out=await selectPerspective(compile(),[s.adapter],{id:'limited',canRead:r=>r.selector!=='secret'});
 assert.equal(out.objects.find(x=>x.ref.selector==='secret').status,'denied');
 assert.equal(out.relations.length,0); assert.equal(out.graph.edges.length,0);
 assert.equal(JSON.stringify(out).includes('must-never-appear'),false);
});
test('advancing empty pages obey explicit neighbor read budget and provide continuation',async()=>{
 const s=source(); let calls=0;
 s.adapter.neighbors=async()=>({relations:[],nextCursor:String(++calls)});
 const out=await selectPerspective(compile({budget:{query:20,render:10,page:1,neighborPages:3}}),[s.adapter],allow);
 assert.equal(calls,3); assert.equal(out.metrics.neighborCalls,3); assert.equal(out.complete,false);
 assert.equal(out.continuation[0].cursor,'3');
 assert.ok(out.omissions.some(x=>x.detail==='neighbor_page_budget'));
});
test('malformed suppression containers cannot be silently discarded on import',()=>{
 const p=compile().sourcePerspective;
 assert.throws(()=>importPerspective(JSON.stringify({...p,layerActions:{op:'exclude',key:'structure'}})),/layer_actions_invalid/);
 assert.throws(()=>importPerspective(JSON.stringify({...p,layerActions:[{op:'exclude'}]})),/layer_action_invalid/);
 const future={...p,layerActions:[{op:'future_suppression',key:'structure'}]};
 const result=importPerspective(JSON.stringify(future));
 assert.deepEqual(result.perspective.layerActions,future.layerActions);
 assert.ok(result.warnings.includes('unsupported_layer_action_preserved:future_suppression'));
});

test('asserted canonical identity plus representation and snapshot deduplicates physical selector aliases',async()=>{
 const packet={schema:'loom.graph_packet/1',sources:[],entities:[0,1].map(i=>({id:`e${i}`,label:`Object ${i}`,attrs:{perspective:{ref:{source:'fixture',selector:`real${i}`,canonicalId:`canon${i}`,representation:`r${i}`,snapshot:'v1'},status:'available',evidence:'source'}}})),claims:[{id:'edge',subject:'e0',object:'e1',predicate:'input'}]};
 const adapter=createPacketAdapter(packet,{id:'fixture',label:'Aliased fixture',structures:[{id:'dependencies',label:'Dependencies',relations:['input']}]});
 const focus={source:'fixture',selector:'alias0',canonicalId:'canon0',representation:'r0',snapshot:'v1'};
 const out=await selectPerspective(compile({}, {focus}),[adapter],allow);
 assert.equal(out.objects.filter(x=>x.ref.canonicalId==='canon0').length,1);
 assert.equal(out.objects.length,2);
});

test('local resolution executes data-selected field projection while preserving source properties',async()=>{
 const s=source({edges:[['root','a','input']],transform:x=>({...x,properties:{value:7,expression:'sum(inputs)',text:'detail'}})});
 const presentation=[{id:'summary',fields:[{id:'value',label:'Value',path:'/properties/value'}]},{id:'full',fields:[{id:'expression',label:'Expression',path:'/properties/expression'},{id:'text',label:'Text',path:'/properties/text'}]}];
 const out=await selectPerspective(compile({presentation,localResolution:[{id:'focused',match:{focus:true},resolution:'full'}]}),[s.adapter],allow);
 assert.deepEqual(out.objects[0].presentation.fields.map(f=>f.value),['sum(inputs)','detail']);
 assert.deepEqual(out.objects[1].presentation.fields.map(f=>f.value),[7]);
 assert.equal(out.objects[1].properties.expression,'sum(inputs)');
 assert.equal(out.objects[0].presentation.status,'supported');
});
test('snapshot differences use canonical identity and exact property paths without semantic or chronology claims',async()=>{
 const out=await selectPerspective(compile({temporal:{snapshot:'new',compareSnapshots:['old']}}),[source().adapter],allow);
 const groups=compareObjectVersions(out.objects);
 const root=groups.find(g=>g.canonicalKey===objectKey(ref('root')));
 assert.equal(root.method,'structural_properties'); assert.equal(root.left.snapshot,'new'); assert.equal(root.right.snapshot,'old');
 assert.deepEqual(root.changes,[{path:'/properties/value',kind:'changed',left:2,right:1}]);
 const unavailable=compareObjectVersions([{...out.objects[0],status:'unavailable'},...out.objects.slice(1)]);
 assert.equal(unavailable.find(g=>g.canonicalKey===objectKey(ref('root'))).status,'unavailable');
});

test('visual colors accept self-contained CSS only and cannot send requests through SVG paint URLs',async()=>{
 for(const value of ['#abc','#abcd','#abcdef','#abcdef12','red','transparent','rgb(1, 2, 3)','rgba(1,2,3,0.2)','rgb(10% 20% 30% / 40%)','hsl(120deg 50% 50%)','hsla(120,50%,50%,0.5)']) assert.equal(isSupportedColor(value),true,value);
 for(const value of ['url(https://example.invalid/pixel)','var(--url)','red; background:url(x)','#abcde','rgb(var(--a),1,2)','hsl(10 20 30)','red/**/','calc(1)']) {
  assert.equal(isSupportedColor(value),false,value);
  const visual=[{id:'color',dimension:'relevance',match:{},style:{color:value},explanation:'Imported color'}];
  const p=compile({visual}); assert.ok(p.errors.some(e=>e.startsWith('visual_color_unsupported')));
  assert.equal(p.sourcePerspective.components && p.visual[0].style.color,value);
  await assert.rejects(selectPerspective({...compile(),visual},[source().adapter],allow),/visual_color_unsupported/);
 }
});
