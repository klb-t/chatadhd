#!/usr/bin/env node
// FOR THE INTEGRATOR ONLY. Real App/ImportPanel/GraphView/ChatView and native
// source actions; one loopback recording provider. No builds or dependency downloads.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import net from 'node:net';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, realpathSync, writeFileSync, readdirSync, cpSync, renameSync } from 'node:fs';
import { closeHttpFixture, request, stopChild, suiteCompletionGuard } from './harness-lifecycle.mjs';
import { makeTmpDir } from './tmp-dirs.mjs';

assert.equal(process.env.LOOM_FILE_GRAPH_NATIVE, '1', 'Only the integrator runs this harness, with explicit native opt-in.');
const loom = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..'), repo = path.dirname(loom);
const ownPath = input => { assert.ok(input); const p = realpathSync(input); assert.ok(p.startsWith(repo + path.sep), 'artifact must be in the integrator worktree'); return p; };
// Run the accepted relocatable package with its own default reader manifest.

const bin = ownPath(process.env.LOOM_SERVER_BIN), dist = ownPath(process.env.LOOM_FILE_GRAPH_DIST);
assert.ok(existsSync(path.join(dist, 'index.html')), 'use the integrator fresh continuous graph web build');
const evidence = path.resolve(process.env.LOOM_FILE_GRAPH_EVIDENCE ?? '');
assert.ok(process.env.LOOM_FILE_GRAPH_EVIDENCE && evidence.startsWith(repo + path.sep));
assert.ok(!existsSync(evidence), 'use a fresh evidence directory; preserve failed attempts');
mkdirSync(evidence, { recursive: true });
const require = createRequire(path.join(loom, 'web/package.json'));
const { chromium } = require('playwright'); // own installed dependencies only; no shared fallback
const tmp = makeTmpDir('loom-afternoon-ui-'), root = path.join(tmp, 'accounts');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const calls = [], wire = [], groups = [], errors = [], observations = {};
const reopenOnly = process.env.LOOM_AFTERNOON_SCOPE === "reopen";
const bundleOnly = reopenOnly || process.env.LOOM_AFTERNOON_SCOPE === "bundle";
const expectedGroups = bundleOnly ? 1 : 3;
const completion = suiteCompletionGuard('afternoon-ui-native', expectedGroups, groups);

const stage = path.dirname(path.dirname(bin));
const readerPath = path.join(stage, 'share/loom/docx_basic/reader.py');
const runtimePath = path.join(stage, 'share/loom/docx_basic/runtime.json');
assert.ok(existsSync(readerPath) && existsSync(runtimePath), 'accepted default package helper and interpreter manifest required');
const starts = [];

