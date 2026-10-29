// PH29 P1: the web store plugin interface - selection by traits (generative), the tab leader (simulated locks and
// channel, generative), and the kernel-canonical JSON the browser rebuilds bundles with (D4), checked against what
// nlohmann::json itself prints (fixture made by gen_nlohmann_doubles.cpp from the vendored library).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const sp = await loader.load('src/context/store-plugin.ts');
const cj = await loader.load('src/context/canonical-json.ts');
const tl = await loader.load('src/context/tab-leader.ts');
test.after(() => loader.close());

// deterministic PRNG so a failure is reproducible
function rng(seed) { let s = seed >>> 0; return () => { s = (s + 0x6D2B79F5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
const pick = (r, xs) => xs[Math.floor(r() * xs.length)];

test('D4: formatDouble prints every double exactly as nlohmann::json does', async () => {
  const cases = JSON.parse(await readFile(new URL('./nlohmann-doubles.json', import.meta.url), 'utf8'));
  assert.ok(cases.length > 5000);
  const buf = new DataView(new ArrayBuffer(8));
  let bad = [];
  for (const [hex, want] of cases) {
    buf.setBigUint64(0, BigInt('0x' + hex));
    const got = cj.formatDouble(buf.getFloat64(0));
    if (got !== want) bad.push([hex, want, got]);
  }
  // Observation 2026-10-29: nlohmann's Grisu2 is not always the shortest form; the differences are rare and learned.
  assert.ok(bad.length / cases.length < 0.01, `${bad.length} of ${cases.length} differ`);
  for (const [, want, got] of bad) assert.equal(Number(want), Number(got), 'same double, other digits');
  // learned from the server's text (as a save does), every one of them is printed exactly
  const known = new Map();
  cj.parseExact('[' + cases.map(([, t]) => t).filter((t) => t !== 'null').join(',') + ']', known);
  assert.equal(known.size, bad.length);
  for (const [hex, want] of cases) { buf.setBigUint64(0, BigInt('0x' + hex)); assert.equal(cj.formatDouble(buf.getFloat64(0), known), want); }
  console.log(`# nlohmann vs JS shortest: ${bad.length}/${cases.length} doubles differ (learned exceptions)`);
});

test('D4: canonical dump keeps reals, key order and kernel escaping', () => {
  const text = '{"b":1,"a":[1.0,2.50,-0.0,1e5,12345678901234567890,"x\\u0001\\n\\"/é"],"c":{"z":null,"y":true}}';
  const { value, exact } = cj.parseExact(text);
  assert.equal(exact, true, 'node exposes the JSON.parse source text (V8)');
  assert.equal(cj.canonicalDump(value), '{"b":1,"a":[1.0,2.5,-0.0,100000.0,12345678901234567890,"x\\u0001\\n\\"/é"],"c":{"z":null,"y":true}}');
  assert.equal(cj.keyOrderSafe(value), true);
  assert.equal(cj.keyOrderSafe(cj.parseExact('{"b":1,"10":2}').value), false, 'integer-like keys are reordered by JS: fallback');
  // generative: a canonical text re-parses to itself
  const r = rng(7);
  const gen = (d) => {
    const k = Math.floor(r() * (d > 2 ? 4 : 7));
    if (k === 0) return null; if (k === 1) return r() < 0.5; if (k === 2) return Math.floor((r() - 0.5) * 1e9);
    if (k === 3) return new cj.F64((r() - 0.5) * 10 ** Math.floor((r() - 0.5) * 40));
    if (k === 4) return pick(r, ['', 'ą', 'a"b', 'tab\t', '\u0007', '😀', 'x/y']);
    if (k === 5) return Array.from({ length: Math.floor(r() * 4) }, () => gen(d + 1));
    return Object.fromEntries(Array.from({ length: Math.floor(r() * 4) }, (_, i) => [pick(r, ['k', 'id', 'ą', 'x_']) + i, gen(d + 1)]));
  };
  for (let i = 0; i < 500; i++) { const t = cj.canonicalDump(gen(0)); assert.equal(cj.canonicalDump(cj.parseExact(t).value), t); }
  assert.notEqual(cj.rowHash('a'), cj.rowHash('b'));
});

test('selection by traits: explicit targets win, auto honours requires/prefers, unmet requirements never block (generative)', () => {
  const T = (o) => Object.fromEntries(Object.entries(o).map(([k, v]) => [k, { value: v }]));
  const opfs = { id: 'opfs', status: 'implemented', available: true, traits: T({ durable: true, ops: ['incremental_write', 'sql'], latency_ms: 3 }) };
  const idb = { id: 'idb', status: 'implemented', available: true, traits: T({ durable: true, ops: ['whole_bundle_write'], latency_ms: 9 }) };
  assert.equal(sp.choose([idb, opfs], { targets: 'auto', requires: { durable: true }, prefers: ['ops'] }).chosen, 'opfs');
  assert.equal(sp.choose([opfs, idb], { targets: 'auto', prefers: ['-latency_ms'] }).chosen, 'opfs');
  assert.equal(sp.choose([opfs, idb], { targets: ['idb'] }).chosen, 'idb');
  const off = { ...opfs, available: false, reason: 'missing: opfs' };
  const c = sp.choose([off, idb], { targets: ['opfs', 'idb'] });
  assert.equal(c.chosen, 'idb');
  assert.ok(c.advisories.some((a) => a.kind === 'store_unavailable' && a.plugin === 'opfs' && a.reason === 'missing: opfs'));
  assert.ok(c.advisories.some((a) => a.kind === 'store_fallback' && a.plugin === 'idb'));
  assert.equal(sp.choose([opfs], { requires: { ops: 'sql' } }).chosen, 'opfs', 'a list trait satisfies a member');
  const unmet = sp.choose([idb], { requires: { survives_site_data_clear: true } });
  assert.equal(unmet.chosen, 'idb');
  assert.deepEqual(unmet.advisories[0].missing, ['survives_site_data_clear'], 'missing trait is unknown -> named, not blocked');

  const r = rng(29);
  const names = ['durable', 'offline', 'persisted', 'free_bytes', 'latency_ms', 'ops', 'brand_new_trait'];
  const val = () => pick(r, [true, false, 'unknown', 1, 5, 1e9, ['a'], ['a', 'b'], 'text']);
  for (let i = 0; i < 2000; i++) {
    const cands = Array.from({ length: 1 + Math.floor(r() * 5) }, (_, j) => ({
      id: `s${j}`, status: r() < 0.85 ? 'implemented' : 'planned', available: r() < 0.8,
      traits: Object.fromEntries(names.filter(() => r() < 0.6).map((n) => [n, { value: val() }])) }));
    const requires = Object.fromEntries(names.filter(() => r() < 0.3).map((n) => [n, val()]));
    const prefers = names.filter(() => r() < 0.4).map((n) => (r() < 0.5 ? '-' : '') + n);
    const pol = r() < 0.2 ? { targets: cands.filter(() => r() < 0.5).map((c) => c.id).concat(r() < 0.2 ? ['ghost'] : []) } : { requires, prefers };
    const out = sp.choose(cands, pol);
    const usable = cands.filter((x) => x.available && x.status === 'implemented');
    if (out.chosen === null) {
      // nothing chosen only when nothing usable is allowed
      if (!Array.isArray(pol.targets)) assert.equal(usable.length, 0);
      else assert.ok(!pol.targets.some((id) => usable.some((u) => u.id === id)));
      continue;
    }
    const ch = cands.find((x) => x.id === out.chosen);
    assert.ok(ch && ch.available && ch.status === 'implemented', 'the chosen store is usable');
    if (Array.isArray(pol.targets)) { assert.equal(out.chosen, pol.targets.find((id) => usable.some((u) => u.id === id))); continue; }
    const miss = sp.missingTraits(ch, requires);
    if (miss.length) {
      assert.ok(!usable.some((u) => sp.missingTraits(u, requires).length === 0), 'a fitting store is never skipped');
      const adv = out.advisories.find((a) => a.kind === 'store_requirements_unmet');
      assert.deepEqual(adv.missing, miss, 'the advisory names exactly the missing traits');
    }
  }
});

// A simulated browser: Web Locks with FIFO waiters, a BroadcastChannel bus with asynchronous delivery.
function world() {
  const holders = new Map(); const queues = new Map();
  const locks = { request(name, _o, cb) {
    return new Promise((resolve, reject) => {
      const q = queues.get(name) ?? []; queues.set(name, q);
      const entry = { cb, resolve, reject, signal: _o.signal };
      _o.signal?.addEventListener('abort', () => { const i = q.indexOf(entry); if (i >= 0) { q.splice(i, 1); reject(new Error('AbortError')); } });
      q.push(entry); grant(name);
    });
  } };
  function grant(name) {
    if (holders.get(name)) return;
    const q = queues.get(name); const e = q?.shift(); if (!e) return;
    holders.set(name, e);
    Promise.resolve().then(() => e.cb({ name })).then((v) => { holders.delete(name); e.resolve(v); grant(name); }, (err) => { holders.delete(name); e.reject(err); grant(name); });
  }
  const members = new Set();
  const channel = () => { const ch = { fns: [], closed: false,
    postMessage(m) { for (const o of members) if (o !== ch && !o.closed) setTimeout(() => { if (!o.closed) for (const f of o.fns) f({ data: structuredClone(m) }); }, 0); },
    addEventListener(_t, f) { this.fns.push(f); }, close() { this.closed = true; members.delete(ch); } }; members.add(ch); return ch; };
  return { locks, channel };
}

test('tab leader: one leader, other tabs forward, a new leader takes over when the old one closes (generative)', async () => {
  for (let seed = 1; seed <= 25; seed++) {
    const r = rng(seed); const w = world();
    let opens = 0, live = 0, maxLive = 0;
    const log = [];
    const tabs = Array.from({ length: 2 + Math.floor(r() * 4) }, (_, i) => tl.createTabLeader({
      name: 'db', tabId: `tab${i}`, locks: w.locks, channel: w.channel(),
      open: async () => { opens++; live++; maxLive = Math.max(maxLive, live); const me = i; return async (op, args) => { log.push([me, op, args]); return `${op}@${me}`; }; },
    }));
    const alive = new Set(tabs.map((_, i) => i));
    const calls = [];
    for (let step = 0; step < 12; step++) {
      const i = pick(r, [...alive]);
      calls.push(tabs[i].call('save', { from: i, step }).then((v) => ({ ok: true, v, i }), (e) => ({ ok: false, e, i })));
      if (r() < 0.25 && alive.size > 1) {
        // the leader (if known) goes away: its lock is released
        const lead = [...alive].find((j) => tabs[j].isLeader());
        if (lead !== undefined) { alive.delete(lead); live--; await tabs[lead].close(); }
      }
      await new Promise((res) => setTimeout(res, Math.floor(r() * 3)));
    }
    const results = await Promise.all(calls);
    await new Promise((res) => setTimeout(res, 20));  // the last leader announcement is delivered asynchronously
    for (const x of results) if (!x.ok) assert.ok(!alive.has(x.i), `a call from a live tab failed: ${x.e}`);
    assert.ok(maxLive <= 1, 'never two leaders at once');
    assert.equal([...alive].filter((j) => tabs[j].isLeader()).length, 1, 'exactly one live leader');
    const lead = [...alive].find((j) => tabs[j].isLeader());
    for (const j of alive) assert.equal(tabs[j].leader(), `tab${lead}`, 'every live tab knows the leader');
    for (const x of results) if (x.ok && alive.has(x.i)) assert.match(x.v, /^save@\d+$/);
    for (const j of alive) await tabs[j].close();
  }
});

test('tab leader: exclusive=false opens a handler in every tab (the single-connection preset is a setting)', async () => {
  const w = world(); let opens = 0;
  const mk = (i) => tl.createTabLeader({ name: 'db', tabId: `t${i}`, locks: w.locks, channel: w.channel(), exclusive: false, open: async () => { opens++; return async () => i; } });
  const a = mk(1), b = mk(2);
  assert.equal(await a.call('x'), 1); assert.equal(await b.call('x'), 2); assert.equal(opens, 2);
  await a.close(); await b.close();
});
