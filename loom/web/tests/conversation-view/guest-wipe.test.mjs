// "Clear everything from this browser": every step reports its own result; the text names what stayed and why. Stubs: store, fetch, storages (synthetic, in-process).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const g = await loader.load('src/context/guest-store.ts');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const locales = pack.entries.find(row => row.key === 'presentation.ui_shell' || row.value?.locales?.en?.['guest.wipe_yes']).value.locales;
const textFor = (lang) => (id, params = {}) => String(locales[lang][id] ?? id).replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? '');
const mkStorage = (n) => { const m = new Map(Array.from({ length: n }, (_, i) => [`k${i}`, 'v'])); return { get length() { return m.size; }, clear() { m.clear(); } }; };
const setup = ({ local = 2, session = 0, failLocal = false } = {}) => {
  globalThis.localStorage = failLocal ? { get length() { return 1; }, clear() { throw new Error('blocked by policy'); } } : mkStorage(local);
  globalThis.sessionStorage = mkStorage(session);
  Object.defineProperty(globalThis, 'navigator', { value: { storage: {} }, configurable: true });
};
const okFetch = async () => ({ ok: true, status: 200 });

test('all steps ok or nothing to remove: the exact "all removed" text in every language', async () => {
  setup();
  const steps = await g.wipeEverything({ wipe: async () => {} }, okFetch);
  assert.deepEqual(steps, [
    { step: 'store', status: 'ok' }, { step: 'opfs', status: 'nothing' }, { step: 'local', status: 'ok' },
    { step: 'session', status: 'nothing' }, { step: 'logout', status: 'ok' }]);
  assert.deepEqual(g.wipeReportText(steps, textFor('en')), ['Everything was removed from this browser.']);
  assert.deepEqual(g.wipeReportText(steps, textFor('pl')), ['Wszystko zostało usunięte z tej przeglądarki.']);
  assert.deepEqual(g.wipeReportText(steps, textFor('nl')), ['Alles is uit deze browser verwijderd.']);
});
test('a forced failure is returned with its reason and named in the shown text; the other steps still run', async () => {
  setup({ failLocal: true });
  const steps = await g.wipeEverything({ wipe: async () => { throw new Error('database is locked'); } }, async () => ({ ok: false, status: 503 }));
  assert.deepEqual(steps.map(s => [s.step, s.status, s.reason]), [
    ['store', 'failed', 'database is locked'], ['opfs', 'nothing', undefined], ['local', 'failed', 'blocked by policy'],
    ['session', 'nothing', undefined], ['logout', 'failed', 'HTTP 503']]);
  assert.deepEqual(g.wipeReportText(steps, textFor('en')), [
    'Not everything was removed. What is left, and why:',
    'stored conversations: not removed (database is locked)', 'browser file storage: nothing to remove',
    'local settings: not removed (blocked by policy)', 'session settings: nothing to remove',
    'ending the session on the server: not removed (HTTP 503)']);
  assert.match(g.wipeReportText(steps, textFor('pl')).join('\n'), /zapisane rozmowy: nie usunięto \(database is locked\)/);
});
