// M1-W2b-1: the pure parts of the screen "Ty" (src/components/me-view-state.ts) without a browser: the parts of the kernel's answer become
// sections with their counts and mix of evidence; the chip is the evidence class in words; the provenance line is the kernel's words with the
// time; a "never" slot has no value; the buttons are the node's actions[] (nothing added, planned ones marked); the typed arguments are declared
// by the pack's words; a gated run is a gate with the kernel's reasons; the inferred look is a preset that a setting changes. The words are
// checked against presentation.graph_screen of the pack (pl and en: the same keys, no raw id on a screen). A generative part walks the whole
// space of evidence classes x provenance x value shapes.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const me = await loader.load('src/components/me-view-state.ts');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const { pl, en } = pack.entries.find(r => r.key === 'presentation.graph_screen').value.locales;
const tIn = table => (id, params = {}) => typeof table[id] === 'string' ? table[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? '') : id;
const t = tIn(pl), tEn = tIn(en);

const ev = (cls, human, extra = {}) => ({ class: cls, confidence: null, provenance: { layer: 'x', source_ref: 'y', method: 'm', time: null, ...extra }, human });
const row = (over = {}) => ({ address: 'memory:1', label: 'Mam kota Filemona', kind: 'memory_note', provenance: { layer: 'memory.json', layers: ['memory.json'] }, evidence: ev('observed', 'napisałeś to Ty'), has_children: false, ...over });
const part = (id, label, count, classes, actions = []) => ({ address: `subgraph:self/${id}`, label, kind: 'person_subgraph', count, evidence_classes: classes, actions, evidence: ev('declared', 'preset ROM'), provenance: { layer: 'rom', layers: ['rom'] }, has_children: true, why: `why ${id}` });

test('the pack has the same me.* words in pl and en, and none of them is empty', () => {
  const keys = o => Object.keys(o).filter(k => k.startsWith('me.')).sort();
  assert.ok(keys(pl).length >= 40);
  assert.deepEqual(keys(pl), keys(en));
  for (const k of keys(pl)) { assert.ok(pl[k].trim() !== '', k); assert.ok(en[k].trim() !== '', k); }
  // the placeholders of a word are the same in both languages
  for (const k of keys(pl)) assert.deepEqual([...pl[k].matchAll(/\{\{(\w+)\}\}/g)].map(m => m[1]).sort(), [...en[k].matchAll(/\{\{(\w+)\}\}/g)].map(m => m[1]).sort(), k);
});

test('the sections are the kernel\'s parts, in its order, each with its count and the mix of evidence; a part without a count says so', () => {
  const answer = { children: [
    part('identity', 'Tożsamość', 2, { declared: 2 }), part('memory', 'Co pamiętam', 1, { observed: 1 }),
    part('network', 'Sieć', null, { inferred: 3, declared: 1 }), row({ address: 'party:self/x', kind: 'profile_slot' }),
  ] };
  const heads = me.sections(answer, t);
  assert.deepEqual(heads.map(h => h.address), ['subgraph:self/identity', 'subgraph:self/memory', 'subgraph:self/network']);
  assert.equal(heads[0].head, 'Tożsamość (2)');
  assert.equal(heads[2].head, t('me.section.head_unknown', { label: 'Sieć' }));
  assert.equal(heads[2].count, null);
  // the mix is ordered declared, observed, derived, inferred ... with the class in words
  assert.equal(heads[2].mix, t('me.section.mix', { list: 'z aplikacji 1 · wywnioskowane 3' }));
  assert.equal(me.looseRows(answer).length, 1, 'what is not a part is shown as it is');
  assert.deepEqual(me.sections({ children: [] }, t), []);
});

test('the evidence chip is the class in words for every class; an unknown class says so instead of showing its id', () => {
  for (const cls of ['declared', 'observed', 'said', 'derived', 'inferred', 'extrapolated']) {
    assert.equal(me.classWord({ class: cls }, t), pl[`me.class.${cls}`]);
    assert.equal(me.classWord({ class: cls }, tEn), en[`me.class.${cls}`]);
  }
  assert.equal(me.classWord({ class: 'inferred', confidence: 0.4 }, t), t('me.class.inferred_conf', { confidence: '0.4' }));
  assert.equal(me.classWord({ class: 'telepathy' }, t), pl['me.class.other']);
  // a person's own interview answer is 'powiedziane', not 'zaobserwowane' (M1-K2c3)
  assert.equal(me.classWord({ class: 'said' }, t), 'powiedziane'); assert.equal(me.classWord({ class: 'said' }, tEn), 'said');
  // the list of classes is open: a class the pack has a word for is worded without a code change, a word that is no class is not taken for one
  assert.equal(me.classWord({ class: 'other' }, t), pl['me.class.other']); assert.equal(me.classWord({ class: 'inferred_conf' }, t), pl['me.class.other']);
});

