// G3b: the pure state of the "Widok" bar (no browser, no kernel): encodings in words, composed channels, advisories as chips by the kernel's ask state
// and the session's answers, and the request of the catalogue action that answers a chip.
import assert from 'node:assert/strict';
import { loadModule } from '../graph-perspectives/test-loader.mjs';
const { barState, advisoryKey, answerRequest, FALLBACK_CHOICES } = await loadModule('src/view-engine/view-bar.ts');
let count = 0;
async function check(name, fn) { await fn(); count++; console.log(`PASS ${name}`); }

const adv = (code, fp, ask = {}) => ({ schema: 'loom.advisory/1', code, level: 'advisory', text: { en: `${code} text`, pl: `${code} tekst` }, params: {},
  ask: { question: { en: 'Is this intended?', pl: 'Czy tak ma być?' }, fingerprint: fp, choices: [{ id: 'keep', label: { pl: 'Zostaw' } }, { id: 'dont_ask', label: { pl: 'Zostaw, nie pytaj więcej' } }], asking: true, state: 'ask', ...ask } });
const model = (extra = {}) => ({
  schema: 'loom.view_model/1', declaration: { id: 'v' }, focus: 'a', sources: [], edges: [], groups: [], measure_stats: {}, coordinate_systems: [], unsupported: [],
  budget: { max_nodes: null, elements: 2, collapsed: 0 }, issues: [],
  encodings: [
    { id: 'e1', channel: 'alpha', measure: 'age', data_type: 'quantitative', status: 'promoted', reasons: [] },
    { id: 'e2', channel: 'alpha', measure: 'doi', data_type: 'quantitative', status: 'promoted', reasons: [], combine: 'multiply' },
    { id: 'e3', channel: 'hue', measure: 'kind', data_type: 'nominal', status: 'allowed', reasons: [] },
  ],
  elements: [
    { id: 'a', label: 'a', kind: 'k', measures: {}, coords: {}, channels: { alpha: 0 }, sources: [], channel_parts: { alpha: [{ encoding: 'e1', value: 0, combine: 'replace' }, { encoding: 'e2', value: 0.5, combine: 'multiply' }] } },
    { id: 'b', label: 'b', kind: 'k', measures: {}, coords: {}, channels: { alpha: 0 }, sources: [], channel_parts: { alpha: [{ encoding: 'e1', value: 0 }, { encoding: 'e2', value: 1, combine: 'multiply' }] } },
  ],
  ...extra,
});

await check('no model: an empty bar', async () => {
  assert.deepEqual(barState(null), { encodings: [], composed: [], chips: [], hidden: 0 });
});

await check('the promoted encodings are listed (allowed ones are not drawn, so not said), with their combine', async () => {
  const s = barState(model());
  assert.deepEqual(s.encodings.map(e => `${e.channel}<-${e.measure}`), ['alpha<-age', 'alpha<-doi']);
  assert.equal(s.encodings[1].combine, 'multiply');
});

await check('a channel composed of several encodings is one line: its encodings, how they combine (missing = replace) and on how many elements', async () => {
  const [c] = barState(model()).composed;
  assert.equal(c.channel, 'alpha');
  assert.deepEqual(c.encodings, ['e1', 'e2']);
  assert.deepEqual(c.combines, ['replace', 'multiply']);
  assert.equal(c.elements, 2);
});

await check('advisories: asking ones are chips, acknowledged / muted ones are hidden but counted; nothing is dropped from the data', async () => {
  const m = model({ advisories: [adv('nothing_visible', 'f1'), adv('marks_identical', 'f2', { asking: false, state: 'acknowledged' }), adv('combine_unknown', 'f3', { asking: false, state: 'muted' })] });
  const s = barState(m);
  assert.deepEqual(s.chips.map(c => c.code), ['nothing_visible']);
  assert.equal(s.hidden, 2);
  assert.equal(s.chips[0].fingerprint, 'f1');
  assert.deepEqual(s.chips[0].choices.map(c => c.id), ['keep', 'dont_ask']);
  assert.equal(m.advisories.length, 3);
});

await check('an answer of this session hides the chip at once (key = code + fingerprint); another fingerprint of the same code still asks', async () => {
  const m = model({ advisories: [adv('nothing_visible', 'f1'), adv('nothing_visible', 'f9')] });
  const s = barState(m, new Set([advisoryKey(m.advisories[0])]));
  assert.deepEqual(s.chips.map(c => c.fingerprint), ['f9']);
  assert.equal(s.hidden, 1);
});

await check('a transient answer ("keep" last time) is shown again: the kernel says it asks', async () => {
  const s = barState(model({ advisories: [adv('nothing_visible', 'f1', { answered: true, ack: { choice: 'keep' } })] }));
  assert.equal(s.chips.length, 1);
});

await check('an advisory without choices still gets the preset ones; one without ask is asked', async () => {
  const a = adv('x', 'f'); delete a.ask.choices;
  const s = barState(model({ advisories: [a, { code: 'y', level: 'advisory', text: { en: 'y' } }] }));
  assert.deepEqual(s.chips[0].choices, FALLBACK_CHOICES);
  assert.equal(s.chips[1].key, 'y|');
});

await check('the answer is the catalogue action advisory.answer with the code, fingerprint, the choice and the context', async () => {
  const [chip] = barState(model({ advisories: [adv('nothing_visible', 'f1')] })).chips;
  assert.deepEqual(answerRequest(chip, 'dont_ask', 'v', 'tekst'), { action: 'advisory.answer', args: { code: 'nothing_visible', fingerprint: 'f1', answer: 'dont_ask', context: { view: 'v', text: 'tekst' } } });
});

await check('generative: random advisories and answered sets - chips + hidden = advisories, no chip is answered or not asking', async () => {
  let seed = 7;
  const rnd = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
  for (let i = 0; i < 300; i++) {
    const n = Math.floor(rnd() * 6);
    const list = Array.from({ length: n }, (_, j) => adv(`c${Math.floor(rnd() * 3)}`, `f${j}`, rnd() < 0.3 ? { asking: false } : {}));
    const answered = new Set(list.filter(() => rnd() < 0.3).map(advisoryKey));
    const s = barState(model({ advisories: list }), answered);
    assert.equal(s.chips.length + s.hidden, n);
    for (const c of s.chips) { assert.ok(!answered.has(c.key)); assert.notEqual(c.advisory.ask?.asking, false); }
  }
});

console.log(`view-bar: ${count} passed`);
