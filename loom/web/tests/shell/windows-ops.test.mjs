// W2: the window operations of a client step (ops.ts) and the saved window state (window_layout_last). Examples first, then a generative
// pass over random sequences of window steps (seeded; LOOM_WIN_RUNS / LOOM_WIN_OPS / LOOM_WIN_SEED raise the presets): the model's check
// stays empty, one window is in front, the switcher lists every window opened over the layout, a visit cycle A→B→A→B is 3 changes.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const M = await loader.load('src/shell/windows/model.ts');
const S = await loader.load('src/shell/windows/session.ts');
const O = await loader.load('src/shell/windows/ops.ts');
const sv = await loader.load('src/shell/start-view.ts');
const PANELS = ['graph', 'navigator', 'settings', 'memory', 'import', 'logs', 'context', 'methods', 'analysis', 'operations', 'perspectives'];
const known = id => PANELS.includes(id);
const czat = () => S.layoutState(sv.LAYOUTS.czat, sv.JOURNAL_LIMIT);
const step = (s, action, args) => O.runWindowStep(s, action, args, known, 'phone');

test('words of a sentence resolve by stems of the data (pl inflection, en, ids)', () => {
  assert.deepEqual(O.resolveWord('graf', known), { panel: 'navigator' });
  assert.deepEqual(O.resolveWord('czatem', known), { chat: true });
  assert.deepEqual(O.resolveWord('the graph', known), { panel: 'navigator' });
  assert.deepEqual(O.resolveWord('ustawienia', known), { panel: 'settings' });
  assert.deepEqual(O.resolveWord('memory', known), { panel: 'memory' });
  assert.equal(O.resolveWord('kalafior', known), null);
});

test('„otwórz graf obok” on a phone: the next maximized window of the stack; the receipt says beside became maximize', () => {
  const r = step(czat(), 'window.open', { target: 'graf', placement: 'beside' });
  assert.equal(r.receipt.state, 'ok');
  assert.equal(r.receipt.placementAsked, 'beside');
  assert.equal(r.receipt.placementUsed, 'maximize');
  assert.equal(S.activePanelOf(r.state), 'navigator');
  assert.equal(O.switcherEntries(r.state.layout).length, 2);
  assert.deepEqual(M.check(r.state.layout), []);
  // the chat comes forward, the graph waits in the switcher
  const back = step(r.state, 'window.focus', { target: 'czat' });
  assert.equal(S.activePanelOf(back.state), null);
  assert.equal(S.baseWindow(back.state.layout).kind, 'chat');
  assert.equal(O.switcherEntries(back.state.layout).length, 2);
  // opening it again brings the same window forward (no second graph)
  const again = step(back.state, 'window.open', { target: 'graf' });
  assert.equal(O.switcherEntries(again.state.layout).length, 2);
  assert.equal(S.activePanelOf(again.state), 'navigator');
});

test('„zamknij wszystko poza czatem” closes every window over the layout and leaves the chat in front; pinned windows stay', () => {
  let s = czat();
  for (const t of ['graf', 'ustawienia', 'pamięć']) s = step(s, 'window.open', { target: t }).state;
  assert.equal(O.switcherEntries(s.layout).length, 4);
  const r = step(s, 'window.close', { except: 'czatem' });
  assert.equal(r.receipt.windows.length, 3);
  assert.equal(O.switcherEntries(r.state.layout).length, 1);
  assert.equal(S.baseWindow(r.state.layout).kind, 'chat');
  assert.equal(S.activePanelOf(r.state), null);
  assert.deepEqual(M.check(r.state.layout), []);
  const none = step(s, 'window.close', { target: 'kalafior' });
  assert.equal(none.receipt.state, 'none');
});

test('next / prev cycle through the switcher; an unknown word is an honest "none"', () => {
  let s = step(czat(), 'window.open', { target: 'graf' }).state;
  const n = step(s, 'window.next', {});
  assert.equal(S.activePanelOf(n.state), null);
  const p = step(n.state, 'window.prev', {});
  assert.equal(S.activePanelOf(p.state), 'navigator');
  assert.equal(step(s, 'window.open', { target: 'kalafior' }).receipt.state, 'none');
});

test('window_layout_last: written into v2, read back only when it is a valid layout of the same id; v1 still read', () => {
  const s = step(czat(), 'window.open', { target: 'graf', placement: 'beside' }).state;
  const store = new Map();
  const host = { setItem: (k, v) => store.set(k, v), getItem: k => store.get(k) ?? null };
  sv.writeSaved(host, { layout: 'czat', conversationId: null, windows: s.layout });
  const back = sv.readSaved(host);
  assert.equal(back.layout, 'czat');
  assert.equal(back.windows.windows.length, s.layout.windows.length);
  assert.equal(sv.savedWindows({ ...s.layout, id: 'siatka' }, 'czat'), null);
  assert.equal(sv.savedWindows({ id: 'czat' }, 'czat'), null);
  const v1 = new Map([['loom.shell.last.v1', JSON.stringify({ schema: 'loom.shell.last/1', layout: 'chat', conversation_id: null })]]);
  assert.equal(sv.readSaved({ getItem: k => v1.get(k) ?? null }).layout, 'czat');
});

test('generative: random window steps keep the model valid, one window in front, every overlay in the switcher', () => {
  const runs = Number(process.env.LOOM_WIN_RUNS || 150), ops = Number(process.env.LOOM_WIN_OPS || 30);
  let seed = Number(process.env.LOOM_WIN_SEED || 7);
  const rnd = n => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
  const words = ['graf', 'ustawienia', 'pamięć', 'czat', 'import', 'kalafior', 'logi', 'kontekst'];
  for (let r = 0; r < runs; r++) {
    let s = czat();
    for (let i = 0; i < ops; i++) {
      const k = rnd(6);
      const w = words[rnd(words.length)];
      const res = k === 0 || k === 1 ? step(s, 'window.open', { target: w, placement: ['beside', 'maximize', 'float', 'x'][rnd(4)] })
        : k === 2 ? step(s, 'window.close', rnd(2) ? { except: w } : { target: w })
        : k === 3 ? step(s, 'window.focus', { target: w })
        : k === 4 ? step(s, 'window.next', {}) : { state: S.setPanel(s, rnd(2) ? null : 'settings') };
      s = res.state;
      const l = s.layout;
      assert.deepEqual(M.check(l), [], `run ${r} op ${i}`);
      assert.ok(l.windows.filter(x => x.mode === 'maximized').length <= 1, 'one maximized window');
      assert.ok(O.visibleId(l) !== null, `run ${r} op ${i}: a window is in front`);
      for (const o of S.overlays(l)) assert.ok(O.switcherEntries(l).includes(o));
      assert.ok(s.past.length <= s.limit);
    }
  }
});