test('the provenance line is the kernel\'s words with the time when there is one; without words it says "brak zapisu", never an id', () => {
  const timed = me.provenanceLine(row({ evidence: ev('observed', 'napisałeś to Ty', { time: '2026-10-22T10:00:00Z', method: 'note_user' }) }), t, 'pl');
  assert.ok(timed.text.startsWith('Skąd: napisałeś to Ty · '), timed.text);
  assert.equal(timed.time, '2026-10-22T10:00:00Z'); assert.equal(timed.method, 'note_user');
  const old = me.provenanceLine(row({ evidence: ev('declared', 'pochodzenie niezapisane - starsza notatka', { method: 'not_recorded_before_m1' }) }), t, 'pl');
  assert.equal(old.text, 'Skąd: pochodzenie niezapisane - starsza notatka');
  assert.equal(me.provenanceLine(row({ evidence: ev('declared', undefined) }), t, 'pl').text, pl['me.provenance.none']);
  assert.equal(me.provenanceLine(row({ evidence: ev('declared', '  ') }), t, 'pl').text, pl['me.provenance.none']);
  assert.equal(me.formatTime('not a time', 'pl'), 'not a time', 'an unreadable time is shown as it came');
});

test('a slot with no value (never, declined, unknown) shows no value, only the kernel\'s word for the state; a known one shows the kernel\'s words', () => {
  const never = me.rowView(row({ kind: 'profile_slot', value: null, value_human: 'nigdy', status: 'never' }), t, 'pl');
  assert.equal(never.value, null); assert.equal(never.state, 'nigdy');
  const known = me.rowView(row({ kind: 'profile_slot', value: 'direct', value_human: 'bezpośrednio', status: 'known' }), t, 'pl');
  assert.equal(known.value, 'bezpośrednio'); assert.equal(known.state, null);
  // a value of the node whose kernel gave no words is shown as it is; an object is never dumped on the screen
  assert.equal(me.rowView(row({ value: 3 }), t, 'pl').value, '3');
  assert.equal(me.rowView(row({ value: { a: 1 } }), t, 'pl').value, null);
  assert.equal(me.rowView(row(), t, 'pl').state, null);
});

test('the buttons are the node\'s actions[]: the kernel\'s title or the pack\'s short word; a planned action is marked and is no button', () => {
  const actions = [
    { id: 'profile.set', label: 'Zapisz o mnie', decision: 'needs_confirmation', implementation: 'implemented', args: { slot: 'communication.style' } },
    { id: 'memory.forget', label: 'Zapomnij coś', decision: 'needs_confirmation', implementation: 'implemented', args: { id: 'abc' } },
    { id: 'something.new', label: 'Coś nowego', decision: 'applicable', implementation: 'planned' },
    { id: 'units.list', label: 'Pokaż jednostki' },
  ];
  const b = me.actionButtons(actions, t);
  assert.deepEqual(b.map(x => x.id), ['profile.set', 'memory.forget', 'something.new', 'units.list'], 'nothing added, nothing dropped, the kernel\'s order');
  assert.equal(b[0].label, 'Popraw'); assert.equal(b[0].title, 'Zapisz o mnie');
  assert.equal(b[1].label, 'Zapomnij');
  assert.equal(b[2].label, 'Coś nowego', 'an action the pack has no word for keeps the kernel\'s title'); assert.equal(b[2].planned, true);
  assert.equal(b[3].planned, false); assert.deepEqual(b[3].args, {});
  assert.deepEqual(me.actionButtons(undefined, t), []);
  assert.equal(me.actionButtons(actions, tEn)[0].label, 'Correct');
});

