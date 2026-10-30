// P1 sign-in state: pure classification of GET /api/auth/me plus data texts. The rendered screen is
// covered against the real server by server/tests/test_server_accounts.py (API) - this is the UI logic.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const a = await loader.load('src/context/auth-state.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const en = pack.entries.find(row => row.key === 'presentation.accounts').value.locales.en;
const feature = p.resolvePresentationFeature(p.resolvePresentation(), 'accounts');

test('single-user server: nothing to sign in to; accounts server: signed out or in', () => {
  assert.deepEqual(a.authStateFromMe({ accounts: false }), { phase: 'open' });
  assert.deepEqual(a.authStateFromMe({ accounts: true, authenticated: false, registration: 'invite' }), { phase: 'signed_out', registration: 'invite' });
  const user = { id: 'u_0123456789ab', login: 'alice' };
  assert.deepEqual(a.authStateFromMe({ accounts: true, authenticated: true, registration: 'open', user }), { phase: 'signed_in', registration: 'open', user });
  assert.equal(a.showsApp({ phase: 'open' }), true);
  assert.equal(a.showsApp(a.authStateFromMe({ accounts: true, authenticated: false, registration: 'closed' })), false);
});

test('a malformed or missing answer degrades to unverified (app shown, server still enforces sessions; R43); unknown registration is treated as closed', () => {
  for (const bad of [null, [], 'x', {}, { accounts: 'yes' }]) {
    const st = a.authStateFromMe(bad);
    assert.equal(st.phase, 'unverified');
    assert.equal(a.showsApp(st), true);
    assert.equal(a.authReduce(st, { type: 'unauthorized' }).phase, 'signed_out');
  }
  assert.equal(a.authStateFromMe({ accounts: true, authenticated: false, registration: 'weird' }).registration, 'closed');
  assert.equal(a.authStateFromMe({ accounts: true, authenticated: true, registration: 'open', user: { id: 1 } }).phase, 'signed_out');
});

test('401 while signed in returns to the sign-in screen; logout too; a single-user server ignores 401', () => {
  const user = { id: 'u_1', login: 'bob' };
  const inState = { phase: 'signed_in', registration: 'closed', user };
  assert.deepEqual(a.authReduce(inState, { type: 'unauthorized' }), { phase: 'signed_out', registration: 'closed' });
  assert.deepEqual(a.authReduce(inState, { type: 'signed_out' }), { phase: 'signed_out', registration: 'closed' });
  assert.deepEqual(a.authReduce({ phase: 'open' }, { type: 'unauthorized' }), { phase: 'open' });
  assert.equal(a.authReduce({ phase: 'loading' }, { type: 'me_failed', reason: 'down' }).phase, 'unverified');
});

test('texts come from the accounts catalog; a missing catalog falls back to ids', () => {
  assert.equal(a.accountText(feature, 'sign_in'), en.sign_in);
  assert.equal(a.accountText(feature, 'signed_in_as', { login: 'alice' }), 'Signed in as alice');
  assert.equal(a.accountText(null, 'logout'), 'logout');
  assert.equal(a.accountText(null, 'error.failed', { reason: 'HTTP 500' }), 'error.failed: HTTP 500');
  assert.equal(a.registrationOffer('closed'), 'none');
  assert.equal(a.registrationOffer('invite'), 'invite');
});

test('R103/R105 guests: auto start skips the gate; a guest session is the full app; texts pl+en from the pack', () => {
  const guests = { enabled: true, start: 'auto', idle_ttl_seconds: 86400, first_answer_notice: { on: true, ask_again: 'per_device' } };
  const fresh = { accounts: true, authenticated: false, registration: 'closed', guests };
  assert.equal(a.startsGuestAutomatically(fresh), true);
  assert.equal(a.startsGuestAutomatically({ ...fresh, guests: { ...guests, start: 'explicit' } }), false);
  assert.equal(a.startsGuestAutomatically({ ...fresh, guests: { enabled: false } }), false);
  assert.equal(a.startsGuestAutomatically({ accounts: false }), false);
  const st = a.authStateFromMe({ ...fresh, guest: { id: 'g_00112233aabbccdd', idle_ttl_seconds: 60, model_source: { provider: 'groq', model: 'm' } } });
  assert.equal(st.phase, 'guest');
  assert.equal(st.guest.idleTtlSeconds, 60);
  assert.equal(st.guests.notice.askAgain, 'per_device');
  assert.equal(a.showsApp(st), true);
  assert.equal(a.authReduce(st, { type: 'unauthorized' }).phase, 'signed_out');
  // no guests offer: an unauthenticated answer stays signed_out even with a guest object
  assert.equal(a.authStateFromMe({ accounts: true, authenticated: false, registration: 'open', guest: { id: 'g_1' } }).phase, 'signed_out');
  const pl = pack.entries.find(row => row.key === 'presentation.accounts').value.locales.pl;
  for (const id of ['guest.notice_title', 'guest.notice_body_browser', 'guest.notice_body_session', 'guest.persisted', 'guest.not_persisted', 'guest.period_unknown', 'guest.download', 'guest.load', 'guest.model_line', 'guest.start'])
    assert.ok(typeof en[id] === 'string' && typeof pl[id] === 'string', id);
});

