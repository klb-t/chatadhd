// W1: the app's window state (session.ts): a panel is a window opened maximized over the layout through journaled model operations,
// closing it gives the covered window back; the start view reads layout ids (v2) and the old v1 values. Examples first, then a generative
// pass over random sequences of panel / layout changes (seeded; LOOM_WIN_RUNS / LOOM_WIN_OPS / LOOM_WIN_SEED raise the presets).
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const M = await loader.load('src/shell/windows/model.ts');
const S = await loader.load('src/shell/windows/session.ts');
const sv = await loader.load('src/shell/start-view.ts');
const czat = () => S.layoutState(sv.LAYOUTS.czat, sv.JOURNAL_LIMIT);
const siatka = () => S.layoutState(sv.LAYOUTS.siatka, sv.JOURNAL_LIMIT);
const win = (s, id) => s.layout.windows.find(w => w.id === id);

test('czat: the plain chat app is the layout (list collapsed, chat maximized); no overlay', () => {
  const s = czat();
  assert.ok(M.isPlainChatApp(s.layout));
  assert.equal(S.baseWindow(s.layout).kind, 'chat');
  assert.equal(S.activePanelOf(s), null);
  assert.equal(S.baseWindow(siatka().layout), null, 'the grid has no maximized window: the host draws it as a grid (W3)');
});

test('a panel opens as a maximized window over the layout, journaled; closing it maximizes the chat again', () => {
  const s0 = czat();
  const s1 = S.setPanel(s0, 'settings');
  assert.equal(S.activePanelOf(s1), 'settings');
  const o = S.overlays(s1.layout); assert.equal(o.length, 1);
  assert.equal(o[0].kind, 'settings'); assert.equal(o[0].mode, 'maximized'); assert.equal(o[0].params.over, 'w_chat');
  assert.equal(win(s1, 'w_chat').mode, 'docked');
  assert.equal(S.baseWindow(s1.layout).id, 'w_chat', 'the covered chat stays the base (drawn behind, inert)');
  assert.ok(s1.past.length > s0.past.length, 'the open is in the journal');
  assert.deepEqual(M.check(s1.layout), []);
  const s2 = S.setPanel(s1, null);
  assert.equal(S.activePanelOf(s2), null);
  assert.ok(M.isPlainChatApp(s2.layout));
  assert.deepEqual(s2.layout.windows.map(w => [w.id, w.mode]), s0.layout.windows.map(w => [w.id, w.mode]));
  assert.equal(M.undo(s2).state.layout.windows.some(w => w.params.panel), true, 'undo of the close brings the panel back');
});

test('one panel at a time; the toggle form; the grid opens settings as settings_fields', () => {
  let s = S.setPanel(czat(), 'settings');
  s = S.setPanel(s, 'navigator');
  assert.equal(S.overlays(s.layout).length, 1); assert.equal(S.activePanelOf(s), 'navigator');
  assert.equal(S.overlays(s.layout)[0].params.over, 'w_chat');
  s = S.setPanel(s, cur => (cur === 'navigator' ? null : 'navigator'));
  assert.equal(S.activePanelOf(s), null); assert.ok(M.isPlainChatApp(s.layout));
  assert.equal(S.setPanel(s, null), s, 'closing nothing changes nothing');
  const g = S.setPanel(siatka(), 'settings');
  assert.equal(S.overlays(g.layout)[0].kind, 'settings_fields'); assert.equal(S.overlays(g.layout)[0].params.over, null);
  assert.equal(S.panelKind('graph-entry', 'czat'), 'graph_entry');
  assert.equal(S.panelKind('a-new-panel', 'czat'), 'a-new-panel', 'open map: an unknown panel opens as its own kind');
});

test('switching the layout keeps the open panel (as before W1)', () => {
  const s = S.switchLayout(S.setPanel(czat(), 'logs'), sv.LAYOUTS.siatka, sv.JOURNAL_LIMIT);
  assert.equal(s.layout.id, 'siatka'); assert.equal(S.activePanelOf(s), 'logs');
  const back = S.switchLayout(S.setPanel(s, null), sv.LAYOUTS.czat, sv.JOURNAL_LIMIT);
  assert.ok(M.isPlainChatApp(back.layout));
});

