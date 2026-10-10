#!/usr/bin/env node
// Real App + native Catalog/ConversationView/store, no model transport.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:net';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, renameSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';
import { suiteCompletionGuard } from './harness-lifecycle.mjs';

const web = fileURLToPath(new URL('../',import.meta.url)), loom=path.resolve(web,'..');
const serverBin=process.env.LOOM_SERVER_BIN || path.join(loom,'build/dev/server/loom-server');
const dist=path.join(web,'dist');
assert.ok(existsSync(serverBin) && existsSync(path.join(dist,'index.html')),'Build the composed native server and actual App first.');
const evidence=process.env.CONVERSATION_VIEW_EVIDENCE_DIR || mkdtempSync(path.join(tmpdir(),'loom-conversation-view-evidence-'));
mkdirSync(evidence,{recursive:true});assert.equal(existsSync(path.join(evidence,'receipt-before.json')),false,'Use fresh evidence directory.');
const data=mkdtempSync(path.join(tmpdir(),'loom-conversation-view-store-'));
const runtimeTmp=path.join(data,'runtime-tmp');mkdirSync(runtimeTmp,{recursive:true});
const source=mkdtempSync(path.join(tmpdir(),'loom-conversation-view-source-'));
const archive=path.join(source,'public.zip');
const canary='PUBLIC_P4_TRANSIENT_CANARY_20261009';
const degradedText='PUBLIC_P4_DEGRADED_SEND_20261010';
const chatSendText=JSON.parse(readFileSync(path.join(web,'src/onboarding/generated/conversation-view.json'),'utf8')).entries.find(entry=>entry.key==='presentation.chat_send').value.locales.en;
const sourceDoc=[{id:'p4-public',title:'P4 linked public conversation',current_node:'a2',mapping:{
  u:{id:'u',parent:null,children:['a1','a2'],message:{id:'u-msg',author:{role:'user'},content:{content_type:'text',parts:[canary+' **source** ![remote](https://example.invalid/p4-canary.png)']},future_field:{keep:[1,'unknown']},future_numeric:'WIRE_NUMERIC_FIXTURE'}},
  a1:{id:'a1',parent:'u',children:[],message:{id:'a1-msg',author:{role:'assistant'},content:{content_type:'text',parts:['Historical alternative']}}},
  a2:{id:'a2',parent:'u',children:[],message:{id:'a2-msg',author:{role:'assistant'},content:{content_type:'text',parts:['Current source answer']}}}}}];
writeFileSync(path.join(source,'conversations.json'),JSON.stringify(sourceDoc).replace('"WIRE_NUMERIC_FIXTURE"','{"large":9007199254740993,"integral_float":1.0,"nested":[{"future":9007199254740995}]}'));
const zip=spawnSync('python3',['-c','import sys,zipfile; z=zipfile.ZipFile(sys.argv[1],"w"); z.write(sys.argv[2],"conversations.json"); z.close()',archive,path.join(source,'conversations.json')],{encoding:'utf8'});
assert.equal(zip.status,0,zip.stderr);
mkdirSync(path.join(data,'profiles'),{recursive:true});
// Explicit fixture scan policy avoids content indexing before the user's read.
// Projection storage stays at its existing snapshot default; UI chooses transient.
writeFileSync(path.join(data,'profiles/worker.pack'),JSON.stringify({schema:'loom.runtime_profile_overlay/1',domain:'worker',overrides:{startup_delay_ms:600000}}));
writeFileSync(path.join(data,'profiles/resource_read.pack'),JSON.stringify({schema:'loom.runtime_profile_overlay/1',domain:'resource_read',overrides:{content_index:'none'}}));
const probe=createServer();await new Promise(resolve=>probe.listen(0,'127.0.0.1',resolve));const port=probe.address().port;await new Promise(resolve=>probe.close(resolve));
const base=`http://127.0.0.1:${port}`;
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const sources=['src/components/ChatView.tsx','src/components/ConversationSourceView.tsx','src/components/ApplicationProfiles.tsx','src/App.tsx','src/api/conversation-view.ts','src/api/loom-http.ts','src/onboarding/presentation.mjs','src/components/ImportedMessageContent.tsx','src/onboarding/generated/conversation-view.json','e2e/conversation-view-native.mjs'];
const sourceHashes=()=>Object.fromEntries(sources.map(file=>[file,sha(readFileSync(path.join(web,file)))]));
const before=sourceHashes();
const binHash=sha(readFileSync(serverBin));
writeFileSync(path.join(evidence,'receipt-before.json'),JSON.stringify({sources:before,server:serverBin,server_sha256:binHash,
  dist_index_sha256:sha(readFileSync(path.join(dist,'index.html'))),fixture_sha256:sha(readFileSync(archive)),data_dir:data,source_dir:source,native_tmpdir:runtimeTmp,
  model_calls:0,retention:{scan_content_index:'none',initial_projection_storage:'snapshot',selected_read_projection_storage:'transient'}},null,2));
