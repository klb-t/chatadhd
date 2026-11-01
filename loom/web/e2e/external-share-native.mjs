#!/usr/bin/env node
// Prepared light-phase gate; no automatic build/download. Run only after native admission slot.
// Real ImportPanel -> HTTP -> default embedded Python worker (HTTPS loopback) -> DB -> ChatView.
import assert from 'node:assert/strict';
import { spawn, execFileSync } from 'node:child_process';
import { createServer } from 'node:http';
import { createServer as createTlsServer } from 'node:https';
import net from 'node:net';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, realpathSync, writeFileSync } from 'node:fs';
import { closeHttpFixture, request, stopChild, suiteCompletionGuard } from './harness-lifecycle.mjs';

assert.equal(process.env.LOOM_EXTERNAL_SHARE_NATIVE, '1', 'Integrator reserved native phase only.');
const loom = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..'), repo = path.dirname(loom);
const own = name => { const p = realpathSync(process.env[name] ?? ''); assert.ok(process.env[name] && p.startsWith(repo + path.sep), name); return p; };
const bin = own('LOOM_SERVER_BIN'), dist = own('LOOM_SHARE_DIST'), preload = own('LOOM_SHARE_NETWORK_FIXTURE');
const binding = JSON.parse(readFileSync(own('LOOM_SHARE_BUILD_RECEIPT')));
const sha = data => createHash('sha256').update(data).digest('hex');
assert.equal(binding.server_sha256, sha(readFileSync(bin)));
assert.equal(binding.network_fixture_sha256, sha(readFileSync(preload)));
assert.equal(binding.network_fixture_source_sha256, sha(readFileSync(path.join(loom, 'tests/url_share/external_share_network_fixture.c'))));
assert.equal(binding.web_index_sha256, sha(readFileSync(path.join(dist, 'index.html'))));
assert.equal(binding.worker_sha256, sha(readFileSync(path.join(loom, 'src/import/url_share/worker.py'))));
assert.equal(binding.runtime_tree, execFileSync('git', ['rev-parse', 'HEAD:loom'], { cwd: repo, encoding: 'utf8' }).trim());
const evidence = path.resolve(process.env.LOOM_SHARE_EVIDENCE ?? '');
assert.ok(process.env.LOOM_SHARE_EVIDENCE && evidence.startsWith(repo + path.sep));
mkdirSync(evidence, { mode: 0o700 }); // never reuse a partial run
const tmp = path.join(evidence, 'private'); mkdirSync(tmp, { mode: 0o700 });
const root = path.join(tmp, 'accounts'), cert = path.join(tmp, 'ca.pem'), key = path.join(tmp, 'key.pem');
// New synthetic key only, never a credential read or exported as product evidence.
execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=chatgpt.com',
  '-addext', 'subjectAltName=DNS:chatgpt.com,DNS:chat.openai.com', '-addext', 'basicConstraints=critical,CA:TRUE',
  '-keyout', key, '-out', cert], { stdio: 'ignore', env: { PATH: '/usr/bin:/bin' } });
