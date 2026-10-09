#!/usr/bin/env node
/** Real workspace reducers and full product renderer modules; no copied logic.
 * Private renderer exports are appended in memory only, before transpilation.
 * React SSR is not mounted browser E2E. Every canonical record is synthetic.
 */
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';
import cp from 'node:child_process';import {createRequire} from 'node:module';import crypto from 'node:crypto';
import net from 'node:net';import http from 'node:http';import https from 'node:https';
const args=Object.fromEntries(process.argv.slice(2).reduce((a,v,i,x)=>i%2?a:[...a,[v.replace(/^--/,''),x[i+1]]],[]));
for(const k of ['repo','sha','node-modules','out'])if(!args[k])throw Error(`--${k} required`);
const repo=path.resolve(args.repo),out=path.resolve(args.out),modules=path.resolve(args['node-modules']);
if(fs.existsSync(out))throw Error('fresh receipt required');
if(cp.execFileSync('git',['-C',repo,'rev-parse','HEAD'],{encoding:'utf8'}).trim()!==args.sha)throw Error('SHA mismatch');
cp.execFileSync('git',['-C',repo,'diff','--exit-code',args.sha,'--','loom/web/src']);
const dep=createRequire(path.join(modules,'__audit_loader.cjs')),ts=dep('typescript'),React=dep('react'),ReactDOM=dep('react-dom/server');
const attempts=[];const deny=()=>{attempts.push('denied');throw Error('audit network denied');};
globalThis.fetch=deny;net.connect=deny;net.createConnection=deny;http.request=deny;http.get=deny;https.request=deny;https.get=deny;
const storage=new Map();globalThis.localStorage={getItem:k=>storage.has(k)?storage.get(k):null,setItem:(k,v)=>storage.set(k,String(v)),removeItem:k=>storage.delete(k)};
const loaded=new Map(),sourceHashes={};
function load(file){
 file=path.resolve(file);if(loaded.has(file))return loaded.get(file).exports;
 if(file.endsWith('.json'))return JSON.parse(fs.readFileSync(file,'utf8'));
 if(file.endsWith('.css'))return {};
 const raw=fs.readFileSync(file,'utf8');sourceHashes[path.relative(repo,file)]=crypto.createHash('sha256').update(raw).digest('hex');
 let source=raw;
 if(file.endsWith('/KnowledgeWorkbench.tsx'))source+='\nexport { CollectionPane as AuditCollectionPane, KnowledgeGraph as AuditKnowledgeGraph };\n';
 if(file.endsWith('/GraphView.tsx'))source+='\nexport { filterGraphNoise as AuditFilterGraphNoise };\n';
 const js=ts.transpileModule(source,{fileName:file,compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.CommonJS,jsx:ts.JsxEmit.ReactJSX,esModuleInterop:true}}).outputText;
 const m={exports:{}};loaded.set(file,m);
 const require=(name)=>{
  if(!name.startsWith('.'))return dep(name);
  const base=path.resolve(path.dirname(file),name);
  for(const candidate of [base,...['.ts','.tsx','.json','.js','/index.ts'].map(e=>base+e)])if(fs.existsSync(candidate)&&fs.statSync(candidate).isFile())return load(candidate);
  throw Error(`unresolved ${name} from ${file}`);
 };
 vm.runInThisContext(`(function(exports,require,module,__filename,__dirname){${js}\n})`,{filename:file})(m.exports,require,m,file,path.dirname(file));return m.exports;
}
const m=load(path.join(repo,'loom/web/src/workspace/state.ts'));
const ui=load(path.join(repo,'loom/web/src/components/KnowledgeWorkbench.tsx'));
const oldGraph=load(path.join(repo,'loom/web/src/components/GraphView.tsx'));
const views=load(path.join(repo,'loom/web/src/profiles/view-state.ts'));
const cases=[];function check(id,kind,ok,oracle,evidence={}){cases.push({id,kind,status:ok?'PASS':'FAIL',oracle,evidence});}
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const sortObject=v=>Array.isArray(v)?v.map(sortObject):v&&typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,sortObject(v[k])])):v;
const semeq=(a,b)=>equal(sortObject(a),sortObject(b));
const reorder=v=>Array.isArray(v)?v.map(reorder):v&&typeof v==='object'?Object.fromEntries(Object.entries(v).reverse().map(([k,w])=>[k,reorder(w)])):v;
const selection={kind:'entities',id:'synthetic:e1',focus:'synthetic:e1',run:'synthetic:history-1'};
let w=m.setWorkspaceRun(m.initialWorkspace(['entities','graph','context']),'synthetic:history-1');
const original=JSON.stringify(w);w=m.changeParameter(w,w.panes[0].id,'selection',selection);
check('A4-E-01.headless-selection','contract',w.panes.every(p=>semeq(p.parameters.selection,selection))&&JSON.parse(original).panes.every(p=>p.parameters.selection===null),'Headless reducer sets the same typed ID/run/focus in linked list/graph/context views without rendering, and does not mutate its input.');
const restored=m.parseWorkspace(JSON.stringify(reorder(w),null,2));
check('A4-E-02.object-order-serialization','contract',semeq(w,restored),'Reordering object keys and adding insignificant JSON whitespace preserves workspace identity, selection and parameters; arrays retain order.');
let reference=m.unlink(w,w.panes[1].id);reference=m.setWorkspaceRun(reference,'synthetic:history-2');
check('A4-E-03.historical-reference','contract',reference.panes[1].parameters.run==='synthetic:history-1'&&semeq(reference.panes[1].parameters.selection,selection)&&reference.panes[0].parameters.selection===null,'An explicitly unlinked historical view keeps its old run and selected ID while following views clear selection for a new run. This does not assert that a mutable server run is immutable.');
const entity={id:'synthetic:e1',label:'Synthetic Alpha',kind:'custom-type',confidence:0.9,evidence_class:'observed',audit_unknown:{retained:true}};
const other={id:'synthetic:e2',label:'Synthetic Beta',kind:'custom-type',confidence:0.4,evidence_class:'inferred'};
const claim={id:'synthetic:c1',subject:entity.id,object:other.id,predicate:'safe-relation',assessment:{confidence:0.7,evidence_class:'inferred'}};
const data={entities:[entity,other],claims:[claim]},canonical=JSON.stringify(data),entityMap=new Map(data.entities.map(e=>[e.id,e]));
const parameters={...w.panes[0].parameters,filter:'',confidence:0,maxNodes:60};const noop=()=>{};
const listProps={kind:'entities',rows:data.entities,entities:entityMap,focus:entity.id,selection:{kind:'entities',record:entity},onSelect:noop,limit:2,parameters,change:noop};
const list=ReactDOM.renderToStaticMarkup(React.createElement(ui.AuditCollectionPane,listProps));
const graph=ReactDOM.renderToStaticMarkup(React.createElement(ui.AuditKnowledgeGraph,{data,focus:entity.id,onSelect:noop,parameters,change:noop}));
check('A4-E-04.real-projection-identity','contract',list.includes('kb-record selected')&&list.includes(entity.label)&&graph.includes('kb-node focused')&&graph.includes('Focus '+entity.label),'Both actual renderers consume the same selected source record/ID. Presentation labels may be shortened; identity is not inferred from label.');
const detailed=ReactDOM.renderToStaticMarkup(React.createElement(ui.AuditKnowledgeGraph,{data,focus:entity.id,onSelect:noop,parameters:{...parameters,depth:0,fade:11,maxNodes:1},change:noop}));
check('A4-E-05.detail-does-not-write','contract',JSON.stringify(data)===canonical&&detailed!==graph,'Changing depth/fade/node count changes real SSR presentation while every synthetic canonical input byte remains unchanged. No statement about untested server authorization.');
const filtered=ReactDOM.renderToStaticMarkup(React.createElement(ui.AuditCollectionPane,{...listProps,parameters:{...parameters,filter:'not-present'}}));
check('A4-E-06.visibility-local','contract',!/class="kb-record(?: |")/.test(filtered)&&JSON.stringify(data)===canonical&&semeq(w.panes[2].parameters.selection,selection)&&attempts.length===0,'View filtering hides rows locally without network requests, mutating canonical records or changing the separate saved selection. Analysis execution/permissions are outside this reducer contract.');
check('A4-E-07.loaded-prefix-disclosure','contract',list.includes('collection may continue beyond the 2 record limit'),'The real list renderer explicitly labels a loaded prefix at the record limit; it does not claim a complete index.');
const v1=views.newApplicationView('synthetic-view','synthetic-profile');const detached=views.setConversationCoupling(v1,'independent','synthetic-old-conversation');
check('A4-E-08.conversation-view-target','contract',views.viewConversationId(detached,'synthetic-new-conversation')==='synthetic-old-conversation'&&views.viewConversationId(v1,'synthetic-new-conversation')==='synthetic-new-conversation','Existing provider-inspired application view detaches to the visible conversation ID and preserves its recipient when shared focus later changes. No execution dispatch is claimed.');
const syntheticNodes=[{id:'synthetic:ai',kind:'entity',label:'AI'},{id:'synthetic:record',kind:'entity',label:'records'},{id:'synthetic:alpha',kind:'entity',label:'Alpha'}];
const filteredGraph=oldGraph.AuditFilterGraphNoise(syntheticNodes,[{id:'edge',src:'synthetic:ai',dst:'synthetic:alpha'}]);
check('A4-CH-VIEW001.domain-filter-reproduction','reproduction',equal(filteredGraph.nodes.map(n=>n.id),['synthetic:alpha'])&&filteredGraph.edges.length===0,'Actual legacy GraphView filter silently removes source-labelled AI and records plus incident edge via fixed length/domain list; this is a presentation policy, not canonical deletion.',{kept_ids:filteredGraph.nodes.map(n=>n.id),edges:filteredGraph.edges.length});
cases.push({id:'A4-CH-VIEW001.configurable-visibility',kind:'acceptance',status:'BLOCKED',reason:'Legacy GraphView has no public filter-policy input: filterGraphNoise(nodes, edges) always applies the source-coded list/length rule. Bind this test to the implemented policy control; do not invent an accepted option parameter.'});
cases.push({id:'A4-E-09.native-perspective-selector-integration',kind:'integration',status:'BLOCKED',reason:'E branch absent at initial pass4 discovery. Existing browser workspace reducers are not a public native selector/authorization/index contract. The successful component tests cannot establish D→E/headless→mounted-UI integration.'});
cases.push({id:'A4-WEB-001.graph-field-to-profile-runtime',kind:'integration',status:'BLOCKED',reason:'B4 does not change reviewed TS workflow/presentation projection or raw_source restore consumers. Native catalog profile projection alone is not evidence that the existing ApplicationProfile runtime resolves these fields. Need a public connecting resolver contract.'});
const result={schema:'klbt.audit.pass4-perspectives/1',repo:'klb-t/chatadhd',sha:args.sha,source_hashes:sourceHashes,cases,dependencies:{react:dep('react/package.json').version,typescript:ts.version},network_attempts:attempts.length,source_instrumentation:'Full original TS/TSX modules transpiled at runtime; export declarations appended for existing private renderer/filter functions only. No function body or implementation is copied or replaced.',boundaries:['React SSR and actual pure state reducers; not mounted browser/device E2E.','Storage object only supplies localStorage host plumbing; no graph store or permissions are mocked into passing integration.','All records and IDs are synthetic. No model call, publication or real transport.']};
result.counts=Object.fromEntries(['PASS','FAIL','BLOCKED'].map(s=>[s,cases.filter(c=>c.status===s).length]));fs.mkdirSync(path.dirname(out),{recursive:true});fs.writeFileSync(out,JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result.counts));process.exitCode=result.counts.FAIL?1:result.counts.BLOCKED?2:0;
