#!/usr/bin/env node
// FOR THE INTEGRATOR ONLY. Real App/ImportPanel/GraphView/ChatView and native
// source actions; one loopback recording provider. Never builds or downloads.
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

assert.equal(process.env.LOOM_G1_GUI_NATIVE, '1', 'Only the integrator runs this harness, with explicit native opt-in.');
const loom = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..'), repo = path.dirname(loom);
const ownPath = input => { assert.ok(input); const p = realpathSync(input); assert.ok(p.startsWith(repo + path.sep), 'artifact must be in the integrator worktree'); return p; };
const bin = ownPath(process.env.LOOM_SERVER_BIN), dist = ownPath(process.env.LOOM_G1_GUI_DIST);
assert.ok(existsSync(path.join(dist, 'index.html')), 'use the integrator fresh G1 web build');
const evidence = path.resolve(process.env.LOOM_G1_GUI_EVIDENCE ?? '');
assert.ok(process.env.LOOM_G1_GUI_EVIDENCE && evidence.startsWith(repo + path.sep));
mkdirSync(evidence, { recursive: true });
const require = createRequire(path.join(loom, 'web/package.json'));
const { chromium } = require('playwright'); // own installed dependencies only; no shared fallback
const tmp = makeTmpDir('loom-g1-gui-'), root = path.join(tmp, 'accounts');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const calls = [], wire = [], groups = [], errors = [], observations = {};
const completion = suiteCompletionGuard('g1-source-parts-native', 8, groups);
const md = '# Nagłówek\nZażółć gęślą jaźń G1_SELECTED.\nZażółć gęślą jaźń G1_SELECTED.\nG1_NOT_SELECTED\n';
const codeOld = '# Łódź\ndef żółw():\n    return "G1_CODE_OLD_żółw"\n';
const codeNew = '# Łódź\ndef żółw():\n    return "G1_CODE_NEW_żółw"\n';
const plain = 'G1_PLAIN_żółw\n';
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
  server = spawn(bin, ['--host', '127.0.0.1', '--port', String(port), '--accounts-root', root, '--static-dir', dist], { cwd: tmp, stdio: 'ignore' });
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
  if (!await page.getByTestId('nav-' + tab).isVisible().catch(() => false)) {
    if (await page.getByTestId('shell-menu-toggle').isVisible().catch(() => false)) {
      await page.getByTestId('shell-menu-toggle').click(); await page.getByTestId('shell-menu-workshop').click();
    }
    if (await page.getByTestId('toggle-nav').isVisible()) await page.getByTestId('toggle-nav').click();
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
  await page.getByTestId('chat-input').fill('Przytocz wskazany fragment grafu.'); await page.getByTestId('send-chat').click();
  if (await page.getByTestId('guest-terms-send').isVisible().catch(() => false)) await page.getByTestId('guest-terms-send').click();
  await until(() => calls.length === before + 1, 'actual provider request');
  const reply = page.locator('[data-testid="message"][data-role="assistant"]').filter({ hasText: 'G1_PROVIDER: ' + (before + 1) + ' ' }).last();
  await reply.waitFor(); await page.getByTestId('streaming-message').waitFor({ state: 'detached' });
  const visibleAnswer = await reply.innerText();
  assert.ok(visibleAnswer.includes(expected), 'visible answer derives from selected text');
  assert.ok(visibleAnswer.includes('[' + id + ']'), 'visible answer cites ID received in system context');
  const system = systemText(calls.at(-1)); assert.ok(system.includes('[' + id + ']')); assert.ok(system.includes(expected));
  assert.ok(!system.includes(absent)); assert.equal(system.split(expected).length - 1, 1, 'one selected occurrence, not every equal text');
  const outgoing = wire.slice(requests).at(-1); assert.ok(outgoing); assert.ok(!outgoing.attachments?.length);
  assert.ok(!outgoing.message.includes(expected)); assert.notEqual(outgoing.include_graph_memory, false);
  observations.lastSent = { id, expected, request: outgoing, system, visibleAnswer };
};
const check = async (name, fn) => { await fn(); groups.push(name); console.log('[g1-source-parts-native] PASS ' + name); await page.screenshot({ path: path.join(evidence, groups.length + '.png'), fullPage: true }); };

