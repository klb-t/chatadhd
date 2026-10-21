// M1-W0 (2) P3: the state of the node card (src/components/node-card-state.ts) without a browser: the chain in words (winning
// level, "bez projektu"), the editor by type and its parsing, "Dla kogo?" from the field's own levels with the way each level is
// written (settings.set / rules.set / unavailable with the reason), the request of a write, "Zdejmij wartość" per layer with what
// returns, the confirmation gate of a run, the outcome in words. Words are checked against presentation.graph_screen of the pack.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const card = await loader.load('src/components/node-card-state.ts');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const { pl } = pack.entries.find(r => r.key === 'presentation.graph_screen').value.locales;
const t = (id, params = {}) => typeof pl[id] === 'string' ? pl[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? '') : id;

// The chain as the real server drew it for setting:temperature (probe of 2026-10-21) with a conversation value on top.
const chain = [
  { level: 'engine', label: 'Silnik (niezmienniki)', role: 'none', active: true },
  { level: 'app_preset', label: 'Preset aplikacji', role: 'none', active: true },
  { level: 'installation', label: 'Instalacja', role: 'overridden', active: true, value: 0.7 },
  { level: 'organization', label: 'Organizacja', role: 'none', active: true, unit_level: true, no_unit: true },
  { level: 'team', label: 'Zespół', role: 'none', active: true, unit_level: true, no_unit: true },
  { level: 'person', label: 'Osoba', role: 'overridden', active: true, value: 0.5 },
  { level: 'project', label: 'Projekt (gałąź)', role: 'none', active: true, unit_level: true, no_unit: true },
  { level: 'conversation', label: 'Rozmowa', role: 'supplied', active: true, value: 0.3, scope_id: 'c_1' },
  { level: 'turn', label: 'Tura / wywołanie', role: 'inactive', active: false },
];
const field = { address: 'setting:temperature', value: 0.3, levels: ['person', 'turn'], inheritance: chain, attrs: { key: 'temperature', type: 'number' } };

test('the chain keeps every level, highlights the supplier and says "bez projektu" instead of skipping the unit', () => {
  const rows = card.chainRows(chain, t);
  assert.equal(rows.length, chain.length);
  assert.deepEqual(rows.filter(r => r.winning).map(r => r.level), ['conversation']);
  assert.equal(rows.find(r => r.level === 'project').noUnitText, pl['inheritance.no_unit.project']);
  assert.equal(rows.find(r => r.level === 'person').roleText, pl['inheritance.role.overridden']);
  // numbers in the chain are written as the kernel's value_human writes them: the decimal separator is the screen's word (pl ",")
  assert.equal(rows.find(r => r.level === 'person').valueText, '0,5');
  assert.equal(rows.find(r => r.level === 'installation').valueText, '0,7');
  assert.equal(card.numberText(4096, t), '4096');
  assert.equal(card.numberText(0.7, (id) => id), '0.7', 'without the word the point stays');
  assert.equal(rows.find(r => r.level === 'turn').active, false);
});

test('r2:b1: no chain → no rows (the card then says card.no_value, no open "Skąd jest wartość" header); a profile slot names the interview', () => {
  assert.deepEqual(card.chainRows(undefined, t), []);
  assert.deepEqual(card.chainRows([], t, { chip: { id: 'rom', label: 'Preset (ROM)' } }), []);
  for (const id of ['card.no_value', 'card.profile.answer_in_interview', 'card.profile.answered_in_interview']) assert.ok(typeof pl[id] === 'string' && pl[id].length > 0, id);
  assert.ok(pl['card.profile.answer_in_interview'].includes('Warsztat'), 'the sentence names the path the owner taps');
});

