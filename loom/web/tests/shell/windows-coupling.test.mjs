// W6 (OKNA §5, §10): coupling of window parameters (coupling.ts, dependencies.ts, binding.ts). Examples first (zoom ×2 between two graph
// windows, a loop raising coupling_cycle, unbind restoring independence, the view-state migration), then a generative pass (seeded;
// LOOM_CPL_RUNS raises the preset): random binding graphs over random windows, parameters, channels and dependencies (also useless ones)
// never loop forever, every loop of the graph is reported, an acyclic graph settles, removing all bindings restores independence, and a
// layout without bindings behaves exactly as before (writing a parameter changes only that parameter).
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const C = await loader.load('src/shell/windows/coupling.ts');
const D = await loader.load('src/shell/windows/dependencies.ts');
const S = await loader.load('src/shell/windows/session.ts');
const A = await loader.load('src/shell/windows/arrange.ts');
const V = await loader.load('src/profiles/view-state.ts');
const sv = await loader.load('src/shell/start-view.ts');
const env = (k, d) => (Number.isFinite(Number(process.env[k])) && process.env[k] ? Number(process.env[k]) : d);
const CONFIG = { runs: env('LOOM_CPL_RUNS', 300), seed: env('LOOM_CPL_SEED', 20261027) };

/** Two navigator windows docked side by side (the e2e's configuration). */
function twoGraphs() {
  let s = S.openPanel(S.layoutState(sv.LAYOUTS.czat), 'navigator');
  const g1 = s.layout.windows.find(w => w.params.panel === 'navigator').id;
  s = A.arrangeWindow(s, g1, 'right');
  s = A.arrangeWindow(s, g1, 'duplicate');
  const g2 = s.layout.windows.filter(w => w.params.panel === 'navigator').find(w => w.id !== g1).id;
  return { s, g1, g2 };
}
const scale2 = from => ({ binding: 'linked', from: { window: from, param: 'zoom' }, dependency: { kind: 'scale', factor: 2 } });

test('duplicate: a second window of the same panel docked beside the first, one journal entry, undo removes it', () => {
  const { s, g1, g2 } = twoGraphs();
  assert.notEqual(g1, g2);
  assert.equal(s.layout.windows.filter(w => w.params.panel === 'navigator').length, 2);
  assert.ok(s.layout.windows.find(w => w.id === g2).dock, 'docked');
  assert.equal(s.layout.windows.filter(w => w.params.panel === 'navigator').length, 2);
  const back = (s.past.at(-1)).layout;
  assert.equal(back.windows.filter(w => w.params.panel === 'navigator').length, 1);
});

test('zoom bound ×2: zooming one changes the other ×2; the bound one zoomed alone keeps its value (not undone); unbind = independent', () => {
  let { s, g1, g2 } = twoGraphs();
  const before = s.past.length;
  s = C.bindParam(s, g2, 'zoom', scale2(g1));
  assert.equal(s.past.length, before + 1, 'bind = one journal entry');
  let v = C.emptyValues();
  v = C.setParam(s.layout, v, g1, 'zoom', 1).values;
  assert.equal(C.resolveParam(s.layout, v, g2, 'zoom'), 2);
  v = C.setParam(s.layout, v, g1, 'zoom', 1.5).values;
  assert.equal(C.resolveParam(s.layout, v, g2, 'zoom'), 3);
  const own = C.setParam(s.layout, v, g2, 'zoom', 7);
  assert.equal(C.resolveParam(s.layout, own.values, g2, 'zoom'), 7, 'the person zoomed the bound window: its change stays');
  assert.equal(C.resolveParam(s.layout, own.values, g1, 'zoom'), 1.5, 'the source is not touched');
  assert.deepEqual(C.couplingAdvisories(s.layout), [], 'no loop, nothing to advise');
  s = C.unbindParam(s, g2, 'zoom');
  v = C.setParam(s.layout, v, g1, 'zoom', 4).values;
  assert.equal(C.resolveParam(s.layout, v, g2, 'zoom'), 3, 'independent again: keeps what it showed');
  assert.equal(s.layout.windows.find(w => w.id === g2).params.bind, undefined, 'no empty bind object left');
});

