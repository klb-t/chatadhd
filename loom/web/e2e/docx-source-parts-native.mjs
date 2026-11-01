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

assert.equal(process.env.LOOM_DOCX_UI_NATIVE, '1', 'Only the integrator runs this harness, with explicit native opt-in.');
const loom = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..'), repo = path.dirname(loom);
const ownPath = input => { assert.ok(input); const p = realpathSync(input); assert.ok(p.startsWith(repo + path.sep), 'artifact must be in the integrator worktree'); return p; };
// Run the accepted relocatable package with its own default reader manifest.

const bin = ownPath(process.env.LOOM_SERVER_BIN), dist = ownPath(process.env.LOOM_DOCX_UI_DIST);
assert.ok(existsSync(path.join(dist, 'index.html')), 'use the integrator fresh DOCX UI web build');
const evidence = path.resolve(process.env.LOOM_DOCX_UI_EVIDENCE ?? '');
assert.ok(process.env.LOOM_DOCX_UI_EVIDENCE && evidence.startsWith(repo + path.sep));
assert.ok(!existsSync(evidence), 'use a fresh evidence directory; preserve failed attempts');
mkdirSync(evidence, { recursive: true });
const require = createRequire(path.join(loom, 'web/package.json'));
const { chromium } = require('playwright'); // own installed dependencies only; no shared fallback
const tmp = makeTmpDir('loom-docx-ui-'), root = path.join(tmp, 'accounts');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const calls = [], wire = [], groups = [], errors = [], observations = {};
const completion = suiteCompletionGuard('docx-source-parts-native', 6, groups);
const fixtures = path.join(loom, 'tests/fixtures/docx_parts');
const expected = JSON.parse(readFileSync(path.join(fixtures, 'base.json')));
const docxMime = 'application/vnd.openxmlformats-officedocument.wordprocessingml.document';
const stage = path.dirname(path.dirname(bin));
const readerPath = path.join(stage, 'share/loom/docx_basic/reader.py');
const runtimePath = path.join(stage, 'share/loom/docx_basic/runtime.json');
assert.ok(existsSync(readerPath) && existsSync(runtimePath), 'accepted default package helper and interpreter manifest required');
const starts = [];
const readFixture = name => readFileSync(path.join(fixtures, name + '.docx'));
const expectedText = Buffer.from(expected.text);
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
  await p.getByTestId('source-materialize').click(); const r = await response; assert.equal(r.status(), 200); const result = await r.json();
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
const check = async (name, fn) => { await fn(); groups.push(name); console.log('[docx-source-parts-native] PASS ' + name); await page.screenshot({ path: path.join(evidence, groups.length + '.png'), fullPage: true }); };

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
    const lines = systemText(body).split('\n'), index = lines.findIndex(s => s.includes('Żółw powtórzony.'));
    const header = lines.slice(0, index).reverse().find(s => /\[n_text_occ_[a-f0-9]+\]/.test(s));
    const answer = 'DOCX_PROVIDER: ' + calls.length + ' ' + (header ?? 'BRAK_ID') + '\n\n```text\n' + (lines[index] ?? 'BRAK_TEKSTU') + '\n```';
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
  let stored, parts, selected, chosenRead, firstRead, nextUpload, next;
  await check('DOCX upload is original-only until explicit extraction; stable key survives reload and original download validates', async () => {
    stored = await upload('żółw.docx', readFixture('base'), docxMime);
    const p = await panel('import-panel'); assert.equal(await p.getByTestId('source-upload-format').inputValue(), 'docx');
    assert.equal(materializationWire.length, 0); assert.equal(await p.getByTestId('source-occurrence').count(), 0);
    assert.equal((await api('/api/sources?limit=100')).filter(s => s.parser === 'loom.source_parts').length, 0);
    await download(p, 'source-original', readFixture('base'));
    await page.reload(); await workshop('import'); const reopened = await panel('import-panel'); await choose(reopened, stored.source_id);
    assert.equal(await reopened.getByTestId('source-descriptor').getAttribute('data-source-key'), stored.source_key);
    assert.equal(materializationWire.length, 0); assert.equal(calls.length, 0);
  });
  await check('explicit format docx produces derived parts, warnings, repeated occurrences and idempotent original-hash retry', async () => {
    parts = await materialize(); selected = parts.occurrence_nodes[1]; const p = await panel('import-panel');
    assert.equal(parts.coordinate, 'derived_utf8_bytes'); assert.equal(parts.status, 'partial');
    assert.equal(parts.source_sha256, sha(expectedText)); assert.equal(parts.derivation.original_sha256, stored.hash); assert.notEqual(parts.source_sha256, stored.hash);
    assert.deepEqual(materializationWire.at(-1), { hash: stored.hash, source_key: stored.source_key, name: 'żółw.docx', format: 'docx', language: 'pl' });
    assert.equal(await p.getByTestId('source-blocks').locator('li').count(), expected.blocks.length);
    for (const kind of ['paragraph', 'heading', 'list_item', 'table']) assert.ok(await p.getByTestId('source-blocks').locator('[data-kind="' + kind + '"]').count());
    for (const warning of expected.warnings) await p.locator('[data-warning="' + warning + '"]').waitFor();
    firstRead = await read(p, parts.occurrence_nodes[0]); chosenRead = await read(p, selected);
    assert.equal(firstRead.text, chosenRead.text); assert.notEqual(firstRead.node_id, chosenRead.node_id);
    assert.notDeepEqual(firstRead.provenance.byte_range, chosenRead.provenance.byte_range);
    for (const item of [firstRead, chosenRead]) assert.equal(expectedText.subarray(...item.provenance.byte_range).toString(), item.text);
    await read(p, parts.occurrence_nodes[5]); await read(p, parts.occurrence_nodes[6]); await read(p, parts.occurrence_nodes[7]);
    await download(p, 'source-original', readFixture('base')); await download(p, 'source-derived', expectedText);
    const count = (await api('/api/sources?limit=100')).length;
    assert.deepEqual(await materialize(), parts); assert.equal((await api('/api/sources?limit=100')).length, count);
    assert.equal(materializationWire.at(-1).hash, stored.hash); assert.equal(calls.length, 0);
  });
  await check('existing GraphView opens occurrence; account-wide selector preserves unrelated execution settings and IDs', async () => {
    await workshop('graph'); const p = await panel('graph-view'); await choose(p, parts.source_id); await read(p, selected);
    await page.locator('#graph-depth').press('ArrowRight'); assert.equal(await page.locator('#graph-depth').inputValue(), '2');
    await p.getByTestId('source-show-graph').click(); await page.locator('[data-testid="graph-view"][data-selected-node="' + selected + '"]').waitFor();
    const graph = await api('/api/graph/expand', { seed_ids: [selected], depth: 2 });
    assert.ok(graph.nodes.some(n => n.id === parts.source_node)); assert.ok(graph.edges.some(e => e.src === selected && e.dst === parts.version_node));
    const before = await api('/api/config'); await select(p, selected); await select(p, selected);
    const after = await api('/api/config'), expectedExecution = structuredClone(before.context_execution);
    Object.assign(expectedExecution.unified, { enabled: true, legacy_relation_hops: 0, legacy_seed_ids: ['synthetic:existing-unresolved', selected] });
    assert.deepEqual(after.context_execution, expectedExecution); observations.preservedExecution = after.context_execution;
  });
  await check('actual chat request and visible synthetic answer contain only the selected repeated DOCX occurrence', async () => {
    await workshop('chat'); await send(selected, chosenRead.text.trim(), 'Komórka A');
    const system = systemText(calls.at(-1)); assert.ok(!system.includes('[' + firstRead.node_id + ']'));
    assert.ok(!system.includes('Wewnątrz')); assert.equal(calls.length, 1);
  });
  await check('ZIP-only revision keeps key, uses previous derived Source, preserves both versions and equal-text distinct occurrences', async () => {
    await workshop('import'); nextUpload = await upload('żółw.docx', readFixture('zip-change'), docxMime, parts.source_id); next = await materialize();
    assert.equal(nextUpload.source_key, stored.source_key); assert.notEqual(nextUpload.hash, stored.hash);
    assert.equal(next.source_sha256, parts.source_sha256); assert.notEqual(next.source_id, parts.source_id);
    assert.notEqual(next.occurrence_nodes[0], parts.occurrence_nodes[0]); assert.equal(materializationWire.at(-1).previous_source_id, parts.source_id);
    assert.notEqual(materializationWire.at(-1).previous_source_id, parts.derivation.original_source_id);
    const p = await panel('import-panel'), nextRead = await read(p, next.occurrence_nodes[0]);
    assert.equal(nextRead.source.metadata.previous_source_id, parts.source_id);
    await download(p, 'source-original', readFixture('zip-change')); await choose(p, parts.source_id); await read(p, selected);
    await download(p, 'source-original', readFixture('base')); assert.ok((await p.getByTestId('source-previous').count()) === 0);
    observations.versions = { parts, next };
  });
  await check('reload and native restart restore global selection/history; original and derived reads validate again', async () => {
    await stopChild(server); await start(); await page.reload(); await workshop('import'); const p = await panel('import-panel');
    await choose(p, parts.source_id); assert.deepEqual(await read(p, selected), chosenRead);
    for (const warning of expected.warnings) await p.locator('[data-warning="' + warning + '"]').waitFor();
    assert.equal(await p.getByTestId('source-blocks').locator('li').count(), expected.blocks.length);
    await download(p, 'source-original', readFixture('base')); await download(p, 'source-derived', expectedText);
    await original(stored, readFixture('base')); await original({ hash: parts.source_sha256 }, expectedText);
    await choose(p, next.source_id); await read(p, next.occurrence_nodes[0]); await download(p, 'source-original', readFixture('zip-change'));
    assert.deepEqual((await api('/api/config')).context_execution.unified.legacy_seed_ids, ['synthetic:existing-unresolved', selected]);
    await workshop('chat');
    // The same account-wide selector must reach another conversation as well.
    await dismiss();
    await page.getByTestId('shell-drawer-toggle').click();
    await page.getByTestId('shell-new-chat').click(); await page.getByTestId('chat-input').waitFor();
    await send(selected, chosenRead.text.trim(), 'Komórka A');
    assert.ok(wire.every(r => r.new_conversation === true && r.conv_id === undefined), 'blank chat explicitly requests a new conversation');
    assert.equal(chatResults.length, 2);
    assert.notEqual(chatResults[0].conv_id, chatResults[1].conv_id, 'server created distinct persisted conversations using the same global selector');
    assert.equal(calls.length, 2); assert.deepEqual(errors, []);
    await workshop('import');
    const beforeRefusal = (await api('/api/config')).context_execution;
    const corrupt = await upload('uszkodzony.docx', Buffer.from('synthetic-not-a-zip'), docxMime);
    const refusedPanel = await panel('import-panel');
    const refusalResponse = page.waitForResponse(r => r.url().endsWith('/api/client/blob/text'));
    await refusedPanel.getByTestId('source-materialize').click();
    const refusal = await refusalResponse; assert.equal(refusal.status(), 400);
    const refusalBody = await refusal.json(); assert.ok(refusalBody.error);
    await refusedPanel.getByTestId('source-error').waitFor();
    assert.equal(await refusedPanel.getByTestId('source-select').count(), 0);
    assert.equal(await refusedPanel.getByTestId('source-occurrence').count(), 0);
    assert.equal(await refusedPanel.getByTestId('source-descriptor').getAttribute('data-source-id'), corrupt.source_id);
    assert.deepEqual((await api('/api/config')).context_execution, beforeRefusal);
    assert.equal(calls.length, 2, 'rejection does not call provider');
    observations.nativeRefusal = { status: refusal.status(), body: refusalBody, selectionPreserved: true };

  });
} finally {
  if (page && groups.length !== 6) { try { observations.failure = await page.locator('body').innerText(); await page.screenshot({ path: path.join(evidence, 'failure.png'), fullPage: true }); } catch { /* preserve original failure */ } }
  writeFileSync(path.join(evidence, 'receipt.json'), JSON.stringify({ groups, expectedGroups: 6, errors, observations, calls, wire, materializationWire, chatResults,
    serverSha256: sha(readFileSync(bin)), readerSha256: sha(readFileSync(readerPath)), runtimeManifestSha256: sha(readFileSync(runtimePath)), starts, status: groups.length === 6 ? 'scoped-pass' : 'incomplete' }, null, 2));
  await browser?.close(); await stopChild(server); if (provider) await closeHttpFixture(provider);
}
completion.complete(); console.log(`[docx-source-parts-native] ${groups.length}/6 groups passed; real UI/backend, synthetic loopback provider`);