test('r2:b1: the kernel\'s real card of temperature (captured by e2e/m1-navigator-native.mjs): the chip, the chain and Ustawienia give ONE answer to "moje czy preset?"', async () => {
  const real = JSON.parse(await readFile(new URL('./fixtures/graph-card-temperature.json', import.meta.url), 'utf8'));
  assert.equal(real.provenance.chip.same_as_preset, true, 'the fixture is the untouched install: the person value is the preset copied into config.json');
  assert.equal(real.provenance.chip.label, pl['chip.same_as_preset'], 'the chip says "preset zapisany w Twoich ustawieniach"');
  const rows = card.chainRows(real.inheritance, t, real.provenance);
  const winner = rows.find(r => r.winning);
  assert.equal(winner.level, 'person');
  assert.equal(winner.roleText, real.provenance.chip.label, 'the winning row says the same words as the chip, not "daje wartość"');
  assert.equal(winner.roleText, pl['inheritance.role.same_as_preset']);
  const installation = rows.find(r => r.level === 'installation');
  assert.equal(installation.valueText, winner.valueText, 'the preset holds the same number');
  assert.equal(installation.roleText, pl['inheritance.role.same_value'], 'an equal lower value is "ta sama wartość", not "przesłonięte"');
  assert.equal(installation.role, 'overridden', 'the kernel\'s role is kept as data (data-role) — only the words change');
  // Ustawienia counts "Twoje" without same_as_preset fields (settings-human-state isYours): the same fact, the third screen agrees.
  // Without the chip (a value you typed) the words are the roles again:
  const yours = card.chainRows(real.inheritance, t, { chip: { id: 'person', label: 'Ty' } });
  assert.equal(yours.find(r => r.winning).roleText, pl['inheritance.role.supplied']);
  assert.equal(yours.find(r => r.level === 'installation').roleText, pl['inheritance.role.same_value'], 'an equal value below is still the same value');
  // a different lower value stays "przesłonięte"
  const changed = card.chainRows(chain, t, { chip: { id: 'conversation', label: 'Ta rozmowa' } });
  assert.equal(changed.find(r => r.level === 'person').roleText, pl['inheritance.role.overridden']);
});

test('the editor follows the declared type, else the value; parsing says what is wrong in a word of the catalogue', () => {
  assert.equal(card.editorKind(field), 'number');
  assert.equal(card.editorKind({ value: true }), 'toggle');
  assert.equal(card.editorKind({ value: 'x', attrs: { type: 'string' } }), 'text');
  assert.equal(card.editorKind({ value: { a: 1 }, attrs: { type: 'object' } }), 'json');
  assert.deepEqual(card.parseDraft('number', '0,3'), { value: 0.3 });
  assert.deepEqual(card.parseDraft('number', 'abc'), { error: 'card.invalid_number' });
  assert.ok(pl['card.invalid_number']);
  assert.deepEqual(card.parseDraft('json', '{'), { error: 'card.invalid_json' });
  assert.deepEqual(card.parseDraft('toggle', 'true'), { value: true });
  assert.equal(card.draftOf('json', { a: 1 }), '{\n  "a": 1\n}');
});

test('"Dla kogo?" lists the chain with the field\'s own levels marked and the way each one is written', () => {
  const levels = card.levelChoices(field, 'c_1', t);
  assert.deepEqual(levels.filter(l => l.declared).map(l => l.level), ['person', 'turn']);
  const by = Object.fromEntries(levels.map(l => [l.level, l]));
  assert.deepEqual(by.person.write, { kind: 'settings.set' });
  assert.deepEqual(by.conversation.write, { kind: 'rules.set', scope: 'conversation', scope_id: 'c_1' });
  assert.equal(by.project.write.kind, 'unavailable');
  assert.equal(by.project.write.reasonText, pl['inheritance.no_unit.project']);
  assert.equal(by.turn.write.reasonText, pl['card.level.turn_planned']);
  assert.equal(by.installation.write.reasonText, pl['card.level.baseline']);
  assert.equal(by.engine.label, 'Silnik (niezmienniki)', 'the label is the kernel\'s');
  assert.equal(card.levelChoices(field, null, t).find(l => l.level === 'conversation').write.reasonText, pl['card.level.no_conversation']);
  // A declared level the chain does not draw is still offered (never dropped silently), with the chip word as its label.
  const extra = card.levelChoices({ levels: ['person', 'installation'], inheritance: [] }, null, t);
  assert.deepEqual(extra.map(l => l.level), ['person', 'installation']);
  assert.equal(extra[1].label, pl['chip.installation']);
});

