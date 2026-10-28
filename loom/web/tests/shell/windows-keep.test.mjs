// W10 (OKNA §8.3, §4.4): which windows stay mounted (window_keep_mounted), the state they come back with (state_keys), many_windows.
// Examples first, then a generative pass (seeded; LOOM_KEEP_RUNS / LOOM_KEEP_OPS / LOOM_KEEP_SEED raise the presets): random sequences of
// window operations over the shipped layouts under random policies (also unknown values), with random state written at every unmount -
// the drawn windows are always mounted, `all` mounts every window, `recent:n` adds at most n, a window that comes back gets exactly the
// declared keys it had when it was unmounted, undeclared keys never come back, closed windows lose their state.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const K = await loader.load('src/shell/windows/keep.ts');
const S = await loader.load('src/shell/windows/session.ts');
const M = await loader.load('src/shell/windows/model.ts');
const R = await loader.load('src/shell/windows/registry.ts');
const sv = await loader.load('src/shell/start-view.ts');
const env = (k, d) => (Number.isFinite(Number(process.env[k])) && process.env[k] ? Number(process.env[k]) : d);
const CONFIG = { runs: env('LOOM_KEEP_RUNS', 300), ops: env('LOOM_KEEP_OPS', 30), seed: env('LOOM_KEEP_SEED', 20261028) };
const ctx = S.WINDOW_CTX;
const open = (s, kind, placement = 'float', params = {}) => M.apply(s, { op: 'open', kind, placement, params }, ctx).state;

test('registry: every kind declares state_keys and every key has a keeper', () => {
  for (const k of R.KINDS) {
    assert.ok(Array.isArray(k.state_keys), `${k.id}: state_keys missing`);
    for (const key of k.state_keys) assert.ok(key in K.STATE_KEEPERS, `${k.id}: key ${key} has no keeper in state_keepers`);
    if (k.status === 'present') assert.ok(k.state_keys.includes('scroll'), `${k.id}: a present kind keeps its scroll`);
  }
  assert.deepEqual(K.stateKeysOf('nobody-knows'), []);
});

test('policies read as open values; unknown = preset of the class + said', () => {
  assert.deepEqual(K.parsePolicy('visible'), { mode: 'visible' });
  assert.deepEqual(K.parsePolicy(' ALL '), { mode: 'all' });
  assert.deepEqual(K.parsePolicy('recent:3'), { mode: 'recent', n: 3 });
  assert.deepEqual(K.parsePolicy('recent: 0'), { mode: 'recent', n: 0 });
  for (const junk of [null, undefined, '', 'recent', 'recent:-1', 'recent:x', 7, {}, 'some']) assert.equal(K.parsePolicy(junk), null, String(junk));
  const data = { keep_mounted: { phone: 'visible', desktop: 'all' } };
  assert.deepEqual(K.keepPolicy(undefined, 'desktop', data), { policy: { mode: 'all' }, unknown: null });
  assert.deepEqual(K.keepPolicy('', 'phone', data), { policy: { mode: 'visible' }, unknown: null });
  assert.deepEqual(K.keepPolicy('lots', 'tv', data), { policy: { mode: 'visible' }, unknown: 'lots' });
  assert.deepEqual(K.keepPolicy('recent:2', 'phone', data).policy, { mode: 'recent', n: 2 });
  assert.equal(K.keepPolicy(undefined, 'phone').policy.mode, 'visible', 'shipped preset');
});