try {
  port = await freePort(); base = 'http://127.0.0.1:' + port; const providerPort = await freePort(), providerBase = 'http://127.0.0.1:' + providerPort;
  provider = createServer(async (req, res) => {
    if (req.method !== 'POST') { res.writeHead(200, { 'Content-Type': 'application/json' }).end('{"data":[]}'); return; }
    const chunks = []; for await (const chunk of req) chunks.push(chunk); const body = JSON.parse(Buffer.concat(chunks)); calls.push(body);
    // The answer uses ONLY the received system context, never the upload fixture or the question.
    const lines = systemText(body).split('\n');
    const index = lines.findIndex(s => /G1_SELECTED|G1_CODE_NEW|G1_PLAIN/.test(s));
    const header = lines.slice(0, index).reverse().find(s => /\[n_text_occ_[a-f0-9]+\]/.test(s));
    const answer = 'G1_PROVIDER: ' + calls.length + ' ' + (header ?? 'BRAK_ID') + '\n\n```text\n' + (lines[index] ?? 'BRAK_WYBRANEGO_FRAGMENTU') + '\n```';
    res.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify({ choices: [{ message: { role: 'assistant', content: answer }, finish_reason: 'stop' }] }));
  }); await new Promise(resolve => provider.listen(providerPort, '127.0.0.1', resolve));
  overlay('server_accounts', { allow_private_hosts: ['127.0.0.1'], guests: { enabled: true, start: 'auto', keys_file: '', sweep_interval_seconds: 0, model_sources: ['free'],
    workdir: { subdir: 'guests', idle_ttl_seconds: 0, max_bytes: 0 }, protection: { pow: { bits: 0, bits_under_load: 0 }, per_session: { chat_per_minute: 0 } } } });
  overlay('free_providers', { providers: [{ id: 'g1_fixture', display_name: 'Syntetyczny dostawca G1', base_url: providerBase, tier: 'free', enabled: true, active_when: 'none', auth_kind: 'none',
    data_terms: { retention: 'none', training: 'no', human_review: false, observed_at: '2026-10-31', source_rating: 'synthetic', source_url: 'synthetic-fixture', summary: { pl: 'Atrapa lokalna.', en: 'Local double.' } } }],
    free_model_choice: { rank_source: 'table', require: { training: 'no' }, table: [{ model: 'synthetic/g1-ui', provider: 'g1_fixture', quality: 1 }] } });
  await start(); browser = await chromium.launch({ ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
  const context = await browser.newContext({ locale: 'pl-PL', viewport: { width: 1280, height: 900 } });
  await context.addInitScript(() => localStorage.setItem('loom.store.policy', JSON.stringify({ targets: ['browser_indexeddb'] })));
  page = await context.newPage(); page.on('pageerror', e => errors.push(e.message));
  page.on('request', r => { if (r.method() === 'POST' && r.url().endsWith('/api/chat')) wire.push(r.postDataJSON()); });
  await page.goto(base); await page.getByTestId('chat-input').waitFor();
  await api('/api/config', { llm_provider: 'local', local_model_base_url: providerBase, model_registry_autorefresh: 'off', semantic_analysis: false, semantic_model: '', auto_title: false,
    intent_routing_method: 'off', stream: false, ui_language: 'pl', chat_flow: 'plain', chat_representation: 'raw', context_receipt_store: 'always',
    context_execution: { unified: { enabled: false, include_knowledge: false, budget_tokens: 8192, legacy_seed_ids: ['synthetic:existing-unresolved'], legacy_relation_hops: 0 } } }, 'PATCH');
  await page.reload(); await page.getByTestId('chat-input').waitFor(); await workshop('import');
  let stored, parts, selected, firstRead, secondRead;
  await check('Markdown upload retains source_key and original before explicit materialization, including page reload', async () => {
    stored = await upload('Zażółć.md', md, 'text/markdown'); await original(stored, md);
    assert.equal((await api('/api/sources?limit=100')).filter(s => s.parser === 'loom.source_parts').length, 0);
    await page.reload(); await workshop('import'); const p = await panel('import-panel'); await choose(p, stored.source_id);
    assert.equal(await p.getByTestId('source-descriptor').getAttribute('data-source-key'), stored.source_key); assert.equal(calls.length, 0);
  });
  await check('explicit materialization, exact repeated occurrences, original ranges and idempotent retry', async () => {
    parts = await materialize(); assert.equal(parts.occurrence_nodes.length, 4); assert.equal(parts.status, 'partial');
    assert.ok(parts.observations.some(o => o.kind === 'heading')); const p = await panel('import-panel');
    firstRead = await read(p, parts.occurrence_nodes[1]); selected = parts.occurrence_nodes[2]; secondRead = await read(p, selected);
    assert.equal(firstRead.text, secondRead.text); assert.notEqual(firstRead.node_id, secondRead.node_id); assert.notDeepEqual(firstRead.provenance.byte_range, secondRead.provenance.byte_range);
    for (const r of [firstRead, secondRead]) { const [a, b] = r.provenance.byte_range; assert.equal(Buffer.from(md).subarray(a, b).toString(), r.text); }
    const count = (await api('/api/sources?limit=100')).length; assert.deepEqual(await materialize(), parts); assert.equal((await api('/api/sources?limit=100')).length, count); assert.equal(calls.length, 0);
  });
  await check('existing GraphView opens the actual selected occurrence subgraph', async () => {
    await workshop('graph'); const p = await panel('graph-view'); await choose(p, parts.source_id); await read(p, selected);
    await page.locator('#graph-depth').press('ArrowRight'); assert.equal(await page.locator('#graph-depth').inputValue(), '2');
    await p.getByTestId('source-show-graph').click();
    await page.locator('[data-testid="graph-view"][data-selected-node="' + selected + '"]').waitFor();
    const graph = await api('/api/graph/expand', { seed_ids: [selected], depth: 2 });
    assert.ok(graph.nodes.some(n => n.id === parts.source_node)); assert.ok(graph.edges.some(e => e.src === selected && e.dst === parts.version_node)); assert.equal(calls.length, 0);
    const previousConfig = await api('/api/config');
    await select(p, selected);
    const cfg = await api('/api/config');
    assert.deepEqual(cfg.context_execution.unified.legacy_seed_ids, ['synthetic:existing-unresolved', selected]);
    assert.equal(cfg.context_execution.unified.budget_tokens, 8192);
    assert.equal(cfg.context_execution.unified.include_knowledge, false);
    assert.equal(cfg.chat_flow, 'plain'); assert.equal(cfg.chat_representation, 'raw');
    const expectedExecution = structuredClone(previousConfig.context_execution);
    Object.assign(expectedExecution.unified, { enabled: true, legacy_relation_hops: 0,
      legacy_seed_ids: ['synthetic:existing-unresolved', selected] });
    assert.deepEqual(cfg.context_execution, expectedExecution, 'all unrelated context profile and budget fields preserved');
    observations.preservedConfig = cfg.context_execution;
  });
  await check('ChatView sends the selected ID and exact fragment in the real provider system context, without attachment', async () => {
    await workshop('chat'); await send(selected, secondRead.text.trim(), 'G1_NOT_SELECTED');
    assert.ok(!systemText(calls.at(-1)).includes('[' + firstRead.node_id + ']'));
  });
  await check('source/selection survive reopen and real backend restart; another chat request contains the same occurrence', async () => {
    await stopChild(server); await start(); await page.reload(); await page.getByTestId('chat-input').waitFor();
    await send(selected, secondRead.text.trim(), 'G1_NOT_SELECTED'); await original(stored, md);
    assert.deepEqual(await api('/api/client/blob/text/read', { node_id: selected }), secondRead);
  });
  await check('code versions preserve source identity, both originals and explicit predecessor; selected new code reaches provider', async () => {
    await workshop('import'); const oldUpload = await upload('żółw.py', codeOld, 'text/plain'), old = await materialize();
    const newUpload = await upload('żółw.py', codeNew, 'text/plain', old.source_id), next = await materialize();
    assert.equal(old.reason, 'code_text_only_no_ast'); assert.equal(next.reason, 'code_text_only_no_ast'); assert.deepEqual(next.observations, []);
    assert.equal(oldUpload.source_key, newUpload.source_key); assert.notEqual(old.source_id, next.source_id);
    await original(oldUpload, codeOld); await original(newUpload, codeNew);
    const p = await panel('import-panel'), item = await read(p, next.occurrence_nodes[2]); assert.equal(item.source.metadata.previous_source_id, old.source_id);
    const row = p.locator('[data-testid="source-selected-id"][data-node-id="' + selected + '"]'); await row.locator('button').click(); await row.waitFor({ state: 'detached' });
    await select(p, next.occurrence_nodes[2]); await workshop('chat'); await send(next.occurrence_nodes[2], item.text.trim(), 'G1_CODE_OLD');
    observations.code = { old, next };
  });
  await check('TXT and another identical file are separate sources and occurrences, even with equal names and bytes', async () => {
    await workshop('import'); const a = await upload('ten-sam.txt', plain, 'text/plain'), pa = await materialize();
    const p = await panel('import-panel'); await p.getByTestId('source-upload-file').setInputFiles([]);
    const b = await upload('ten-sam.txt', plain, 'text/plain'), pb = await materialize();
    assert.equal(a.hash, b.hash); assert.notEqual(a.source_key, b.source_key); assert.notEqual(pa.source_id, pb.source_id); assert.notEqual(pa.occurrence_nodes[0], pb.occurrence_nodes[0]);
    await original(a, plain); await original(b, plain);
    while (await p.getByTestId('source-selected-id').count()) {
      const row = p.getByTestId('source-selected-id').first(), id = await row.getAttribute('data-node-id');
      await row.locator('button').click(); await p.locator('[data-testid="source-selected-id"][data-node-id="' + id + '"]').waitFor({ state: 'detached' });
    }
    await select(p, pb.occurrence_nodes[0]); await workshop('chat'); await send(pb.occurrence_nodes[0], plain.trim(), 'G1_CODE_NEW');
  });
  await check('PNG original remains byte-identical and has no invented G1 parts or enabled materialization', async () => {
    await workshop('import');
    const png = readFileSync(path.join(loom, 'tests/fixtures/local_ocr/synthetic-pl.png')), before = calls.length;
    const image = await upload('synthetic-pl.png', png, 'image/png'), p = await panel('import-panel'); await original(image, png);
    assert.equal(await p.getByTestId('source-upload-format').inputValue(), ''); assert.ok(await p.getByTestId('source-materialize').isDisabled());
    assert.equal(await p.getByTestId('source-occurrence').count(), 0); assert.equal(calls.length, before); assert.deepEqual(errors, []);
  });
} finally {
  if (page && groups.length !== 8) { try { observations.failure = await page.locator('body').innerText(); await page.screenshot({ path: path.join(evidence, 'failure.png'), fullPage: true }); } catch { /* preserve the original failure */ } }
  writeFileSync(path.join(evidence, 'receipt.json'), JSON.stringify({ groups, expectedGroups: 8, errors, observations, calls, wire, serverSha256: sha(readFileSync(bin)) }, null, 2));
  await browser?.close(); await stopChild(server); if (provider) await closeHttpFixture(provider);
}
completion.complete(); console.log(`[g1-source-parts-native] ${groups.length}/8 groups passed; real backend, synthetic provider`);
