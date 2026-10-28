// R101 D3 (PULPIT_R101_2026-10-28 §3.1, §3.2, §8, §12, §14 row D3): the other desktop concepts as presets of ONE mechanism - every
// declaration of shell/data/desktop.json resolves cleanly on every device class; the icon sources (window_kinds, minimized_windows,
// layouts, recent_conversations), the live focus of pulpit_graf_zywy, desktop_minimized and the words of the sentence „pulpit jako …”.
// Examples first, then generative passes (seeded; LOOM_DESKP_RUNS / LOOM_DESKP_SEED raise the presets): any preset over any context gives
// well-formed icons (no duplicate, every greyed one with its reason, every available one with what a tap does) and keeps P1.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const I = await loader.load('src/shell/windows/desktop-icons.ts');
const D = await loader.load('src/shell/windows/desktop.ts');
const DATA = JSON.parse(readFileSync(new URL('../../src/shell/data/desktop.json', import.meta.url), 'utf8'));
const KINDS = JSON.parse(readFileSync(new URL('../../src/shell/data/window_kinds.json', import.meta.url), 'utf8')).kinds;
const RUNS = Number(process.env.LOOM_DESKP_RUNS ?? 300);
let seed = Number(process.env.LOOM_DESKP_SEED ?? 3003);
const rnd = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
const pick = a => a[Math.floor(rnd() * a.length)];
const PANELS = ['graph', 'navigator', 'settings', 'memory', 'import', 'logs', 'context', 'methods', 'analysis', 'operations', 'perspectives',
  'onboarding', 'user-knowledge', 'graph-entry', 'view-engine', 'request', 'conversations'];
const entries = D.kindEntries(id => PANELS.includes(id), 'czat', 'pl');
const CLASSES = ['phone', 'tablet', 'desktop'];
const IDS = Object.keys(DATA.declarations);
const ctx0 = { entries, minimized: [{ id: 'w_chat', title: 'Czat' }], layouts: { shipped: [{ id: 'czat', label: 'Czat' }], own: [{ id: 'l_1', label: 'Praca' }] }, conversations: [{ id: 'c_1', title: 'Pierwsza' }] };

test('every declaration of desktop.json resolves on every device class: no unsupported element, no advisory, its own shape', () => {
  assert.ok(IDS.length >= 7, `declarations: ${IDS}`);
  for (const id of IDS) for (const dc of CLASSES) {
    const r = I.resolveDeclaration(id, dc);
    assert.equal(r.id, id); assert.equal(r.origin, 'setting');
    assert.deepEqual(r.unsupported, [], `${id}/${dc}`); assert.deepEqual(r.advisories, [], `${id}/${dc}`);
  }
  const R = id => I.resolveDeclaration(id, 'desktop');
  assert.equal(R('pulpit_ikony').around, null); assert.ok(R('pulpit_ikony').pins);
  assert.deepEqual(R('pulpit_start').sources.map(s => s.perspective), ['window_kinds', 'recent_conversations', 'layouts']);
  assert.equal(R('pulpit_start').pins, null); assert.deepEqual(R('pulpit_start').sections, ['icons', 'sentence', 'restore']);
  assert.equal(I.resolveDeclaration('pulpit_start', 'phone').sources[1].limit, 4);
  assert.deepEqual(R('pulpit_polka').sources.map(s => s.perspective), ['minimized_windows']); assert.equal(R('pulpit_polka').pins, null);
  const empty = R('pulpit_pusty');
  assert.equal(empty.pins, null); assert.deepEqual(empty.sources, []); assert.equal(empty.around, null); assert.equal(empty.icon, null); assert.deepEqual(empty.sections, ['sentence', 'open']);
  assert.deepEqual(R('pulpit_karta').card, { focus: 'party:self', limit: 10 }); assert.ok(R('pulpit_karta').pins);
  const live = R('pulpit_graf_zywy');
  assert.equal(live.around.channel, 'focus'); assert.equal(live.around.focus, 'party:self'); assert.ok(live.pins);
  assert.equal(R('pulpit_graf').around.channel, undefined, 'the default graph is not live');
});