test('the arguments a person types are declared by the words; the run is the node\'s arguments plus what was typed, on the application', () => {
  assert.deepEqual(me.inputFields('profile.set', t), [{ arg: 'value', label: 'Nowa wartość' }]);
  assert.deepEqual(me.inputFields('units.define', t).map(f => f.arg), ['kind', 'id', 'label']);
  assert.deepEqual(me.inputFields('memory.forget', t), [], 'no word = nothing to type');
  const button = { id: 'profile.set', args: { slot: 'communication.style' } };
  assert.deepEqual(me.runArgs(button, { value: '  luzno ' }), { slot: 'communication.style', value: 'luzno' });
  assert.deepEqual(me.runArgs(button, { value: '   ' }), { slot: 'communication.style' }, 'an empty field is left out: the kernel says what is missing');
  assert.deepEqual(me.runRequest(button, { slot: 's' }), { action: 'profile.set', args: { slot: 's' }, element: { class: 'application' } });
  for (const k of Object.keys(pl).filter(k => k.startsWith('me.input.') && Object.keys(pl).some(o => o.startsWith(k + '.')))) {   // a key that has labels under it declares an action's arguments
    for (const f of me.inputFields(k.slice('me.input.'.length), t)) assert.notEqual(f.label, f.arg, `${k}: ${f.arg} has a label`);
  }
});

test('the form comes from the kernel\'s params of the action; the words are only a label override; no params = the old behaviour', () => {
  const [b] = me.actionButtons([{ id: 'inference.reject', label: 'Odrzuć wniosek', args: { ref: 'n_1' }, params: [{ name: 'why', kind: 'free_text', required: true, label: 'Dlaczego?' }] }], t);
  assert.deepEqual(b.params.map(p => p.name), ['why']);
  assert.deepEqual(me.inputFields(b, t), [{ arg: 'why', label: 'Dlaczego?' }], 'the kernel\'s label when the pack has no word');
  // a word of the pack overrides the kernel's label (profile.set.value has one), and a kernel list beats the word listing the names
  const set = { id: 'profile.set', params: [{ name: 'value', label: 'Wartość (jądro)' }, { name: 'extra', label: 'Dodatkowe' }] };
  assert.deepEqual(me.inputFields(set, t), [{ arg: 'value', label: 'Nowa wartość' }, { arg: 'extra', label: 'Dodatkowe' }]);
  // an older kernel (no params) or an empty list: the words decide, as before
  assert.deepEqual(me.inputFields({ id: 'profile.set', params: [] }, t), [{ arg: 'value', label: 'Nowa wartość' }]);
  assert.deepEqual(me.actionButtons([{ id: 'x.y', label: 'X' }], t)[0].params, []);
  assert.deepEqual(me.inputFields({ id: 'x.y', params: [] }, t), []);
  // the confirm / reject buttons have words in both languages and the verdict is told in the details
  assert.equal(me.actionButtons([{ id: 'inference.confirm', label: 'k' }], t)[0].label, 'Potwierdź');
  assert.equal(me.actionButtons([{ id: 'inference.reject', label: 'k' }], tEn)[0].label, 'Reject');
  const lines = me.detailLines({ review: { decision: 'rejected', time: '2026-10-25T10:00:00Z', was: { class: 'inferred' } } }, t, 'pl');
  assert.equal(lines.length, 2); assert.match(lines[0], /odrzucone/); assert.match(lines[1], /wywnioskowane/);
  assert.equal(me.stateText({ value: null, value_human: null }), null);
  // a forgotten entry: when and by whom in words (no raw ids), and whether the words were kept
  const f = me.detailLines({ forgotten: { time: '2026-10-25T10:00:00Z', by: 'user', kept: true } }, t, 'pl');
  assert.equal(f.length, 2); assert.match(f[0], /^Zapomniane przez Ciebie, /); assert.match(f[1], /możesz go przywrócić/);
  const g2 = me.detailLines({ forgotten: { time: '2026-10-25T10:00:00Z', by: 'someone-new', kept: false } }, t, 'pl');
  assert.match(g2[0], /^Zapomniane, /); assert.doesNotMatch(g2[0], /someone/); assert.match(g2[1], /nie da się go przywrócić/);
  assert.equal(me.actionButtons([{ id: 'memory.restore', label: 'k' }], t)[0].label, 'Przywróć');
  // an entry that was brought back keeps the story of its earlier forgetting: when, by whom, and the restoring, in words
  const h = me.detailLines({ forget_history: [{ time: '2026-10-24T10:00:00Z', by: 'user', kept: true, restored: { time: '2026-10-25T10:00:00Z', by: 'user' } }, { time: '2026-10-23T10:00:00Z', by: 'new-one', restored: { time: '2026-10-23T11:00:00Z' } }] }, t, 'pl');
  assert.equal(h.length, 2); assert.match(h[0], /^Wcześniej: zapomniane przez Ciebie, .*; przywrócone przez Ciebie, /);
  assert.match(h[1], /^Wcześniej: zapomniane, /); assert.doesNotMatch(h[1], /new-one| ,/);
});