test('a write is settings.set for the person level and rules.set with set_value for a scoped one', () => {
  const levels = card.levelChoices(field, 'c_1', t);
  assert.deepEqual(card.writeRequest(levels.find(l => l.level === 'person'), 'temperature', 0.3),
    { kind: 'settings.set', run: { action: 'settings.set', args: { key: 'temperature', value: 0.3 } } });
  assert.deepEqual(card.writeRequest(levels.find(l => l.level === 'conversation'), 'temperature', 0.3),
    { kind: 'rules.set', rule: { scope: 'conversation', scope_id: 'c_1', effect: 'set_value', key: 'temperature', value: 0.3 } });
  assert.equal(card.writeRequest(levels.find(l => l.level === 'turn'), 'temperature', 0.3), null);
});

test('"Zdejmij wartość" names the layer and its value, says what returns, and takes only a rule it found', () => {
  const rules = [{ id: 'user:conversation:c_1:value:temperature', scope: 'conversation', scope_id: 'c_1', effect: 'set_value', target: { kind: 'setting', key: 'temperature' }, value: 0.3, disabled: false }];
  const choices = card.unsetChoices(field, 'temperature', rules, t);
  assert.deepEqual(choices.map(c => c.level), ['person', 'conversation'], 'the installation baseline is not a layer of yours');
  const conv = choices.find(c => c.level === 'conversation');
  assert.equal(conv.text, t('card.unset', { layer: 'Rozmowa', value: '0,3' }));
  assert.equal(conv.returnsText, t('card.unset.returns', { layer: 'Osoba', value: '0,5' }));
  assert.equal(conv.ruleId, rules[0].id);
  const person = choices.find(c => c.level === 'person');
  assert.equal(person.ruleId, null);
  assert.equal(person.reasonText, pl['card.unset.person_planned']);
  assert.equal(person.returnsText, t('card.unset.returns', { layer: 'Rozmowa', value: '0,3' }));
  assert.equal(card.unsetChoices(field, 'temperature', [], t).find(c => c.level === 'conversation').reasonText, t('card.unset.no_rule', { layer: 'Rozmowa' }));
});

test('a run that waits for a confirmation becomes a gate with the reason in the language asked; outcomes are in words', () => {
  const run = { id: 'ar_1', status: 'awaiting_confirmation', pending: { receipt_id: 'arr_1', gates: [{ rule: 'confirm:rules.set', reason: { en: 'Writing a rule…', pl: 'Zapisanie reguły…' } }] } };
  assert.deepEqual(card.pendingGate(run, 'pl'), { runId: 'ar_1', receiptId: 'arr_1', reasons: ['Zapisanie reguły…'] });
  assert.equal(card.pendingGate(run, 'de').reasons[0], 'Writing a rule…', 'falls back to English, never empty');
  assert.equal(card.pendingGate({ id: 'x', status: 'completed' }, 'pl'), null);
  assert.deepEqual(card.runOutcome({ id: 'x', status: 'completed' }, t), { ok: true });
  assert.deepEqual(card.runOutcome({ id: 'x', status: 'failed', error: { message: 'boom' } }, t), { ok: false, text: t('card.write_error', { reason: 'boom' }) });
});

