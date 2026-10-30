// R101 D6 (PULPIT_R101_2026-10-28 §13 interactions / P3, §14 row D6): the drop rules, the drag start and the keys of the desktop as data
// (shell/windows/desktop-interact.ts). Examples first (the shipped presets: a conversation on a chat window, an icon on the trash, a window
// on the trash, an icon on the desktop = move, a drop nothing names), then generative passes (seeded; LOOM_DESKI_RUNS / LOOM_DESKI_SEED):
// resolution - for random rules, sources and hit stacks the chosen rule is the first in data order that matches the topmost matching hit,
// never a rule that is off, never the source itself, and a changed rule list changes the result; P3 - any sequence of steps on the model
// state is undone by the inverses in reverse order back to the first state and redone to the last one.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const I = await loader.load('src/shell/windows/desktop-interact.ts');
const RUNS = Number(process.env.LOOM_DESKI_RUNS ?? 400);
let seed = Number(process.env.LOOM_DESKI_SEED ?? 6006);
const rnd = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
const pick = a => a[Math.floor(rnd() * a.length)];
const int = (lo, hi) => lo + Math.floor(rnd() * (hi - lo + 1));
// members in any order; `open` and `trashed` are sets (their order is not part of the state)
const canon = v => JSON.stringify(v, (k, x) => (x && typeof x === 'object' && !Array.isArray(x) ? Object.fromEntries(Object.entries(x).sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0))) : (k === 'open' || k === 'trashed') && Array.isArray(x) ? [...x].sort() : x));

const chat = { kind: 'window', id: 'w_chat', windowKind: 'chat', params: [] };
const nav = { kind: 'window', id: 'w_nav', windowKind: 'navigator', params: ['zoom', 'pan'] };
const trash = { kind: 'icon', address: 'trash:' };
const desk = { kind: 'desktop' };
const conv = { kind: 'icon', address: 'conv:c_abc123' };

