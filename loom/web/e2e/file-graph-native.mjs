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
assert.ok(existsSync(path.join(dist, 'index.html')), 'use the integrator fresh DOCX UI web build');
const evidence = path.resolve(process.env.LOOM_FILE_GRAPH_EVIDENCE ?? '');
assert.ok(process.env.LOOM_FILE_GRAPH_EVIDENCE && evidence.startsWith(repo + path.sep));
assert.ok(!existsSync(evidence), 'use a fresh evidence directory; preserve failed attempts');
mkdirSync(evidence, { recursive: true });
const require = createRequire(path.join(loom, 'web/package.json'));
const { chromium } = require('playwright'); // own installed dependencies only; no shared fallback
const tmp = makeTmpDir('loom-docx-ui-'), root = path.join(tmp, 'accounts');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const calls = [], wire = [], groups = [], errors = [], observations = {};
const contextOnly = process.env.LOOM_FILE_GRAPH_CONTEXT_ONLY === '1';
const expectedGroups = contextOnly ? 1 : 3;
const completion = suiteCompletionGuard('file-graph-native', expectedGroups, groups);

const docxMime = 'application/vnd.openxmlformats-officedocument.wordprocessingml.document';
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
const choose = async (p, id) => { await p.getByTestId('source-version').selectOption(id); await p.locator('[data-testid="source-descriptor"][data-source-id="' + id + '"]').waitFor(); };
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
const original = async (stored, bytes) => {
  const actual = await page.evaluate(async hash => {
    const r = await fetch('/api/client/blob/' + hash + '/raw'); if (!r.ok) throw Error('original ' + r.status); return Array.from(new Uint8Array(await r.arrayBuffer()));
  }, stored.hash ?? stored.source_sha256);
  assert.deepEqual(Buffer.from(actual), Buffer.from(bytes));
};
const send = async (id, expected, absent) => {
  const p = await panel('chat-view'); await p.getByTestId('source-reload').click();
  await p.locator('[data-testid="source-selected-id"][data-node-id="' + id + '"]').waitFor();
  const before = calls.length, requests = wire.length;
  const chatResponse = page.waitForResponse(r => r.url().endsWith('/api/chat') && r.request().method() === 'POST');
  await page.getByTestId('chat-input').fill('Przytocz wskazany fragment grafu.'); await page.getByTestId('send-chat').click();
  if (await page.getByTestId('guest-terms-send').isVisible().catch(() => false)) await page.getByTestId('guest-terms-send').click();
  await until(() => calls.length === before + 1, 'actual provider request');
  const reply = page.locator('[data-testid="message"][data-role="assistant"]').filter({ hasText: 'DOCX_PROVIDER: ' + (before + 1) + ' ' }).last();
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
  assert.equal(await page.getByTestId('chat-conversation').getAttribute('data-conversation'), done.conv_id);
  const history = await api('/api/conversations/' + done.conv_id + '/messages?all=1');
  assert.ok(history.some(m => m.id === done.message_id && m.text.includes(expected)));
  chatResults.push({ conv_id: done.conv_id, message_id: done.message_id });
  observations.lastSent = { id, expected, request: outgoing, system, visibleAnswer };
};
const check = async (name, fn) => { await fn(); groups.push(name); console.log('[file-graph-native] PASS ' + name); await page.screenshot({ path: path.join(evidence, groups.length + '.png'), fullPage: true }); };