test('pulpit_start: a tile for every kind of window_kinds.json, the planned ones greyed with their reason, never hidden', () => {
  const src = I.resolveDeclaration('pulpit_start', 'desktop').sources.find(s => s.perspective === 'window_kinds');
  const tiles = I.sourceIcons(src, ctx0, 'pl');
  assert.equal(tiles.length, KINDS.length);
  const planned = tiles.filter(t => t.status === 'planned');
  assert.equal(planned.length, KINDS.filter(k => k.status !== 'present').length);
  for (const t of tiles) {
    assert.equal(t.address, `kind:${t.kind}`); assert.equal(t.source, 'kinds');
    if (t.status === 'available') assert.ok(t.open?.target, t.kind); else assert.ok((t.reason ?? '').length > 3, `${t.kind} greyed with a reason`);
  }
  const lim = I.sourceIcons({ ...src, limit: 3 }, ctx0, 'pl'); assert.equal(lim.length, 3);
  const layouts = I.sourceIcons({ id: 'l', perspective: 'layouts', limit: null }, ctx0, 'pl');
  assert.deepEqual(layouts.map(l => [l.address, l.act]), [['layout:czat', { layout: 'czat', own: false }], ['layout:l_1', { layout: 'l_1', own: true }]]);
  const conv = I.sourceIcons({ id: 'r', perspective: 'recent_conversations', limit: null }, ctx0, 'pl');
  assert.deepEqual([conv[0].address, conv[0].status, conv[0].open.conversation, conv[0].kind], ['conv:c_1', 'available', 'c_1', 'chat']);
  assert.deepEqual(I.sourceIcons({ id: 'z', perspective: 'nope', limit: null }, ctx0, 'pl'), []);
});

test('pulpit_polka and desktop_minimized: minimized windows as icons that restore; shelf/icons per device class; unknown = preset + advisory', () => {
  const r = I.resolveDeclaration('pulpit_polka', 'phone');
  const icons = I.allIcons(r, [], ctx0, 'pl');
  assert.deepEqual(icons.map(i => [i.address, i.act, i.status]), [['window:w_chat', { restore: 'w_chat' }, 'available']]);
  assert.deepEqual(CLASSES.map(c => I.minimizedMode(undefined, c).mode), ['shelf', 'icons', 'icons']);
  assert.equal(I.minimizedMode('both', 'phone').mode, 'both');
  const u = I.minimizedMode('taca', 'desktop'); assert.equal(u.mode, 'icons'); assert.equal(u.advisory.code, 'desktop_minimized_unknown');
  const none = D.desktopSettings({}, D.DESKTOP, null, 'none'); assert.ok(!none.sections.includes('restore'), 'none: no shelf, no path from it');
  const pusty = D.desktopSettings({}, D.DESKTOP, ['sentence', 'open']); assert.deepEqual(pusty.sections, ['sentence', 'open']);
  const wins = D.desktopSettings({ desktop_sections: ['icons'] }, D.DESKTOP, ['sentence']); assert.deepEqual(wins.sections, ['icons'], 'the setting wins over the declaration');
});

test('pulpit_graf_zywy: the graph behind follows the coupling channel, else the focused window, else the last window, else the fallback', () => {
  const a = I.resolveDeclaration('pulpit_graf_zywy', 'desktop').around;
  const wins = [{ id: 'w_a' }, { id: 'w_b' }];
  assert.deepEqual(I.liveFocus(a, null), { focus: 'party:self', from: 'fallback' });
  assert.equal(I.liveFocus(a, { windows: {}, channels: {} }, wins).from, 'fallback');
  assert.deepEqual(I.liveFocus(a, { windows: { w_a: { focus: 'area:x' }, w_b: { focus: 'area:y' } }, channels: {} }, wins), { focus: 'area:y', from: 'window', window: 'w_b' });
  assert.equal(I.liveFocus(a, { windows: { w_a: { focus: 'area:x' }, w_b: { focus: 'area:y' } }, channels: {} }, wins, 'w_a').focus, 'area:x');
  assert.deepEqual(I.liveFocus(a, { windows: { w_a: { focus: 'area:x' } }, channels: { focus: 'conv:c_1' } }, wins), { focus: 'conv:c_1', from: 'channel' });
  const still = I.resolveDeclaration('pulpit_graf', 'desktop').around;
  assert.equal(I.liveFocus(still, { windows: { w_a: { focus: 'area:x' } }, channels: { focus: 'q' } }, wins).focus, 'party:self', 'a declaration without channel ignores the coupling');
});

