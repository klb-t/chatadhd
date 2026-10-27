// G3c: the advisories of the navigator map (src/components/navigator-advisory.ts) without a browser: the generic degenerate checks over the
// browser-built view model, the kernel's codes and shape, the fingerprint (mapping, not data), remembered answers, and the chip state of ViewBar.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const map = await loader.load('src/components/navigator-map.ts');
const adv = await loader.load('src/components/navigator-advisory.ts');
const bar = await loader.load('src/view-engine/view-bar.ts');

const row = (address, kind, extra = {}) => ({ address, label: 'L ' + address, kind, provenance: { layer: 'rom' }, evidence: { class: 'declared', confidence: null }, has_children: false, count: 1, ...extra });   // G3d: a count everywhere, so 'size by count' is drawn (no count = measure_missing, tested below)
const level = children => ({ focus: { address: 'area:rom', label: 'ROM', kind: 'area' }, children, edges: [], counts: { total: children.length, shown: children.length, collapsed: 0, by_source: {} } });
const modelOf = children => map.viewModelFor(level(children), 'Graf');
const ON = { ...adv.NAV_ADVISORY, advise_on_defaults: true };
const codes = list => list.map(a => `${a.code}:${a.params.channel}`).sort();

test('a level of one kind and no counts: hue, saturation, lightness and size are identical on every mark (the focus is the anchor, not a mark)', () => {
  const list = adv.navigatorAdvisories(modelOf([row('a', 'topic'), row('b', 'topic'), row('c', 'topic')]), ON);
  assert.deepEqual(codes(list), ['marks_identical:hue', 'marks_identical:lightness', 'marks_identical:saturation', 'marks_identical:size']);
  const a = list.find(x => x.params.channel === 'size');
  assert.equal(a.schema, 'loom.advisory/1'); assert.equal(a.level, 'advisory'); assert.equal(a.params.total, 3);
  assert.equal(a.ask.fingerprint, 'navigator_level|size');
  assert.ok(a.text.pl.includes("'size'") && a.text.en.includes('3') && !/[{}]/.test(a.text.pl + a.text.en));
  assert.ok(a.ask.choices.some(c => c.id === 'dont_ask'));
});

test('a level of different kinds and counts asks nothing; a single mark or no marks asks nothing either', () => {
  assert.deepEqual(adv.navigatorAdvisories(modelOf([row('a', 'topic', { count: 3 }), row('b', 'person', { count: 9 })]), ON), []);
  assert.deepEqual(adv.navigatorAdvisories(modelOf([row('a', 'topic')]), ON), []);
  assert.deepEqual(adv.navigatorAdvisories(modelOf([]), ON), []);
});

test('nothing visible: every mark at or below the threshold of a channel that can vanish (any value works, also degenerate ones)', () => {
  const m = modelOf([row('a', 'topic', { count: 1 }), row('b', 'person', { count: 2 })]);
  for (const e of m.elements) e.channels.size = 0;
  const list = adv.navigatorAdvisories(m, ON);
  assert.ok(list.some(a => a.code === 'nothing_visible' && a.params.channel === 'size' && a.params.invisible === 2));
  const data = { ...ON, min_elements: 0, channels: { hue: { invisible_at_or_below: 1000 } } };
  assert.ok(adv.navigatorAdvisories(modelOf([row('a', 'topic')]), data).some(a => a.code === 'nothing_visible'));
  assert.deepEqual(adv.navigatorAdvisories(modelOf([row('a', 'topic'), row('b', 'topic')]), { ...ON, channels: {} }), []);
});

test('the fingerprint is the mapping, not the data: other labels and counts on the same channel give the same one', () => {
  const f = m => adv.navigatorAdvisories(m, ON).find(a => a.params.channel === 'hue').ask.fingerprint;
  assert.equal(f(modelOf([row('a', 'topic'), row('b', 'topic')])), f(modelOf([row('x', 'topic'), row('y', 'topic'), row('z', 'topic')])));
});