// Downloads use the actual existing hash-checking button. Verify the saved bytes independently too.
const download = async (p, section, expectedBytes) => {
  const event = page.waitForEvent('download');
  await p.getByTestId(section).getByTestId('message-attachment-original').click();
  const saved = await event, file = await saved.path(); assert.ok(file);
  const bytes = readFileSync(file); assert.deepEqual(bytes, expectedBytes); assert.equal(sha(bytes), sha(expectedBytes));
  assert.equal(await p.getByTestId('attachment-original-error').count(), 0);
};
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
    const answer = contextOnly ? 'FILE_GRAPH_PROVIDER:\n'+systemText(body) : 'DOCX_PROVIDER: ' + calls.length + ' ' + (header ?? 'BRAK_ID') + '\n\n```text\n' + (lines[index] ?? 'BRAK_TEKSTU') + '\n```';
    res.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify({ choices: [{ message: { role: 'assistant', content: answer }, finish_reason: 'stop' }] }));
  }); await new Promise(resolve => provider.listen(providerPort, '127.0.0.1', resolve));
  overlay('server_accounts', { allow_private_hosts: ['127.0.0.1'], guests: { enabled: true, start: 'auto', keys_file: '', sweep_interval_seconds: 0, model_sources: ['free'],
    workdir: { subdir: 'guests', idle_ttl_seconds: 0, max_bytes: 0 }, protection: { pow: { bits: 0, bits_under_load: 0 }, per_session: { chat_per_minute: 0 } } } });
  overlay('free_providers', { providers: [{ id: 'docx_fixture', display_name: 'Lokalna atrapa DOCX', base_url: providerBase, tier: 'free', enabled: true, active_when: 'none', auth_kind: 'none',
    data_terms: { retention: 'none', training: 'no', human_review: false, observed_at: '2026-11-01', source_rating: 'synthetic', source_url: 'synthetic-fixture', summary: { pl: 'Atrapa lokalna.', en: 'Local double.' } } }],
    free_model_choice: { rank_source: 'table', require: { training: 'no' }, table: [{ model: 'synthetic/docx-ui', provider: 'docx_fixture', quality: 1 }] } });
  await start(); browser = await chromium.launch({ ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
  const context = await browser.newContext({ locale: 'pl-PL', viewport: { width: 1280, height: 1000 }, acceptDownloads: true });
  await context.route('**/*', route => new URL(route.request().url()).hostname === '127.0.0.1' ? route.continue() : route.abort());
  await context.addInitScript(() => localStorage.setItem('loom.store.policy', JSON.stringify({ targets: ['browser_indexeddb'] })));
  page = await context.newPage(); page.on('pageerror', e => errors.push(e.message));
  page.on('request', r => { if (r.method() === 'POST' && r.url().endsWith('/api/chat')) wire.push(r.postDataJSON());
    if (r.method() === 'POST' && r.url().endsWith('/api/client/blob/text')) materializationWire.push(r.postDataJSON()); });
  await page.goto(base); await page.getByTestId('chat-input').waitFor();
  await api('/api/config', { llm_provider: 'local', local_model_base_url: providerBase, model_registry_autorefresh: 'off', semantic_analysis: false, semantic_model: '', auto_title: false,
    intent_routing_method: 'off', stream: false, ui_language: 'pl', chat_flow: 'plain', chat_representation: 'raw', context_receipt_store: 'always',
    context_execution: { unified: { enabled: false, include_knowledge: false, budget_tokens: 8192, legacy_seed_ids: ['synthetic:existing-unresolved'], legacy_relation_hops: 0 }, graph_reply: { mode: 'off' } } }, 'PATCH');
  await page.reload(); await workshop('import');
  const files=ownPath(process.env.LOOM_FILE_GRAPH_FIXTURES);
  if (contextOnly) {
    await check('one combined real selector/context request across formats and archive-member occurrences',async()=>{
      const selected=[];
      for(const [name,mime] of [['document.pdf','application/pdf'],['workbook.xlsx','application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'],['simple.zip','application/zip'],['documents.zip','application/zip'],['document.odt','application/vnd.oasis.opendocument.text'],['publication.epub','application/epub+zip']]) {
        await workshop('import');await upload(name,readFileSync(path.join(files,name)),mime);const made=await materialize();
        const leaves=made.children??[made];
        for(const leaf of leaves) {
          const format=leaf.method.format;
          const wanted={pdf:'PDF_WYBRANY',xlsx:'Żółw WYBRANY',odt:'Żółw',epub:'Końcowy akapit.'}[format];
          let i=wanted?leaf.observations.findIndex(o=>o.text.includes(wanted)):0;assert.ok(i>=0);
          const p=await panel('import-panel');await choose(p,leaf.source_id);
          const id=leaf.occurrence_nodes[i],row=await read(p,id);await select(p,id);
          selected.push({format,archive:name.endsWith('.zip'),id,text:row.text,source:leaf.source_id});
        }
      }
      assert.equal(new Set(selected.map(x=>x.id)).size,selected.length);
      const sheets=selected.filter(x=>x.format==='xlsx');assert.equal(sheets.length,2);assert.notEqual(sheets[0].id,sheets[1].id);assert.equal(sheets[0].text,sheets[1].text);
      await workshop('chat');const before=calls.length;
      const response=page.waitForResponse(r=>r.url().endsWith('/api/chat')&&r.request().method()==='POST');
      await page.getByTestId('chat-input').fill('Przytocz wybrane fragmenty grafu.');await page.getByTestId('send-chat').click();
      if(await page.getByTestId('guest-terms-send').isVisible().catch(()=>false))await page.getByTestId('guest-terms-send').click();
      await until(()=>calls.length===before+1,'combined actual provider request');assert.equal((await response).status(),200);
      const reply=page.locator('[data-testid="message"][data-role="assistant"]').filter({hasText:'FILE_GRAPH_PROVIDER:'}).last();await reply.waitFor();
      const answer=await reply.innerText(),system=systemText(calls.at(-1));
      for(const part of selected){assert.ok(system.includes('['+part.id+']'));assert.ok(system.includes(part.text));assert.ok(answer.includes(part.id)&&answer.includes(part.text));}
      assert.ok(!system.includes('INNY_ZAKRES'));assert.ok(!system.includes('Drugi akapit:'));
      assert.equal(system.split('Żółw WYBRANY').length-1,2,'two separate occurrences of equal text');
      assert.ok(!wire.at(-1).message.includes('Żółw')&&!wire.at(-1).attachments?.length);
      observations.combined={selected,system,answer,request:wire.at(-1)};assert.deepEqual(errors,[]);
    });
  } else {
  const results={}, originals={}, ids=[];
  await check('application file import: PDF structure, two-sheet workbook, ZIP hierarchy, same nested documents, ODT and EPUB',async()=>{
    for(const [name,mime] of [['document.pdf','application/pdf'],['workbook.xlsx','application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'],['simple.zip','application/zip'],['documents.zip','application/zip'],['document.odt','application/vnd.oasis.opendocument.text'],['publication.epub','application/epub+zip']]){
      await workshop('import');const bytes=readFileSync(path.join(files,name));originals[name]=bytes;
      const stored=await upload(name,bytes,mime);const made=await materialize();results[name]={stored,made};ids.push(made.source_id);
      await original(stored,bytes);const p=await panel('import-panel');
      assert.equal(await p.getByTestId('source-error').count(),0);
      if(name==='document.pdf'){
        assert.equal(made.structure_nodes.pages.length,2);assert.equal(made.structure_nodes.chapters.length,2);
        assert.ok(made.observations.some(o=>o.text.includes('PDF_WYBRANY')));
        await p.getByTestId('source-structure').waitFor();
      }
      if(name==='workbook.xlsx'){
        const st=made.structure_nodes;assert.equal(st.filter(n=>n.kind==='sheet').length,2);
        assert.ok(st.filter(n=>n.kind==='range').length>=3);
        assert.ok(st.some(n=>n.kind==='cell'&&n.attrs.status==='unsupported'));
        assert.ok(!made.observations.some(o=>o.text.includes('123456789')),'formula cached value cannot become observation');
        const ob=made.observations.find(o=>o.text==='Żółw WYBRANY');assert.ok(ob);
        observations.selected=made.occurrence_nodes[ob.ordinal];observations.selectedText=ob.text;
        const all=await api('/api/graph/expand',{seed_ids:st.map(n=>n.node_id),depth:1});
        for(const n of st)if(n.parent)assert.ok(all.edges.some(e=>e.src===n.node_id&&e.dst===n.parent),'persisted parent '+n.node_id);
        await p.getByTestId('source-structure').waitFor();
        await workshop('graph');const gp=await panel('graph-view');await choose(gp,made.source_id);await gp.getByTestId('source-show-graph').click();await page.getByTestId('graph-view').waitFor();
        await workshop('import');
      }
      if(name.endsWith('.zip')){
        assert.equal(made.coordinate,'archive_members');assert.ok(made.structure_nodes.some(n=>n.kind==='folder'));
        for(const child of made.children)assert.ok(child.source_id && child.source_node,'supported member has actual internal graph');
        await p.getByTestId('source-archive-children').waitFor();
        if(name==='documents.zip'){
          assert.equal(made.children.length,2);
          const pdf=made.children.find(c=>c.method.format==='pdf'),xlsx=made.children.find(c=>c.method.format==='xlsx');
          assert.equal(pdf.derivation.original_sha256,sha(originals['document.pdf']));assert.equal(xlsx.derivation.original_sha256,sha(originals['workbook.xlsx']));
          assert.equal(pdf.structure_nodes.pages.length,2);assert.equal(xlsx.structure_nodes.filter(n=>n.kind==='sheet').length,2);
          const graph=await api('/api/graph/expand',{seed_ids:[made.source_node,...made.structure_nodes.map(n=>n.node_id),...made.children.map(c=>c.source_node)],depth:1});
          observations.archiveGraph={nodes:graph.nodes,edges:graph.edges};assert.ok(graph.nodes.some(n=>n.id===pdf.source_node));
          await p.getByTestId('source-archive-children').getByRole('button').first().click();await p.getByTestId('source-derived').waitFor();
        }
      }
      if(name.endsWith('.odt'))assert.equal(made.structure_nodes.length,14);
      if(name.endsWith('.epub'))assert.equal(made.structure_nodes.filter(n=>n.kind==='chapter').length,2);
      console.log('[file-graph-native] imported '+name);observations[name]={originalSha:stored.hash,source:made.source_id,structure:made.structure_nodes,occurrences:made.occurrence_nodes};
    }
  });
  await check('global account selector reaches actual provider context; one common restart and originals/idempotence/refusal',async()=>{
    await workshop('import');let p=await panel('import-panel');await choose(p,results['workbook.xlsx'].made.source_id);
    await select(p,observations.selected);await workshop('chat');await send(observations.selected,observations.selectedText,'INNY_ZAKRES');
    await stopChild(server);server=null;await start();await page.reload();await workshop('import');p=await panel('import-panel');
    for(const [name,{stored,made}] of Object.entries(results)){
      await choose(p,made.source_id);await original(stored,originals[name]);
      if(made.occurrence_nodes.length)await read(p,made.occurrence_nodes[0]);
    }
    await choose(p,results['documents.zip'].made.source_id);
    const again=await materialize();assert.deepEqual(again,results['documents.zip'].made);
    await upload('unsafe.zip',readFileSync(path.join(files,'unsafe.zip')),'application/zip');p=await panel('import-panel');
    const response=page.waitForResponse(r=>r.url().endsWith('/api/client/blob/text'));await p.getByTestId('source-materialize').click();assert.ok((await response).status()>=400);await p.getByTestId('source-error').waitFor();
    const reject=await page.evaluate(async req=>{const r=await fetch('/api/client/blob/text',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(req)});return r.status;},{hash:results['workbook.xlsx'].stored.hash,source_key:'synthetic:forged',name:'workbook.xlsx',format:'xlsx',receipt:{approved:true}});
    assert.ok(reject>=400);observations.reopened=ids;assert.deepEqual(errors,[]);
  });
  await check('one synthetic image: actual application local OCR; actual vision explicitly NOT RUN',async()=>{
    await workshop('import');
    const png=await page.evaluate(()=>{const c=document.createElement('canvas');c.width=1100;c.height=650;const x=c.getContext('2d');x.fillStyle='white';x.fillRect(0,0,c.width,c.height);x.fillStyle='black';x.font='32px sans-serif';x.fillText('Termin spotkania: piątek godzina 09:00',35,60);x.fillText('Zażółć gęślą jaźń. Plan działania.',35,110);x.strokeStyle='blue';x.lineWidth=4;x.strokeRect(60,230,250,150);x.beginPath();x.arc(750,305,80,0,Math.PI*2);x.stroke();x.beginPath();x.moveTo(330,305);x.lineTo(630,305);x.lineTo(605,290);x.moveTo(630,305);x.lineTo(605,320);x.stroke();return c.toDataURL('image/png').split(',')[1];});
    const image=Buffer.from(png,'base64');writeFileSync(path.join(evidence,'synthetic-image.png'),image);
    await page.getByTestId('import-file-input').setInputFiles({name:'synthetic-diagram.png',mimeType:'image/png',buffer:image});
    const response=page.waitForResponse(r=>r.url().endsWith('/api/import'));const before=calls.length;await page.getByTestId('start-import').click();const r=await response;assert.equal(r.status(),200);await page.getByTestId('import-result').waitFor();
    const events=(await r.text()).split('\n').filter(x=>x.startsWith('data:')).map(x=>JSON.parse(x.slice(5)));const done=events.find(e=>e.type==='done');assert.ok(done&&!done.error);assert.equal(done.format,'screenshot');assert.equal(calls.length,before);
    const convs=await api('/api/conversations');const imported=convs.find(c=>c.title.includes('synthetic-diagram'));assert.ok(imported);
    const messages=await api('/api/conversations/'+imported.id+'/messages');assert.ok(messages.some(m=>m.text.includes('Termin spotkania')));
    observations.image={sha256:sha(image),OCR:'PASS actual ImportPanel/local engine',vision:'NOT RUN: no verified authorized configured product vision provider/cost coverage; synthetic provider is not vision',truth:done.truth};
  });
  }
} finally {
  observations.groups=groups;observations.errors=errors;observations.starts=starts;observations.package={serverSha:sha(readFileSync(bin)),webSha:sha(readFileSync(path.join(dist,'index.html')))};
  if(page&&groups.length!==expectedGroups){try{observations.failure=await page.locator('body').innerText();await page.screenshot({path:path.join(evidence,'failure.png'),fullPage:true});}catch{}}
  writeFileSync(path.join(evidence,'observations.json'),JSON.stringify(observations,null,2));
  await browser?.close();await stopChild(server);await closeHttpFixture(provider);
}
completion.complete();console.log('[file-graph-native] '+groups.length+'/'+expectedGroups+' groups passed; actual vision NOT RUN');