const materializationWire = [], chatResults = [];
const text = m => typeof m.content === 'string' ? m.content : (m.content ?? []).filter(p => p.type === 'text').map(p => p.text).join('\n');
const systemText = body => body.messages.filter(m => m.role === 'system').map(text).join('\n');
const overlay = (domain, overrides) => {
  mkdirSync(path.join(root, 'profiles'), { recursive: true });
  writeFileSync(path.join(root, 'profiles', domain + '.pack'), JSON.stringify({ schema: 'loom.runtime_profile_overlay/1', domain, overrides }));
};
const freePort = () => new Promise((resolve, reject) => { const s = net.createServer(); s.once('error', reject); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); }); });
const until = async (fn, label) => { for (const end = Date.now() + 30000; Date.now() < end;) { if (await fn()) return; await new Promise(r => setTimeout(r, 75)); } throw Error('timeout: ' + label); };
let server, provider, browser, page, port, base;
const start = async () => {
  const args = ['--no-new-privs', '--landlock-access', 'fs'];
  for (const rule of ['execute,read-file,read-dir:/usr', 'read-file,write-file:/dev/null',
    'execute,read-file,read-dir:' + stage, 'read-file,read-dir:' + dist,
    'execute,write-file,read-file,read-dir,remove-dir,remove-file,make-dir,make-reg,make-sym,refer,truncate:' + tmp]) {
    args.push('--landlock-rule', 'path-beneath:' + rule);
  }
  args.push(bin, '--host', '127.0.0.1', '--port', String(port), '--accounts-root', root, '--data-dir', path.join(tmp, 'data'), '--static-dir', dist);
  const env = { PATH: '/usr/bin:/bin', LANG: 'C.UTF-8', TMPDIR: tmp, LOOM_LOG_ENABLED: '0', PYTHONDONTWRITEBYTECODE: '1' };
  starts.push({ command: ['/usr/bin/setpriv', ...args], env });
  server = spawn('/usr/bin/setpriv', args, { cwd: tmp, env, stdio: 'ignore' });
  await until(async () => { try { return (await request(base + '/api/healthz', { timeoutMs: 1000 })).ok; } catch { return false; } }, 'own native server');
};
const api = (route, body, method = body === undefined ? 'GET' : 'POST') => page.evaluate(async ({ route, body, method }) => {
  const r = await fetch(route, { method, headers: { 'Content-Type': 'application/json' }, ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  const parsed = await r.json(); if (!r.ok || parsed.error) throw Error(route + ': ' + r.status + ' ' + JSON.stringify(parsed)); return parsed;
}, { route, body, method });
const dismiss = async () => { if (await page.getByTestId('guest-notice-close').isVisible().catch(() => false)) await page.getByTestId('guest-notice-close').click(); };
const workshop = async tab => {
  await dismiss();
  if (await page.getByTestId('close-panel').isVisible().catch(() => false)) await page.getByTestId('close-panel').click();
  await until(async () => await page.getByTestId('nav-' + tab).isVisible().catch(() => false) || await page.getByTestId('shell-menu-toggle').isVisible().catch(() => false), 'shell ready after navigation');
  if (!await page.getByTestId('nav-' + tab).isVisible().catch(() => false)) {
    if (await page.getByTestId('shell-menu-toggle').isVisible().catch(() => false)) {
      await page.getByTestId('shell-menu-toggle').click(); await page.getByTestId('shell-menu-workshop').click();
      await page.getByTestId('window-switcher').waitFor({ state: 'visible' });
    }
    if (!await page.getByTestId('nav-' + tab).isVisible()) await page.getByTestId('toggle-nav').click();
  }
  await page.getByTestId('nav-' + tab).waitFor({ state: 'visible' });
  await page.getByTestId('nav-' + tab).click();
};
const panel = async host => {
  const p = page.getByTestId(host).getByTestId('source-parts-panel');
  await p.waitFor(); if (await p.getAttribute('open') === null) await p.locator('summary').click();
  await p.getByTestId('source-reload').waitFor(); await until(async () => !await p.getByTestId('source-reload').isDisabled(), 'source read');
  assert.equal(await p.getByTestId('source-error').count(), 0); return p;
};
const upload = async (name, bytes, mime, previous = '') => {
  const p = await panel('import-panel');
  await p.getByTestId('source-upload-file').setInputFiles({ name, mimeType: mime, buffer: Buffer.from(bytes) });
  await p.getByTestId('source-upload-revision').selectOption(previous);
  const response = page.waitForResponse(r => r.url().endsWith('/api/client/blob/upload'));
  await p.getByTestId('source-upload').click(); const r = await response; assert.equal(r.status(), 200);
  const stored = await r.json(); assert.equal(stored.hash, sha(bytes)); assert.ok(stored.source_key && stored.source_id, 'durable upload hook required');
  await p.locator('[data-testid="source-descriptor"][data-source-key="' + stored.source_key + '"][data-hash="' + stored.hash + '"]').waitFor();
  assert.equal(await p.getByTestId('source-error').count(), 0); return stored;
};
const materialize = async () => {
  const p = await panel('import-panel'), response = page.waitForResponse(r => r.url().endsWith('/api/client/blob/text'));
  await p.getByTestId('source-materialize').click(); const r = await response; assert.equal(r.status(), 200, await r.text()); const result = await r.json();
  await p.locator('[data-testid="source-descriptor"][data-source-id="' + result.source_id + '"]').waitFor();
  await until(async () => (await p.getByTestId('source-occurrence').locator('option').count()) === result.occurrence_nodes.length, 'occurrence list');
  return result;
};
const read = async (p, id) => {
  await p.getByTestId('source-occurrence').selectOption(id);
  const response = page.waitForResponse(r => r.url().endsWith('/api/client/blob/text/read'));
  await p.getByTestId('source-read').click(); const r = await response; assert.equal(r.status(), 200);
  const result = await r.json(); await p.locator('[data-testid="source-read-result"][data-node-id="' + id + '"]').waitFor();
  assert.equal(await p.getByTestId('source-text').textContent(), result.text); return result;
};
const select = async (p, id) => {
  await read(p, id); await p.getByTestId('source-hops').fill('0'); await p.getByTestId('source-select').click();
  await p.locator('[data-testid="source-selected-id"][data-node-id="' + id + '"]').waitFor();
  await p.getByTestId('source-notice').waitFor();
};
const send = async (id, expected, absent) => {
  const p = await panel('chat-view'); await p.getByTestId('source-reload').click();
  await p.locator('[data-testid="source-selected-id"][data-node-id="' + id + '"]').waitFor();
  const before = calls.length, requests = wire.length;
  const chatResponse = page.waitForResponse(r => r.url().endsWith('/api/chat') && r.request().method() === 'POST');
  await page.getByTestId('chat-input').fill('Przytocz wskazany fragment grafu.'); await page.getByTestId('send-chat').click();
  if (await page.getByTestId('guest-terms-send').isVisible().catch(() => false)) await page.getByTestId('guest-terms-send').click();
  await until(() => calls.length === before + 1, 'actual provider request');
  const reply = page.locator('[data-testid="message"][data-role="assistant"]').filter({ hasText: 'GRAPH_PROVIDER: ' + (before + 1) + ' ' }).last();
  await reply.waitFor(); await page.getByTestId('streaming-message').waitFor({ state: 'detached' });
  const visibleAnswer = await reply.innerText();
  assert.ok(visibleAnswer.includes(expected), 'visible answer derives from selected text');
  assert.ok(visibleAnswer.includes('[' + id + ']'), 'visible answer cites ID received in system context');
  const system = systemText(calls.at(-1)); assert.ok(system.includes('[' + id + ']')); assert.ok(system.includes(expected));
  assert.ok(!system.includes(absent)); assert.equal(system.split(expected).length - 1, 1, 'one selected occurrence, not every equal text');
  const outgoing = wire.slice(requests).at(-1); assert.ok(outgoing); assert.ok(!outgoing.attachments?.length);
  assert.ok(!outgoing.message.includes(expected)); assert.notEqual(outgoing.include_graph_memory, false);
  const response = await chatResponse; assert.equal(response.status(), 200);
  const events = (await response.text()).split('\n').filter(l => l.startsWith('data:')).map(l => JSON.parse(l.slice(5)));
  assert.ok(!events.some(e => e.type === 'error'));
  const done = events.find(e => e.type === 'done'); assert.ok(done?.conv_id && done.message_id);
  if(chatResults.length) assert.equal(done.conv_id,chatResults[0].conv_id,'same actual conversation after view changes');
  if(await page.getByTestId('chat-conversation').count()) assert.equal(await page.getByTestId('chat-conversation').getAttribute('data-conversation'),done.conv_id);
  const history = await api('/api/conversations/' + done.conv_id + '/messages?all=1');
  assert.ok(history.some(m => m.id === done.message_id && m.text.includes(expected)));
  chatResults.push({ conv_id: done.conv_id, message_id: done.message_id });
  observations.lastSent = { id, expected, request: outgoing, system, visibleAnswer };
};
const check = async (name, fn) => { await fn(); groups.push(name); console.log('[continuous-graph-native] PASS ' + name); await page.screenshot({ path: path.join(evidence, groups.length + '.png'), fullPage: true }); };

const fixtureDir = ownPath(process.env.LOOM_AFTERNOON_FIXTURES);
const seed = JSON.parse(readFileSync(path.join(fixtureDir,'seed.json')));
const bundleSeed = JSON.parse(readFileSync(path.join(fixtureDir,'complete-seed.json')));
const partialSeed = JSON.parse(readFileSync(path.join(fixtureDir,'partial-seed.json')));
const chooseProjection = async (p,receipt) => {
  await p.getByTestId('source-version').selectOption(receipt.source_id);
  await p.locator('[data-testid="source-descriptor"][data-source-id="'+receipt.source_id+'"]').waitFor();
  await until(async()=>await p.getByTestId('source-projection').inputValue()===receipt.projection_node,'projection loaded');
  return p.getByTestId('projection-review');
};
const operation = async (button,op) => {
  const response=page.waitForResponse(r=>r.url().endsWith('/api/client/blob/text')&&r.request().postDataJSON()?.operation===op);
  await button.click();const res=await response;assert.equal(res.status(),200,await res.text());return res.json();
};
const rawHash = async h => page.evaluate(async h=>{const r=await fetch('/api/client/blob/'+h+'/raw');if(!r.ok)throw Error('original HTTP '+r.status);const b=await r.arrayBuffer();return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',b)),x=>x.toString(16).padStart(2,'0')).join('');},h);
let newProjection, chosenRegion, restored;
try {
  port=await freePort();base='http://127.0.0.1:'+port;
  const providerPort=await freePort(),providerBase='http://127.0.0.1:'+providerPort;
  provider=createServer(async(req,res)=>{
    if(req.method!=='POST'){res.writeHead(200,{'Content-Type':'application/json'}).end('{"data":[]}');return;}
    const chunks=[];for await(const c of req)chunks.push(c);const body=JSON.parse(Buffer.concat(chunks));calls.push(body);
    const system=systemText(body), selected=seed.regions.find(n=>system.includes(n.id));
    const answer='REGION_PROVIDER: '+(selected?'['+selected.id+'] '+selected.text:'BRAK_WYBRANEGO_REGIONU');
    res.writeHead(200,{'Content-Type':'application/json'}).end(JSON.stringify({choices:[{message:{role:'assistant',content:answer},finish_reason:'stop'}]}));
  });await new Promise(resolve=>provider.listen(providerPort,'127.0.0.1',resolve));
  overlay('server_accounts',{allow_private_hosts:['127.0.0.1'],guests:{enabled:true,start:'auto',keys_file:'',sweep_interval_seconds:0,model_sources:['free'],workdir:{subdir:'guests',idle_ttl_seconds:0,max_bytes:0},protection:{pow:{bits:0,bits_under_load:0},per_session:{chat_per_minute:0}}}});
  overlay('free_providers',{providers:[{id:'ui_fixture',display_name:'Lokalna atrapa UI',base_url:providerBase,tier:'free',enabled:true,active_when:'none',auth_kind:'none',data_terms:{retention:'none',training:'no',human_review:false,observed_at:'2026-11-01',source_rating:'synthetic',source_url:'synthetic-fixture',summary:{pl:'Atrapa lokalna.',en:'Local double.'}}}],free_model_choice:{rank_source:'table',require:{training:'no'},table:[{model:'synthetic/ui',provider:'ui_fixture',quality:1}]}});
  await start();browser=await chromium.launch();const context=await browser.newContext({locale:'pl-PL',viewport:{width:1280,height:1050},acceptDownloads:true});
  await context.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
  await context.addInitScript(()=>{if(location.hostname==='127.0.0.1')localStorage.setItem('loom.store.policy',JSON.stringify({targets:['browser_indexeddb']}));});
  page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(r.method()==='POST'&&r.url().endsWith('/api/chat'))wire.push(r.postDataJSON());if(r.method()==='POST'&&r.url().endsWith('/api/client/blob/text'))materializationWire.push(r.postDataJSON());});
  await page.goto(base);await page.getByTestId('chat-input').waitFor();await api('/api/config');
  // Only this freshly-created guest's runtime is replaced, while both handles are stopped.
  await page.goto('about:blank');await stopChild(server);server=undefined;
  const guests=readdirSync(path.join(root,'guests'));assert.equal(guests.length,1);const guest=path.join(root,'guests',guests[0]);
  renameSync(guest,path.join(tmp,'empty-guest-before-seed'));cpSync(process.env.LOOM_AFTERNOON_RUNTIME_SEED ? ownPath(process.env.LOOM_AFTERNOON_RUNTIME_SEED) : path.join(fixtureDir,'seed'),guest,{recursive:true});
  observations.seed={closedFixture:seed.copied_closed_fixture,seedLibrary:seed.seed_library_sha256,oldProjection:seed.old_projection.projection_node,ocrEngineRerun:false};
  await start();await page.goto(base);await page.getByTestId('chat-input').waitFor();
  await api('/api/config',{llm_provider:'local',local_model_base_url:providerBase,model_registry_autorefresh:'off',semantic_analysis:false,semantic_model:'',auto_title:false,intent_routing_method:'off',stream:false,ui_language:'pl',chat_flow:'plain',chat_representation:'raw',context_receipt_store:'always'},'PATCH');
  await page.reload();await workshop('import');
  if(!bundleOnly) await check('projection actual check/explicit reproject/mapping and no silent selector replacement',async()=>{
    const p=await panel('import-panel'),old=seed.old_projection;let view=await chooseProjection(p,old);
    const before=await api('/api/config'),writes=materializationWire.length;
    const checked=await operation(view.getByTestId('projection-check'),'projection_check');assert.equal(checked.current_replay,'refused');
    await view.locator('[data-testid="projection-replay"][data-status="refused"]').waitFor();assert.equal(await view.getByTestId('projection-reproject').isDisabled(),true);
    assert.equal(materializationWire.slice(writes).filter(r=>r.operation==='projection_reproject').length,0);
    // Actual fresh account state change invalidates confirmation; no write dispatched.
    await api('/api/config',{context_execution:{...before.context_execution,unified:{...before.context_execution.unified,legacy_seed_ids:[...before.context_execution.unified.legacy_seed_ids,'synthetic:stale']}}},'PATCH');
    await view.getByTestId('projection-confirm').check();await view.getByTestId('projection-reproject').click();
    await view.getByTestId('projection-review-error').waitFor();assert.match(await view.getByTestId('projection-review-error').innerText(),/zmienił/);
    assert.equal(materializationWire.slice(writes).filter(r=>r.operation==='projection_reproject').length,0);
    await api('/api/config',{context_execution:before.context_execution},'PATCH');
    await operation(view.getByTestId('projection-check'),'projection_check');await view.getByTestId('projection-confirm').check();
    const made=await operation(view.getByTestId('projection-reproject'),'projection_reproject');await view.getByTestId('projection-upgrade-result').waitFor();
    assert.equal(made.selection_changed,false);assert.deepEqual(made.mapping.map(m=>m.old_node),[old.occurrence_nodes[0]]);assert.equal(made.mapping[0].status,'decision_required');
    assert.deepEqual((await api('/api/config')).context_execution,before.context_execution);assert.equal((await api('/api/config')).synthetic_preserve,'keep');
    assert.equal(await p.getByTestId('source-version').inputValue(),old.source_id);
    const req=materializationWire.findLast(r=>r.operation==='projection_reproject');assert.deepEqual(req.selected_nodes,[old.occurrence_nodes[0]]);assert.equal(req.expected_receipt_sha256,checked.old_receipt_sha256);
    const nodes=await api('/api/graph/nodes?kind=text_source_projection&limit=1000');newProjection=nodes.find(n=>n.id===made.new_projection).metadata;
    view=await chooseProjection(p,newProjection);const now=await operation(view.getByTestId('projection-check'),'projection_check');assert.equal(now.current_replay,'compatible');
    await select(p,newProjection.occurrence_nodes[0]);const cfg=await api('/api/config');assert.ok(cfg.context_execution.unified.legacy_seed_ids.includes(old.occurrence_nodes[0]));assert.ok(cfg.context_execution.unified.legacy_seed_ids.includes(newProjection.occurrence_nodes[0]));
    view=await chooseProjection(p,seed.empty);const empty=await operation(view.getByTestId('projection-check'),'projection_check');assert.equal(empty.current_replay,'not_exercised');await view.locator('[data-status="not_exercised"]').waitFor();
    assert.equal(await rawHash(old.derivation.original_sha256),old.derivation.original_sha256);
    observations.projection={checked,made,current:now,empty,request:req,selectionBefore:before.context_execution,selectionAfterExplicit:cfg.context_execution};
  });
  if(!bundleOnly) await check('existing OCR region UI -> global selection -> graph -> real provider context',async()=>{
    // Explicit test-user choice removes the previous PDF selectors before isolating this region.
    const cfg=await api('/api/config');await api('/api/config',{context_execution:{...cfg.context_execution,unified:{...cfg.context_execution.unified,legacy_seed_ids:['synthetic:unrelated'],include_knowledge:false,budget_tokens:8192,legacy_relation_hops:0}}},'PATCH');
    await workshop('graph');const p=page.getByTestId('image-region-context-panel');await p.locator('summary').click();await until(async()=>!await p.getByTestId('image-region-reload').isDisabled(),'OCR list');
    const rep=seed.representations[0];chosenRegion=seed.regions.find(n=>rep.metadata.region_nodes.includes(n.id)&&n.text.length>3);assert.ok(chosenRegion);
    await p.getByTestId('image-region-representation').selectOption(rep.id);await p.getByTestId('image-region-select').selectOption(chosenRegion.id);
    await p.locator('[data-testid="image-region-detail"][data-node-id="'+chosenRegion.id+'"]').waitFor();assert.equal(await p.getByTestId('image-region-text').innerText(),chosenRegion.text);
    const other=seed.regions.filter(n=>n.id!==chosenRegion.id&&!chosenRegion.text.includes(n.text)&&n.text.length>3);assert.ok(other.length);
    await p.getByTestId('image-region-hops').fill('0');await p.getByTestId('image-region-add').click();await p.getByTestId('image-region-notice').waitFor();
    const saved=await api('/api/config');assert.deepEqual(saved.context_execution.unified.legacy_seed_ids,['synthetic:unrelated',chosenRegion.id]);assert.equal(saved.synthetic_preserve,'keep');
    await p.getByTestId('image-region-graph').click();await page.locator('[data-testid="graph-view"][data-selected-node="'+chosenRegion.id+'"]').waitFor();
    await workshop('chat');await page.getByTestId('chat-input').fill('Przytocz wybrany region grafu.');await page.getByTestId('send-chat').click();
    if(await page.getByTestId('guest-terms-send').isVisible().catch(()=>false))await page.getByTestId('guest-terms-send').click();
    await until(()=>calls.length===1,'selected OCR actual provider');const reply=page.locator('[data-testid="message"][data-role="assistant"]').filter({hasText:'REGION_PROVIDER:'}).last();await reply.waitFor();await page.getByTestId('streaming-message').waitFor({state:'detached'});
    const system=systemText(calls[0]);assert.ok(system.includes(chosenRegion.id)&&system.includes(chosenRegion.text));assert.ok(system.includes('inferred')&&system.includes('null'));for(const n of other)assert.ok(!system.includes(n.text),n.text);
    assert.ok((await reply.innerText()).includes(chosenRegion.text));assert.ok(!wire.at(-1).message.includes(chosenRegion.text));assert.ok(!wire.at(-1).attachments?.length);
    observations.ocr={chosenId:chosenRegion.id,original:rep.metadata.source_sha256,regionText:chosenRegion.text,system,unselected:other.map(n=>n.id),selector:saved.context_execution};
  });
  if(bundleOnly){
    const previous=JSON.parse(readFileSync(path.join(fixtureDir,'browser-first/observations.json')));
    const nodes=await api('/api/graph/nodes?kind=text_source_projection&limit=1000');
    newProjection=nodes.find(n=>n.id===previous.projection.made.new_projection).metadata;
    chosenRegion=seed.regions.find(n=>n.id===previous.ocr.chosenId);
    observations.priorAccepted={file:'browser-first/observations.json',groups:previous.groups,notRerun:true};
  }
  // Source fixture needs an actual parent edge; use existing API, never fabricated restore assertions.
  const sourceMessages=await api('/api/conversations/'+bundleSeed.conversation+'/messages?all=1');
  const sourceFirst=sourceMessages.find(m=>m.id===bundleSeed.messages[0]),sourceSecond=sourceMessages.find(m=>m.id===bundleSeed.messages[1]);
  assert.ok(sourceFirst&&sourceSecond);
  if(sourceSecond.parent_id!==sourceFirst.id)await api('/api/messages/'+sourceSecond.id,{parent_id:sourceFirst.id},'PATCH');
  assert.equal((await api('/api/messages/'+sourceSecond.id)).parent_id,sourceFirst.id);
  observations.bundleSourceParent={first:sourceFirst.id,second:sourceSecond.id};
  if(!reopenOnly) await check('native bundle UI export/hash/restore/parents/originals/INCOMPLETE and common restart',async()=>{
    const cfgBefore=await api('/api/config');await workshop('import');const p=page.getByTestId('conversation-bundle-panel');await p.locator('summary').click();await p.getByTestId('bundle-load-conversations').click();await until(async()=>!await p.getByTestId('bundle-load-conversations').isDisabled(),'conversation list');
    await p.getByTestId('bundle-conversation').selectOption(bundleSeed.conversation);
    const exported=await operation(p.getByTestId('bundle-export'),'bundle_export');await p.getByTestId('bundle-download').waitFor();
    const dl=page.waitForEvent('download');await p.getByTestId('bundle-download').click();const download=await dl,zip=path.join(evidence,'conversation.zip');await download.saveAs(zip);assert.equal(sha(readFileSync(zip)),exported.hash);
    await p.getByTestId('bundle-restore-file').setInputFiles(zip);await p.getByTestId('bundle-expected-hash').fill('0'.repeat(64));const writeCount=materializationWire.length;await p.getByTestId('bundle-restore').click();await p.getByTestId('bundle-error').waitFor();assert.match(await p.getByTestId('bundle-error').innerText(),/nie zgadza/);assert.equal(materializationWire.length,writeCount);
    await p.getByTestId('bundle-expected-hash').fill(exported.hash);restored=await operation(p.getByTestId('bundle-restore'),'bundle_import');await p.locator('[data-testid="bundle-restore-result"][data-status="assets_verified"]').waitFor();
    assert.equal(restored.verified_slots,1);assert.equal(restored.lossless_database,false);const first=await api('/api/messages/'+restored.message_mapping['/messages/0']),second=await api('/api/messages/'+restored.message_mapping['/messages/1']);assert.equal(second.parent_id,first.id);assert.equal(first.attachments[0].hash,bundleSeed.original_hash);assert.equal(await rawHash(bundleSeed.original_hash),bundleSeed.original_sha256);
    await p.getByTestId('bundle-conversation').selectOption(partialSeed.conversation);await p.getByTestId('bundle-export-incomplete').check();const partial=await operation(p.getByTestId('bundle-export'),'bundle_export');assert.equal(partial.complete_assets,false);
    const partialDownload=page.waitForEvent('download');await p.getByTestId('bundle-download').click();const pd=await partialDownload,partialZip=path.join(evidence,'partial.zip');await pd.saveAs(partialZip);assert.equal(sha(readFileSync(partialZip)),partial.hash);
    await p.getByTestId('bundle-restore-file').setInputFiles(partialZip);await p.getByTestId('bundle-expected-hash').fill(partial.hash);await p.getByTestId('bundle-restore-incomplete').check();const incomplete=await operation(p.getByTestId('bundle-restore'),'bundle_import');await p.locator('[data-testid="bundle-restore-result"][data-status="INCOMPLETE"]').waitFor();assert.equal(incomplete.complete_assets,false);assert.equal(incomplete.verified_slots,0);assert.equal(incomplete.attachment_slots,1);
    assert.deepEqual((await api('/api/config')).context_execution,cfgBefore.context_execution);
    const beforeRestartRead=await api('/api/client/blob/text/read',{node_id:newProjection.occurrence_nodes[0]});
    observations.bundle={exported,restored,incomplete,parents:[first.id,second.id],originalSha:bundleSeed.original_sha256,reopened:false};
    await page.goto('about:blank');await stopChild(server);server=undefined;await start();await page.goto(base);await page.getByTestId('chat-input').waitFor();
    assert.deepEqual((await api('/api/config')).context_execution,cfgBefore.context_execution);assert.equal((await api('/api/messages/'+second.id)).parent_id,first.id);assert.equal(await rawHash(bundleSeed.original_hash),bundleSeed.original_sha256);
    const readback=await api('/api/client/blob/text/read',{node_id:newProjection.occurrence_nodes[0]});assert.deepEqual(readback,beforeRestartRead);
    await workshop('graph');const region=page.getByTestId('image-region-context-panel');await region.locator('summary').click();await until(async()=>!await region.getByTestId('image-region-reload').isDisabled(),'OCR reopen');await region.getByTestId('image-region-representation').selectOption(seed.representations[0].id);await region.getByTestId('image-region-select').selectOption(chosenRegion.id);await region.getByTestId('image-region-text').waitFor();assert.equal(await region.getByTestId('image-region-text').innerText(),chosenRegion.text);
    observations.bundle={exported,restored,incomplete,parents:[first.id,second.id],originalSha:bundleSeed.original_sha256,reopened:true};assert.deepEqual(errors,[]);
  });
  if(reopenOnly) await check('affected tail: exact selected PDF read and restored bundle/OCR persistence after native restart',async()=>{
    const rows=await api('/api/graph/nodes?kind=bundle_restore&limit=1000');
    const archive=sha(readFileSync(path.join(fixtureDir,'browser-bundle-fix/conversation.zip'))),partial=sha(readFileSync(path.join(fixtureDir,'browser-bundle-fix/partial.zip')));
    const receipt=rows.find(n=>n.metadata.archive_sha256===archive).metadata,inc=rows.find(n=>n.metadata.archive_sha256===partial).metadata;
    assert.equal(receipt.status,'assets_verified');assert.equal(receipt.verified_slots,1);assert.equal(inc.status,'INCOMPLETE');assert.equal(inc.verified_slots,0);assert.equal(inc.attachment_slots,1);
    const one=await api('/api/messages/'+receipt.message_mapping['/messages/0']),two=await api('/api/messages/'+receipt.message_mapping['/messages/1']);assert.equal(two.parent_id,one.id);assert.equal(one.attachments[0].hash,bundleSeed.original_hash);
    const beforeRead=await api('/api/client/blob/text/read',{node_id:newProjection.occurrence_nodes[0]}),beforeConfig=await api('/api/config');assert.ok(beforeRead.text.trim().length>0);
    await page.goto('about:blank');await stopChild(server);server=undefined;await start();await page.goto(base);await page.getByTestId('chat-input').waitFor();
    assert.deepEqual(await api('/api/client/blob/text/read',{node_id:newProjection.occurrence_nodes[0]}),beforeRead);assert.deepEqual((await api('/api/config')).context_execution,beforeConfig.context_execution);
    assert.equal((await api('/api/messages/'+two.id)).parent_id,one.id);assert.equal(await rawHash(bundleSeed.original_hash),bundleSeed.original_sha256);
    await workshop('import');const p=await panel('import-panel');await chooseProjection(p,newProjection);const visible=await read(p,newProjection.occurrence_nodes[0]);assert.deepEqual(visible,beforeRead);
    const bundle=page.getByTestId('conversation-bundle-panel');await bundle.locator('summary').click();await bundle.getByTestId('bundle-load-conversations').click();await until(async()=>!await bundle.getByTestId('bundle-load-conversations').isDisabled(),'restored list');await bundle.getByTestId('bundle-conversation').selectOption(receipt.conversation_id);
    await workshop('graph');const region=page.getByTestId('image-region-context-panel');await region.locator('summary').click();await until(async()=>!await region.getByTestId('image-region-reload').isDisabled(),'OCR reopen');await region.getByTestId('image-region-representation').selectOption(seed.representations[0].id);await region.getByTestId('image-region-select').selectOption(chosenRegion.id);await region.getByTestId('image-region-text').waitFor();assert.equal(await region.getByTestId('image-region-text').innerText(),chosenRegion.text);
    observations.reopen={receipt,incomplete:inc,read:beforeRead,selector:beforeConfig.context_execution,parents:[one.id,two.id],originalSha:bundleSeed.original_sha256};assert.deepEqual(errors,[]);assert.equal(calls.length,0,'no provider replay for persistence-only continuation');
  });
} finally {
  observations.groups=groups;observations.errors=errors;observations.starts=starts;observations.wire=materializationWire;observations.package={serverSha:sha(readFileSync(bin)),libSha:sha(readFileSync(path.join(stage,'lib/libloom.so'))),webSha:sha(readFileSync(path.join(dist,'index.html')))};
  if(page&&groups.length!==expectedGroups){try{observations.failure=await page.locator('body').innerText();await page.screenshot({path:path.join(evidence,'failure.png'),fullPage:true});}catch{}}
  writeFileSync(path.join(evidence,'observations.json'),JSON.stringify(observations,null,2));await browser?.close();await stopChild(server);await closeHttpFixture(provider);
}
completion.complete();console.log('[afternoon-ui-native] combined target-environment flow PASS');
