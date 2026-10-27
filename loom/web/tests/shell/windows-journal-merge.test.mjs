// W5b: no journal is lost between tabs or devices. Before W5b each page wrote the WHOLE window_layout_journal 600 ms after a change
// from its own (stale) copy of the config, so the last writer won. Now each tab writes only its own record (writers.<client id>.<class>)
// as an RFC 7386 merge patch the host merges under its config lock (server PATCH /api/config/:key; JNI: read-merge-write in one
// process). The host here is that merge over one stored value; writers read it once (at their start) and then write from a stale copy,
// the worst case. Examples first, then a generative pass over random interleavings.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const S = await loader.load('src/shell/windows/session.ts');
const M = await loader.load('src/shell/windows/model.ts');
const N = await loader.load('src/shell/windows/named.ts');
const P = await loader.load('src/api/merge-patch.ts');
const sv = await loader.load('src/shell/start-view.ts');
const L = sv.LAYOUTS;
const czat = () => S.layoutState(L.czat, sv.JOURNAL_LIMIT);
const t = (i) => `2026-10-27T10:${String(Math.floor(i / 60)).padStart(2, '0')}:${String(i % 60).padStart(2, '0')}.000Z`;
const clone = (v) => JSON.parse(JSON.stringify(v));

/** A tab: its own state and a copy of the config value read at its start (never refreshed - the worst case). */
function tab(host, writer, installation, cls = 'phone') {
  const who = { writer, installation };
  const seen = clone(host.value ?? null);
  const j = N.readWriterJournal(seen, cls, who);
  return { who, cls, seen, s: { ...czat(), past: j.past, future: j.future, limit: j.limit }, source: j.source };
}
const step = (w, i) => { w.s = N.replaceLayout(w.s, w.s.layout.id === 'czat' ? L.siatka : L.czat, `op${i}`); };
const write = (host, w, i) => { host.value = P.mergePatch(host.value, N.journalPatch(w.seen, w.cls, w.s, w.who, t(i))); };

test('merge patch (RFC 7386): members merge, null removes, arrays and scalars replace, a JSON text of an object is merged as the object', () => {
  assert.deepEqual(P.mergePatch({ a: 1, b: { c: 2, d: 3 } }, { b: { c: null, e: [1] }, f: 'x' }), { a: 1, b: { d: 3, e: [1] }, f: 'x' });
  assert.deepEqual(P.mergePatch('{"a":1}', { b: 2 }), { a: 1, b: 2 });
  assert.deepEqual(P.mergePatch('praca', { b: 2 }), { b: 2 });
  assert.deepEqual(P.mergePatch({ a: [1, 2] }, { a: [3] }), { a: [3] });
  assert.equal(P.mergePatch({ a: 1 }, 5), 5);
});

test('two tabs, interleaved changes from stale copies: both journals survive; a reload of each gives back its own steps', () => {
  const host = { value: undefined };
  const a = tab(host, 'web-a', 'inst-1'), b = tab(host, 'web-b', 'inst-1');
  for (let i = 0; i < 6; i++) { const w = i % 2 ? b : a; step(w, i); write(host, w, i); }
  assert.equal(a.s.past.length, 3); assert.equal(b.s.past.length, 3);
  const ra = tab(host, 'web-a', 'inst-1'), rb = tab(host, 'web-b', 'inst-1');
  assert.equal(ra.source, 'own'); assert.equal(rb.source, 'own');
  assert.deepEqual(ra.s.past.map(e => e.op), a.s.past.map(e => e.op));
  assert.deepEqual(rb.s.past.map(e => e.op), b.s.past.map(e => e.op));
  assert.notDeepEqual(ra.s.past.map(e => e.op), rb.s.past.map(e => e.op));
  // the W5 whole-value write from the same stale copies loses the first tab's journal
  const old = { value: undefined };
  for (const w of [a, b]) old.value = N.writeJournal(w.seen, w.cls, w.s);
  assert.equal(Object.keys(old.value).length, 1);   // one class key: b's journal replaced a's
});

test('a new tab inherits a copy: installation first, then person, then the W5 per-class journal; preset [] = empty', () => {
  const host = { value: undefined };
  const a = tab(host, 'web-a', 'inst-1'); step(a, 0); write(host, a, 1);
  const b = tab(host, 'web-b', 'inst-2'); step(b, 2); step(b, 3); write(host, b, 5);
  const c = tab(host, 'web-c', 'inst-1');
  assert.equal(c.source, 'installation'); assert.equal(c.s.past.length, a.s.past.length);
  const d = tab(host, 'web-d', 'inst-9');
  assert.equal(d.source, 'person'); assert.equal(d.s.past.length, b.s.past.length);   // the newest writer of the class
  assert.equal(N.readWriterJournal(host.value, 'phone', { writer: 'x' }, []).source, 'none');
  const legacy = N.writeJournal(undefined, 'phone', a.s);
  const e = N.readWriterJournal(legacy, 'phone', { writer: 'web-e', installation: 'inst-1' });
  assert.equal(e.source, 'class'); assert.equal(e.past.length, a.s.past.length);
  assert.equal(N.readWriterJournal({ ...legacy, inherit: [] }, 'desktop', { writer: 'web-e' }).source, 'none');
});

