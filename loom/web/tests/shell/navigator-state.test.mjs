// M1-W0 (2) P2: the state of the graph navigator's list (src/components/navigator-state.ts) without a browser: path and drill-in,
// back to a step, the request graph.navigate gets, the words of a row (count, chip, evidence, status) through a translator that
// returns the id of a missing word, the bottom bar's budget/depth steps, and the honesty lines. The words themselves are checked
// against the catalogue presentation.graph_screen of the pack, so nothing here is a literal of the screen.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const nav = await loader.load('src/components/navigator-state.ts');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const feature = pack.entries.find(r => r.key === 'presentation.graph_screen').value;
const { en, pl } = feature.locales;
// The translator of the screen: the catalogue word with {{params}} filled, or the id when the word is missing (shell/text.ts does the same).
const translator = (words) => (id, params = {}) => {
  const text = words[id];
  if (typeof text !== 'string') return id;
  return text.replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? '');
};
const t = translator(pl);

test('every word the screen asks for exists in both languages, and the feature says it is present (the screen exists)', () => {
  const ids = ['root', 'back', 'card', 'children', 'count.value', 'count.unknown', 'count.unknown_reason', 'collapsed', 'hidden_by_you',
    'no_translation', 'no_children', 'unknown_source', 'query.placeholder', 'search', 'search.close', 'path', 'loading', 'error', 'card.stub',
    'bar.planned', 'bar.budget', 'bar.budget.unlimited', 'chip.rom', ...nav.BAR_BUTTONS.map(b => b.word),
    ...['present', 'partial', 'missing', 'calibration', 'planned'].map(s => `status.${s}`)];
  for (const id of ids) { assert.equal(typeof en[id], 'string', `en ${id}`); assert.equal(typeof pl[id], 'string', `pl ${id}`); }
  assert.equal(feature.status.state, 'present');
  assert.deepEqual(Object.keys(en).sort(), Object.keys(pl).sort(), 'en and pl have the same ids');
});

test('path: drill in, back to a step, the root; the query of a level does not carry over', () => {
  let s = nav.initialState();
  assert.equal(nav.focusOf(s), '');
  s = nav.setQuery(s, 'rom');
  s = nav.drillInto(s, { address: 'area:rom', label: 'ROM' });
  assert.equal(nav.focusOf(s), 'area:rom'); assert.equal(s.query, '');
  assert.equal(nav.drillInto(s, { address: 'area:rom', label: 'ROM' }), s, 'the same focus again is no step');
  s = nav.drillInto(s, { address: 'rom:boot', label: 'Rozruch' });
  assert.deepEqual(s.path.map(p => p.address), ['area:rom', 'rom:boot']);
  assert.deepEqual(nav.goBack(s, 1).path.map(p => p.address), ['area:rom']);
  assert.deepEqual(nav.goBack(s, 0).path, []);
  assert.equal(nav.goBack(s, 2), s, 'back to the current step changes nothing');
  assert.equal(nav.goBack(s, 9), s);
  assert.deepEqual(nav.initialState('area:profiles_settings', 'Ustawienia').path, [{ address: 'area:profiles_settings', label: 'Ustawienia' }]);
});

test('the request carries only what the state sets; the kernel presets fill the rest', () => {
  assert.deepEqual(nav.requestFor(nav.initialState(), 'pl'), { lang: 'pl' });
  let s = nav.drillInto(nav.initialState(), { address: 'area:rom', label: 'ROM' });
  s = nav.setQuery(s, 'boot');
  assert.deepEqual(nav.requestFor(s, 'pl', 'c_1'), { lang: 'pl', focus: 'area:rom', query: 'boot', conv_id: 'c_1' });
  assert.deepEqual(nav.requestFor(nav.wider(s, 50), 'en'), { lang: 'en', focus: 'area:rom', query: 'boot', limit: 100 });
});

test('Szerzej/Węziej and Głębiej/Płycej step from the effective values the kernel reported', () => {
  const s = nav.initialState();
  assert.equal(nav.wider(s, 50).limit, 100);
  assert.equal(nav.wider(nav.wider(s, 50), 50).limit, 200, 'the second step doubles the state, not the preset');
  assert.equal(nav.wider(s, 0).limit, null, 'no budget stays no budget');
  assert.equal(nav.narrower(s, 50, 25).limit, 25);
  assert.equal(nav.narrower(s, 0, 25).limit, 12, 'from no budget: half of what is shown');
  assert.equal(nav.narrower(nav.narrower(s, 1, 1), 1, 1).limit, nav.LIMIT_MIN_PRESET);
  assert.equal(nav.deeper(s, 1).hops, 2);
  assert.equal(nav.shallower(nav.deeper(s, 1), 1).hops, 1);
  assert.equal(nav.shallower(s, 1).hops, nav.HOPS_MIN_PRESET);
});