test('a run is a gate (the kernel\'s reasons in the language), an error (the kernel\'s message) or done (the kernel\'s text)', () => {
  const gated = { id: 'ar_1', status: 'awaiting_confirmation', pending: { kind: 'confirmation', receipt_id: 'arr_1', gates: [{ reason: { en: 'Forgetting is final.', pl: 'Zapomnienie jest na stałe.' } }] } };
  const g = me.interpretRun(gated, t, 'pl');
  assert.equal(g.kind, 'gate'); assert.deepEqual(g.gate, { runId: 'ar_1', receiptId: 'arr_1', reasons: ['Zapomnienie jest na stałe.'] });
  assert.deepEqual(me.interpretRun(gated, tEn, 'en').gate.reasons, ['Forgetting is final.']);
  const failed = me.interpretRun({ id: 'ar_2', status: 'failed', error: { code: 'invalid_argument', message: 'value is required' } }, t, 'pl');
  assert.equal(failed.kind, 'error'); assert.equal(failed.text, t('me.confirm.failed', { reason: 'value is required' }));
  const done = me.interpretRun({ id: 'ar_3', status: 'completed', result: { parts: [{ kind: 'text', text: 'Zapamiętano.' }], data: { id: 'x' } } }, t, 'pl');
  assert.deepEqual(done, { kind: 'done', text: 'Zapamiętano.', data: { id: 'x' } });
});

test('the inferred look is a labelled preset that a setting changes; the open sections are remembered as a string of addresses', () => {
  assert.equal(me.inferredOpacityOf(undefined), me.INFERRED_OPACITY_PRESET);
  assert.equal(me.inferredOpacityOf(0.4), 0.4); assert.equal(me.inferredOpacityOf('0.5'), 0.5); assert.equal(me.inferredOpacityOf(1), 1); assert.equal(me.inferredOpacityOf(0), 0);
  for (const bad of [-1, 1.5, 'x', null, NaN, {}, '']) assert.equal(me.inferredOpacityOf(bad), me.INFERRED_OPACITY_PRESET, String(bad));
  assert.deepEqual(me.openSectionsOf('subgraph:self/a  subgraph:self/b'), ['subgraph:self/a', 'subgraph:self/b']);
  assert.deepEqual(me.openSectionsOf(undefined), []); assert.deepEqual(me.openSectionsOf(true), []);
  const opened = me.withSection(['a'], 'b', true); assert.deepEqual(opened, ['a', 'b']);
  assert.deepEqual(me.withSection(opened, 'a', false), ['b']); assert.deepEqual(me.withSection(opened, 'b', true), ['a', 'b']);
  assert.equal(me.serializeSections(opened), 'a b');
  assert.equal(me.isMeFocus('party:self'), true); assert.equal(me.isMeFocus('subgraph:self/memory'), false);
});

test('details: the reason of a slot, that it is within the level, the words of a note\'s origin', () => {
  assert.deepEqual(me.detailLines({ reason_text: 'wykracza poza wybrany poziom', asked_at_level: false }, t), ['Dlaczego tak: wykracza poza wybrany poziom']);
  assert.deepEqual(me.detailLines({ asked_at_level: true }, t), [pl['me.details.in_level']]);
  assert.deepEqual(me.detailLines({ provenance: { known: false, words: 'pochodzenie niezapisane - starsza notatka' } }, t), ['Pochodzenie: pochodzenie niezapisane - starsza notatka']);
  assert.deepEqual(me.detailLines(undefined, t), []); assert.deepEqual(me.detailLines({}, t), []);
});