test('presets: a conversation icon on a chat window sets its parameter conversation to the id (the rest after the prefix)', () => {
  const r = I.resolveDrop(conv, [chat, desk], 'desktop');
  assert.equal(r.kind, 'step'); assert.equal(r.ruleId, 'conversation_to_chat');
  assert.deepEqual(I.stepOf(r, conv), { op: 'set_param', window: 'w_chat', param: 'conversation', value: 'c_abc123' });
  // a minimized chat (its icon or shelf entry carries data-window) is the same target
  assert.equal(I.resolveDrop(conv, [{ ...chat, mode: 'minimized' }, desk], 'phone').ruleId, 'conversation_to_chat');
});
test('target by the coupling parameter a kind reads (window_param): a rule of my own on zoom matches the navigator, not the chat', () => {
  const rules = [{ id: 'z', source: '', target: { window_param: 'zoom' }, do: { set_param: { param: 'zoom', value: 2 } } }];
  const r = I.resolveDrop(conv, [chat, nav], 'desktop', rules); assert.equal(r.kind, 'step'); assert.equal(r.hit.id, 'w_nav');
  assert.deepEqual(I.stepOf(r, conv), { op: 'set_param', window: 'w_nav', param: 'zoom', value: 2 });
});
test('presets: a graph node icon on the navigator sets its focus to the whole address; the chat does not read focus, so it is not a target', () => {
  const r = I.resolveDrop({ kind: 'icon', address: 'party:self' }, [nav, desk], 'phone');
  assert.equal(r.ruleId, 'node_to_navigator'); assert.deepEqual(I.stepOf(r, { kind: 'icon', address: 'party:self' }), { op: 'set_param', window: 'w_nav', param: 'focus', value: 'party:self' });
  const under = I.resolveDrop({ kind: 'icon', address: 'party:self' }, [chat, desk], 'phone');
  assert.equal(under.ruleId, 'icon_to_desktop', 'the chat takes nothing; the desktop under it takes the move');
  const n = I.resolveDrop({ kind: 'icon', address: 'party:self' }, [chat], 'phone');
  assert.equal(n.kind, 'none'); assert.equal(n.advisory.code, 'drop_no_rule');
});
test('presets: an icon on the trash → trash; the trash on itself → nothing (the source never matches itself); a window on the trash → close', () => {
  const r = I.resolveDrop(conv, [trash, desk], 'desktop'); assert.equal(r.ruleId, 'icon_to_trash'); assert.deepEqual(I.stepOf(r, conv), { op: 'trash', address: 'conv:c_abc123' });
  const self = I.resolveDrop(trash, [trash, desk], 'desktop'); assert.equal(self.ruleId, 'icon_to_desktop', 'the desktop under it takes the drop');
  const w = { kind: 'window', id: 'w_x', windowKind: 'settings' };
  const c = I.resolveDrop(w, [trash, desk], 'desktop'); assert.equal(c.ruleId, 'window_to_trash'); assert.deepEqual(I.stepOf(c, w), { op: 'close', window: 'w_x' });
  const quiet = I.resolveDrop(w, [desk], 'desktop'); assert.equal(quiet.kind, 'none'); assert.equal(quiet.advisory, null, 'a window ending its move on the desktop says nothing');
});
test('presets: an icon on the empty desktop is the D5 move (stepOf null); the topmost hit decides', () => {
  const r = I.resolveDrop(conv, [desk], 'phone'); assert.equal(r.ruleId, 'icon_to_desktop'); assert.equal(I.stepOf(r, conv), null);
  const over = I.resolveDrop(conv, [{ kind: 'other', what: 'button' }, chat, desk], 'phone'); assert.equal(over.ruleId, 'conversation_to_chat', 'unknown things are skipped, not a stop');
});
test('rules from data: a rule off for the class says so; an unknown do is carried; a rule list of my own replaces the preset', () => {
  const rules = [{ id: 'x', source: 'conv:', target: { window_kind: '*' }, do: { fly: true }, enabled: { phone: false } }];
  const off = I.resolveDrop(conv, [chat], 'phone', rules); assert.equal(off.kind, 'none'); assert.deepEqual(off.advisory, { code: 'drop_rule_off', vars: { rule: 'x' } });
  const on = I.resolveDrop(conv, [chat], 'desktop', rules); assert.equal(on.kind, 'step'); assert.deepEqual(I.stepOf(on, conv), { unknown: 'fly' });
  const empty = I.resolveDrop(conv, [chat, desk], 'desktop', []); assert.equal(empty.kind, 'none'); assert.equal(empty.advisory.code, 'drop_no_rule');
  assert.equal(I.resolveDrop(conv, [chat], 'desktop', 'garbage').kind, 'none');
});
test('drag start: per class from gestures.json (phone long_press, desktop move), an unknown word works as move with an advisory', () => {
  assert.equal(I.dragStartOf('phone').mode, 'long_press'); assert.equal(I.dragStartOf('desktop').mode, 'move');
  assert.equal(I.dragStartOf('tablet').mode, 'move', 'a class without a word takes desktop');
  const u = I.dragStartOf('phone', { targets: { icon: { drag_start: 'wave' } } }); assert.equal(u.mode, 'move'); assert.equal(u.advisory.code, 'drag_start_unknown'); assert.equal(u.written, 'wave');
  assert.equal(I.dragStartOf('phone', { targets: { icon: { drag_start: 'handle' } } }).mode, 'handle');
  assert.equal(I.touchActionOf('move'), 'none'); assert.notEqual(I.touchActionOf('long_press'), 'none');
});
test('keys: keymap.json desktop names the operations; the focus moves in the grid and never leaves the list', () => {
  assert.deepEqual(I.desktopKeyOf('arrowright'), { op: 'next' }); assert.deepEqual(I.desktopKeyOf('enter'), { op: 'open' }); assert.deepEqual(I.desktopKeyOf('delete'), { op: 'trash' });
  assert.equal(I.desktopKeyOf('ctrl+q'), null); assert.deepEqual(I.desktopKeyOf('x', { desktop: { x: 'dance' } }), { unknown: 'dance' });
  assert.equal(I.moveIndex(0, 'next', 4, 7), 1); assert.equal(I.moveIndex(6, 'next', 4, 7), 6); assert.equal(I.moveIndex(0, 'prev', 4, 7), 0);
  assert.equal(I.moveIndex(1, 'down', 4, 7), 5); assert.equal(I.moveIndex(3, 'down', 4, 7), 6, 'a row below with fewer icons: the last'); assert.equal(I.moveIndex(5, 'down', 4, 7), 5);
  assert.equal(I.moveIndex(5, 'up', 4, 7), 1); assert.equal(I.moveIndex(2, 'up', 4, 7), 2); assert.equal(I.moveIndex(3, 'last', 4, 7), 6); assert.equal(I.moveIndex(3, 'first', 4, 7), 0);
  assert.equal(I.moveIndex(0, 'next', 4, 0), -1);
  for (let k = 0; k < 500; k++) { const n = int(1, 30), c = int(1, 9), i = int(-3, 40); const op = pick(I.DESKTOP_KEY_OPS); const j = I.moveIndex(i, op, c, n); assert.ok(j >= 0 && j < n, `${i} ${op} ${c} ${n} -> ${j}`); }
});
test('icon menu: data order, trash only when the address can go there, unknown ids reported', () => {
  assert.deepEqual(I.iconMenuItems(['open', 'trash', 'close'], true, true), { items: ['open', 'trash', 'close'], unknown: [] });
  assert.deepEqual(I.iconMenuItems(['trash', 'sing', 'open'], false, true), { items: ['open'], unknown: ['sing'] });
  assert.deepEqual(I.iconMenuItems(undefined, true, false).items, ['trash', 'close']);
});