test('the words of a row: count or unknown with the reason (never 0), the chip from the kernel or the layer, evidence, status', () => {
  const row = (extra) => ({ address: 'x', label: 'X', kind: 'k', has_children: false, provenance: { layer: 'rom', layers: ['rom'] },
    evidence: { class: 'declared', confidence: null, provenance: { layer: 'rom', source_ref: '', method: '', time: null } }, ...extra });
  assert.equal(nav.countText(row({ count: 13, has_children: true }), t), '13');
  assert.equal(nav.countText(row({ count: 0, has_children: false }), t), '0', 'a known 0 is a 0');
  assert.equal(nav.countText(row({ count_unknown_reason: 'no_projection_source', count_unknown_human: 'żadne źródło nie wypisuje jeszcze elementów' }), t),
    pl['count.unknown_reason'].replace('{{reason}}', 'żadne źródło nie wypisuje jeszcze elementów'));
  assert.equal(nav.countText(row({ count_unknown_reason: 'no_projection_source' }), t), pl['count.unknown_reason'].replace('{{reason}}', pl['reason.no_projection_source']),
    'without the kernel words the catalogue reason is used');
  assert.equal(nav.countText(row({ has_children: true }), t), pl['count.unknown']);
  assert.equal(nav.countText(row({}), t), null, 'a leaf claims no count');
  assert.equal(nav.chipText(row({}), t), pl['chip.rom']);
  assert.equal(nav.chipText(row({ provenance: { layer: 'person', layers: ['rom', 'person'], chip: { id: 'person', label: 'Ty' } } }), t), 'Ty', 'the kernel chip wins');
  assert.equal(nav.chipText(row({ provenance: { layer: 'martian', layers: [] } }), t), 'chip.martian', 'a missing word shows its id');
  assert.equal(nav.evidenceText(row({}), t), null, 'declared = no evidence chip');
  assert.equal(nav.evidenceText(row({ evidence: { class: 'observed', confidence: null, human: 'powiedziałeś', provenance: {} } }), t), 'powiedziałeś');
  assert.equal(nav.evidenceText(row({ evidence: { class: 'inferred', confidence: null, provenance: {} } }), t), pl['evidence.inferred_unknown']);
  assert.equal(nav.evidenceText(row({ evidence: { class: 'inferred', confidence: 0.7, provenance: {} } }), t), pl['evidence.inferred'].replace('{{confidence}}', '0.7'));
  assert.equal(nav.statusText(row({ status: 'partial' }), t), pl['status.partial']);
  assert.equal(nav.statusText(row({ status: 'odd' }), t), 'status.odd');
  assert.equal(nav.statusText(row({}), t), null);
  assert.equal(nav.noTranslationText(row({ no_translation: { lang: 'pl', shown_lang: 'en', words: 'x' } }), t), pl['no_translation'].replace('{{lang}}', 'pl').replace('{{shown_lang}}', 'en'));
});

test('the honesty lines: collapsed by the budget, hidden by you, sources that could not answer', () => {
  const none = nav.honestyLines({ counts: { total: 3, shown: 3, collapsed: 0, by_source: {} }, hidden_by_you: { count: 0, items: [] }, unknown: [] }, t);
  assert.deepEqual(none, []);
  const lines = nav.honestyLines({ counts: { total: 9, shown: 4, collapsed: 5, by_source: {} }, hidden_by_you: { count: 1, items: [] },
    unknown: [{ source: 'kb', source_label: 'Wiedza', reason: 'database_unavailable', reason_human: 'bazy nie da się odczytać' }, { source: 'x', source_label: '', reason: 'planned', reason_human: '' }] }, t);
  assert.deepEqual(lines.map(l => l.id), ['collapsed', 'hidden', 'unknown-kb', 'unknown-x']);
  assert.equal(lines[0].text, pl.collapsed.replace('{{count}}', '5'));
  assert.equal(lines[2].text, pl.unknown_source.replace('{{source}}', 'Wiedza').replace('{{reason}}', 'bazy nie da się odczytać'));
  assert.equal(lines[3].text, pl.unknown_source.replace('{{source}}', 'x').replace('{{reason}}', pl['reason.planned']));
});

test('the bottom bar is the order of the spec; what does not work yet is marked so, never silently', () => {
  assert.deepEqual(nav.BAR_BUTTONS.map(b => b.id), ['perspective', 'wider', 'narrower', 'deeper', 'shallower', 'actions', 'view']);
  assert.deepEqual(nav.BAR_BUTTONS.filter(b => !b.works).map(b => b.id), ['perspective', 'actions', 'view']);
});