test('writers are bounded (writers_limit, else preset journal_writers): the oldest other records go, never the writer itself', () => {
  let host = { value: { writers_limit: 3 } };
  const tabs = ['w1', 'w2', 'w3', 'w4', 'w5'].map((id, i) => { const w = tab(host, id, 'inst'); step(w, i); host.value = P.mergePatch(host.value, N.journalPatch(host.value, 'phone', w.s, w.who, t(i))); return w; });
  assert.deepEqual(Object.keys(host.value.writers).sort(), ['w3', 'w4', 'w5']);
  host = { value: { writers_limit: 0 } };
  for (const [i, w] of tabs.entries()) host.value = P.mergePatch(host.value, N.journalPatch(host.value, 'phone', w.s, w.who, t(i)));
  assert.equal(Object.keys(host.value.writers).length, 5);
  assert.equal(Object.keys(P.mergePatch({}, N.journalPatch({}, 'phone', tabs[0].s, tabs[0].who, t(0), 1)).writers).length, 1);
});

test('active layout at the installation level (D19): phone A applies „praca”, phone B keeps its own; a new phone falls back to the person value', () => {
  let v;
  v = P.mergePatch(v, N.activePatch(v, 'phone', 'u_dom', 'inst-B'));
  const seenA = clone(v);
  v = P.mergePatch(v, N.activePatch(seenA, 'phone', 'u_praca', 'inst-A'));
  assert.equal(N.readActive(v, 'phone', 'inst-A'), 'u_praca');
  assert.equal(N.readActive(v, 'phone', 'inst-B'), 'u_dom');
  assert.equal(N.readActive(v, 'phone', 'inst-C'), 'u_praca');   // person level = the last choice of any installation
  assert.equal(N.readActive(v, 'phone'), 'u_praca');
  v = P.mergePatch(v, N.activePatch(v, 'phone', null, 'inst-A'));   // A picks Czat from the menu
  assert.equal(N.readActive(v, 'phone', 'inst-A'), null);
  assert.equal(N.readActive(v, 'phone', 'inst-B'), 'u_dom');
  assert.equal(N.readActive(v, 'phone', 'inst-C'), null);
  // levels are data: person only = the W5 behaviour; installation only leaves the person value alone
  assert.deepEqual(N.activePatch({}, 'phone', 'x', 'inst-A', ['person']), { phone: 'x' });
  assert.deepEqual(N.activePatch({ levels: ['installation'] }, 'phone', 'x', 'inst-A'), { installations: { 'inst-A': { phone: 'x' } } });
  assert.deepEqual(P.mergePatch('u_old', N.activePatch('u_old', 'phone', 'x', 'inst-A')), { any: 'u_old', installations: { 'inst-A': { phone: 'x' } }, phone: 'x' });
});

test('generative: random interleavings of 2-5 tabs on 1-2 installations and 2 classes, stale copies - nothing is lost', () => {
  let seed = 5;
  const rnd = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
  for (let run = 0; run < 150; run++) {
    const host = { value: rnd(3) === 0 ? N.writeJournal(undefined, 'phone', czat()) : undefined };
    const n = 2 + rnd(4);
    const tabs = Array.from({ length: n }, (_, k) => tab(host, `web-${run}-${k}`, `inst-${rnd(2)}`, rnd(2) ? 'phone' : 'desktop'));
    const steps = 4 + rnd(12);
    for (let i = 0; i < steps; i++) {
      const w = tabs[rnd(n)];
      if (rnd(4) === 0 && w.s.past.length) w.s = M.undo(w.s).state; else step(w, i);
      if (rnd(3) === 0) w.seen = clone(host.value ?? null);   // sometimes a tab sees a fresher copy
      write(host, w, run * 100 + i);
    }
    for (const w of tabs) {
      if (!host.value?.writers?.[w.who.writer]) continue;   // a tab that never wrote has nothing to lose
      const r = N.readWriterJournal(host.value, w.cls, w.who);
      assert.equal(r.source, 'own', `run ${run} ${w.who.writer}`);
      assert.deepEqual(r.past.map(e => e.op), w.s.past.map(e => e.op), `run ${run} ${w.who.writer} past`);
      assert.deepEqual(r.future.map(e => e.op), w.s.future.map(e => e.op), `run ${run} ${w.who.writer} future`);
    }
  }
});
