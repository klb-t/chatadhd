// W5: named layouts, the active layout per device class and the persisted journal (named.ts), plus the window settings that override the
// presets of layouts.json (ops.ts windowDataOf). Examples first, then a generative pass: random values of the three settings fields (any
// shape, also rubbish) never throw, a saved journal read back is the same journal within its bound, and an applied layout is undone whole.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const M = await loader.load('src/shell/windows/model.ts');
const S = await loader.load('src/shell/windows/session.ts');
const N = await loader.load('src/shell/windows/named.ts');
const O = await loader.load('src/shell/windows/ops.ts');
const sv = await loader.load('src/shell/start-view.ts');
const czat = () => S.layoutState(sv.LAYOUTS.czat, sv.JOURNAL_LIMIT);
const NOW = '2026-10-27T10:00:00.000Z';

test('device classes: preset phone up to 599 px, desktop above; the rules are data (an open list)', () => {
  assert.equal(N.deviceClassOf(390), 'phone');
  assert.equal(N.deviceClassOf(599), 'phone');
  assert.equal(N.deviceClassOf(1280), 'desktop');
  const rules = [{ id: 'watch', max_width: 300 }, { id: 'tablet', min_width: 600, max_width: 1100 }, { id: 'tv', min_width: 1101 }];
  assert.deepEqual([200, 390, 800, 2000].map(w => N.deviceClassOf(w, rules)), ['watch', 'any', 'tablet', 'tv']);
});

test('save „praca”: one entry, found by any spelling; the same name replaces; rename refuses a clash; delete', () => {
  const s = S.openPanel(czat(), 'navigator');
  let r = N.saveNamed([], '«praca»', s.layout, NOW, 'phone');
  assert.equal(r.entry.label, 'praca');
  assert.equal(r.entry.schema, 'loom.window_layout/2');
  assert.equal(r.replaced, false);
  for (const name of ['praca', 'Praca', '„praca”', '"praca"', r.entry.id]) assert.equal(N.findNamed(r.list, name)?.id, r.entry.id, name);
  const r2 = N.saveNamed(r.list, 'PRACA', czat().layout, NOW);
  assert.equal(r2.list.length, 1);
  assert.equal(r2.replaced, true);
  const r3 = N.saveNamed(r2.list, 'dom', s.layout, NOW);
  assert.equal(r3.list.length, 2);
  assert.equal(N.renameNamed(r3.list, r3.entry.id, 'Praca').conflict, 'PRACA');
  assert.equal(N.renameNamed(r3.list, r3.entry.id, 'wieczór').list.find(x => x.id === r3.entry.id).label, 'wieczór');
  assert.equal(N.deleteNamed(r3.list, r3.entry.id).length, 1);
  // read back from the settings value (list, {layouts}, JSON text); rubbish entries are skipped
  for (const v of [r3.list, { layouts: r3.list }, JSON.stringify(r3.list)]) assert.equal(N.readNamed(v).length, 2);
  assert.equal(N.readNamed([...r3.list, { label: 'x' }, 7, null, { label: 'y', layout: { id: 'czat' } }]).length, 2);
});

test('active layout per device class; lastOwn = the active one, else the newest saved', () => {
  let v = N.writeActive(undefined, 'phone', 'u_praca');
  v = N.writeActive(v, 'desktop', 'u_dom');
  assert.equal(N.readActive(v, 'phone'), 'u_praca');
  assert.equal(N.readActive(v, 'desktop'), 'u_dom');
  assert.equal(N.readActive(N.writeActive(v, 'phone', null), 'phone'), null);
  assert.equal(N.readActive(N.writeActive(v, 'phone', null), 'desktop'), 'u_dom', 'clearing one class leaves the other');
  assert.equal(N.readActive('u_x', 'tablet'), 'u_x', 'a bare id is every class');
  const a = N.saveNamed([], 'a', czat().layout, '2026-01-01T00:00:00Z').list;
  const b = N.saveNamed(a, 'b', czat().layout, '2026-02-01T00:00:00Z').list;
  assert.equal(N.lastOwn(b, null).label, 'b');
  assert.equal(N.lastOwn(b, b[0].id).label, 'a');
  assert.equal(N.lastOwn([], 'u_a'), null);
});

