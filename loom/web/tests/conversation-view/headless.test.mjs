// Envelope/transport consumers, not a replacement for native operator/store gates.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const m = await loader.load('src/api/conversation-view.ts');
const { LoomHttpApi } = await loader.load('src/api/loom-http.ts');
const { LoomJniApi } = await loader.load('src/api/loom-jni.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
test.after(() => loader.close());
const reference = () => ({id:'source:v:m1',conv_id:'c',role:'tool',text:'PUBLIC_VIEW_CANARY',status:'excluded',
  parent_id:null,version_group_id:'source:v:g1',version_num:1,weight:0,created:null,
  attachments:[{unknown:{exact:[1,2,'x']}}],metadata:{export:{raw:{unknown:true}}},storage:'reference',
  capabilities:{edit:false,set_status:false,restore:false,native_lookup:false},source_ref:{version:'sha256:test',selector:{pointer:'/0'},message_index:1}});
const view = () => ({schema:'loom.conversation_view/1',conversation_id:'c',view_id:'view:1',status:'partial',messages:[reference()],
  resources:[{unit_id:'unit',status:'available',current:true,mapping_status:'uncertain'}],omissions:[{status:'unknown'}],
  capabilities:{source_history_send:{available:false,reason:'source_egress_not_bound'}},read_configuration:{values:{projection_storage:'snapshot',content_index:'sketch'},
    value_schema:{properties:{projection_storage:{type:'string',enum:['snapshot','transient']},content_index:{type:'string',enum:['sketch','none']}}}}});

test('same decoded native envelope supplies pre-render history, field structure and null dates without coercion',()=>{
 const original=view(),out=m.readConversationView(original,'c');
 assert.equal(out,original);assert.deepEqual(m.conversationViewMessages(out,true),original.messages);
 assert.deepEqual(m.conversationViewMessages(out,false),[]);assert.equal(out.messages[0].created,null);
 assert.equal(out.messages[0].role,'tool');assert.deepEqual(out.messages[0].attachments,reference().attachments);
 assert.equal(out.resources[0].mapping_status,'uncertain');assert.equal(out.status,'partial');
});
test('capabilities fail closed for references and retain native/legacy edit semantics',()=>{
 for(const key of ['edit','set_status','restore','native_lookup']) {
  assert.equal(m.messageCan(reference(),key),false);assert.equal(m.messageCan({...reference(),storage:'native',capabilities:undefined},key),true);
  assert.equal(m.messageCan(undefined,key),false);
 }
 const forged=view();forged.messages[0].capabilities.edit=true;
 assert.throws(()=>m.readConversationView(forged,'c'),/reference_capability/);
 assert.equal(m.sourceHistorySendUnavailable(view()),true);
 const stored=view();stored.resources=[];stored.messages=[];assert.equal(m.sourceHistorySendUnavailable(stored),false);
});
test('unavailable, partial, unknown mapping and recognized empty remain distinct',()=>{
 for(const status of ['partial','unavailable','complete']) {const doc=view();doc.status=status;doc.messages=[];assert.equal(m.readConversationView(doc,'c').status,status);}
 const wrong=view();wrong.conversation_id='other';assert.throws(()=>m.readConversationView(wrong,'c'),/envelope/);
 const duplicates=view();duplicates.messages.push(reference());assert.throws(()=>m.readConversationView(duplicates,'c'),/message/);
});
test('native resolved values and enum alternatives drive options; missing configuration has no manual fallback',()=>{
 assert.deepEqual(m.resourceReadChoices(view()).map(row=>row.value),['snapshot','sketch']);
 const changed=view();changed.read_configuration.values.projection_storage='transient';assert.equal(m.resourceReadChoices(changed)[0].value,'transient');
 delete changed.read_configuration;assert.throws(()=>m.resourceReadChoices(changed),/configuration_unavailable/);
});
test('HTTP source access is explicit GET/POST, no-store, envelope-preserving and never an authorization JSON field',async()=>{
 const original=globalThis.fetch,calls=[];globalThis.fetch=async(url,options)=>{calls.push({url,options});return new Response(JSON.stringify(view()));};
 try {
  const api=new LoomHttpApi();await api.readConversationView('c',{sourceAccess:'metadata'});
  await api.readConversationView('c',{sourceAccess:'local_read',readOptions:{projection_storage:'transient',content_index:'none'}});
  assert.equal(calls[0].options.method,'GET');assert.equal(calls[0].options.body,undefined);assert.equal(calls[0].options.cache,'no-store');
  assert.equal(calls[1].options.method,'POST');assert.deepEqual(JSON.parse(calls[1].options.body),{read_options:{projection_storage:'transient',content_index:'none'}});
  assert.equal(calls[1].options.cache,'no-store');assert.equal(calls[1].url,'/api/conversations/c/view');
  await assert.rejects(api.readConversationView('c',{sourceAccess:'metadata',readOptions:{}}),/metadata_options/);
  assert.equal(calls.length,2);
 } finally {globalThis.fetch=original;}
});
test('optional feature migration leaves old base catalogs valid and honors native suppression without bootstrap fallback',async()=>{
 const pack=JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack',import.meta.url),'utf8'));
 const base=p.resolvePresentation();assert.ok(p.resolvePresentationFeature(base,'conversation_view'));
 assert.throws(()=>p.resolvePresentationFeature(base,'conversation_view',null),p.PresentationError);
 for(const status of ['disabled','excluded','proposal','missing']) assert.throws(()=>p.resolvePresentationFeature(base,'conversation_view',{
  status,enabled:false,excluded:status==='excluded',value:pack.entries[0].value}),p.PresentationError);
 const effective={status:'effective',enabled:true,excluded:false,value:pack.entries[0].value};
 assert.match(p.featureMessage(p.resolvePresentationFeature(base,'conversation_view',effective),'local_read'),/locally/);
 const custom=structuredClone(effective);custom.value.locales.en.local_read='Synthetic owner label';
 assert.equal(p.featureMessage(p.resolvePresentationFeature(base,'conversation_view',custom),'local_read'),'Synthetic owner label');
 const polish=p.resolvePresentation(base.pack,'pl');assert.match(p.featureMessage(p.resolvePresentationFeature(polish,'conversation_view',effective),'local_read'),/lokalnie/);
 assert.throws(()=>p.resolvePresentation({available:false,status:'excluded',value:base.pack}),p.PresentationError);
});

test('older JNI host does not advertise a source reader without a native dispatch',()=>{
 assert.equal(LoomJniApi.prototype.readConversationView,undefined);
});

test('source wire preserves large integers, integral floats and nested unknown fields outside serializable display DTOs',async()=>{
 const doc=view();doc.messages[0].metadata.export.raw={unknown:'RAW_NUMERIC_TOKEN'};
 const wire=JSON.stringify(doc).replace('"RAW_NUMERIC_TOKEN"','{"large":9007199254740993,"integral_float":1.0,"nested":[{"future":9007199254740995}]}');
 const original=globalThis.fetch;globalThis.fetch=async()=>new Response(wire);
 try {
  const out=await new LoomHttpApi().readConversationView('c',{sourceAccess:'local_read'});
  assert.equal(m.conversationViewWireJson(out),wire);
  assert.equal(m.conversationViewWireJson(structuredClone(out)),undefined,'Opaque wire is not a serialized field');
  assert.equal(JSON.stringify(out).includes('9007199254740993'),false,'Decoded JS numbers are explicitly not numeric evidence');
  assert.equal(m.conversationViewWireJson(out).includes('"integral_float":1.0'),true);
  assert.equal(m.conversationViewWireJson(out).includes('"future":9007199254740995'),true);
  globalThis.fetch=async()=>new Response('{"PRIVATE_PARSE_CANARY":');
  await assert.rejects(new LoomHttpApi().readConversationView('c',{sourceAccess:'local_read'}),error=>
    error.message==='native_response_json_invalid'&&!error.message.includes('PRIVATE_PARSE_CANARY'));
 } finally {globalThis.fetch=original;}
});

test('view DTO rejects coercible enums and invalid UI identities without dropping or rewriting source rows',()=>{
 for(const patch of [{status:['active']},{storage:['reference']},{parent_id:17},{version_group_id:9007199254740992},
   {version_num:0},{version_num:-1},{version_num:9007199254740992},{created:{}},{weight:Infinity},{model:17}]) {
  const doc=view();Object.assign(doc.messages[0],patch);assert.throws(()=>m.readConversationView(doc,'c'),/message_invalid/);
 }
 const doc=view();doc.status=['complete'];assert.throws(()=>m.readConversationView(doc,'c'),/envelope_invalid/);
 const bad=view();bad.messages[0].source_ref.message_index=9007199254740992;
 assert.throws(()=>m.readConversationView(bad,'c'),/reference_index_invalid/);
});