test('relations keep only groups with members; actions say planned from the kernel\'s implementation', () => {
  const groups = card.relationGroups({ relations: { a: { label: 'A', meaning: null, out: [], in: [] }, b: { label: 'B', meaning: 'm', out: [{ address: 'x', label: 'X' }], in: [] } } });
  assert.deepEqual(groups.map(g => g.kind), ['b']);
  assert.equal(groups[0].inverseLabel, null, 'a kind without an inverse name: one heading, arrows mark the direction');
  const named = card.relationGroups({ relations: { part_of: { label: 'jest częścią', meaning: 'm', inverse: 'contains', inverse_label: 'zawiera', out: [], in: [{ address: 'y', label: 'Y' }] } } });
  assert.equal(named[0].inverseLabel, 'zawiera', 'the in-rows are headed by the inverse kind\'s name (r1:b5)');
  const rows = card.actionRows({ actions: [{ id: 's', decision: 'applicable', label: 'Zmień', implementation: 'implemented' }, { id: 'p', decision: 'needs_confirmation', label: 'Zapisz', implementation: 'planned' }] }, t);
  assert.deepEqual(rows.map(r => r.planned), [false, true]);
  assert.equal(rows[1].decisionText, pl['card.decision.needs_confirmation']);
  assert.equal(card.valueText(true, t), pl['card.value.on']);
  assert.equal(card.valueText('', t), pl['card.value.empty']);
});

test('"Wartość:" never shows a raw status id: a ROM item (value = status) leaves the line to the status chip; value_human wins; "Warstwy:" yields to the chain and to a single layer', () => {
  // The card of a ROM item as the server answers it (probe of 2026-10-21): value and status carry the same id.
  assert.equal(card.valueLine({ value: 'partial', status: 'partial' }, t), null);
  assert.equal(card.valueLine({ value: 'partial', status: 'partial', value_human: 'częściowo' }, t), 'częściowo');
  assert.equal(card.valueLine({ value: 0.3 }, t), '0,3');
  assert.equal(card.valueLine({ value: true }, t), pl['card.value.on']);
  assert.equal(card.valueLine({ value: 'present', status: 'partial' }, t), 'present');   // a real string value that only looks like a status id
  assert.equal(card.valueLine({}, t), null);
  const prov = (...layers) => ({ provenance: { layer: layers[0], layers } });
  assert.equal(card.layersLine({ ...prov('installation', 'person'), inheritance: chain }, t), null, 'the chain is the one statement of provenance');
  assert.equal(card.layersLine(prov('rom'), t), null, 'one layer = the chip');
  assert.equal(card.layersLine({ ...prov('installation', 'person'), inheritance: [] }, t), `${pl['card.layers'].replace('{{layers}}', `${pl['chip.installation']} → ${pl['chip.person']}`)}`);
  assert.equal(card.layersLine(prov('rom', 'overlay:graph_entry'), t), pl['card.layers'].replace('{{layers}}', `${pl['chip.rom']} → ${pl['chip.overlay']}`), 'a suffixed layer takes the word of its family, never its id');
});

test('r2:b4: the level meaning from the kernel catalogue is the second line of a chain row and of a "Dla kogo?" row; absent = null, never a made-up word', async () => {
  const withMeaning = chain.map(l => l.level === 'installation' ? { ...l, meaning: 'ten telefon albo ta kopia aplikacji' } : l);
  const rows = card.chainRows(withMeaning, t);
  assert.equal(rows.find(r => r.level === 'installation').meaningText, 'ten telefon albo ta kopia aplikacji');
  assert.equal(rows.find(r => r.level === 'person').meaningText, null, 'no meaning in the data → no line');
  assert.equal(card.chainRows(withMeaning.map(l => ({ ...l, meaning: '' })), t).find(r => r.level === 'installation').meaningText, null, 'an empty string is no meaning');
  const levels = card.levelChoices({ ...field, inheritance: withMeaning }, 'c_1', t);
  assert.equal(levels.find(l => l.level === 'installation').meaningText, 'ten telefon albo ta kopia aplikacji', 'the same words on the "Dla kogo?" row');
  assert.equal(levels.find(l => l.level === 'turn').meaningText, null);
  // the real card of the kernel (fixture written by e2e/m1-navigator-native.mjs): every level of the shipped catalogue has one line
  const real = JSON.parse(await readFile(new URL('./fixtures/graph-card-temperature.json', import.meta.url), 'utf8'));
  for (const r of card.chainRows(real.inheritance, t, real.provenance)) assert.ok(r.meaningText && r.meaningText.length > 0, `${r.level} has its meaning in the fixture`);
});