const require = createRequire(path.join(loom, 'web/package.json'));
const { chromium } = require('playwright');
const fixture = name => readFileSync(path.join(loom, 'tests/fixtures/url_share', name));
const raw = fixture('chatgpt_turbo_stream.html'), legacy = fixture('chatgpt_next_data.html');
const calls = [], wire = [], sourceCalls = [], groups = [], errors = [], observations = {};
const completed = suiteCompletionGuard('external-share-native', 6, groups);
const text = m => typeof m.content === 'string' ? m.content : (m.content ?? []).filter(p => p.type === 'text').map(p => p.text).join('\n');
const freePort = () => new Promise((resolve, reject) => { const s = net.createServer(); s.once('error', reject); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); }); });
const until = async (fn, label) => { for (const end = Date.now() + 30000; Date.now() < end;) { if (await fn()) return; await new Promise(r => setTimeout(r, 75)); } throw Error('timeout: ' + label); };
const overlay = (domain, overrides) => { mkdirSync(path.join(root, 'profiles'), { recursive: true }); writeFileSync(path.join(root, 'profiles', domain + '.pack'), JSON.stringify({ schema: 'loom.runtime_profile_overlay/1', domain, overrides })); };
let server, source, provider, browser, page, port, base, sourcePort, modelPort;
const start = async (proxy = false) => {
  const env = { PATH: '/usr/bin:/bin', LANG: 'C.UTF-8', TMPDIR: tmp, LOOM_LOG_ENABLED: '0', LD_PRELOAD: preload,
    LOOM_TEST_URL_SOURCE_PORT: String(sourcePort), LOOM_TEST_SHARE_MODEL_PORT: String(modelPort), SSL_CERT_FILE: cert,
    ...(proxy ? { HTTPS_PROXY: 'http://127.0.0.1:' + sourcePort } : {}) };
  const isolation = ['--no-new-privs', '--landlock-access', 'fs'];
  for (const rule of ['path-beneath:execute,read-file,read-dir:/usr', 'path-beneath:read-file,write-file:/dev/null',
    'path-beneath:execute,read-file:' + bin, 'path-beneath:read-file:' + preload,
    'path-beneath:read-file,read-dir:' + dist,
    'path-beneath:execute,write-file,read-file,read-dir,remove-dir,remove-file,make-dir,make-reg,make-sym,refer,truncate:' + tmp]) isolation.push('--landlock-rule', rule);
  server = spawn('/usr/bin/setpriv', [...isolation, bin, '--host', '127.0.0.1', '--port', String(port), '--data-dir', root, '--accounts-root', root, '--static-dir', dist], { cwd: tmp, env, stdio: 'ignore' });
  await until(async () => { assert.equal(server.exitCode, null); try { return (await request(base + '/api/healthz', { timeoutMs: 1000 })).ok; } catch { return false; } }, 'own candidate startup');
};
const api = (route, body, method = body === undefined ? 'GET' : 'POST') => page.evaluate(async ({ route, body, method }) => {
  const r = await fetch(route, { method, headers: { 'Content-Type': 'application/json' }, ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  const data = await r.json(); if (!r.ok || data.error) throw Error(route + ': ' + r.status); return data;
}, { route, body, method });
const workshop = async tab => {
  if (await page.getByTestId('guest-notice-close').isVisible().catch(() => false)) await page.getByTestId('guest-notice-close').click();
  if (await page.getByTestId('close-panel').isVisible().catch(() => false)) await page.getByTestId('close-panel').click();
  if (!await page.getByTestId('nav-' + tab).isVisible().catch(() => false)) {
    if (await page.getByTestId('shell-menu-toggle').isVisible().catch(() => false)) { await page.getByTestId('shell-menu-toggle').click(); await page.getByTestId('shell-menu-workshop').click(); }
    if (await page.getByTestId('toggle-nav').isVisible()) await page.getByTestId('toggle-nav').click();
  }
  await page.getByTestId('nav-' + tab).click();
};
const importUrl = async url => {
  await workshop('import'); const panel = page.getByTestId('url-import-panel'); await panel.waitFor();
  await panel.locator('#import-url').fill(url); await panel.locator('#import-url-mode').selectOption('share');
  const done = page.waitForResponse(r => r.url().endsWith('/api/import') && r.request().method() === 'POST');
  await panel.getByTestId('url-import-start').click(); const response = await done; assert.equal(response.status(), 200);
  const events = (await response.text()).split('\n').filter(l => l.startsWith('data:')).map(l => JSON.parse(l.slice(5)));
  assert.ok(!events.some(e => e.type === 'error')); const result = events.find(e => e.type === 'done'); assert.ok(result);
  await panel.getByTestId('url-import-result').waitFor(); assert.equal(await panel.getByTestId('url-import-result').getAttribute('data-status'), result.status);
  return result;
};
const original = async result => {
  const bytes = await page.evaluate(async hash => { const r = await fetch('/api/client/blob/' + hash + '/raw'); if (!r.ok) throw Error('raw ' + r.status); return [...new Uint8Array(await r.arrayBuffer())]; }, result.blob_hash);
  assert.equal(sha(Buffer.from(bytes)), result.blob_hash); return Buffer.from(bytes);
};
const history = id => api('/api/conversations/' + id + '/messages?all=1');
const send = async cid => {
  await workshop('chat'); await page.getByTestId('shell-drawer-toggle').click();
  await page.getByTestId('shell-conv').filter({ hasText: 'Syntetyczny share — żółw' }).first().click();
  assert.equal(await page.getByTestId('chat-conversation').getAttribute('data-conversation'), cid);
  const n = calls.length, w = wire.length;
  await page.getByTestId('chat-input').fill('Odpowiedz na podstawie bieżącej rozmowy.'); await page.getByTestId('send-chat').click();
  if (await page.getByTestId('guest-terms-send').isVisible().catch(() => false)) await page.getByTestId('guest-terms-send').click();
  await until(() => calls.length === n + 1, 'recorded synthetic provider request');
  const body = calls.at(-1); assert.ok(Array.isArray(body.messages));
  const contents = body.messages.map(text), all = JSON.stringify(body);
  assert.ok(contents.some(t => t.includes('Wybrana odpowiedź'))); assert.ok(contents.some(t => t.includes('Zażółć & <tag>')));
  assert.ok(!all.includes('Poprzednia gałąź'), 'off-path kVersion must not enter ANY provider field');
  const userIndex = body.messages.findIndex(m => m.role === 'user' && text(m).includes('Zażółć'));
  const assistantIndex = body.messages.findIndex(m => m.role === 'assistant' && text(m).includes('Wybrana odpowiedź'));
  assert.ok(userIndex >= 0 && assistantIndex > userIndex, 'selected branch order/roles');
  assert.equal(wire.length, w + 1); const sent = wire.at(-1); assert.equal(sent.conv_id, cid);
  assert.ok(!sent.message.includes('Wybrana odpowiedź') && !sent.message.includes('Zażółć'));
  assert.ok(!sent.attachments?.length, 'history is not pasted as an attachment');
  const reply = page.locator('[data-testid="message"][data-role="assistant"]').filter({ hasText: 'SHARE_PROVIDER:' }).last();
  await reply.waitFor(); await page.getByTestId('streaming-message').waitFor({ state: 'detached' });
  assert.ok((await reply.innerText()).includes('Wybrana odpowiedź'));
};
const check = async (name, fn) => { await fn(); groups.push(name); await page.screenshot({ path: path.join(evidence, groups.length + '.png'), fullPage: true }); console.log('[external-share-native] PASS ' + name); };
try {
  port = await freePort(); base = 'http://127.0.0.1:' + port;
  source = createTlsServer({ key: readFileSync(key), cert: readFileSync(cert) }, (req, res) => {
    sourceCalls.push({ method: req.method, path: req.url, headerKeys: Object.keys(req.headers) });
    let code = 200, body = raw, headers = { 'Content-Type': 'text/html' };
    if (req.url === '/share/legacy-share') body = legacy;
    else if (req.url === '/share/login-wall') { code = 403; body = Buffer.from('<html>login required</html>'); }
    else if (req.url === '/share/unknown-share') body = Buffer.from('<html>No recognized data</html>');
    else if (req.url === '/share/to-private') { code = 302; headers.Location = 'https://127.0.0.1/private'; body = Buffer.alloc(0); }
    else if (req.url === '/share/to-http') { code = 302; headers.Location = 'http://chatgpt.com/share/synthetic-share'; body = Buffer.alloc(0); }
    else if (req.url !== '/share/synthetic-share') { code = 404; body = Buffer.alloc(0); }
    res.writeHead(code, { ...headers, 'Content-Length': body.length }).end(body);
  }); await new Promise(r => source.listen(0, '127.0.0.1', r)); sourcePort = source.address().port;
  provider = createServer(async (req, res) => {
    if (req.method !== 'POST') { res.writeHead(200, { 'Content-Type': 'application/json' }).end('{"data":[]}'); return; }
    const chunks = []; for await (const c of req) chunks.push(c); const body = JSON.parse(Buffer.concat(chunks)); calls.push(body);
    // Derive the answer only from actual received history, never the HTML fixture.
    const selected = body.messages.find(m => m.role === 'assistant' && text(m).includes('Wybrana odpowiedź'));
    res.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify({ choices: [{ message: { role: 'assistant', content: 'SHARE_PROVIDER: ' + (selected ? text(selected) : 'MISSING_HISTORY') }, finish_reason: 'stop' }] }));
  }); await new Promise(r => provider.listen(0, '127.0.0.1', r)); modelPort = provider.address().port; const providerBase = 'http://127.0.0.1:' + modelPort;
  overlay('server_accounts', { allow_private_hosts: ['127.0.0.1'], guests: { enabled: true, start: 'auto', keys_file: '', sweep_interval_seconds: 0, model_sources: ['free'], workdir: { subdir: 'guests', idle_ttl_seconds: 0, max_bytes: 0 }, protection: { pow: { bits: 0, bits_under_load: 0 }, per_session: { chat_per_minute: 0 } } } });
  overlay('free_providers', { providers: [{ id: 'share_fixture', display_name: 'Syntetyczny dostawca', base_url: providerBase, tier: 'free', enabled: true, active_when: 'none', auth_kind: 'none', data_terms: { retention: 'none', training: 'no', human_review: false, observed_at: '2026-11-01', source_rating: 'synthetic', source_url: 'synthetic-fixture', summary: { pl: 'Atrapa.', en: 'Double.' } } }], free_model_choice: { rank_source: 'table', require: { training: 'no' }, table: [{ model: 'synthetic/share-ui', provider: 'share_fixture', quality: 1 }] } });
  await start(); browser = await chromium.launch(); const context = await browser.newContext({ locale: 'pl-PL', viewport: { width: 1280, height: 900 } });
  await context.route('**/*', route => { if (new URL(route.request().url()).origin === base) return route.continue(); errors.push('blocked non-loopback browser request'); return route.abort(); });
  await context.addInitScript(() => localStorage.setItem('loom.store.policy', JSON.stringify({ targets: ['browser_indexeddb'] })));
  page = await context.newPage(); page.on('pageerror', e => errors.push(e.message)); page.on('request', r => { if (r.method() === 'POST' && r.url().endsWith('/api/chat')) wire.push(r.postDataJSON()); });
  await page.goto(base); await page.getByTestId('chat-input').waitFor();
  await api('/api/config', { llm_provider: 'local', local_model_base_url: providerBase, model_registry_autorefresh: 'off', semantic_analysis: false, semantic_model: '', auto_title: false, intent_routing_method: 'off', stream: false, ui_language: 'pl', chat_flow: 'plain', chat_representation: 'raw', context_receipt_store: 'always', context_execution: { unified: { enabled: false, include_knowledge: false, budget_tokens: 8192 } } }, 'PATCH');
  await page.reload(); await page.getByTestId('chat-input').waitFor(); let imported, cid, rows; const provenance = {};
  await check('real UI/HTTP external HTML imports roles/tree/reference/provenance/original, no model', async () => {
    imported = await importUrl('https://chatgpt.com/share/synthetic-share'); cid = imported.conversations[0].id;
    assert.equal(imported.adapter, 'chatgpt.share_html/1'); assert.equal(imported.status, 'complete'); assert.equal(imported.messages, 4); assert.equal(imported.branch.selected_node, 'tool');
    assert.equal(imported.llm_calls, 0); assert.equal(calls.length, 0); assert.deepEqual(imported.attachments.map(a => a.status), ['reference', 'reference']);
    assert.ok((await page.getByTestId('url-import-result').innerText()).includes('Odniesienie'));
    assert.ok((await page.getByTestId('url-import-result').innerText()).includes('Aktywna jest gałąź'));
    assert.deepEqual(await original(imported), raw); rows = await history(cid);
    assert.deepEqual(rows.map(m => [m.role, m.text, m.status]), [['user', '  Zażółć & <tag>\nhttps://example.invalid/link  ', 'active'], ['assistant', 'Poprzednia gałąź', 'version'], ['assistant', 'Wybrana odpowiedź\n', 'active'], ['tool', '', 'active']]);
    assert.ok(!rows[0].parent_id); assert.equal(rows[1].parent_id, rows[0].id); assert.equal(rows[2].parent_id, rows[0].id); assert.equal(rows[3].parent_id, rows[2].id);
    for (const row of rows) { const p = await api('/api/provenance/' + row.id); assert.equal(p.subject_id, row.id); assert.equal(p.records.length, 1); assert.equal(p.records[0].source_id, imported.source_id); assert.ok(p.sources[imported.source_id]); assert.equal(p.records[0].locator.embedded_data.format, 'turbo_stream_v2'); assert.equal(p.records[0].locator.embedded_data.json_pointer, row.metadata.url_import.pointer); provenance[row.id] = p; }
    observations.imported = imported;
  });
  await check('same HTML is idempotent through UI and HTTP without duplicate rows', async () => { const next = await importUrl('https://chatgpt.com/share/synthetic-share'); assert.equal(next.already_imported, true); assert.equal(next.source_id, imported.source_id); assert.equal(next.conversations[0].id, cid); assert.deepEqual(await history(cid), rows); assert.equal(calls.length, 0); });
  await check('ChatView actual provider history contains selected branch only; response derives from it', async () => { await send(cid); });
  await check('server restart/browser reload preserve parent/version/provenance/original and selected context', async () => { const saved = await history(cid); await stopChild(server); await start(); await page.reload(); await page.getByTestId('chat-input').waitFor(); assert.deepEqual(await history(cid), saved); for (const row of rows) assert.deepEqual(await api('/api/provenance/' + row.id), provenance[row.id]); assert.deepEqual(await original(imported), raw); await send(cid); const next = await importUrl('https://chatgpt.com/share/synthetic-share'); assert.equal(next.already_imported, true); assert.equal(next.source_id, imported.source_id); });
  await check('second real HTML format retains independent original and same branch semantics', async () => { const n = calls.length; const other = await importUrl('https://chatgpt.com/share/legacy-share'); assert.equal(other.status, 'complete'); assert.notEqual(other.source_id, imported.source_id); assert.deepEqual(await original(other), legacy); assert.deepEqual((await history(other.conversations[0].id)).map(m => [m.role, m.text, m.status]), rows.map(m => [m.role, m.text, m.status])); assert.equal(calls.length, n); });
  await check('HTTP/UI refusals, redirects and proxy keep production boundary; references never fetched', async () => {
    const n = calls.length;
    for (const [url, reason] of [['https://chatgpt.com/share/login-wall', 'http_403'], ['https://chatgpt.com/share/unknown-share', 'share_structure_unavailable'], ['https://chatgpt.com/share/to-private', 'address_not_public'], ['https://chatgpt.com/share/to-http', 'redirect_tls_downgrade'], ['https://127.0.0.1/private', 'address_not_public']]) {
      const denied = await importUrl(url); assert.notEqual(denied.status, 'complete'); assert.equal(denied.reason, reason); assert.deepEqual(denied.conversations, []); assert.equal(denied.messages, 0);
      assert.ok((await page.getByTestId('url-import-result').innerText()).includes('importu pliku'));
    }
    assert.ok(sourceCalls.every(c => c.method === 'GET' && c.path.startsWith('/share/'))); assert.ok(sourceCalls.every(c => !c.headerKeys.some(k => ['authorization', 'cookie', 'proxy-authorization'].includes(k))));
    const fetched = sourceCalls.length; await stopChild(server); await start(true); await page.reload(); await page.getByTestId('chat-input').waitFor();
    const proxy = await importUrl('https://chatgpt.com/share/synthetic-share'); assert.equal(proxy.reason, 'proxy_destination_pinning_unavailable'); assert.equal(proxy.status, 'unsupported'); assert.equal(sourceCalls.length, fetched); assert.equal(calls.length, n); assert.deepEqual(errors, []);
  });
} finally {
  try {
    writeFileSync(path.join(evidence, 'receipt.json'), JSON.stringify({ groups, expectedGroups: 6, errors, observations, calls, wire, sourceCalls, binding, execution: 'real candidate + local TLS/provider, NOT public provider acceptance' }, null, 2));
  } finally {
    const cleanup = await Promise.allSettled([browser?.close(), stopChild(server), source ? closeHttpFixture(source) : undefined, provider ? closeHttpFixture(provider) : undefined]);
    assert.ok(cleanup.every(r => r.status === 'fulfilled'), 'all owned processes/listeners must close');
  }
}
completed.complete(); console.log('[external-share-native] 6/6 groups passed; local TLS and synthetic provider');