test('R103 §3.5: the notice period follows what the browser store reported', async () => {
  const g = await loader.load('src/context/guest-store.ts');
  assert.deepEqual(g.noticePeriod({ available: false, persisted: null, freeBytes: null }), { body: 'guest.notice_body_session', period: null });
  assert.equal(g.noticePeriod({ available: true, persisted: true, freeBytes: 1 }).period, 'guest.persisted');
  assert.equal(g.noticePeriod({ available: true, persisted: false, freeBytes: 1 }).period, 'guest.not_persisted');
  assert.equal(g.noticePeriod({ available: true, persisted: null, freeBytes: null }).period, 'guest.period_unknown');
  assert.equal(g.humanSeconds(86400), '1 d');
  assert.equal(g.humanSeconds(7200), '2 h');
  assert.equal(g.humanBytes(1536), '1.5 KB');
  assert.equal(g.isBundle({ schema: 'loom.guest_bundle/1', conversations: [] }), true);
  assert.equal(g.isBundle({ schema: 'x', conversations: [] }), false);
  // R103 B1b: v2 = the server's exact text; a file with the server's JSON is accepted, its size counts rows + memory
  assert.equal(g.isBundle({ schema: 'loom.guest_bundle/2', saved_at: 's', text: '{}' }), true);
  const raw = JSON.stringify({ schema: 'loom.guest_bundle/2', saved_at: '2026-10-29', content: { tables: { conversations: { columns: ['id'], rows: [['c_1'], ['c_2']] } }, memory: [{ id: 'mem' }] } });
  const b = g.bundleFromText(raw);
  assert.equal(b.schema, 'loom.guest_bundle/2');
  assert.equal(b.text, raw);
  assert.equal(g.bundleSize(b), 3);
  assert.equal(g.bundleText(b), raw);
  assert.equal(g.bundleFromText('not json'), null);
  assert.equal(g.bundleSize({ schema: 'loom.guest_bundle/1', conversations: [] }), 0);
  const pl = pack.entries.find(row => row.key === 'presentation.accounts').value.locales.pl;
  for (const id of ['guest.wipe', 'guest.wipe_confirm', 'guest.wipe_yes', 'guest.wipe_no'])
    assert.ok(typeof en[id] === 'string' && typeof pl[id] === 'string', id);
});

test('the guest model line says what the source really is (tier from data): free, paid, unknown - exact words per tier and language', async () => {
  const gl = await loader.load('src/context/guest-line.ts');
  const pl = pack.entries.find(row => row.key === 'presentation.accounts').value.locales.pl;
  const fill = (w, k) => w.replace('{{model}}', 'M').replace('{{provider}}', 'P').replace('{{terms}}', 'T');
  assert.equal(gl.modelLineId('free'), 'guest.model_line_free');
  assert.equal(gl.modelLineId('paid_credits'), 'guest.model_line_paid');
  assert.equal(gl.modelLineId('anything else'), 'guest.model_line_paid');  // not free = never called free
  assert.equal(gl.modelLineId(undefined), 'guest.model_line');
  assert.equal(gl.modelLineId(''), 'guest.model_line');
  assert.equal(fill(en['guest.model_line_free']), "Answers: M (P), the best of today's free models. T");
  assert.equal(fill(en['guest.model_line_paid']), 'Answers: M (P), a paid source (not one of the free models). T');
  assert.equal(fill(en['guest.model_line']), 'Answers: M (P). T');
  assert.equal(fill(pl['guest.model_line_free']), 'Odpowiada: M (P), najlepszy z dzisiejszych darmowych modeli. T');
  assert.equal(fill(pl['guest.model_line_paid']), 'Odpowiada: M (P), źródło płatne (nie jeden z darmowych modeli). T');
  assert.ok(!/free|darmow/.test(en['guest.model_line'] + pl['guest.model_line']), 'the neutral line makes no claim about cost');
  assert.ok(!/free|darmow/.test(en['guest.model_line_paid'].replace('not one of the free models', '') + pl['guest.model_line_paid'].replace('nie jeden z darmowych modeli', '')));
});

test('"Nothing leaves this device" is said only while no message of the conversation went to a model provider', async () => {
  const iv = await loader.load('src/context/interview.ts');
  const view = pack.entries.find(row => row.key === 'presentation.interview').value.locales;
  const form = { role: 'assistant', model: 'profile.interview', metadata: { interview: { kind: 'question' } } };
  const sent = { role: 'assistant', model: 'google/gemini-3.5-flash', metadata: {} };
  assert.equal(iv.reachedProvider([]), false);
  assert.equal(iv.reachedProvider([{ role: 'user', model: null }, { role: 'assistant', model: null, metadata: {} }]), false);
  assert.equal(iv.reachedProvider([sent]), true);
  assert.equal(iv.reachedProvider([{ role: 'user' }, sent]), true);
  for (const loc of ['en', 'pl']) {
    assert.ok(typeof view[loc]['info.why'] === 'string' && typeof view[loc]['info.why_sent'] === 'string', loc);
    assert.notEqual(view[loc]['info.why'], view[loc]['info.why_sent']);
  }
  assert.equal(view.en['info.why'], 'Nothing leaves this device.');
  assert.match(view.en['info.why_sent'], /sent to a model provider/);
  assert.equal(iv.reachedProvider([form]), false);
});