test('start view: layout ids, aliases, v1 saved values still read, v2 written', () => {
  assert.equal(sv.layoutOfMode(undefined), 'czat'); assert.equal(sv.layoutOfMode('full_screen_chat'), 'czat');
  assert.equal(sv.layoutOfMode('views_grid'), 'siatka'); assert.equal(sv.layoutOfMode('windows'), 'siatka');
  assert.equal(sv.layoutOfMode('layout:siatka'), 'siatka'); assert.equal(sv.layoutOfMode('junk'), 'czat');
  assert.deepEqual(sv.parseShellHash('#uklad/siatka/c_1'), { layout: 'siatka', conversationId: 'c_1' });
  assert.deepEqual(sv.parseShellHash('#uklad/nie_ma'), { layout: 'czat', conversationId: null }, 'an unknown layout opens the default');
  assert.equal(sv.resolveLayoutId('workshop'), 'siatka'); assert.equal(sv.resolveLayoutId('chat'), 'czat');
  const mem = new Map(); const store = { getItem: k => mem.get(k) ?? null, setItem: (k, v) => mem.set(k, v) };
  mem.set(sv.LAST_KEY_V1, JSON.stringify({ schema: 'loom.shell.last/1', layout: 'workshop', conversation_id: 'c_old' }));
  assert.deepEqual(sv.readSaved(store), { layout: 'siatka', conversationId: 'c_old' }, 'a v1 value saved before W1 is read');
  sv.writeSaved(store, { layout: 'czat', conversationId: 'c_new' });
  assert.equal(JSON.parse(mem.get(sv.LAST_KEY)).schema, 'loom.shell.last/2');
  assert.deepEqual(sv.readSaved(store), { layout: 'czat', conversationId: 'c_new' }, 'v2 wins over v1');
  mem.set(sv.LAST_KEY, '{broken'); assert.deepEqual(sv.readSaved(store), { layout: 'siatka', conversationId: 'c_old' }, 'unreadable v2 falls back to v1');
});

test('generative: any sequence of panel / layout changes keeps one overlay at most, valid invariants, and closing restores the preset', () => {
  const runs = Number(process.env.LOOM_WIN_RUNS ?? 200), ops = Number(process.env.LOOM_WIN_OPS ?? 30);
  let seed = Number(process.env.LOOM_WIN_SEED ?? 1101);
  const rnd = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
  const panels = ['settings', 'navigator', 'logs', 'graph-entry', 'import', 'x-unknown', null];
  const ids = Object.keys(sv.LAYOUTS);
  for (let r = 0; r < runs; r++) {
    const start = seed;
    let s = czat();
    for (let i = 0; i < ops; i++) {
      const p = rnd();
      if (p < 0.15) s = S.switchLayout(s, sv.LAYOUTS[ids[Math.floor(rnd() * ids.length)]], sv.JOURNAL_LIMIT);
      else if (p < 0.25) { const want = panels[Math.floor(rnd() * panels.length)]; s = S.setPanel(s, cur => (cur === want ? null : want)); }
      else s = S.setPanel(s, panels[Math.floor(rnd() * panels.length)]);
      const msg = `seed ${start}, step ${i}`;
      assert.ok(S.overlays(s.layout).length <= 1, msg);
      assert.deepEqual(M.check(s.layout), [], msg);
      assert.ok(s.past.length <= s.limit, msg);
      if (s.layout.id === 'czat') assert.equal(S.baseWindow(s.layout)?.kind, 'chat', msg);
    }
    const closed = S.setPanel(s, null);
    const preset = S.layoutState(sv.LAYOUTS[closed.layout.id]);
    assert.deepEqual(closed.layout.windows.map(w => [w.id, w.mode]), preset.layout.windows.map(w => [w.id, w.mode]), `seed ${start}: closing restores the preset`);
  }
});

test('W2 restore in siatka with window_restore_front=layout leaves no panel open (it waits in the switcher); `saved` keeps it; czat still brings the chat forward', () => {
  const opened = S.openPanel(siatka(), 'graph');
  assert.equal(S.activePanelOf(opened), 'graph');
  const left = M.createState(opened.layout, sv.JOURNAL_LIMIT);   // what a reload reads back from window_layout_last
  const r = S.restoreFront(left, undefined, 'layout');
  assert.equal(S.activePanelOf(r), null, 'no open drawer after the start');
  assert.equal(S.overlays(r.layout).length, 1, 'the restored panel window still exists (waits in the switcher)');
  assert.equal(r.past.length, 0);
  assert.equal(S.activePanelOf(S.restoreFront(left, undefined, 'saved')), 'graph');
  const c = S.openPanel(czat(), 'graph');
  const rc = S.restoreFront(M.createState(c.layout, sv.JOURNAL_LIMIT), sv.LAYOUTS.czat.windows.find(w => w.mode === 'maximized').id, 'layout');
  assert.equal(S.activePanelOf(rc), null);
  assert.equal(S.baseWindow(rc.layout).kind, 'chat');
});
