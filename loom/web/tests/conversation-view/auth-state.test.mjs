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

test('a malformed answer is an error and never opens the app; unknown registration is treated as closed', () => {
  for (const bad of [null, [], 'x', {}, { accounts: 'yes' }]) assert.equal(a.authStateFromMe(bad).phase, 'error');
  assert.equal(a.authStateFromMe({ accounts: true, authenticated: false, registration: 'weird' }).registration, 'closed');
  assert.equal(a.authStateFromMe({ accounts: true, authenticated: true, registration: 'open', user: { id: 1 } }).phase, 'signed_out');
});

test('401 while signed in returns to the sign-in screen; logout too; a single-user server ignores 401', () => {
  const user = { id: 'u_1', login: 'bob' };
  const inState = { phase: 'signed_in', registration: 'closed', user };
  assert.deepEqual(a.authReduce(inState, { type: 'unauthorized' }), { phase: 'signed_out', registration: 'closed' });
  assert.deepEqual(a.authReduce(inState, { type: 'signed_out' }), { phase: 'signed_out', registration: 'closed' });
  assert.deepEqual(a.authReduce({ phase: 'open' }, { type: 'unauthorized' }), { phase: 'open' });
  assert.equal(a.authReduce({ phase: 'loading' }, { type: 'me_failed', reason: 'down' }).phase, 'error');
});

test('texts come from the accounts catalog; a missing catalog falls back to ids', () => {
  assert.equal(a.accountText(feature, 'sign_in'), en.sign_in);
  assert.equal(a.accountText(feature, 'signed_in_as', { login: 'alice' }), 'Signed in as alice');
  assert.equal(a.accountText(null, 'logout'), 'logout');
  assert.equal(a.accountText(null, 'error.failed', { reason: 'HTTP 500' }), 'error.failed: HTTP 500');
  assert.equal(a.registrationOffer('closed'), 'none');
  assert.equal(a.registrationOffer('invite'), 'invite');
});
