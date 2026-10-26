// W0: the window model (model.ts), the v1 -> v2 readers (migrate.ts) and the preset layouts of layouts.json v2: examples; the generative file covers the space.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const M = await loader.load('src/shell/windows/model.ts');
const G = await loader.load('src/shell/windows/migrate.ts');
const R = await loader.load('src/shell/windows/registry.ts');
const file = JSON.parse(await readFile(new URL('../../src/shell/data/layouts.json', import.meta.url), 'utf8'));
const { layouts, limit } = G.readLayoutsFile(file);
let n = 0;
const ctx = { kinds: R.kindsCtx(), newId: () => 'w_t' + (n++).toString(16).padStart(10, '0') };
const st = id => M.createState(layouts[id], limit, ctx);

test('layouts.json v2: czat and siatka are valid dock trees; czat is the plain chat app (P8), siatka is not maximized', () => {
  assert.deepEqual(Object.keys(layouts).sort(), ['czat', 'siatka']);
  for (const l of Object.values(layouts)) assert.deepEqual(M.check(l), [], l.id);
  assert.ok(M.isPlainChatApp(layouts.czat));
  assert.ok(!M.isPlainChatApp(layouts.siatka));
  assert.equal(file.layouts.czat.menu.length, 9);   // the menu data travels with the layout untouched
  assert.equal(limit, 50);
});

test('reader v1 -> v2: today\'s file (czat + warsztat, no trees) loads as czat + siatka with the same menu', async () => {
  const v1 = { schema: 'loom.shell_layouts/1', default: 'czat', layouts: { czat: { label: 'Czat', layout_mode: 'full_screen_chat', menu: [{ id: 'graph' }], starters: 4 }, warsztat: { label: 'Warsztat', layout_mode: 'views_grid' } } };
  const r = G.readLayoutsFile(v1);
  assert.deepEqual(Object.keys(r.layouts).sort(), ['czat', 'siatka']);
  assert.ok(M.isPlainChatApp(r.layouts.czat));
  assert.deepEqual(r.layouts.czat.menu, [{ id: 'graph' }]);
  for (const l of Object.values(r.layouts)) assert.deepEqual(M.check(l), []);
  assert.equal(r.default, 'czat');
});

test('readers never throw: junk gives the default preset and an advisory', () => {
  for (const junk of [null, 42, 'x', [], {}, { layouts: 7 }, { layouts: { a: null } }]) {
    const r = G.readLayoutsFile(junk);
    assert.ok(r.layouts.czat || Object.keys(r.layouts).length);
    assert.ok(r.advisories.length >= 1);
  }
  assert.equal(G.layoutIdOfSaved('chat'), 'czat'); assert.equal(G.layoutIdOfSaved('workshop'), 'siatka'); assert.equal(G.layoutIdOfSaved(7), 'czat');
  assert.deepEqual(G.layoutIdOfMode('views_grid'), { id: 'siatka' });
  assert.equal(G.layoutIdOfMode('layout:praca').id, 'praca');
  assert.ok(G.layoutIdOfMode('???').advisory);
  assert.deepEqual(G.parseLayoutHash('#warsztat'), { layout: 'siatka', conversationId: null });
  assert.deepEqual(G.parseLayoutHash('#czat/c_ab'), { layout: 'czat', conversationId: 'c_ab' });
  assert.deepEqual(G.parseLayoutHash('#uklad/praca/c_1'), { layout: 'praca', conversationId: 'c_1' });
  assert.equal(G.parseLayoutHash('#nic'), null);
});

test('open beside, focus, float, dock, tab, minimize, restore, close: the basic life of windows', () => {
  let s = st('czat');
  let r = M.apply(s, { op: 'open', kind: 'navigator', node: 'conv:x', placement: 'beside' }, ctx); s = r.state;
  assert.ok(r.applied); assert.equal(s.layout.windows.length, 3);
  const nav = s.layout.windows[2];
  assert.equal(nav.mode, 'docked'); assert.deepEqual(M.check(s.layout), []);
  r = M.apply(s, { op: 'float', id: nav.id }, ctx); s = r.state;
  assert.equal(M.apply(s, { op: 'focus', id: nav.id }, ctx).state.layout.focus, nav.id);
  assert.equal(s.layout.windows[2].mode, 'floating');
  r = M.apply(s, { op: 'dock', id: nav.id, target: { leaf: 'L0' } }, ctx); s = r.state;
  assert.equal(s.layout.windows.find(w => w.id === nav.id).mode, 'tabbed');
  r = M.apply(s, { op: 'minimize', id: nav.id }, ctx); s = r.state;
  assert.equal(M.reachability(s.layout)[nav.id], 'switcher');
  r = M.apply(s, { op: 'restore', id: nav.id }, ctx); s = r.state;
  assert.notEqual(s.layout.windows.find(w => w.id === nav.id).mode, 'minimized');
  r = M.apply(s, { op: 'close', id: nav.id }, ctx); s = r.state;
  assert.equal(s.layout.windows.length, 2); assert.deepEqual(M.check(s.layout), []);
});