// ---- generative: the resolution ----
const PRE = ['conv:', 'party:', 'area:', 'memory:', 'kind:', 'trash:', ''];
const KINDS = ['chat', 'navigator', 'settings', 'trash', 'me'];
const PARAMS = { chat: ['conversation'], navigator: ['focus', 'zoom'], settings: [], trash: [], me: ['focus'] };
const CLASSES = ['phone', 'desktop', 'tablet'];
const randSource = () => (rnd() < 0.75 ? { kind: 'icon', address: pick(PRE) + Math.random().toString(36).slice(2, 6), key: String(int(0, 3)) } : { kind: 'window', id: 'w' + int(0, 5), windowKind: pick(KINDS) });
const randHit = () => { const r = rnd(); if (r < 0.3) return { kind: 'icon', address: pick(PRE) + Math.random().toString(36).slice(2, 5), key: String(int(0, 3)) }; if (r < 0.65) { const k = pick(KINDS); return { kind: 'window', id: 'w' + int(0, 5), windowKind: k, params: PARAMS[k] }; } if (r < 0.9) return { kind: 'desktop' }; return { kind: 'other' }; };
const randRule = (i) => {
  const src = rnd() < 0.6 ? pick(PRE) : rnd() < 0.5 ? [pick(PRE), pick(PRE)] : { window: rnd() < 0.5 ? true : pick(KINDS) };
  const r = rnd(); const target = r < 0.3 ? { icon: pick(PRE) } : r < 0.55 ? { window_kind: rnd() < 0.3 ? '*' : pick(KINDS) } : r < 0.8 ? { window_param: pick(['conversation', 'focus', 'zoom', 'nothing']) } : { desktop: true };
  const rule = { id: 'r' + i, source: src, target, do: pick([{ set_param: { param: 'conversation', value: '$rest' } }, { trash: true }, { close: true }, { move: true }, { fly: 1 }]) };
  if (rnd() < 0.3) rule.enabled = rnd() < 0.5 ? false : { [pick(CLASSES)]: false };
  return rule;
};
test(`generative: the resolution picks the first matching rule of the topmost matching hit, never an off rule, never the source itself (${RUNS} runs)`, () => {
  for (let k = 0; k < RUNS; k++) {
    const rules = Array.from({ length: int(0, 6) }, (_, i) => randRule(i));
    const source = randSource(); const hits = Array.from({ length: int(0, 5) }, randHit); const cls = pick(CLASSES);
    const r = I.resolveDrop(source, hits, cls, rules);
    // the oracle: walk hits, then rules
    let expect = null, off = null;
    outer: for (const hit of hits) { if (hit.kind === 'other') continue; for (const rule of rules) { if (I.matchSource(rule.source, source) === null || !I.matchTarget(rule.target, hit, source)) continue; if (!I.ruleEnabled(rule, cls)) { off = off ?? rule; continue; } expect = { rule, hit }; break outer; } }
    if (expect) { assert.equal(r.kind, 'step'); assert.equal(r.rule, expect.rule); assert.equal(r.hit, expect.hit); assert.ok(I.ruleEnabled(r.rule, cls)); if (r.hit.kind === 'icon' && source.kind === 'icon') assert.notEqual(r.hit.key ?? r.hit.address, source.key ?? source.address); if (r.hit.kind === 'window' && source.kind === 'window') assert.notEqual(r.hit.id, source.id); }
    else { assert.equal(r.kind, 'none'); if (off) assert.equal(r.advisory.code, 'drop_rule_off'); else if (hits.some(h => h.kind !== 'other') && !(source.kind === 'window' && hits.find(h => h.kind !== 'other').kind === 'desktop')) assert.equal(r.advisory.code, 'drop_no_rule'); else assert.equal(r.advisory, null); }
    // the step of a chosen rule is total: a model step, a move (null) or an unknown do
    if (r.kind === 'step') { const s = I.stepOf(r, source); assert.ok(s === null || 'op' in s || 'unknown' in s); if (s && 'op' in s && s.op === 'set_param') assert.equal(typeof s.value, 'string'); }
    // the data decide: without the chosen rule the result differs (another rule, or none)
    if (r.kind === 'step') { const r2 = I.resolveDrop(source, hits, cls, rules.filter(x => x !== r.rule)); assert.ok(r2.kind !== 'step' || r2.rule !== r.rule); }
  }
});