test('remembered answers: dont_ask under code + fingerprint (or the code alone) or a policy ask=false marks it not asking; the chip bar hides it and counts it', () => {
  const list = adv.navigatorAdvisories(modelOf([row('a', 'topic'), row('b', 'topic')]), ON);
  const model = adv.resolveAsk;
  assert.equal(bar.barState({ advisories: list }).chips.length, 4);
  const one = model(list, new Set(['marks_identical|navigator_level|hue']));
  const st = bar.barState({ advisories: one });
  assert.equal(st.chips.length, 3); assert.equal(st.hidden, 1);
  assert.equal(bar.barState({ advisories: model(list, new Set(['marks_identical|'])) }).chips.length, 0);
  assert.equal(bar.barState({ advisories: model(list, new Set(), false) }).chips.length, 0);
  assert.equal(list[0].ask.asking, true);   // the input is not changed
});

test('a chip is answered through advisory.answer with the code and the fingerprint', () => {
  const [chip] = bar.barState({ advisories: adv.navigatorAdvisories(modelOf([row('a', 'topic'), row('b', 'topic')]), ON) }).chips;
  const req = bar.answerRequest(chip, 'dont_ask', 'navigator_level', 'x');
  assert.equal(req.action, 'advisory.answer'); assert.equal(req.args.fingerprint, chip.fingerprint); assert.equal(req.args.answer, 'dont_ask');
});

test('advise_on_defaults false (the preset): the shipped mapping asks nothing; a changed mapping asks only on the changed channel, with its own fingerprint; true asks on all', () => {
  const m = modelOf([row('a', 'topic'), row('b', 'topic'), row('c', 'topic')]);
  assert.equal(adv.NAV_ADVISORY.advise_on_defaults, false);
  assert.deepEqual(adv.navigatorAdvisories(m), []);
  assert.deepEqual(adv.navigatorAdvisories(m, adv.NAV_ADVISORY, adv.readMapping({ hue: 'kind', size: 'count' })), [], 'the same as shipped is not a change');
  const rows3 = [row('a', 'topic'), row('b', 'topic'), row('c', 'topic')];
  const list = adv.navigatorAdvisories(map.viewModelFor(level(rows3), 'Graf', '{"size":"age"}'), adv.NAV_ADVISORY, adv.readMapping('{"size":"age"}'));   // the map draws what the setting says
  assert.deepEqual(codes(list), ['measure_missing:size'], 'G3d: no row has an age, so every mark draws the preset - said once, not twice');
  assert.equal(list[0].ask.fingerprint, 'navigator_level|size|age');
  assert.deepEqual(adv.readMapping('not json'), {}); assert.deepEqual(adv.readMapping([1]), {}); assert.deepEqual(adv.readMapping({ a: 1, b: 'x' }), { b: 'x' });
  assert.equal(adv.navigatorAdvisories(m, ON).length, 4);
});

test('G3d: the mapping report - a measure no mark has (measure_missing, any path), a channel the map does not draw (channel_unknown), a constant asks only "all identical"', () => {
  const rows = [row('a', 'topic', { order: 1 }), row('b', 'person', { order: 2 })];
  const at = (mapping) => adv.navigatorAdvisories(map.viewModelFor(level(rows), 'Graf', mapping), adv.NAV_ADVISORY, adv.readMapping(mapping));
  assert.deepEqual(codes(at({ size: 'order' })), [], 'a measure the rows have, different values: nothing to say');
  const miss = at({ hue: 'receipt.tokens_est' });
  assert.deepEqual(codes(miss), ['measure_missing:hue']);
  assert.ok(miss[0].text.pl.includes('receipt.tokens_est') && miss[0].text.pl.includes('2') && !/[{}]/.test(miss[0].text.pl + miss[0].text.en));
  const unk = at({ glow: 'kind' });
  assert.deepEqual(codes(unk), ['channel_unknown:glow']);
  assert.equal(unk[0].ask.fingerprint, 'navigator_level|glow|kind');
  assert.deepEqual(codes(at({ alpha: 'none' })), ['marks_identical:alpha']);
  assert.deepEqual(codes(at({ size: { measure: 'order', range: [0, 0], curve: 'linear' } })), ['marks_identical:size', 'nothing_visible:size'], 'the finer form: a degenerate range is drawn and said, never refused');
});