test('bound back the other way: coupling_cycle (non-blocking), propagation stops at the preset instead of oscillating', () => {
  let { s, g1, g2 } = twoGraphs();
  s = C.bindParam(C.bindParam(s, g2, 'zoom', scale2(g1)), g1, 'zoom', scale2(g2));
  const adv = C.couplingAdvisories(s.layout, w => w.id);
  assert.equal(adv.length, 1); assert.equal(adv[0].code, 'coupling_cycle');
  assert.equal(adv[0].evidence.consistent, false);
  assert.equal(adv[0].fingerprint, 'zoom|scale|navigator');
  assert.match(adv[0].text.pl, /w kółko/);
  // the source of the new binding is pinned: values computed once, settled
  const r = C.afterBind(s.layout, C.setParam(s.layout, C.emptyValues(), g1, 'zoom', 1).values, g1, 'zoom');
  assert.ok(r.converged);
  // without a pin a ×2 loop does not settle: it stops after max_iterations rounds
  const free = C.propagate(s.layout, { windows: { [g1]: { zoom: 1 }, [g2]: { zoom: 1 } }, channels: {} }, []);
  assert.equal(free.converged, false); assert.equal(free.iterations, C.COUPLING.max_iterations);
  // a loop of equalities is consistent (R58: allowed) and says so
  let e = twoGraphs();
  const same = from => ({ binding: 'linked', from: { window: from, param: 'zoom' }, dependency: { kind: 'same' } });
  const t = C.bindParam(C.bindParam(e.s, e.g2, 'zoom', same(e.g1)), e.g1, 'zoom', same(e.g2));
  const a2 = C.couplingAdvisories(t.layout);
  assert.equal(a2[0].evidence.consistent, true);
  const r2 = C.setParam(t.layout, C.emptyValues(), e.g1, 'zoom', 3);
  assert.ok(r2.converged); assert.equal(C.resolveParam(t.layout, r2.values, e.g2, 'zoom'), 3);
});

test('shared channel: windows bound to one channel read and write the same value; leaving keeps what it shows', () => {
  let { s, g1, g2 } = twoGraphs();
  s = C.bindParam(C.bindParam(s, g1, 'zoom', { binding: 'shared', channel: 'zoom' }), g2, 'zoom', { binding: 'shared', channel: 'zoom' });
  let v = C.setParam(s.layout, C.emptyValues(), g2, 'zoom', 2.5).values;
  assert.equal(C.resolveParam(s.layout, v, g1, 'zoom'), 2.5);
  v = C.beforeUnbind(s.layout, v, g1, 'zoom'); s = C.unbindParam(s, g1, 'zoom');
  assert.equal(C.resolveParam(s.layout, v, g1, 'zoom'), 2.5);
  v = C.setParam(s.layout, v, g2, 'zoom', 1).values;
  assert.equal(C.resolveParam(s.layout, v, g1, 'zoom'), 2.5);
});

test('dependencies as data: offset on dates and ranges, scale element-wise on a pan, unknown kind said, expression whitelisted, direction from another variable', () => {
  assert.deepEqual(D.applyDependency({ from: '2026-10-01', to: '2026-10-23' }, { kind: 'offset', days: -1 }).value, { from: '2026-09-30', to: '2026-10-22' });
  assert.deepEqual(D.applyDependency({ x: 10, y: -4 }, { kind: 'scale', factor: 2 }).value, { x: 20, y: -8 });
  assert.equal(D.applyDependency(1, { kind: 'nope' }).code, 'unknown_dependency');
  assert.equal(D.applyDependency('n_1', { kind: 'parent_of' }).code, 'dependency_unavailable');
  assert.equal(D.applyDependency('n_1', { kind: 'parent_of' }, { parentOf: () => 'n_0' }).value, 'n_0');
  assert.equal(D.applyDependency('abc', { kind: 'scale', factor: 2 }).code, 'dependency_not_numeric');
  // the owner's example of a mapping whose direction follows another variable: older = further from 128, the side chosen by self.dir
  const ex = { kind: 'expression', expr: 'clamp(128 + (self.dir >= 0 ? 1 : -1) * value * 10, 0, 255)' };
  assert.equal(D.applyDependency(3, ex, { self: { dir: 1 } }).value, 158);
  assert.equal(D.applyDependency(3, ex, { self: { dir: -2 } }).value, 98);
  assert.equal(D.applyDependency(2, { kind: 'expression', expr: 'other.zoom * 2 + value' }, { other: { zoom: 3 } }).value, 8);
  for (const bad of ['constructor.constructor("x")()', 'value.__proto__', 'globalThis', 'x = 1', 'eval("1")', '1 +', '(((', '"a" + ', 'a[0]'])
    assert.ok(!D.applyDependency(1, { kind: 'expression', expr: bad }).ok || D.applyDependency(1, { kind: 'expression', expr: bad }).value === undefined, bad);
  assert.equal(D.applyDependency(1, { kind: 'expression', expr: '1/0' }).code, 'expression_failed');
  assert.deepEqual(D.expressionRefs('other.zoom * self.k + focus'), { other: ['zoom'], self: ['k'], channels: ['focus'] });
});

