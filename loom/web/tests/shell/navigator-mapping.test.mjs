// G3d: the channel mapping of the navigator map (src/components/navigator-mapping.ts + navigator-map.ts viewModelFor) without a browser:
// the shipped default draws exactly the M1-W0 picture, a changed mapping changes the picture, any mapping (unknown measures and channels,
// degenerate ranges, several channels on one measure) is drawn and reported, never refused, and the setting round-trips.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const map = await loader.load('src/components/navigator-map.ts');
const mp = await loader.load('src/components/navigator-mapping.ts');
const kc = await loader.load('src/components/kind-colors.ts');

const KINDS = ['topic', 'person', 'graph_area', 'rom_item', 'setting', 'something_new', ''];
let seed = 7; const rnd = () => (seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648;
const pick = a => a[Math.floor(rnd() * a.length)];
const row = (i, extra = {}) => ({ address: `n:${i}`, label: `L${i}`, kind: pick(KINDS), provenance: { layer: 'rom' }, evidence: { class: 'declared', confidence: null }, has_children: false, ...extra });
const level = children => ({ focus: { address: 'area:x', label: 'X', kind: 'graph_area' }, children, edges: [], counts: { total: children.length, shown: children.length, collapsed: 0, by_source: {} } });
// the M1-W0 formula, written out again here on purpose: the default must keep drawing it
const oldSize = r => 14 + (typeof r.count === 'number' && r.count > 0 ? 6 * Math.log10(1 + r.count) : 0);
const oldColour = k => mp.hslOfHex(kc.colorFor(k));

test('the shipped default is the M1-W0 picture exactly (generative: 200 levels, any kinds, counts present or absent), also as {} and as unreadable text', () => {
  for (let n = 0; n < 200; n++) {
    const rows = Array.from({ length: Math.floor(rnd() * 30) }, (_, i) => row(i, rnd() < 0.6 ? { count: Math.floor(rnd() ** 3 * 1e6) } : {}));
    for (const setting of [undefined, {}, '', 'not json', '{}', { size: 'count', hue: 'kind' }]) {
      const m = map.viewModelFor(level(rows), 'Graf', setting);
      const [focus, ...marks] = m.elements;
      assert.deepEqual(focus.channels, { ...oldColour('graph_area'), size: 18 });
      marks.forEach((e, i) => assert.deepEqual(e.channels, { ...oldColour(rows[i].kind), size: oldSize(rows[i]) }, `row ${i}`));
      assert.deepEqual(Object.keys(focus.channels), ['hue', 'saturation', 'lightness', 'size'], 'the same channels in the same order');
    }
  }
});

test('a changed mapping changes the picture: size by order differs from size by count; several channels on one measure; the focus keeps its anchor size', () => {
  const rows = [row(1, { count: 5, order: 0 }), row(2, { count: 5, order: 1 }), row(3, { count: 5, order: 2 })];
  const sizes = s => map.viewModelFor(level(rows), 'Graf', s).elements.slice(1).map(e => e.channels.size);
  assert.equal(new Set(sizes(undefined)).size, 1, 'by count: one size');
  assert.deepEqual(sizes({ size: 'order' }), [14, 14 + 6 * Math.log10(2), 14 + 6 * Math.log10(3)], 'by order: log1p of the order');
  const m = map.viewModelFor(level(rows), 'Graf', { hue: 'order', saturation: 'order', alpha: 'order', size: 'order' });
  assert.deepEqual(m.elements.slice(1).map(e => e.channels.hue), [240, 120, 0]);
  assert.deepEqual(m.elements.slice(1).map(e => e.channels.alpha), [0.25, 0.625, 1]);
  assert.equal(m.elements[0].channels.size, 18);
  const outlier = [row(1, { order: 0 }), row(2, { order: 1 }), row(3, { order: 1000 })];
  assert.deepEqual(map.viewModelFor(level(outlier), 'Graf', { hue: 'order' }).elements.slice(1).map(e => e.channels.hue), [240, 239.76, 0], 'linear: an outlier squashes the rest');
  assert.deepEqual(map.viewModelFor(level(outlier), 'Graf', { hue: { measure: 'order', curve: 'rank' } }).elements.slice(1).map(e => e.channels.hue), [240, 120, 0], 'rank: evenly spread');
  // a category on a non-colour channel: its rank among the level's distinct values; a constant: the middle of the range
  const cats = map.viewModelFor(level([row(1, { kind: 'b' }), row(2, { kind: 'a' }), row(3, { kind: 'b' })]), 'Graf', { size: { measure: 'kind', range: [10, 30] }, alpha: 'none' });
  assert.deepEqual(cats.elements.slice(1).map(e => e.channels.size), [30, 10, 30]);
  assert.deepEqual(cats.elements.slice(1).map(e => e.channels.alpha), [0.625, 0.625, 0.625]);
});

test('age: days since the evidence time (now given); no time = the channel preset; an unknown measure or channel is reported and never refused', () => {
  const now = Date.parse('2026-10-27T00:00:00Z');
  const ev = t => ({ evidence: { class: 'stated', confidence: 1, provenance: { layer: 'person', source_ref: '', method: '', time: t } } });
  const rows = [row(1, ev('2026-10-26T00:00:00Z')), row(2, ev('2026-09-27T00:00:00Z')), row(3)];
  const r = mp.levelChannels(level(rows).focus, rows, { size: 'age', glow: 'kind', hue: 'attrs.nothing' }, now);
  assert.deepEqual(rows.map(x => r.channels.get(x).size), [14 + 6 * Math.log10(2), 14 + 6 * Math.log10(31), 14]);
  assert.deepEqual(rows.map(x => r.channels.get(x).hue), [0, 0, 0], 'a measure no row has: the preset');
  const rep = Object.fromEntries(r.report.map(x => [x.channel, x]));
  assert.equal(rep.size.present, 2); assert.equal(rep.hue.present, 0); assert.equal(rep.hue.known_measure, false); assert.equal(rep.glow.drawn, false);
  assert.ok(rows.every(x => !('glow' in r.channels.get(x))), 'a channel the map does not draw adds nothing to a mark');
});

test('generative: any mapping (random channels, measures, paths, finer forms, junk) draws a finite number on every drawn channel and never throws', () => {
  const channels = ['hue', 'saturation', 'lightness', 'size', 'alpha', 'glow', ''];
  const measures = ['kind', 'count', 'age', 'order', 'depth', 'evidence', 'confidence', 'layer', 'source', 'status', 'none', 'label', 'x.y.z', 'evidence.provenance', ''];
  for (let n = 0; n < 300; n++) {
    const rows = Array.from({ length: Math.floor(rnd() * 12) }, (_, i) => row(i, { count: rnd() < 0.5 ? Math.floor(rnd() * 1000) - 100 : undefined, order: i, depth: rnd() < 0.5 ? 2 : undefined }));
    const setting = {};
    for (let k = 0; k < 4; k++) {
      const c = pick(channels), m = pick(measures);
      setting[c] = rnd() < 0.3 ? { measure: m, range: [rnd() * 50 - 10, rnd() * 50 - 10], curve: pick(['linear', 'log1p', 'weird']), missing: rnd() } : rnd() < 0.1 ? 42 : m;
    }
    const model = map.viewModelFor(level(rows), 'Graf', rnd() < 0.5 ? setting : JSON.stringify(setting));
    for (const e of model.elements) for (const [c, v] of Object.entries(e.channels)) assert.ok(Number.isFinite(v), `${c}=${v} for ${JSON.stringify(setting)}`);
    assert.ok(Array.isArray(model.mapping_report));
  }
});

test('the setting round-trips: choicesOnly keeps only what differs from the default ({} = the default); parseMapping reads objects, text and the finer form', () => {
  assert.deepEqual(mp.choicesOnly(mp.effectiveMapping(undefined)), {});
  assert.deepEqual(mp.choicesOnly(mp.effectiveMapping('{"size":"age"}')), { size: 'age' });
  assert.deepEqual(mp.choicesOnly(mp.effectiveMapping({ size: { measure: 'count', range: [1, 2] } })), { size: { measure: 'count', range: [1, 2] } });
  assert.deepEqual(mp.parseMapping({ a: 1, b: 'x', c: { measure: 'kind' }, d: { no: 1 }, e: '' }), { b: { measure: 'x' }, c: { measure: 'kind' } });
  assert.deepEqual(mp.parseMapping('[1]'), {});
  assert.equal(mp.measureWords('age', 'pl'), 'wiek (dni od dowodu)');
  assert.equal(mp.measureWords('receipt.tokens_est', 'en'), 'other: receipt.tokens_est');
  assert.equal(mp.channelWords('size', 'pl'), 'rozmiar');
});
