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
import { existsSync, mkdirSync, readFileSync, realpathSync, writeFileSync } from 'node:fs';
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
const tmp = makeTmpDir('loom-docx-ui-'), root = path.join(tmp, 'accounts');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const calls = [], wire = [], groups = [], errors = [], observations = {};
const expectedGroups = 1;
const completion = suiteCompletionGuard('continuous-graph-native', expectedGroups, groups);

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

try {
  port = await freePort(); base = 'http://127.0.0.1:' + port;
  const providerPort = await freePort(), providerBase = 'http://127.0.0.1:' + providerPort;
  provider = createServer(async (req, res) => {
    if (req.method !== 'POST') { res.writeHead(200, { 'Content-Type': 'application/json' }).end('{"data":[]}'); return; }
    const chunks = []; for await (const chunk of req) chunks.push(chunk);
    const body = JSON.parse(Buffer.concat(chunks)); calls.push(body);
    // Answer is computed ONLY from the actual request's system text.
    const lines = systemText(body).split('\n'), index = lines.findIndex(s => s.includes('Żółw WYBRANY'));
    const header = lines.slice(0, index).reverse().find(s => /\[n_text_occ_[a-f0-9]+\]/.test(s));
    const answer = 'GRAPH_PROVIDER: ' + calls.length + ' ' + (header ?? 'BRAK_ID') + '\n\n```text\n' + (lines[index] ?? 'BRAK_TEKSTU') + '\n```';
    res.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify({ choices: [{ message: { role: 'assistant', content: answer }, finish_reason: 'stop' }] }));
  }); await new Promise(resolve => provider.listen(providerPort, '127.0.0.1', resolve));
  overlay('server_accounts', { allow_private_hosts: ['127.0.0.1'], guests: { enabled: true, start: 'auto', keys_file: '', sweep_interval_seconds: 0, model_sources: ['free'],
    workdir: { subdir: 'guests', idle_ttl_seconds: 0, max_bytes: 0 }, protection: { pow: { bits: 0, bits_under_load: 0 }, per_session: { chat_per_minute: 0 } } } });
  overlay('free_providers', { providers: [{ id: 'docx_fixture', display_name: 'Lokalna atrapa grafu', base_url: providerBase, tier: 'free', enabled: true, active_when: 'none', auth_kind: 'none',
    data_terms: { retention: 'none', training: 'no', human_review: false, observed_at: '2026-11-01', source_rating: 'synthetic', source_url: 'synthetic-fixture', summary: { pl: 'Atrapa lokalna.', en: 'Local double.' } } }],
    free_model_choice: { rank_source: 'table', require: { training: 'no' }, table: [{ model: 'synthetic/docx-ui', provider: 'docx_fixture', quality: 1 }] } });
  await start(); browser = await chromium.launch({ args: ['--enable-unsafe-swiftshader'], ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
  const context = await browser.newContext({ locale: 'pl-PL', viewport: { width: 1280, height: 1000 }, hasTouch: true, acceptDownloads: true });
  await context.route('**/*', route => new URL(route.request().url()).hostname === '127.0.0.1' ? route.continue() : route.abort());
  await context.addInitScript(() => localStorage.setItem('loom.store.policy', JSON.stringify({ targets: ['browser_indexeddb'] })));
  page = await context.newPage(); page.on('pageerror', e => errors.push(e.message));
  page.on('response', async r => {if(r.url().endsWith('/api/graph/navigate')) {try {const v=await r.json(); observations.graphs??=[]; observations.graphs.push(v);}catch{}}});
  page.on('request', r => { if (r.method() === 'POST' && r.url().endsWith('/api/chat')) wire.push(r.postDataJSON());
    if (r.method() === 'POST' && r.url().endsWith('/api/client/blob/text')) materializationWire.push(r.postDataJSON()); });
  await page.goto(base); await page.getByTestId('chat-input').waitFor();
  await api('/api/config', { llm_provider: 'local', local_model_base_url: providerBase, model_registry_autorefresh: 'off', semantic_analysis: false, semantic_model: '', auto_title: false,
    intent_routing_method: 'off', stream: false, ui_language: 'pl', chat_flow: 'plain', chat_representation: 'raw', context_receipt_store: 'always', navigator_mapping: JSON.stringify({alpha:{measure:'confidence',range:[0.25,1],missing:0.35,curve:'linear'}}),
    context_execution: { unified: { enabled: false, include_knowledge: false, budget_tokens: 8192, legacy_seed_ids: ['synthetic:existing-unresolved'], legacy_relation_hops: 0 }, graph_reply: { mode: 'off' } } }, 'PATCH');
  await page.reload(); await workshop('import');
  await check('actual selected file, continuous map/3D, camera, profiles, fallback and preserved provider context',async()=>{
    const bytes=Buffer.from('Żółw WYBRANY — rzeczywisty plik do widoku grafu.\n\nDrugi akapit poza wybranym zakresem.\n');
    const stored=await upload('widok.txt',bytes,'text/plain');const made=await materialize();
    const id=made.occurrence_nodes[0];assert.ok(id);const selectedText=(await read(await panel('import-panel'),id)).text.trimEnd();assert.ok(selectedText.includes('Żółw WYBRANY')); 
    observations.file={sha256:sha(bytes),stored,made,id};
    await select(await panel('import-panel'),id);await workshop('chat');await send(id,selectedText,'Drugi akapit poza');
    const configBefore=await api('/api/config');
    // Existing import graph is a separate projection of the same retained original;
    // a receipt's unified_context summary is not claimed to be an occurrence node.
    await workshop('import');await page.getByTestId('import-file-input').setInputFiles({name:'widok.txt',mimeType:'text/plain',buffer:bytes});
    const imported=page.waitForResponse(r=>r.url().endsWith('/api/import'));await page.getByTestId('start-import').click();const ir=await imported;assert.equal(ir.status(),200);await page.getByTestId('import-result').waitFor();
    const events=(await ir.text()).split('\n').filter(x=>x.startsWith('data:')).map(x=>JSON.parse(x.slice(5)));observations.import=events.find(e=>e.type==='done');assert.ok(observations.import&&!observations.import.error);
    await workshop('navigator');await page.getByTestId('nav-loading').waitFor({state:'detached'});
    await page.getByTestId('nav-row-area:imports').click();await page.getByTestId('nav-loading').waitFor({state:'detached'});
    let graph=observations.graphs.at(-1);observations.initialGraph=graph;
    writeFileSync(path.join(evidence,'initial-graph.json'),JSON.stringify(graph,null,2));
    const row=graph.children.find(r=>r.label.includes('widok.txt'));assert.ok(row,'retained imported file must appear in native graph');
    const nav=page.getByTestId('graph-navigator'), scope=page.getByTestId('nav-map-scope');
    await page.getByTestId('nav-row-3d-'+row.address).click();
    const state=page.getByTestId('ve3d-state');await state.waitFor({state:'attached'});await until(async()=>Number(await state.getAttribute('data-frames'))>2,'real rendered WebGL frames');
    assert.equal(await scope.getAttribute('data-selected-address'),row.address);
    assert.equal(await scope.getAttribute('data-context-id'),chatResults[0].conv_id);
    const snapshot=async()=>state.evaluate(el=>({...el.dataset}));
    observations.initial3d=await snapshot();assert.ok(JSON.parse(observations.initial3d.positions).some(p=>p.id===row.address));
    const camera=JSON.parse(observations.initial3d.camera),requests=observations.graphs.length;
    await page.getByTestId('nav-fov').fill('78');await until(async()=>await state.getAttribute('data-fov')==='78','FOV');
    const afterFov=await snapshot();assert.equal(JSON.parse(afterFov.camera).distance,camera.distance);assert.deepEqual(JSON.parse(afterFov.camera).target,camera.target);
    await page.getByTestId('nav-3d-dollyIn').click();await until(async()=>JSON.parse(await state.getAttribute('data-camera')).distance<camera.distance,'dolly');
    assert.equal(await state.getAttribute('data-fov'),'78');observations.fovDolly={before:observations.initial3d,afterFov,afterDolly:await snapshot()};
    const controls=page.locator('.gnav-visual-controls');
    await controls.getByText('Legenda kanałów',{exact:true}).click();
    assert.match(await controls.innerText(),/krycie/);assert.match(await controls.innerText(),/Brak miary/);
    // Same real backend nodes, opt-in Z = actual relation depth. Missing times remain missing, never synthetic zero.
    await controls.getByLabel('Układ / profil').selectOption('measures');
    await controls.getByLabel(/^Oś X/).selectOption('age');await controls.getByLabel(/^Oś Z/).selectOption('depth');
    await until(async()=>JSON.parse(await state.getAttribute('data-positions')).length>0,'cartesian real measures');
    observations.cartesian=await snapshot();
    const positions=JSON.parse(observations.cartesian.positions);assert.ok(positions.some(p=>p.id===row.address));
    // A deeper loaded subgraph is obtained explicitly, separately from local visibility.
    await page.getByTestId('nav-bar-deeper').click();await page.getByTestId('nav-loading').waitFor({state:'detached'});
    await until(()=>observations.graphs.length>requests,'explicit deeper result');
    const deep=observations.graphs.at(-1);observations.deepGraph=deep;
    await until(async()=>JSON.parse(await state.getAttribute('data-positions')).length>0,'deep positions');
    observations.deep3d=await snapshot();
    const pos=JSON.parse(observations.deep3d.positions),matched=pos.map(p=>({p,r:deep.children.find(r=>r.address===p.id)})).filter(x=>x.r);
    for(const {r,p} of matched){assert.equal(typeof r.depth,'number');assert.ok(Number.isFinite(p.wz));}
    const depths=[...new Set(matched.map(x=>x.r.depth))];
    assert.ok(depths.length>1,'real loaded graph must exercise independent varied Z, not flat rotation');
    assert.ok(new Set(matched.map(x=>x.p.wz)).size>1);observations.realZ=matched.map(({p,r})=>({id:p.id,depth:r.depth,wz:p.wz}));
    await page.getByTestId('ve3d-host').screenshot({path:path.join(evidence,'actual-data-z.png')});
    writeFileSync(path.join(evidence,'progress.json'),JSON.stringify(observations,null,2));
    const reqBeforeLocal=observations.graphs.length;
    await page.getByTestId('nav-local-floor').fill('0');await page.getByTestId('nav-local-softness').fill('1');
    const alphas=[];
    const fadeDepth=matched.find(x=>x.r.address!==row.address).r.depth;
    for(const reach of [fadeDepth-0.75,fadeDepth-0.50,fadeDepth-0.25].map(String)){await page.getByTestId('nav-local-reach').fill(reach);await page.waitForTimeout(160);alphas.push(JSON.parse(await state.getAttribute('data-alpha')));}
    const candidates=Object.keys(alphas[0]).filter(k=>k!==row.address&&alphas.every(a=>a[k]>0&&a[k]<1));
    assert.ok(candidates.some(k=>alphas[0][k]<alphas[1][k]&&alphas[1][k]<alphas[2][k]),'continuous intermediate opacity on real nodes');
    assert.equal(observations.graphs.length,reqBeforeLocal,'no fetch from fade');observations.fade=alphas;writeFileSync(path.join(evidence,'progress.json'),JSON.stringify(observations,null,2));
    const beforeKey=JSON.parse(await state.getAttribute('data-camera'));await page.getByTestId('nav-spatial').focus();await page.getByTestId('nav-spatial').press('ArrowLeft');
    await until(async()=>JSON.parse(await state.getAttribute('data-camera')).x!==beforeKey.x,'keyboard orbit');assert.equal(await state.getAttribute('data-fov'),'78');
    await controls.getByRole('button',{name:'Reset widoczności',exact:true}).click();
    for(const profile of ['radial','measures','age']){await controls.getByLabel('Układ / profil').selectOption(profile);await page.waitForTimeout(120);observations['profile_'+profile]={counts:await page.getByTestId('nav-local-counts').innerText(),unplaced:await page.getByTestId('nav-3d-unplaced').innerText(),legend:await controls.innerText()};}
    await page.screenshot({path:path.join(evidence,'actual-3d.png'),fullPage:true});
    await nav.getByRole('button',{name:'2D',exact:true}).click();assert.equal(await scope.getAttribute('data-selected-address'),row.address);
    await controls.getByLabel('Układ / profil').selectOption('radial');
    const map=page.getByTestId('nav-map');await map.focus();const zoom=await map.getAttribute('data-zoom');await map.press('+');assert.notEqual(await map.getAttribute('data-zoom'),zoom);
    await map.press('Home');await map.press('ArrowRight');
    // PointerType touch exercises the same installed pointer handlers, not a claim about physical Android.
    const circle=map.locator('circle').first(),cx=await circle.getAttribute('cx');
    await map.dispatchEvent('pointerdown',{pointerId:31,pointerType:'touch',clientX:80,clientY:80,bubbles:true});
    await map.dispatchEvent('pointermove',{pointerId:31,pointerType:'touch',clientX:120,clientY:100,bubbles:true});
    await map.dispatchEvent('pointerup',{pointerId:31,pointerType:'touch',clientX:120,clientY:100,bubbles:true});
    await until(async()=>await circle.getAttribute('cx')!==cx,'touch pan moves rendered mark');
    observations.touch='browser pointer touch handlers; physical mobile NOT RUN';
    await page.getByTestId('nav-map-open-3d').tap();await state.waitFor({state:'attached'});
    // Controlled WebGL failure exercises real app fallback without pretending failure is actual hardware lack.
    await page.getByTestId('ve3d-host').locator('canvas').first().evaluate(c=>c.dispatchEvent(new WebGLContextEvent('webglcontextlost',{bubbles:true,statusMessage:'synthetic context loss'})));
    await page.getByTestId('nav-3d-fallback').waitFor();assert.ok(await map.isVisible());assert.ok(await page.getByTestId('nav-rows').isVisible());
    assert.equal(await scope.getAttribute('data-selected-address'),row.address);
    observations.fallback=await page.getByTestId('nav-3d-fallback').innerText();writeFileSync(path.join(evidence,'progress.json'),JSON.stringify(observations,null,2));
    observations.relationProfiles={};
    for(const label of ['Kod','Historia','Cele']){
      await Promise.all([page.waitForResponse(r=>r.url().endsWith('/api/graph/navigate')),nav.locator('.gnav-relation-controls').getByRole('button',{name:label,exact:true}).click()]);
      await page.getByTestId('nav-loading').waitFor({state:'detached'});
      observations.relationProfiles[label]={perspectives:await nav.getAttribute('data-perspectives'),rows:observations.graphs.at(-1).children.length};
      assert.equal(await scope.getAttribute('data-selected-address'),row.address);
      if(!observations.graphs.at(-1).children.length) await nav.getByText(/^Brak wierszy dla wybranych relacji/).waitFor();
    }
    const configAfter=await api('/api/config');assert.deepEqual(configAfter.context_execution,configBefore.context_execution);assert.deepEqual(configAfter.navigator_mapping,configBefore.navigator_mapping);
    await page.getByTestId('close-panel').click();await send(id,selectedText,'Drugi akapit poza');
    assert.deepEqual(errors,[]);observations.contextPreserved={before:configBefore.context_execution,after:configAfter.context_execution,providerCalls:calls.length};
  });
} finally {
  observations.groups=groups;observations.errors=errors;observations.starts=starts;observations.package={serverSha:sha(readFileSync(bin)),webSha:sha(readFileSync(path.join(dist,'index.html')))};
  if(page&&groups.length!==expectedGroups){try{observations.failure=await page.locator('body').innerText();await page.screenshot({path:path.join(evidence,'failure.png'),fullPage:true});}catch{}}
  writeFileSync(path.join(evidence,'observations.json'),JSON.stringify(observations,null,2));
  await browser?.close();await stopChild(server);await closeHttpFixture(provider);
}
completion.complete();console.log('[continuous-graph-native] one combined browser flow PASS');