test('the words of „pulpit jako …”: every word maps back to its own declaration, ids with and without pulpit_, a clear word = the preset', () => {
  const seen = new Map();
  for (const [id, w] of Object.entries(DATA.declaration_words)) {
    if (id === 'note') continue;
    assert.ok(IDS.includes(id), `words for an unknown declaration ${id}`);
    for (const word of [...(w.pl ?? []), ...(w.en ?? [])]) {
      assert.equal(I.declarationOfWord(word), id, word);
      assert.ok(!seen.has(word) || seen.get(word) === id, `${word} names two declarations`); seen.set(word, id);
    }
  }
  for (const id of IDS) { assert.equal(I.declarationOfWord(id), id); assert.equal(I.declarationOfWord(id.replace(/^pulpit_/, '').replace(/_/g, ' ')), id); }
  for (const [word, id] of [['ikony', 'pulpit_ikony'], ['start', 'pulpit_start'], ['półkę', 'pulpit_polka'], ['pusty', 'pulpit_pusty'], ['graf żywy', 'pulpit_graf_zywy'],
    ['live graph', 'pulpit_graf_zywy'], ['żywy', 'pulpit_graf_zywy'], ['karta', 'pulpit_karta'], ['empty', 'pulpit_pusty'], ['icons', 'pulpit_ikony'], ['GRAF', 'pulpit_graf']])
    assert.equal(I.declarationOfWord(word), id, word);
  assert.equal(I.declarationOfWord('domyślny'), null); assert.equal(I.declarationOfWord('default'), null);
  assert.equal(I.declarationOfWord('pulpit z kotami'), undefined); assert.equal(I.declarationOfWord(''), undefined);
});

test('generative: every preset over any context gives well-formed icons and keeps P1 (a way to a window without the emergency path)', () => {
  let icons = 0, greyed = 0, acts = 0, states = 0;
  for (let run = 0; run < RUNS; run++) {
    const id = pick(IDS), dc = pick(CLASSES);
    const r = I.resolveDeclaration(id, dc);
    const ents = entries.filter(() => rnd() < 0.8);
    if (!ents.some(e => e.kind === 'chat')) ents.push(entries.find(e => e.kind === 'chat'));
    const nMin = Math.floor(rnd() * 5), nConv = Math.floor(rnd() * 12);
    const ctx = { entries: ents, minimized: Array.from({ length: nMin }, (_, i) => ({ id: `w_${i}`, title: `W${i}` })),
      layouts: { shipped: [{ id: 'czat', label: 'Czat' }, { id: 'siatka', label: 'Siatka' }].filter(() => rnd() < 0.7), own: rnd() < 0.5 ? [{ id: 'l_x', label: 'X' }] : [] },
      conversations: Array.from({ length: nConv }, (_, i) => ({ id: `c_${i}`, title: rnd() < 0.2 ? '' : `T${i}` })) };
    const pins = r.pins ? Array.from({ length: Math.floor(rnd() * 8) }, (_, i) => I.iconOf({ node: `n_${i}`, address: pick(['party:self', 'conversations:', 'kind:chat', 'trash:', 'x:y', `conv:c_${i}`]), nodeLabel: `N${i}`, nodeKind: '', order: i }, 'pl', ents)) : [];
    const all = I.allIcons(r, pins, ctx, 'pl');
    const keys = all.map(i => i.node);
    assert.equal(new Set(keys).size, keys.length, `${id}: duplicate icon`);
    for (const i of all) {
      assert.ok(['available', 'planned', 'cannot_open'].includes(i.status));
      if (i.status === 'available') { assert.ok(i.open || i.act, `${id}: ${i.address} does nothing`); acts++; }
      if (i.status === 'planned') { assert.ok(i.reason, `${i.address} planned without a reason`); greyed++; }
    }
    icons += all.length;
    const mm = I.minimizedMode(pick([undefined, 'shelf', 'icons', 'both', 'none', 'zzz']), dc).mode;
    const s = D.desktopSettings({ desktop_emergency_path: false }, D.DESKTOP, r.sections, mm);
    const layoutStub = { id: 'czat', windows: ctx.minimized.map(m => ({ id: m.id, kind: 'chat', mode: 'minimized' })) };
    const paths = D.desktopPaths(layoutStub, s, ents, [...ctx.layouts.shipped, ...ctx.layouts.own].map(l => l.id), D.DESKTOP, all);
    assert.ok(paths.length > 0, `P1: ${id} on ${dc} with ${mm} leaves no way to a window`);
    states++;
  }
  console.log(`[desktop-presets] ${states} states, ${icons} icons (${acts} with a tap, ${greyed} planned greyed), seed ${process.env.LOOM_DESKP_SEED ?? 3003}`);
});