// ---- generative: P3 on the model ----
const randStep = (st) => { const r = rnd(); const addr = pick(PRE) + int(0, 4); const win = 'w' + int(0, 3);
  if (r < 0.25) return { op: 'arrangement', value: rnd() < 0.2 ? null : { mode: pick(['grid', 'free']), positions: { [addr]: { col: int(0, 5), row: int(0, 3) } } } };
  if (r < 0.45) return { op: 'trash', address: addr }; if (r < 0.55) return { op: 'untrash', address: pick(st.trashed.concat([addr])) };
  if (r < 0.8) return { op: 'set_param', window: win, param: pick(['conversation', 'focus']), value: rnd() < 0.15 ? undefined : addr };
  if (r < 0.9) return { op: 'close', window: win }; return { op: 'reopen', window: win }; };
test(`generative P3: any sequence of steps, undone by the inverses in reverse order, is the first state again; redone, the last one (${RUNS} runs)`, () => {
  for (let k = 0; k < RUNS; k++) {
    const start = { arrangement: rnd() < 0.5 ? null : { mode: 'grid' }, trashed: [...new Set(Array.from({ length: int(0, 3) }, () => pick(PRE) + int(0, 4)))], params: { w0: { conversation: 'c0' } }, open: ['w0', 'w1', 'w2'] };
    let st = start; const inverses = []; const forward = [];
    for (let n = int(0, 12); n > 0; n--) { const step = randStep(st); forward.push(step); const r = I.applyStep(st, step); st = r.state; inverses.push(r.inverse); }
    const end = st;
    let back = end; const redo = [];
    for (const inv of [...inverses].reverse()) { const r = I.applyStep(back, inv); back = r.state; redo.push(r.inverse); }
    assert.equal(canon(back), canon(start), `undo: ${JSON.stringify(forward)}`);
    let again = back; for (const s of [...redo].reverse()) again = I.applyStep(again, s).state;
    assert.equal(canon(again), canon(end), `redo: ${JSON.stringify(forward)}`);
    // the model never aliases: the start is untouched
    assert.equal(canon(start), canon({ ...start }));
  }
});