test('invalid input: unchanged state + advisory, never a throw (unknown id, unknown leaf, planned kind, junk op)', () => {
  const s = st('czat');
  for (const op of [{ op: 'close', id: 'nope' }, { op: 'dock', id: 'w_chat', target: { leaf: 'zzz' } }, { op: 'open', kind: 'connection' }, { op: 'wat' }, null, { op: 'float', id: 'w_chat', rect: { x: NaN } }, { op: 'split', leaf: 'L1', dir: 'q' }]) {
    const r = M.apply(s, op, ctx);
    if (!r.applied) { assert.strictEqual(r.state, s); assert.ok(r.advisories.length >= 1, JSON.stringify(op)); }
  }
  assert.equal(M.apply(s, { op: 'open', kind: 'connection' }, ctx).advisories[0].id, 'planned_kind');
});

test('degenerate configurations are accepted, with advisories (ratio 0 and 1, zero-size window, 40 leaves)', () => {
  let s = st('czat');
  let r = M.apply(s, { op: 'dock', id: 'w_list', target: { side: 'right', ratio: 1, of: 'L1' } }, ctx);
  assert.ok(r.applied); assert.ok(r.advisories.some(a => a.id === 'zero_size')); s = r.state;
  r = M.apply(s, { op: 'split', leaf: 'L1', dir: 'h', ratio: 7 }, ctx);
  assert.ok(r.advisories.some(a => a.id === 'ratio_clamped')); s = r.state;
  for (let i = 0; i < 40; i++) s = M.apply(s, { op: 'split', leaf: M.firstLeaf(s.layout.tree), dir: i % 2 ? 'h' : 'v' }, ctx).state;
  assert.ok(M.leafIds(s.layout.tree).length >= 40); assert.deepEqual(M.check(s.layout), []);
  const fit = M.fitToArea(s.layout, { w: 390, h: 844 });
  assert.equal(fit.advisories.filter(a => a.id === 'dock_narrow').length, M.leafIds(s.layout.tree).filter(k => s.layout.leaves[k].windows.length && M.leafSizes(s.layout.tree, { w: 390, h: 844 })[k].w < 160).length);
});

test('empty leaf: collapsed with an advisory by default, kept for explicit leaves and under policy none', () => {
  let s = st('czat');
  let r = M.apply(s, { op: 'dock', id: 'w_list', target: { leaf: 'L1' } }, ctx);   // L0 emptied
  assert.ok(r.advisories.some(a => a.id === 'empty_leaf_collapsed')); assert.deepEqual(M.leafIds(r.state.layout.tree), ['L1']);
  r = M.apply(s, { op: 'dock', id: 'w_list', target: { leaf: 'L1' } }, { ...ctx, collapseEmptyLeaves: 'none' });
  assert.deepEqual(M.leafIds(r.state.layout.tree).sort(), ['L0', 'L1']);
  r = M.apply(s, { op: 'split', leaf: 'L1', dir: 'h' }, ctx);   // explicit, empty, stays
  assert.equal(M.leafIds(r.state.layout.tree).length, 3); assert.deepEqual(M.check(r.state.layout), []);
});

test('journal: undo/redo restore exact layouts, limit applies, nothing to undo is an advisory', () => {
  let s = M.createState(layouts.czat, 2, ctx); const l0 = s.layout;
  for (const k of ['a', 'b', 'c']) s = M.apply(s, { op: 'open', kind: 'logs', node: k, placement: 'float' }, ctx).state;
  assert.equal(s.past.length, 2);
  const u = M.undo(s); assert.equal(u.state.layout.windows.length, 4); assert.equal(M.redo(u.state).state.layout.windows.length, 5);
  assert.equal(M.undo(M.createState(l0)).advisories[0].id, 'nothing_to_undo');
});