test('apply is one journal entry: undo brings the previous layout back whole; reset goes back to the shipped form', () => {
  const s0 = czat();
  const saved = S.openPanel(S.openPanel(s0, 'settings'), 'navigator').layout;
  const s1 = N.replaceLayout(s0, saved, 'layout.apply');
  assert.equal(s1.past.length, 1);
  assert.equal(S.activePanelOf(s1), 'navigator');
  assert.deepEqual(M.undo(s1).state.layout, s0.layout);
  assert.equal(N.replaceLayout(s1, saved, 'layout.apply'), s1, 'the same layout again changes nothing');
  assert.equal(N.resetTarget('czat', 'phone', sv.LAYOUTS), 'czat');
  assert.equal(N.resetTarget('u_whatever', 'phone', sv.LAYOUTS, { layout_reset: { phone: 'siatka' } }), 'siatka');
  assert.equal(N.shippedId('Siatka', sv.LAYOUTS), 'siatka');
  assert.equal(N.shippedId('warsztat', sv.LAYOUTS), 'siatka');
  assert.equal(N.shippedId('praca', sv.LAYOUTS), null);
});

test('journal: written per device class, read back within the bound; limit 0 = no undo', () => {
  let s = czat();
  for (const p of ['settings', 'navigator', 'memory']) s = S.openPanel(s, p);
  s = S.closePanels(s);
  const v = N.writeJournal({ desktop: { past: [], future: [] } }, 'phone', s);
  const j = N.readJournal(JSON.parse(JSON.stringify(v)), 'phone');
  assert.equal(j.past.length, s.past.length);
  assert.deepEqual(j.past.map(e => e.op), s.past.map(e => e.op));
  assert.equal(N.readJournal(v, 'desktop').past.length, 0, 'the other class keeps its own journal');
  assert.equal(N.readJournal({ ...v, limit: 2 }, 'phone').past.length, 2);
  assert.equal(N.readJournal({ ...v, limit: 0 }, 'phone').past.length, 0);
  assert.equal(N.readJournal(undefined, 'phone').limit, 50);
});

test('window settings override the presets of layouts.json; unset or unreadable = the preset', () => {
  assert.equal(O.windowDataOf({}).window_back, 'visits');
  assert.equal(O.windowDataOf({ window_back: 'none' }).window_back, 'none');
  assert.equal(O.windowDataOf({ window_back: 7 }).window_back, 'visits');
  assert.equal(O.windowDataOf({ window_restore_front: 'saved' }).window_restore_front, 'saved');
  assert.equal(O.windowDataOf({ window_restore_last: false }).window_restore_last, false);
  assert.equal(O.windowDataOf({ window_restore_last: 'false' }).window_restore_last, false);
  assert.deepEqual(O.windowDataOf({ window_placements: '{"phone":{"beside":"float"}}' }).window_placements, { phone: { beside: 'float' } });
  assert.deepEqual(O.windowDataOf({ window_placements: 'nonsense' }).window_placements, O.WINDOW_DATA.window_placements);
});

test('generative: random values of the settings never throw; save → read → apply → undo round-trips', () => {
  let seed = Number(process.env.LOOM_WIN_SEED ?? 20261027);
  const rnd = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648);
  const pick = a => a[Math.floor(rnd() * a.length)];
  const junk = () => pick([undefined, null, 0, 7, '', 'x', '{', '[]', [], {}, [1, 2], { phone: 3 }, { layouts: 'no' }, { limit: -1 }, { limit: 'a' }, { phone: { past: [1], future: 'x' } }]);
  const panels = ['settings', 'navigator', 'memory', 'logs', 'import'];
  const runs = Number(process.env.LOOM_WIN_RUNS ?? 200);
  for (let i = 0; i < runs; i++) {
    for (const f of [N.readNamed, v => N.readActive(v, 'phone'), v => N.readJournal(v, 'phone'), v => N.writeActive(v, 'phone', 'u_a'), v => N.writeJournal(v, 'desktop', czat()), v => O.windowDataOf({ window_back: v, window_placements: v, window_restore_last: v, window_restore_front: v })])
      assert.doesNotThrow(() => f(junk()));
    let s = czat();
    for (let k = Math.floor(rnd() * 6); k > 0; k--) s = rnd() < 0.7 ? S.openPanel(s, pick(panels)) : S.closePanels(s);
    const list = N.saveNamed(N.readNamed(junk()), `u${i}`, s.layout, NOW).list;
    const back = N.readNamed(JSON.parse(JSON.stringify(list)));
    const own = N.findNamed(back, `U${i}`);
    assert.ok(own, 'the saved layout reads back');
    const base = czat();
    const applied = N.replaceLayout(base, own.layout, 'layout.apply');
    assert.deepEqual(M.check(applied.layout), []);
    if (applied !== base) assert.deepEqual(M.undo(applied).state.layout, base.layout);
    const j = N.readJournal(N.writeJournal(junk(), 'phone', applied), 'phone');
    assert.ok(j.past.length <= Math.max(applied.past.length, 0));
  }
});
