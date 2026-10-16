// R35-2 / R35-4: graph panel strings PL/EN, label truncation and collision, and the source-history notice condition.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const gt = await loader.load('src/graph/graph-text.ts');
const gl = await loader.load('src/graph/graph-labels.ts');
const sn = await loader.load('src/shell/source-notice.ts');

test('graph strings: PL and EN, parameters, fallback to en', () => {
  assert.equal(gt.graphText('en', 'reload'), 'Reload');
  assert.equal(gt.graphText('pl', 'reload'), 'Odśwież');
  assert.equal(gt.graphText('pl-PL', 'depth', { n: 3 }), 'Głębokość rozwinięcia po kliknięciu: 3');
  assert.equal(gt.graphText('en', 'counts', { nodes: 2, edges: 1 }), '2 nodes · 1 edges');
  assert.equal(gt.graphText('de', 'reload'), 'Reload');
  for (const id of ['filter_placeholder', 'reload', 'depth', 'counts', 'page_count', 'selected', 'canvas_label'])
    assert.notEqual(gt.graphText('pl', id), gt.graphText('en', id), id);
});

test('truncateLabel: ellipsis, no cut when short or maxChars = 0', () => {
  assert.equal(gl.truncateLabel('short', 10), 'short');
  assert.equal(gl.truncateLabel('abcdefghijkl', 6), 'abcde…');
  assert.equal(gl.truncateLabel('abcdefghijkl', 0), 'abcdefghijkl');
  assert.equal(Array.from(gl.truncateLabel('żółćżółćżółć', 5)).length, 5);
});

test('presets: narrow viewport gets the narrow preset, overrides win', () => {
  assert.equal(gl.graphLabelOptions(360).maxChars, gl.GRAPH_LABEL_PRESET_NARROW.maxChars);
  assert.equal(gl.graphLabelOptions(1200).maxChars, gl.GRAPH_LABEL_PRESET.maxChars);
  assert.equal(gl.graphLabelOptions(360, { maxChars: 99 }).maxChars, 99);
});

const measure = text => text.length * 6;
const opts = { ...gl.GRAPH_LABEL_PRESET, maxChars: 8 };
test('planLabels: overlapping label of the lower-degree node is hidden, the higher-degree one stays', () => {
  const placed = gl.planLabels([
    { id: 'a', x: 10, y: 10, text: 'alpha-long-label', degree: 1 },
    { id: 'b', x: 14, y: 12, text: 'beta', degree: 5 },
    { id: 'c', x: 10, y: 200, text: 'gamma', degree: 0 },
  ], opts, measure);
  assert.deepEqual(placed.map(p => p.id).sort(), ['b', 'c']);
  assert.equal(placed.find(p => p.id === 'c').text, 'gamma');
});
test('planLabels: long text is truncated; forced (hover/tap) label is full and never hidden', () => {
  const placed = gl.planLabels([
    { id: 'a', x: 10, y: 100, text: 'alpha-long-label', degree: 9 },
    { id: 'b', x: 12, y: 10, text: 'forced-node-with-long-label', degree: 0, forced: true },
  ], opts, measure);
  assert.equal(placed.length, 2);
  assert.equal(placed.find(p => p.id === 'b').text, 'forced-node-with-long-label');
  assert.equal(placed.find(p => p.id === 'a').text, 'alpha-l…');
  const hidden = gl.planLabels([{ id: 'a', x: 10, y: 10, text: 'aaaa', degree: 9 }, { id: 'b', x: 12, y: 10, text: 'bbbb', degree: 0, forced: true }], opts, measure);
  assert.deepEqual(hidden.map(p => p.id), ['b']);
});
test('planLabels: minDegree and maxLabels are thresholds of the preset, not hard limits', () => {
  const items = [0, 1, 2, 3].map(i => ({ id: 'n' + i, x: i * 200, y: 0, text: 'n' + i, degree: i }));
  assert.equal(gl.planLabels(items, { ...opts, minDegree: 2 }, measure).length, 2);
  assert.equal(gl.planLabels(items, { ...opts, maxLabels: 1 }, measure)[0].id, 'n3');
  assert.equal(gl.planLabels(items, opts, measure).length, 4);
  assert.equal(gl.planLabels(items, { ...opts, minDegree: 99 }, measure).length, 0);
  assert.equal(gl.planLabels([{ ...items[0], forced: true }], { ...opts, minDegree: 99 }, measure).length, 1);
});
test('planLabels: no two placed non-forced labels overlap (dense field)', () => {
  const items = Array.from({ length: 200 }, (_, i) => ({ id: 'n' + i, x: (i * 37) % 300, y: (i * 53) % 200, text: 'node-' + i, degree: i % 5 }));
  const placed = gl.planLabels(items, opts, measure);
  for (let i = 0; i < placed.length; i++) for (let j = i + 1; j < placed.length; j++) assert.equal(gl.overlaps(placed[i], placed[j], 0), false);
  assert.ok(placed.length > 0 && placed.length < 200);
});
test('degreeMap counts both ends', () => {
  const d = gl.degreeMap([{ src: 'a', dst: 'b' }, { src: 'a', dst: 'c' }]);
  assert.equal(d.get('a'), 2); assert.equal(d.get('c'), 1);
});

test('source-history notice: hidden for an ordinary conversation, shown for linked source history', () => {
  assert.equal(sn.sourceHistoryUnavailable(null, []), false);
  assert.equal(sn.sourceHistoryUnavailable({ metadata: {} }, [{ storage: 'native' }]), false);
  assert.equal(sn.sourceHistoryUnavailable({ metadata: { source_history: [] } }, []), false);
  assert.equal(sn.sourceHistoryUnavailable({}, [{ storage: 'native' }, { storage: 'reference', source_ref: { version: '1' } }]), true);
  assert.equal(sn.sourceHistoryUnavailable({}, [{ source_ref: { version: '1' } }]), true);
  assert.equal(sn.sourceHistoryUnavailable({ metadata: { source_resources: [{ id: 'r' }] } }, []), true);
});
