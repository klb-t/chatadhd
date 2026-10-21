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
  assert.equal(rows.find(r => r.level === 'person').valueText, '0.5');
  assert.equal(rows.find(r => r.level === 'turn').active, false);
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
  assert.equal(conv.text, t('card.unset', { layer: 'Rozmowa', value: '0.3' }));
  assert.equal(conv.returnsText, t('card.unset.returns', { layer: 'Osoba', value: '0.5' }));
  assert.equal(conv.ruleId, rules[0].id);
  const person = choices.find(c => c.level === 'person');
  assert.equal(person.ruleId, null);
  assert.equal(person.reasonText, pl['card.unset.person_planned']);
  assert.equal(person.returnsText, t('card.unset.returns', { layer: 'Rozmowa', value: '0.3' }));
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
  const rows = card.actionRows({ actions: [{ id: 's', decision: 'applicable', label: 'Zmień', implementation: 'implemented' }, { id: 'p', decision: 'needs_confirmation', label: 'Zapisz', implementation: 'planned' }] }, t);
  assert.deepEqual(rows.map(r => r.planned), [false, true]);
  assert.equal(rows[1].decisionText, pl['card.decision.needs_confirmation']);
  assert.equal(card.valueText(true, t), pl['card.value.on']);
  assert.equal(card.valueText('', t), pl['card.value.empty']);
});