let server,browser,page,serverLog='',failure;
const groups=[],remoteRequests=[],sendRequests=[],mutations=[],sourceReads=[],errors=[];
const complete=suiteCompletionGuard('conversation-view-native',9,groups);
async function request(method,route,body){const response=await fetch(base+route,{method,headers:{'Content-Type':'application/json'},...(body===undefined?{}:{body:JSON.stringify(body)})});
 const result=await response.json();assert.ok(response.ok && !result.error,`${route}: ${JSON.stringify(result)}`);return {result,response};}
async function start(){server=spawn(serverBin,['--host','127.0.0.1','--port',String(port),'--data-dir',data,'--static-dir',dist],{cwd:loom,env:{...process.env,TMPDIR:runtimeTmp},stdio:['ignore','pipe','pipe']});
 for(const output of [server.stdout,server.stderr])output.on('data',chunk=>{serverLog+=chunk.toString();});
 const deadline=Date.now()+20000;while(Date.now()<deadline){try{if((await fetch(base+'/api/healthz')).ok){await request('POST','/api/semantic/pause',{});return;}}catch{}if(server.exitCode!==null)throw new Error(serverLog);await new Promise(resolve=>setTimeout(resolve,100));}throw new Error('Native startup timeout');}
async function stop(){if(!server||server.exitCode!==null||server.signalCode!==null)return;const ended=new Promise(resolve=>server.once('exit',resolve));server.kill('SIGTERM');let timer;await Promise.race([ended,new Promise(resolve=>{timer=setTimeout(()=>{server.kill('SIGKILL');resolve();},3000);})]);clearTimeout(timer);}
async function check(name,fn){await fn();groups.push(name);console.log(`[conversation-view-native] PASS ${name}`);}
const chat=()=>page.getByTestId('application-profile-view').first();
async function selectLinked(){await page.getByTestId('conv-item').filter({has:page.locator('.title',{hasText:convTitle})}).click();}
const visibleReferences=()=>chat().locator('[data-testid="message"][data-storage="reference"]');
let convId,convTitle,headless,nativeRows,initialConfig;
try{
 await start();await request('PATCH','/api/config',{semantic_analysis:false});initialConfig=(await request('GET','/api/config')).result;
 await request('POST','/api/catalog/scan',{sources:[archive],retain_raw:'none',threads:1});
 const imported=(await request('POST','/api/catalog/import',{mode:'full',store_mode:'link',import_messages:true})).result;
 assert.equal(imported.conversations.length,1);convId=imported.conversations[0];
 convTitle=(await request('GET',`/api/conversations/${convId}`)).result.title;assert.equal(typeof convTitle,'string');
 nativeRows=(await request('GET',`/api/conversations/${convId}/messages?all=1`)).result;assert.equal(nativeRows.length,1);
 await check('headless metadata does not read source; explicit local read pins identity, mapping and evidence',async()=>{
  const {result:metadata,response}=await request('GET',`/api/conversations/${convId}/view`);
  assert.match(response.headers.get('cache-control'),/no-store/);assert.ok(metadata.resources.length);assert.equal(metadata.resources[0].status,'read_denied');
  assert.equal(metadata.read_configuration.values.projection_storage,'snapshot');assert.equal(metadata.capabilities.source_history_send.available,false);
  assert.deepEqual(metadata.messages.map(row=>row.id),nativeRows.map(row=>row.id));
  headless=(await request('POST',`/api/conversations/${convId}/view`,{read_options:{projection_storage:'transient',content_index:'none'}})).result;
  assert.equal(headless.status,'complete');assert.equal(headless.messages.length,3);assert.ok(headless.messages.every(row=>row.storage==='reference'));
  assert.equal(headless.resources[0].current,true);assert.ok(headless.resources[0].produced_by);assert.ok(headless.resources[0].method_receipt);
  assert.equal(headless.messages.find(row=>row.role==='user').metadata.export.raw.future_field.keep[1],'unknown');
  assert.deepEqual((await request('GET',`/api/conversations/${convId}/messages?all=1`)).result,nativeRows);
 });
 browser=await chromium.launch({headless:true,...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE?{executablePath:process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE}:{})});
 const context=await browser.newContext({hasTouch:true,viewport:{width:1500,height:1100}});page=await context.newPage();page.on('pageerror',err=>errors.push(err.message));
 await page.route('**/*',async route=>{const req=route.request(),url=new URL(req.url());
  if(url.origin!==base){remoteRequests.push(req.url());return route.abort();}
  if(url.pathname==='/api/chat'){sendRequests.push(req.postDataJSON());return route.abort();}
  if(url.pathname.includes('/messages/')&&req.method()!=='GET')mutations.push(req.url());
  if(url.pathname.endsWith('/view')&&req.method()==='POST')sourceReads.push(req.postDataJSON());
  return route.continue();});
 await page.goto(base);await selectLinked();
 await chat().getByTestId('conversation-sources').waitFor();
 const settings=async()=>({model:await chat().getByTestId('model-picker').inputValue(),history:await chat().getByTestId('include-history').isChecked(),
  memory:await chat().getByTestId('include-memory').isChecked(),graph:await chat().getByTestId('include-graph-memory').isChecked()});
 const beforeSettings=await settings();
 await check('ordinary App requires focus/Enter local read and preserves native effective defaults',async()=>{
  assert.equal(sourceReads.length,0);assert.equal(await visibleReferences().count(),0);
  assert.equal(await chat().getByTestId('source-read-projection_storage').inputValue(),'snapshot');
  await chat().getByTestId('source-read-projection_storage').selectOption('transient');
  await chat().getByTestId('source-read-content_index').selectOption('none');
  await chat().getByTestId('read-conversation-sources').focus();await page.keyboard.press('Enter');
  await visibleReferences().first().waitFor();await chat().getByTestId('show-all-messages').check();
  assert.equal(await visibleReferences().count(),headless.messages.length);assert.equal(sourceReads.length,1);
  assert.deepEqual(sourceReads[0],{read_options:{projection_storage:'transient',content_index:'none'}});
  assert.deepEqual(await settings(),beforeSettings);assert.deepEqual((await request('GET','/api/config')).result,initialConfig);
  await chat().getByTestId('conversation-view-evidence').locator('summary').click();
  const wire=await chat().getByTestId('conversation-view-exact-json').innerText();
  assert.match(wire,/"large"\s*:\s*9007199254740993/);assert.match(wire,/"integral_float"\s*:\s*1\.0/);
  assert.match(wire,/"future"\s*:\s*9007199254740995/);
  assert.equal(await chat().getByTestId('imported-source-raw').count(),0,'Reference raw JSON is inspected through exact native evidence');
 });
 await check('same version feeds branches and safe source renderer; mutation is refused and Send degrades to stored messages',async()=>{
  const user=visibleReferences().filter({has:page.locator('.body',{hasText:canary})});await user.waitFor();
  assert.equal(await user.getByTestId('edit-message').isDisabled(),true);assert.equal(await user.getByTestId('toggle-exclude').isDisabled(),true);
  await chat().getByTestId('show-conversation-branches').check();await chat().getByTestId('conversation-branches').locator('summary').click();
  const rows=chat().locator('.conversation-branch-list li');assert.deepEqual(await rows.evaluateAll(elements=>elements.map(el=>el.dataset.messageId).sort()),headless.messages.map(row=>row.id).sort());
  await rows.last().locator('button').tap();await chat().getByTestId('return-current-branch').click();
  assert.equal(sourceReads.length,1,'Display changes never repeat source access');
  // R43: the missing source-history capability degrades Send to stored messages; only the user's own setting blocks.
  const notice=chat().getByTestId('send-notice-source-history');await notice.waitFor();
  assert.equal(await notice.getAttribute('data-reason'),'source_egress_not_bound');
  assert.equal(await notice.locator('p').first().innerText(),chatSendText.source_history_omitted);
  await chat().getByTestId('chat-context-controls').locator('summary').click();
  await chat().getByTestId('block-send-without-source-history').check();
  await chat().getByTestId('chat-input').fill('This must not dispatch while the user blocks it');await chat().getByTestId('chat-input').press('Enter');
  assert.equal(await chat().getByTestId('send-chat').isDisabled(),true);
  assert.equal(await chat().getByTestId('send-chat').getAttribute('data-gate-initiator'),'user');
  assert.equal(await chat().getByTestId('preview-chat-request').isDisabled(),true);
  assert.deepEqual(sendRequests,[]);
  await chat().getByTestId('send-gate-unblock').click();
  await chat().getByTestId('chat-input').fill(degradedText);await chat().getByTestId('send-chat').click();
  await chat().getByTestId('chat-error').waitFor();
  assert.deepEqual(sendRequests,[{message:degradedText,conv_id:convId}],'Client request carries no source history; native history is the stored rows');
  assert.deepEqual(mutations,[]);assert.deepEqual(remoteRequests,[]);
  assert.equal(await visibleReferences().locator('img,iframe,audio,video,object,embed').count(),0);
  assert.deepEqual(await settings(),beforeSettings);
  assert.deepEqual((await request('GET',`/api/conversations/${convId}/messages?all=1`)).result,nativeRows);
 });
 await check('second explicit tap keeps source message identity and does not persist content in browser storage',async()=>{
  await chat().getByTestId('read-conversation-sources').tap();await visibleReferences().first().waitFor();
  const again=(await request('POST',`/api/conversations/${convId}/view`,{read_options:{projection_storage:'transient',content_index:'none'}})).result;
  assert.deepEqual(again.messages.map(row=>row.id),headless.messages.map(row=>row.id));assert.equal(sourceReads.length,2);
  const stored=await page.evaluate(()=>JSON.stringify({local:{...localStorage},session:{...sessionStorage}}));assert.equal(stored.includes(canary),false);
 });
 await check('old selected profile keeps existing settings until explicit native feature installation',async()=>{
  const user_id='synthetic/p4-legacy-view';
  const opened=(await request('POST','/api/onboarding',{operation:'open',user_id})).result;
  const oldPack=structuredClone(opened.snapshot.pack);
  oldPack.revision+=1;oldPack.entries=oldPack.entries.filter(entry=>entry.key!=='presentation.conversation_view');
  oldPack.entry_packs=oldPack.entry_packs.filter(pack=>pack.pack_id!=='loom.product.conversation_view');
  const old=(await request('POST','/api/onboarding',{operation:'update_pack',user_id,expected_revision:opened.revision,pack:oldPack,scenario:opened.snapshot.scenario_definition})).result;
  await page.evaluate(user=>localStorage.setItem('loom.user-profile.identity.v1',JSON.stringify({schema:'loom.user_profile_identity/1',user_id:user})),user_id);
  const countBefore=sourceReads.length;
  await page.reload();await selectLinked();await chat().getByTestId('install-conversation-source-catalog').waitFor();
  assert.equal(await chat().getByTestId('read-conversation-sources').count(),0);assert.equal(sourceReads.length,countBefore);
  await chat().getByTestId('install-conversation-source-catalog').tap();await chat().getByTestId('read-conversation-sources').waitFor();
  const installed=(await request('POST','/api/onboarding',{operation:'read',user_id})).result;
  assert.deepEqual(installed.snapshot.profile.privacy,old.snapshot.profile.privacy);
  assert.deepEqual(installed.snapshot.profile.settings,old.snapshot.profile.settings);
  for(const entry of oldPack.entries)assert.deepEqual(installed.snapshot.pack.entries.find(row=>row.id===entry.id),entry);
  assert.equal(sourceReads.length,countBefore,'Installing label data never grants local source access');
  await chat().getByTestId('source-read-projection_storage').selectOption('transient');
  await chat().getByTestId('read-conversation-sources').tap();await visibleReferences().first().waitFor();
 });
 await check('unavailable source clears transient display and retains explicit status plus stored placeholder',async()=>{
  renameSync(archive,archive+'.unavailable');await chat().getByTestId('read-conversation-sources').tap();
  await page.waitForFunction(()=>{
   const status=document.querySelector('[data-testid="conversation-source-status"] code');
   const read=document.querySelector('[data-testid="read-conversation-sources"]');
   try{return read&&!read.disabled&&JSON.parse(status?.textContent??'null')?.status==='unavailable';}catch{return false;}
  });
  assert.equal(await chat().getByTestId('conversation-view-status').getAttribute('data-status'),'unavailable');
  assert.equal(await visibleReferences().count(),0);assert.ok(await chat().locator('[data-testid="message"][data-storage="native"]').count());
  assert.match(await chat().getByTestId('conversation-source-status').innerText(),/unavailable/);
  assert.deepEqual((await request('GET',`/api/conversations/${convId}/messages?all=1`)).result,nativeRows);
  renameSync(archive+'.unavailable',archive);
 });
 await check('reopen requires a fresh explicit read and preserves version-pinned identities',async()=>{
  await stop();await start();await page.reload();await selectLinked();await chat().getByTestId('conversation-sources').waitFor();
  assert.equal(await visibleReferences().count(),0);await chat().getByTestId('source-read-projection_storage').selectOption('transient');
  await chat().getByTestId('read-conversation-sources').tap();await visibleReferences().first().waitFor();
  const after=(await request('POST',`/api/conversations/${convId}/view`,{read_options:{projection_storage:'transient',content_index:'none'}})).result;
  assert.deepEqual(after.messages.map(row=>row.id),headless.messages.map(row=>row.id));
  assert.deepEqual(remoteRequests,[]);assert.deepEqual(sendRequests,[{message:degradedText,conv_id:convId}]);assert.deepEqual(errors,[]);
 });
 await check('locally edited placeholder stays visible and keeps native edit/status capabilities',async()=>{
  await request('POST',`/api/messages/${nativeRows[0].id}/edit`,{text:'PUBLIC_LOCAL_OVERLAY_9281'});
  await page.reload();await selectLinked();const local=chat().locator('[data-testid="message"][data-storage="native"]').filter({has:page.locator('.body',{hasText:'PUBLIC_LOCAL_OVERLAY_9281'})});
  await local.waitFor();assert.equal(await local.getByTestId('toggle-exclude').isDisabled(),false);
  // Placeholder is a system message in some stored schemas; the existing role-specific
  // edit button must remain available whenever that native row supports the user editor.
  const stored=(await request('GET',`/api/conversations/${convId}/messages?all=1`)).result;
  const edited=stored.find(row=>row.text==='PUBLIC_LOCAL_OVERLAY_9281'&&row.status==='active');assert.ok(edited);
  const read=(await request('POST',`/api/conversations/${convId}/view`,{read_options:{projection_storage:'transient',content_index:'none'}})).result;
  const retained=read.messages.find(row=>row.id===edited.id);assert.ok(retained);assert.equal(retained.storage,'native');
  assert.equal(retained.capabilities.edit,true);assert.equal(retained.capabilities.set_status,true);
  assert.ok(read.resources.some(resource=>resource.status==='binding_unresolved'));
  await local.getByTestId('toggle-exclude').click();await chat().getByTestId('show-all-messages').check();
  await chat().locator('[data-testid="message"][data-status="excluded"]').filter({has:page.locator('.body',{hasText:'PUBLIC_LOCAL_OVERLAY_9281'})}).waitFor();
  assert.equal(await visibleReferences().count(),0);assert.deepEqual(sendRequests,[{message:degradedText,conv_id:convId}]);assert.deepEqual(remoteRequests,[]);
 });
 await check('transient source content is absent from native store, logs and browser settings',async()=>{
  await stop();const scanned=[];
  function walk(dir){for(const entry of readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);if(entry.isDirectory())walk(file);else if(entry.isFile()){scanned.push(file);assert.equal(readFileSync(file).includes(Buffer.from(canary)),false,`Unexpected retained source bytes: ${file}`);}}}
  walk(data);assert.equal(serverLog.includes(canary),false);assert.deepEqual(sourceHashes(),before);assert.equal(sha(readFileSync(serverBin)),binHash);
  writeFileSync(path.join(evidence,'retention-scan.json'),JSON.stringify({scanned,canary_found:false,scope:'Native data directory including isolated TMPDIR and captured server logs; authored fixture/evidence excluded'},null,2));
 });
 await page.screenshot({path:path.join(evidence,'ordinary-app.png'),fullPage:true});
}catch(error){failure=error;console.error(error.stack);if(page)await page.screenshot({path:path.join(evidence,'failure.png'),fullPage:true}).catch(()=>{});}
finally{await browser?.close();await stop();writeFileSync(path.join(evidence,'server.log'),serverLog);writeFileSync(path.join(evidence,'result.json'),JSON.stringify({groups,expected:9,passed:groups.length,error:failure?.stack??null,source_after:sourceHashes(),remoteRequests,sendRequests,mutations},null,2));}
if(failure)throw failure;
complete.complete();
console.log(`[conversation-view-native] ${groups.length}/9 groups passed; zero model calls; evidence ${evidence}`);