test('mounted sets: tabs, minimized and switcher windows under visible / all / recent', () => {
  let s = S.layoutState(sv.LAYOUTS.siatka);
  s = open(s, 'settings', 'tab', { panel: 'settings' });
  s = open(s, 'logs', 'float', { panel: 'logs' });
  const logs = s.layout.windows.find(w => w.kind === 'logs').id;
  s = M.apply(s, { op: 'minimize', id: logs }, ctx).state;
  const ids = s.layout.windows.map(w => w.id);
  const shown = K.shownIds(s.layout);
  assert.ok(!shown.has(logs), 'minimized is not drawn');
  assert.deepEqual([...K.mountedIds(s.layout, { mode: 'visible' })].sort(), [...shown].sort());
  assert.deepEqual([...K.mountedIds(s.layout, { mode: 'all' })].sort(), [...ids].sort());
  const hidden = ids.filter(id => !shown.has(id));
  assert.ok(hidden.length >= 2, 'a hidden tab and a minimized window');
  const recent = K.touchRecent([logs, ...hidden], shown, s.layout);
  const r1 = K.keptHidden(s.layout, { mode: 'recent', n: 1 }, recent);
  assert.deepEqual([...r1], [logs], 'the most recent hidden one');
  assert.equal(K.keptHidden(s.layout, { mode: 'recent', n: 0 }, recent).size, 0);
});

test('state_keys: unmount keeps only declared keys; closed windows lose their state', () => {
  let store = new Map();
  const art = { id: 'w1', kind: 'artifact' };
  store = K.keepState(store, art, { view_mode: true, scroll: [{ i: 2, top: 40, left: 0 }], secret: 'x' });
  assert.deepEqual(K.restoreState(store, 'w1'), { view_mode: true, scroll: [{ i: 2, top: 40, left: 0 }] });
  store = K.keepState(store, art, { view_mode: false });
  assert.equal(K.restoreState(store, 'w1').view_mode, false);
  assert.deepEqual(K.restoreState(K.keepState(new Map(), { id: 'u', kind: 'unknown-kind' }, { scroll: 1 }), 'u'), {}, 'an unknown kind keeps nothing');
  const l = { windows: [{ id: 'w2' }] };
  assert.deepEqual(K.restoreState(K.pruneState(store, l), 'w1'), {});
});

test('many_windows: above the preset of the class, evidence with the estimate; park minimizes the others in one entry', () => {
  let s = S.layoutState(sv.LAYOUTS.siatka);
  for (const p of ['settings', 'logs', 'memory', 'operations']) s = open(s, p, 'float', { panel: p });
  const data = { mounted_soft: { phone: 3 }, cost_estimate: { observed: '2026-10-28', machine: 'vm', per_window_bytes: { logs: 2097152 }, default_bytes: 1048576 },
    advisories: { many_windows: { text: { pl: '{count}/{soft} {mb} MB {observed}', en: '{count}' }, fix: { id: 'park', label: { pl: 'Odstaw', en: 'Park' } } } } };
  const mounted = K.mountedIds(s.layout, { mode: 'visible' });
  const a = K.mountAdvisories(s.layout, mounted, 'phone', { mode: 'visible' }, null, data);
  assert.equal(a.length, 1); assert.equal(a[0].code, 'many_windows');
  assert.equal(a[0].evidence.mounted, mounted.size); assert.equal(a[0].evidence.threshold, 3);
  assert.equal(a[0].evidence.estimated_bytes, K.estimatedBytes(s.layout, mounted, data));
  assert.match(a[0].text.pl, /^\d+\/3 [\d.]+ MB 2026-10-28$/);
  assert.equal(K.mountAdvisories(s.layout, mounted, 'desktop', { mode: 'visible' }, null, data).length, 0, 'a class without a preset is never asked');
  assert.equal(K.mountAdvisories(s.layout, mounted, 'phone', { mode: 'visible' }, 'lots', data).map(x => x.code).join(), 'many_windows,window_keep_mounted_unknown');
  const p = K.parkWindows(s);
  assert.equal(p.past.length, s.past.length + 1, 'one journal entry');
  const floats = p.layout.windows.filter(w => w.mode === 'floating');
  assert.ok(floats.length <= 1 && floats.every(w => w.id === p.layout.focus));
  assert.equal(K.parkWindows(p), p, 'nothing to park = same state');
});