test('advisories of broken bindings: dangling source, unknown dependency, invalid expression, unknown mode; fix = unbind', () => {
  let { s, g1, g2 } = twoGraphs();
  s = C.bindParam(s, g2, 'zoom', { binding: 'linked', from: { window: 'w_gone', param: 'zoom' } });
  s = C.bindParam(s, g2, 'pan', { binding: 'linked', from: { window: g1, param: 'pan' }, dependency: { kind: 'telepathy' } });
  s = C.bindParam(s, g1, 'zoom', { binding: 'linked', from: { window: g2 }, dependency: { kind: 'expression', expr: 'value *' } });
  s = C.bindParam(s, g1, 'pan', { binding: 'braided' });
  const codes = C.couplingAdvisories(s.layout).map(a => a.code).sort();
  assert.deepEqual(codes, ['coupling_dangling', 'expression_invalid', 'unknown_binding', 'unknown_dependency']);
  const dangling = C.couplingAdvisories(s.layout).find(a => a.code === 'coupling_dangling');
  const fixed = C.applyCouplingFix(s, dangling);
  assert.equal(C.bindingsOf(fixed.layout.windows.find(w => w.id === g2)).zoom, undefined);
  // propagation over all of it does not throw and keeps values where nothing can be computed
  const r = C.propagate(s.layout, { windows: { [g2]: { zoom: 5 } }, channels: {} });
  assert.equal(C.resolveParam(s.layout, r.values, g2, 'zoom'), 5);
});

test('menu ⋯ and the chain: entries only for parameters the kind follows (data), words name the parameter, dependency and source', () => {
  let { s, g1, g2 } = twoGraphs();
  const title = w => (w.id === g1 ? 'Graf' : 'Graf 2');
  const menu = C.couplingMenu(s.layout, g2, 'pl', title);
  const zoom2 = menu.find(m => m.action.op === 'bind' && m.action.param === 'zoom' && m.action.binding.dependency?.factor === 2);
  assert.equal(zoom2.label, 'Powiąż przybliżenie: ×2 okna «Graf»');
  assert.ok(menu.every(m => ['zoom', 'pan'].includes(m.action.param)));
  const chat = s.layout.windows.find(w => w.kind === 'chat');
  assert.deepEqual(C.couplingMenu(s.layout, chat.id, 'pl', title), [], 'the chat follows no parameter yet: nothing offered (no fake control)');
  assert.equal(C.chainOf(s.layout, g2, 'pl', title), null);
  s = C.bindParam(s, g2, 'zoom', zoom2.action.binding);
  assert.deepEqual(C.chainOf(s.layout, g2, 'pl', title), { icon: '↔', lines: ['przybliżenie: ×2 okna «Graf»'] });
  assert.ok(C.couplingMenu(s.layout, g2, 'pl', title).some(m => m.id === 'unbind:zoom'));
});

test('view-state migration: coupled = shared/conversation, independent = own value; behaviour identical', () => {
  const v = V.newApplicationView('a', 'p');
  assert.deepEqual(V.conversationBinding(v), { binding: 'shared', channel: 'conversation' });
  assert.equal(V.viewConversationId(v, 'c_shared'), 'c_shared');
  const d = V.setConversationCoupling(v, 'independent', 'c_a');
  assert.deepEqual(V.conversationBinding(d), { binding: 'independent', value: 'c_a' });
  assert.equal(V.viewConversationId(d, 'c_other'), 'c_a');
  assert.equal(V.viewConversationId(V.newApplicationView('b', 'p'), null), null);
});

// ---------- generative ----------
function rng(seed) { let x = seed >>> 0 || 1; return () => { x ^= x << 13; x >>>= 0; x ^= x >> 17; x ^= x << 5; x >>>= 0; return x / 2 ** 32; }; }
const DEPS = [{ kind: 'same' }, { kind: 'scale', factor: 2 }, { kind: 'scale', factor: 0.5 }, { kind: 'scale', factor: -1 }, { kind: 'offset', by: 1 }, { kind: 'offset', by: 0 },
  { kind: 'expression', expr: 'value * 2 + other.b' }, { kind: 'expression', expr: 'self.a > 0 ? value : -value' }, { kind: 'expression', expr: 'min(value, 3)' },
  { kind: 'parent_of' }, { kind: 'unknown_x' }, { kind: 'expression', expr: '((' }, { kind: 'scale', factor: 1 }];
