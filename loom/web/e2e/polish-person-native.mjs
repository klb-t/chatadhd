#!/usr/bin/env node
// One synthetic Polish person's full path. Actual App, native server, public authored PDF fixtures;
// an auth-free HTTP model double bound only to loopback. No credentials, .env, installs or external services.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';
import { request, stopChild, closeHttpFixture, suiteCompletionGuard } from './harness-lifecycle.mjs';
import { makeTmpDir } from './tmp-dirs.mjs';

const loom = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const bin = process.env.LOOM_SERVER_BIN || path.join(loom, 'build/dev/server/loom-server');
const dist = path.join(loom, 'web/dist');
assert.ok(existsSync(bin) && existsSync(path.join(dist, 'index.html')), 'build the own server and web first');
const tmp = makeTmpDir('loom-polish-person-');
const evidence = process.env.P0_LOCALE_EVIDENCE || makeTmpDir('loom-polish-evidence-', { evidence: true });
mkdirSync(evidence, { recursive: true });
const root = path.join(tmp, 'root');
mkdirSync(path.join(root, 'profiles'), { recursive: true });
const groups = [], observations = {}, providerRequests = [];
const completion = suiteCompletionGuard('polish-person', 8, groups);
let mock, server, browser, page, context, failureMode = false;
const freePort = () => new Promise((resolve, reject) => { const s = net.createServer(); s.once('error', reject); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); }); });
const until = async (fn, label, ms = 20000) => {
  for (const end = Date.now() + ms; Date.now() < end;) { const result = await fn(); if (result) return result; await new Promise(r => setTimeout(r, 100)); }
  throw Error(`timeout: ${label}`);
};
const check = async (name, fn) => { await fn(); groups.push(name); console.log(`[polish-person] PASS ${name}`); };
const shot = async name => { await page.screenshot({ path: path.join(evidence, `${name}.png`), fullPage: true }); };
const inPage = (route, body, method = body === undefined ? 'GET' : 'PATCH') => page.evaluate(async ({ route, body, method }) => {
  const r = await fetch(route, { method, headers: { 'Content-Type': 'application/json' }, ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  if (!r.ok) throw Error(`${route}: ${r.status}`);
  return r.json();
}, { route, body, method });
const dismissNotice = async () => {
  const close = page.getByTestId('guest-notice-close');
  if (await close.isVisible()) await close.click();
};
const attach = async (fixture, name) => {
  await dismissNotice();
  await page.getByTestId('shell-attach').click();
  const [chooser] = await Promise.all([page.waitForEvent('filechooser'), page.getByTestId('composer-menu-attach_file').click()]);
  await chooser.setFiles({ name, mimeType: 'application/pdf', buffer: readFileSync(path.join(loom, 'tests/fixtures/documents', fixture)) });
  await page.locator('[data-testid="attachment-chip"][data-state="ready"]').waitFor();
};
const send = async (question, expected) => {
  await dismissNotice();
  await page.getByTestId('chat-input').fill(question);
  await page.getByTestId('send-chat').click();
  if (await page.getByTestId('guest-terms-send').isVisible().catch(() => false)) await page.getByTestId('guest-terms-send').click();
  await page.getByText(expected, { exact: true }).first().waitFor({ timeout: 20000 });
  await page.getByTestId('streaming-message').waitFor({ state: 'detached' });
  assert.equal(await page.evaluate(() => document.documentElement.lang), 'pl');
};
try {
  const mockPort = await freePort(), port = await freePort();
  const base = `http://127.0.0.1:${port}`;
  const mockBase = `http://127.0.0.1:${mockPort}`;
  mock = createServer(async (req, res) => {
    if (req.method !== 'POST') { res.writeHead(200, { 'Content-Type': 'application/json' }).end('{"data":[]}'); return; }
    const chunks = []; for await (const chunk of req) chunks.push(chunk);
    const body = JSON.parse(Buffer.concat(chunks).toString());
    providerRequests.push(body);
    if (failureMode) { res.writeHead(503, { 'Content-Type': 'application/json' }).end(JSON.stringify({ error: { message: 'synthetic_upstream_unavailable' } })); return; }
    const question = JSON.stringify(body.messages?.at(-1)?.content ?? '');
    const answer = question.includes('drugiej stronie') ? 'Druga strona: Łódź, żółw, ąćęłńóśźż.'
      : question.includes('pustą stroną') ? 'Odczytano tekst dwóch stron.'
      : question.includes('dokumencie') ? 'Dokument zawiera dwie strony z polskimi znakami.'
      : 'This is the synthetic provider reply.';
    if (body.stream === false) { res.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify({ choices: [{ message: { role: 'assistant', content: answer }, finish_reason: 'stop' }] })); return; }
    res.writeHead(200, { 'Content-Type': 'text/event-stream' });
    res.write('data: ' + JSON.stringify({ choices: [{ delta: { role: 'assistant', content: answer }, finish_reason: null }] }) + '\n\n');
    res.end('data: ' + JSON.stringify({ choices: [{ delta: {}, finish_reason: 'stop' }] }) + '\n\ndata: [DONE]\n\n');
  });
  await new Promise(resolve => mock.listen(mockPort, '127.0.0.1', resolve));
  const overlay = (domain, overrides) => writeFileSync(path.join(root, 'profiles', `${domain}.pack`), JSON.stringify({ schema: 'loom.runtime_profile_overlay/1', domain, overrides }));
  overlay('server_accounts', { allow_private_hosts: ['127.0.0.1'], guests: { enabled: true, start: 'auto', keys_file: '', sweep_interval_seconds: 0,
    model_sources: ['free'], workdir: { subdir: 'guests', idle_ttl_seconds: 600, max_bytes: 0 }, protection: { per_session: { chat_per_minute: 0 } } } });
  overlay('free_providers', { providers: [{ id: 'mock_free', display_name: 'Syntetyczne źródło darmowe', base_url: mockBase, tier: 'free', enabled: true,
    active_when: 'none', auth_kind: 'none', data_terms: { retention: 'none', training: 'no', human_review: false,
      observed_at: '2026-10-31', source_rating: 'synthetic', source_url: 'synthetic-fixture',
      summary: { pl: 'Atrapa lokalna: tekst nie opuszcza testu.', en: 'Local double: text stays in the test.' } } }],
    free_model_choice: { rank_source: 'table', require: { training: 'no' }, table: [{ model: 'synthetic/polish-ui', provider: 'mock_free', quality: 1 }] } });
  server = spawn(bin, ['--host', '127.0.0.1', '--port', String(port), '--accounts-root', root, '--static-dir', dist], { cwd: loom, stdio: 'ignore' });
  console.log(`[polish-person] own server pid ${server.pid}`);
  await until(async () => { try { return (await request(base + '/api/healthz', { timeoutMs: 1000 })).ok; } catch { return false; } }, 'own server');
  browser = await chromium.launch({ executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || undefined });
  context = await browser.newContext({ locale: 'pl-PL', viewport: { width: 420, height: 900 } });
  // Use the existing IndexedDB plugin so the scenario is independent of OPFS availability.
  await context.addInitScript(() => localStorage.setItem('loom.store.policy', JSON.stringify({ targets: ['browser_indexeddb'] })));
  page = await context.newPage();
  let convId;
  await check('fresh pl-PL profile: Polish shell and a named synthetic free source', async () => {
    await page.goto(base);
    await page.getByTestId('chat-input').waitFor();
    assert.equal(await page.evaluate(() => document.documentElement.lang), 'pl');
    assert.match(await page.getByTestId('guest-model-line').innerText(), /Syntetyczne źródło darmowe/);
    observations.firstStart = await page.locator('body').innerText();
    // The supported local route has no authentication and reaches only our loopback double.
    await inPage('/api/config', { llm_provider: 'local', local_model_base_url: mockBase, semantic_analysis: false, semantic_model: '', intent_routing_method: 'off' });
    await shot('01-first-start');
  });
  await check('deliberate PL via the existing setting; conversation reply in English keeps Polish UI', async () => {
    await page.getByTestId('shell-menu-toggle').click();
    await page.getByTestId('shell-menu-settings').click();
    await page.getByTestId('shum-group-open-interfejs').click();
    await page.getByTestId('shum-card-btn-ui_language').click();
    await page.getByTestId('card-change').click();
    await page.getByTestId('card-editor-input').fill('pl');
    await page.getByTestId('card-next').click();
    await page.getByTestId('card-level-person').click();
    await until(async () => (await inPage('/api/config')).ui_language === 'pl', 'persisted PL');
    await page.getByTestId('close-panel').click();
    await send('Dzień dobry. Jak się masz?', 'This is the synthetic provider reply.');
    convId = (await inPage('/api/conversations'))[0].id;
    observations.conversation = await page.locator('body').innerText();
    if (await page.getByTestId('guest-notice-close').isVisible()) await page.getByTestId('guest-notice-close').click();
    await shot('02-conversation');
  });
  await check('attach authored Unicode PDF through the file chooser; actual provider request contains both pages', async () => {
    await attach('unicode-compressed.pdf', 'Zażółć gęślą.pdf');
    await send('Co jest w dokumencie?', 'Dokument zawiera dwie strony z polskimi znakami.');
    const payload = JSON.stringify(providerRequests.at(-1).messages);
    assert.ok(payload.includes('Zażółć gęślą jaźń'));
    assert.ok(payload.includes('Druga strona: Łódź'));
    assert.equal(await page.getByTestId('message-attachment').first().innerText(), 'Zażółć gęślą.pdf');
    await shot('03-pdf');
  });
  await check('follow-up about page two retains the previously attached document', async () => {
    await send('Co jest na drugiej stronie?', 'Druga strona: Łódź, żółw, ąćęłńóśźż.');
    assert.ok(JSON.stringify(providerRequests.at(-1).messages).includes('Druga strona: Łódź'));
    await shot('04-page-two');
  });
  await check('reopen conversation from history after a new page; PDF name and page-two context survive', async () => {
    await page.close(); page = await context.newPage(); await page.goto(base);
    await page.getByTestId('chat-input').waitFor();
    await page.getByTestId('shell-drawer-toggle').click();
    await page.getByTestId('shell-conv').first().click();
    await page.getByTestId('message-attachment').first().waitFor();
    assert.equal(await page.getByTestId('chat-conversation').getAttribute('data-conversation'), convId);
    const previous = providerRequests.length;
    const answersBefore = await page.locator('[data-testid="message"][data-role="assistant"]').count();
    await page.getByTestId('chat-input').fill('Przypomnij, co jest na drugiej stronie?');
    await page.getByTestId('send-chat').click();
    await until(() => providerRequests.length > previous, 'provider after reopen');
    await until(async () => (await page.locator('[data-testid="message"][data-role="assistant"]').count()) > answersBefore, 'saved answer after reopen');
    await page.getByTestId('streaming-message').waitFor({ state: 'detached' });
    assert.ok(JSON.stringify(providerRequests.at(-1).messages).includes('Druga strona: Łódź'));
    observations.history = await page.locator('body').innerText();
    await shot('05-reopened');
  });
  await check('partial PDF reports a blank page in Polish and retains the notice after reload', async () => {
    await attach('mixed-text-blank.pdf', 'Zażółć z pustą stroną.pdf');
    await send('Co odczytano z PDF-a z pustą stroną?', 'Odczytano tekst dwóch stron.');
    const notice = page.getByTestId('message-attachment-notices').first(); await notice.waitFor();
    assert.match(await notice.innerText(), /strony bez odczytywalnego tekstu: 3/);
    observations.pdfWarning = await notice.innerText();
    await page.reload(); await page.getByTestId('message-attachment-notices').first().waitFor();
    assert.match(await page.getByTestId('message-attachment-notices').first().innerText(), /strony bez odczytywalnego tekstu: 3/);
    await shot('06-pdf-warning');
  });
  await check('open this conversation in the own graph and navigate with localized controls', async () => {
    await page.getByTestId('shell-menu-toggle').click();
    const navigated = page.waitForResponse(r => r.url().endsWith('/api/graph/navigate') && r.request().method() === 'POST');
    await page.getByTestId('shell-menu-this_conversation').click();
    const response = await navigated;
    assert.equal(response.status(), 200);
    const projected = await response.json();
    const canonicalFocus = projected.focus.address; // the actual product request and native reply, no copied id mapping
    assert.ok(canonicalFocus);
    assert.ok(projected.children.some(row => row.label.includes('Dzień dobry')), 'native graph contains this synthetic conversation');
    const graph = page.getByTestId('graph-navigator'); await graph.waitFor();
    await page.getByTestId('nav-loading').waitFor({ state: 'detached' });
    assert.equal(await graph.getAttribute('data-focus'), canonicalFocus);
    assert.match(await page.getByTestId('nav-bar').innerText(), /Szerzej|Głębiej/);
    const rows = page.getByTestId('nav-rows'); await rows.waitFor();
    assert.ok(Number(await rows.getAttribute('data-total')) > 0, 'the own conversation has actual graph children');
    const first = rows.locator('button[data-has-children]').first();
    const drills = (await first.getAttribute('data-has-children')) === 'true';
    await first.click();
    if (drills) {
      await until(async () => (await graph.getAttribute('data-focus')) !== canonicalFocus, 'graph child navigation');
      await page.getByTestId('nav-back').click();
      await until(async () => (await graph.getAttribute('data-focus')) === canonicalFocus, 'graph back navigation');
    } else {
      await page.getByTestId('node-card').waitFor();
      await page.getByTestId('card-loading').waitFor({ state: 'detached' });
      assert.equal(await page.getByTestId('card-error').count(), 0);
      await page.getByTestId('card-close').click();
    }
    observations.graph = await graph.innerText();
    await shot('07-own-graph');
    await page.getByTestId('close-panel').click();
  });
  await check('provider unavailable: visible error, retained conversation and unchanged Polish UI (wording audited separately)', async () => {
    failureMode = true;
    await page.getByTestId('chat-input').fill('Czy dostawca jest dostępny?');
    await page.getByTestId('send-chat').click();
    await page.getByTestId('chat-error').waitFor();
    observations.providerError = await page.getByTestId('chat-error').innerText();
    assert.ok(observations.providerError.length > 0);
    assert.equal(await page.evaluate(() => document.documentElement.lang), 'pl');
    assert.ok((await inPage(`/api/conversations/${convId}`)).id === convId);
    await shot('08-provider-error');
  });
} finally {
  if (page && groups.length !== 8) { try { await shot('failure'); observations.failureScreen = await page.locator('body').innerText(); } catch {} }
  writeFileSync(path.join(evidence, 'observations.json'), JSON.stringify({ groups, observations, providerRequests: providerRequests.length }, null, 2));
  await browser?.close();
  await stopChild(server);
  if (mock) await closeHttpFixture(mock);
}
completion.complete();
console.log(`[polish-person] ${groups.length}/8 groups passed; synthetic audit evidence: ${evidence}`);