function rng(seed) { let x = seed >>> 0 || 1; return () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; }; }
const pick = (r, a) => a[Math.floor(r() * a.length)];
const PANELS = ['settings', 'logs', 'memory', 'operations', 'methods', 'analysis', 'perspectives', 'navigator', 'import'];
const POLICIES = ['visible', 'all', 'recent:0', 'recent:1', 'recent:3', 'recent:99', 'nonsense', undefined, ''];
test(`generative: ${CONFIG.runs} random sequences × ${CONFIG.ops} ops (seed ${CONFIG.seed})`, () => {
  const r = rng(CONFIG.seed);
  const kinds = R.KINDS;
  let restored = 0, kept = 0;
  for (let run = 0; run < CONFIG.runs; run++) {
    const value = pick(r, POLICIES);
    const { policy } = K.keepPolicy(value, pick(r, ['phone', 'desktop', 'tv']));
    let s = S.layoutState(pick(r, [sv.LAYOUTS.siatka, sv.LAYOUTS.czat]));
    let store = new Map(), recent = [], mounted = new Set(), truth = new Map();
    const step = () => {
      recent = K.touchRecent(recent, K.shownIds(s.layout), s.layout);
      const next = K.mountedIds(s.layout, policy, recent);
      const shown = K.shownIds(s.layout);
      const msg = `run ${run} policy ${JSON.stringify(value)} seed ${CONFIG.seed}`;
      for (const id of shown) assert.ok(next.has(id), `drawn ${id} not mounted (${msg})`);
      if (policy.mode === 'all') assert.equal(next.size, s.layout.windows.length, msg);
      if (policy.mode === 'visible') assert.equal(next.size, shown.size, msg);
      if (policy.mode === 'recent') assert.ok(next.size <= shown.size + policy.n, msg);
      for (const id of next) assert.ok(s.layout.windows.some(w => w.id === id), `mounted ${id} not in the layout (${msg})`);
      // unmount: the window writes random state (declared and undeclared keys); mount: it reads back exactly the declared ones
      for (const id of mounted) if (!next.has(id)) {
        const w = s.layout.windows.find(x => x.id === id); if (!w) continue;
        const snap = { scroll: [{ i: Math.floor(r() * 9), top: Math.floor(r() * 900), left: 0 }], camera: { zoom: r() }, view_mode: r() < 0.5, junk: r() };
        store = K.keepState(store, w, snap, kinds);
        const keys = K.stateKeysOf(w.kind, kinds);
        truth.set(id, Object.fromEntries(Object.entries({ ...(truth.get(id) ?? {}), ...snap }).filter(([k]) => keys.includes(k))));
      }
      for (const id of next) if (!mounted.has(id) && truth.has(id)) { assert.deepEqual(K.restoreState(store, id), truth.get(id), `state of ${id} (${msg})`); restored++; }
      store = K.pruneState(store, s.layout);
      for (const id of [...truth.keys()]) if (!s.layout.windows.some(w => w.id === id)) { assert.deepEqual(K.restoreState(store, id), {}, msg); truth.delete(id); }
      kept += next.size - shown.size;
      mounted = next;
    };
    step();
    for (let i = 0; i < CONFIG.ops; i++) {
      const ws = s.layout.windows; const w = ws.length ? pick(r, ws) : null; const x = r();
      if (x < 0.3 || !w) { const p = pick(r, PANELS); s = open(s, S.panelKind(p, s.layout.id), pick(r, ['float', 'tab', 'maximize', 'beside', 'shelf']), { panel: p }); }
      else if (x < 0.45) s = M.apply(s, { op: 'minimize', id: w.id }, ctx).state;
      else if (x < 0.6) s = M.apply(s, { op: 'restore', id: w.id }, ctx).state;
      else if (x < 0.7) s = M.apply(s, { op: 'maximize', id: w.id }, ctx).state;
      else if (x < 0.8) s = M.apply(s, { op: 'focus', id: w.id }, ctx).state;
      else if (x < 0.9) s = M.apply(s, { op: 'close', id: w.id }, ctx).state;
      else s = K.parkWindows(s);
      step();
    }
  }
  assert.ok(restored > 100 && kept > 100, `the space was exercised: ${restored} remounts checked, ${kept} hidden windows kept`);
  console.log(`# keep generative: ${restored} remounts checked, ${kept} hidden-window mounts`);
});