function randomLayout(r) {
  const n = 1 + Math.floor(r() * 5), params = ['a', 'b', 'c'].slice(0, 1 + Math.floor(r() * 3)), chans = ['k1', 'k2'];
  const windows = Array.from({ length: n }, (_, i) => ({ id: `w${i}`, kind: 'navigator', node: '', mode: 'docked', z: i, params: { panel: 'navigator' }, profile: null, title_override: null, collapsed: false, pinned: false }));
  for (const w of windows) for (const p of params) {
    const roll = r();
    if (roll < 0.35) continue;
    if (roll < 0.5) { (w.params.bind ??= {})[p] = { binding: 'shared', channel: chans[Math.floor(r() * 2)] }; continue; }
    if (roll < 0.55) { (w.params.bind ??= {})[p] = { binding: 'independent' }; continue; }
    const src = r() < 0.15 ? { channel: chans[Math.floor(r() * 2)] } : { window: r() < 0.05 ? 'w_gone' : `w${Math.floor(r() * n)}`, param: params[Math.floor(r() * params.length)] };
    (w.params.bind ??= {})[p] = { binding: 'linked', from: src, dependency: DEPS[Math.floor(r() * DEPS.length)] };
  }
  return { layout: { id: 'g', tree: { leaf: 'L0' }, leaves: {}, windows, focus: null, channels: { k1: 1 }, shelf: { handles: [] } }, params };
}
/** Independent brute-force loop check: a node reaches itself following source → target edges. */
function hasLoop(edges) {
  const next = new Map(); for (const e of edges) for (const s of e.sources) { if (!next.has(s)) next.set(s, []); next.get(s).push(e.target); }
  const reach = (from) => { const seen = new Set(), st = [...(next.get(from) ?? [])]; while (st.length) { const x = st.pop(); if (x === from) return true; if (seen.has(x)) continue; seen.add(x); st.push(...(next.get(x) ?? [])); } return false; };
  return [...next.keys()].filter(reach);
}

test(`generative: ${CONFIG.runs} random binding graphs — bounded, loops reported exactly, acyclic settles, unbinding all restores independence`, () => {
  const r = rng(CONFIG.seed);
  let loops = 0, settled = 0, unsettled = 0;
  for (let run = 0; run < CONFIG.runs; run++) {
    const { layout, params } = randomLayout(r);
    const edges = C.edgesOf(layout);
    const cyc = C.cyclesOf(layout, edges);
    const looping = new Set(hasLoop(edges));
    assert.deepEqual(new Set(cyc.flat()), looping, `run ${run}: Tarjan agrees with brute force`);
    const adv = C.couplingAdvisories(layout).filter(a => a.code === 'coupling_cycle');
    assert.equal(adv.length, cyc.length, `run ${run}: one coupling_cycle per loop`);
    loops += cyc.length;
    let v = C.emptyValues();
    for (let k = 0; k < 6; k++) {
      const w = layout.windows[Math.floor(r() * layout.windows.length)], p = params[Math.floor(r() * params.length)];
      const res = C.setParam(layout, v, w.id, p, Math.round(r() * 100) / 10);
      assert.ok(res.iterations <= C.COUPLING.max_iterations, 'bounded');
      if (!cyc.length) assert.ok(res.converged, `run ${run}: an acyclic graph settles`);
      res.converged ? settled++ : unsettled++;
      v = res.values;
      for (const x of Object.values(v.windows)) for (const y of Object.values(x)) assert.ok(typeof y !== 'number' || Number.isFinite(y), 'no NaN/Infinity leaks into a value');
    }
    // removing every binding restores independence: a write changes exactly the written parameter
    let s = { layout, past: [], future: [], limit: 1000 };
    for (const w of layout.windows) for (const p of Object.keys(C.bindingsOf(w))) s = C.unbindParam(s, w.id, p);
    assert.deepEqual(C.edgesOf(s.layout), []); assert.deepEqual(C.couplingAdvisories(s.layout), []);
    const w0 = s.layout.windows[0], before = v;   // setParam never mutates its input
    const after = C.setParam(s.layout, v, w0.id, params[0], 'X').values;
    for (const w of s.layout.windows) for (const p of params) if (!(w === w0 && p === params[0])) assert.deepEqual(C.resolveParam(s.layout, after, w.id, p), C.resolveParam(s.layout, before, w.id, p));
  }
  console.log(`[windows-coupling] runs=${CONFIG.runs} loops=${loops} settled=${settled} stopped_at_preset=${unsettled}`);
  assert.ok(loops > 0 && settled > 0, 'the generator reaches both loops and settling graphs');
});

test('default behaviour unchanged: the shipped layouts carry no bindings, nothing is advised, nothing propagates', () => {
  for (const l of Object.values(sv.LAYOUTS)) {
    assert.deepEqual(C.edgesOf(l), []); assert.deepEqual(C.couplingAdvisories(l), []);
    const w = l.windows[0]; if (!w) continue;
    const r = C.setParam(l, C.emptyValues(), w.id, 'zoom', 2);
    assert.deepEqual(r.changed, [`w:${w.id}:zoom`]);
  }
});