// The possibility space: every class x human words x time x confidence x value shape gives a row view in words only (no dotted/snake id), the
// inferred look exactly for inferred and extrapolated, and the setter chip only on a preference.
test('every combination of evidence and value shape gives a row in words', () => {
  const classes = ['declared', 'observed', 'said', 'derived', 'inferred', 'extrapolated', 'weird'];
  const humans = [undefined, '', 'napisałeś to Ty', 'wniosek z analizy wiadomości'];
  const times = [null, '2026-10-22T10:00:00Z', 'garbage'];
  const values = [[undefined, undefined], [null, 'nigdy'], [null, undefined], ['a', 'a'], [3, '3'], [false, 'wyłączone'], [{ x: 1 }, undefined]];
  const kinds = ['memory_note', 'preference', 'profile_slot', 'privacy_rule', 'person_subgraph', 'anything'];
  let n = 0;
  for (const cls of classes) for (const human of humans) for (const time of times) for (const [value, vh] of values) for (const kind of kinds) for (const conf of [null, 0.8]) {
    const r = row({ kind, value, value_human: vh, evidence: { ...ev(cls, human, { time }), confidence: conf }, provenance: { layer: 'l', layers: ['l'], chip: { id: 'person', label: 'Ty' } } });
    const v = me.rowView(r, t, 'pl'); n++;
    assert.ok(v.chip.trim() !== '' && !/[._]/.test(v.chip.replace(/\d\.\d/, '')), `chip ${cls}: ${v.chip}`);
    assert.ok(v.provenance.text.startsWith('Skąd: '), v.provenance.text);
    assert.equal(v.inferred, cls === 'inferred' || cls === 'extrapolated');
    assert.equal(v.inferredNote !== null, v.inferred);
    assert.equal(v.setBy !== null, kind === 'preference');
    assert.ok(v.value === null || v.state === null, 'a row has a value or a state word, not both');
    assert.ok(v.value === null || typeof v.value === 'string');
  }
  assert.ok(n > 3000);
  // an entry with no value has no evidence: the chip is its state in words (bez odpowiedzi), the provenance line is absent, nothing is inferred
  for (const state of ['unanswered', 'declined', 'never', 'something_new', undefined]) for (const kind of kinds) {
    const v = me.rowView(row({ kind, value: null, value_human: 'nieznane', evidence: null, state, status: 'unknown' }), t, 'pl');
    assert.equal(v.chip, null); assert.equal(v.provenance, null); assert.equal(v.inferred, false); assert.equal(v.classId, '');
    assert.ok(v.stateChip && !/[._]/.test(v.stateChip), `state chip ${state}: ${v.stateChip}`);
    if (state === 'unanswered') assert.equal(v.stateChip, 'bez odpowiedzi');
    if (state === 'something_new' || state === undefined) assert.equal(v.stateChip, pl['me.state.other'], 'a state with no word says "no value", never its id');
  }
  assert.equal(me.rowView(row(), t, 'pl').stateChip, null, 'an entry with evidence has no state chip');
});

test('the chain of a preference is read from the card of a setting (inheritance) or of a person\'s preference (attrs.resolved.inheritance)', () => {
  const chain = [{ level: 'installation', label: 'Instalacja', role: 'overridden', active: true }, { level: 'person', label: 'Ty', role: 'supplied', active: true }];
  assert.deepEqual(me.inheritanceOf({ inheritance: chain }), chain);
  assert.deepEqual(me.inheritanceOf({ attrs: { resolved: { inheritance: chain } } }), chain);
  assert.deepEqual(me.inheritanceOf({ inheritance: [], attrs: { resolved: { inheritance: chain } } }), chain);
  for (const none of [null, undefined, {}, { attrs: {} }, { attrs: { resolved: {} } }, { attrs: { resolved: { inheritance: 'x' } } }]) assert.deepEqual(me.inheritanceOf(none), []);
});

test('a field that depends on another offers only the choices of its current value; a choice with no levels fits any; an empty controller offers all', () => {
  const field = { arg: 'scope_id', label: 'x', depends_on: 'scope', choices: [
    { value: 'c1', label: 'Rozmowa', levels: ['conversation'] }, { value: 'p1', label: 'Remont', levels: ['project'] }, { value: 'any', label: 'Wszędzie' }] };
  assert.deepEqual(me.visibleChoices(field, { scope: 'conversation' }).map(c => c.value), ['c1', 'any']);
  assert.deepEqual(me.visibleChoices(field, { scope: 'project' }).map(c => c.value), ['p1', 'any']);
  assert.deepEqual(me.visibleChoices(field, { scope: '' }).map(c => c.value), ['c1', 'p1', 'any']);
  assert.deepEqual(me.visibleChoices({ ...field, depends_on: undefined }, { scope: 'project' }).map(c => c.value), ['c1', 'p1', 'any']);
  const fields = me.inputFields({ id: 'rules.set', params: [{ name: 'scope_id', kind: 'choice', choices: field.choices, depends_on: 'scope' }] }, (k) => null);
  assert.equal(fields[0].depends_on, 'scope');
});
