#!/usr/bin/env node
// Audit harness: transpiles complete checkout modules, never reimplements them.
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync} from 'node:fs';
import {createRequire, syncBuiltinESMExports} from 'node:module';
import {pathToFileURL} from 'node:url';
import path from 'node:path';
import {webcrypto} from 'node:crypto';
const [repo, typescript, output, mode] = process.argv.slice(2);
const require = createRequire(import.meta.url);
let blocked = 0;
const deny = () => { blocked++; throw Error('audit_network_forbidden'); };
for (const name of ['node:http', 'node:https']) { const m=require(name); m.request=deny; m.get=deny; }
const net=require('node:net'); net.connect=deny; net.createConnection=deny; net.Socket.prototype.connect=deny;
syncBuiltinESMExports();
globalThis.crypto ??= webcrypto;
const ts=(await import(pathToFileURL(typescript).href)).default;
const urls=new Map();
function load(file) {
  if (urls.has(file)) return urls.get(file);
  let code=ts.transpileModule(readFileSync(path.join(repo,file),'utf8'), {compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.ES2022}}).outputText;
  code=code.replace(/from "(\.\.?\/[^"\n]+)"/g,(_,p)=>`from "${load(path.posix.normalize(path.posix.join(path.posix.dirname(file),p+'.ts')))}"`);
  const url='data:text/javascript;base64,'+Buffer.from(code).toString('base64'); urls.set(file,url); return url;
}
const runtime=await import(load('loom/web/src/profiles/runtime.ts'));
const graph=await import(load('loom/web/src/profiles/graph.ts'));
const capability=await import(load('loom/web/src/profiles/capability-graph.ts'));
const {createLoomProfileRegistry}=await import(load('loom/web/src/profiles/loom-adapter.ts'));
const {LoomHttpApi}=await import(load('loom/web/src/api/loom-http.ts'));
const cases=[];
async function check(id,phase,fn) { try {const evidence=await fn(); cases.push({id,phase,status:'PASS',evidence});} catch(e) {cases.push({id,phase,status:'FAIL',error:e.message});} }
const sent=[];
globalThis.fetch=async (url,options={}) => {
  // Actual LoomHttpApi ends here. No original fetch/real socket is retained.
  if(url!='/api/conversations' || options.method!=='POST') return deny();
  const body=JSON.parse(options.body); sent.push({path:url,method:options.method,body});
  return new Response(JSON.stringify({id:`synthetic-conv-${sent.length}`,title:body.title}),{status:200});
};
function fixture(title='Synthetic Alpha') {
  return {schema:'loom.application_profile/1',id:'audit.external-profile',profile_revision:1,label:'Synthetic profile',target:{application_id:'audit',version:'1',platform:'web'},
    evidence:{status:'partial',sources:[],gaps:['Synthetic; no service parity asserted.']},
    presentation:{renderer:'chat',tokens:{background:'#111',surface:'#222',text:'#fff',muted:'#aaa',accent:'#bbb',border:'#333'},sidebar:{side:'left',width:240},content_width:720,message_style:'plain'},
    composer:{submit:'enter',placeholder:'Synthetic input'},actions:[{id:'create',label:'Create',operation:'chat.create',capability:'chat.create',required:true}],
    workflows:[{id:'create-flow',initial:'ready',states:['ready','done'],transitions:[{from:'ready',event:'go',action:'create',to:'done',payload:{object:{title:{literal:title}}},save:{conversation_id:{from:'result',pointer:'/id'}}}]}]};
}
const original='\ufeff'+JSON.stringify(fixture(),null,2)+' \r\n';
const source={text:original,sourceRef:'synthetic:external-profile.json',actor:'audit-synthetic',knownAt:'2026-10-09T00:00:00Z'};
const registry=createLoomProfileRegistry(new LoomHttpApi());
const profile=runtime.registerProfile(registry,graph.parseApplicationProfileSource(original));
const request=await graph.makeApplicationProfileGraphAcceptance(profile,source);
if(mode==='prepare') {
  writeFileSync(path.join(output,'acceptance-request.json'),JSON.stringify(request,null,2)+'\n');
  writeFileSync(path.join(output,'synthetic-profile.json'),original);
  await check('WEB-01','acceptance',async()=>{assert.equal(request.packet.sources[0].observation.text,original);assert.equal(request.packet.entities[0].attrs.raw_source,original);return {exact_source_retained:true,entities:request.packet.entities.length,claims:request.packet.claims.length};});
  await check('WEB-02','reproduction',async()=>{assert.equal(request.packet.entities.length,1);assert.equal(request.packet.claims.length,0);assert.ok(!('workflows' in request.packet.entities[0].attrs));return {finding:'A3-WEB-001',structure_available_only_inside_raw_source:true};});
  // No invented resource API is emulated to turn the missing graph-query path green.
  cases.push({id:'WEB-03',phase:'acceptance',status:'BLOCKED',reason:'No existing structured resource-field query and runtime resolver binding in this serializer. Requires B contract; raw JSON reparse is not a graph-field query.'});
  await check('WEB-04','acceptance',async()=>{const unknown=fixture();unknown.future_mapping={unknown:'retained in caller source'};const bytes=JSON.stringify(unknown);assert.throws(()=>runtime.registerProfile(registry,graph.parseApplicationProfileSource(bytes)),/unknown field/);assert.equal(JSON.parse(bytes).future_mapping.unknown,'retained in caller source');return {closed_schema_rejects_explicitly:true,caller_source_unmodified:true,not_a_graph_preservation_claim:true};});
  await check('WEB-05','acceptance',async()=>{const empty=runtime.createProfileRegistry();const p=runtime.registerProfile(empty,fixture());assert.equal(runtime.profileAvailability(p,empty).supported,false);assert.throws(()=>runtime.createProfileSession(p,empty),/unavailable/);return {declaration_does_not_install_execution:true};});
  await check('WEB-06','acceptance',async()=>{const p=await capability.makeCapabilityGraphAcceptance(profile,[{operation:'chat.create',capability:'chat.create',status:'limited',detail:'Synthetic evidence only',evidence:['synthetic:test']}],source);assert.equal(p.packet.entities.length,2);assert.equal(p.packet.entities[1].attrs.availability,'limited');assert.equal(p.packet.entities[1].attrs.original_service_parity,'not_asserted');return {capability_has_graph_entity:true,permission_or_discovery_execution_not_asserted:true};});
  cases.push({id:'WEB-07',phase:'acceptance',status:'BLOCKED',reason:'Unknown-field retention plus uncertain mapping in a graph resource needs a separate ingestion contract; strict executable-profile rejection alone does not implement it.'});
} else if(mode==='restore') {
  const stored=JSON.parse(readFileSync(path.join(output,'native-read.json'),'utf8'));
  const restored=await graph.readProfileFromReceipt(stored,registry);
  await check('WEB-08','acceptance',async()=>{assert.equal(restored.source.text,original);assert.equal(restored.profile.workflows[0].transitions[0].payload.object.title.literal,'Synthetic Alpha');return {read_from_reopened_native_store:true,raw_source_exact:true};});
  await check('WEB-09','acceptance',async()=>{const a=runtime.createProfileSession(restored.profile,registry);const r=await runtime.executeProfileAction(a,registry,'create',{workflowId:'create-flow',event:'go'});assert.equal(sent.at(-1).body.title,'Synthetic Alpha');assert.equal(r.session.variables['create-flow'].conversation_id,'synthetic-conv-1');const other=fixture('Synthetic Beta');other.profile_revision=2;const p=runtime.registerProfile(registry,other);await runtime.executeProfileAction(runtime.createProfileSession(p,registry),registry,'create',{workflowId:'create-flow',event:'go'});assert.equal(sent.at(-1).body.title,'Synthetic Beta');assert.equal(restored.profile.profile_revision,1);return {actual_path:'readProfileFromReceipt→registerProfile→executeProfileAction→createLoomProfileRegistry→LoomHttpApi→captured fetch',titles:sent.map(s=>s.body.title),native_chat_execution:false};});
  await check('WEB-10','acceptance',async()=>{const restoredAgain=await graph.readProfileFromReceipt(stored,registry);assert.equal(restoredAgain.profile.profile_revision,1);assert.equal(restoredAgain.source.text,original);return {old_receipt_still_pins_old_revision:true};});
  cases.push({id:'WEB-11',phase:'acceptance',status:'BLOCKED',reason:'sourceRef is provenance here, not a transport resolver. No source watch/live-change/unavailability graph status consumer; native snapshot roundtrip does not prove those paths.'});
} else throw Error('unknown mode');
writeFileSync(path.join(output,`web-${mode}.json`),JSON.stringify({schema:'klbt.audit.web-resource-probe/1',cases,transport:{real_external_calls:0,attempts_blocked:blocked,captured_requests:sent},loaded_modules:[...urls.keys()],limits:['Node host, no mounted React/browser E2E.','Only final fetch is substituted; actual parser, registry, graph serializer, workflow, adapter and HTTP encoder run.']},null,2)+'\n');
process.exitCode=cases.some(c=>c.status==='FAIL')?1:0;
