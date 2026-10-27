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

const row = (address, kind, extra = {}) => ({ address, label: 'L ' + address, kind, provenance: { layer: 'rom' }, evidence: { class: 'declared', confidence: null }, has_children: false, ...extra });
const level = children => ({ focus: { address: 'area:rom', label: 'ROM', kind: 'area' }, children, edges: [], counts: { total: children.length, shown: children.length, collapsed: 0, by_source: {} } });
const modelOf = children => map.viewModelFor(level(children), 'Graf');
const codes = list => list.map(a => `${a.code}:${a.params.channel}`).sort();

test('a level of one kind and no counts: hue, saturation, lightness and size are identical on every mark (the focus is the anchor, not a mark)', () => {
  const list = adv.navigatorAdvisories(modelOf([row('a', 'topic'), row('b', 'topic'), row('c', 'topic')]));
  assert.deepEqual(codes(list), ['marks_identical:hue', 'marks_identical:lightness', 'marks_identical:saturation', 'marks_identical:size']);
  const a = list.find(x => x.params.channel === 'size');
  assert.equal(a.schema, 'loom.advisory/1'); assert.equal(a.level, 'advisory'); assert.equal(a.params.total, 3);
  assert.equal(a.ask.fingerprint, 'navigator_level|size');
  assert.ok(a.text.pl.includes("'size'") && a.text.en.includes('3') && !/[{}]/.test(a.text.pl + a.text.en));
  assert.ok(a.ask.choices.some(c => c.id === 'dont_ask'));
});

test('a level of different kinds and counts asks nothing; a single mark or no marks asks nothing either', () => {
  assert.deepEqual(adv.navigatorAdvisories(modelOf([row('a', 'topic', { count: 3 }), row('b', 'person', { count: 9 })])), []);
  assert.deepEqual(adv.navigatorAdvisories(modelOf([row('a', 'topic')])), []);
  assert.deepEqual(adv.navigatorAdvisories(modelOf([])), []);
});

test('nothing visible: every mark at or below the threshold of a channel that can vanish (any value works, also degenerate ones)', () => {
  const m = modelOf([row('a', 'topic', { count: 1 }), row('b', 'person', { count: 2 })]);
  for (const e of m.elements) e.channels.size = 0;
  const list = adv.navigatorAdvisories(m);
  assert.ok(list.some(a => a.code === 'nothing_visible' && a.params.channel === 'size' && a.params.invisible === 2));
  const data = { ...adv.NAV_ADVISORY, min_elements: 0, channels: { hue: { invisible_at_or_below: 1000 } } };
  assert.ok(adv.navigatorAdvisories(modelOf([row('a', 'topic')]), data).some(a => a.code === 'nothing_visible'));
  assert.deepEqual(adv.navigatorAdvisories(modelOf([row('a', 'topic'), row('b', 'topic')]), { ...adv.NAV_ADVISORY, channels: {} }), []);
});

test('the fingerprint is the mapping, not the data: other labels and counts on the same channel give the same one', () => {
  const f = m => adv.navigatorAdvisories(m).find(a => a.params.channel === 'hue').ask.fingerprint;
  assert.equal(f(modelOf([row('a', 'topic'), row('b', 'topic')])), f(modelOf([row('x', 'topic'), row('y', 'topic'), row('z', 'topic')])));
});

test('remembered answers: dont_ask under code + fingerprint (or the code alone) or a policy ask=false marks it not asking; the chip bar hides it and counts it', () => {
  const list = adv.navigatorAdvisories(modelOf([row('a', 'topic'), row('b', 'topic')]));
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
  const [chip] = bar.barState({ advisories: adv.navigatorAdvisories(modelOf([row('a', 'topic'), row('b', 'topic')])) }).chips;
  const req = bar.answerRequest(chip, 'dont_ask', 'navigator_level', 'x');
  assert.equal(req.action, 'advisory.answer'); assert.equal(req.args.fingerprint, chip.fingerprint); assert.equal(req.args.answer, 'dont_ask');
});
