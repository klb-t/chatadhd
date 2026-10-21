// M1-W0 (3) P5: the state of the human settings screen (src/components/settings-human-state.ts) without a browser. The groups and
// fields come from the PACK (loom/data/runtime/settings_fields.pack), never from a literal: the counts of the groups must add up to
// the number of fields the pack declares (config + secret fields + profile values), every field must have a group, "yours" follows
// the provenance the kernel sends, the plural form comes from the language, the subgroup labels come from the pack, and the words come
// from the catalogue presentation.settings_human (a missing word shows its id). The real answer of settings.resolved is compared in
// the native e2e (m1-settings-native.mjs).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const st = await loader.load('src/components/settings-human-state.ts');
const read = async rel => JSON.parse(await readFile(new URL(rel, import.meta.url), 'utf8'));
const fieldsPack = (await read('../../../data/runtime/settings_fields.pack')).defaults;
const view = await read('../../../data/profiles/conversation_view.pack');
const feature = view.entries.find(r => r.key === 'presentation.settings_human').value;
const { en, pl } = feature.locales;
const translator = words => (id, params = {}) => typeof words[id] === 'string' ? words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? '') : id;
const t = translator(pl);
const graphT = id => id;

// A settings.resolved-shaped document built from the pack: the kernel puts each field in the group its human.group names.
const baseline = { scope: 'installation', layer: 'x', chip: { id: 'installation', label: 'Preset' } };
const resolvedField = (f, provenance = baseline) => ({ key: f.key ?? `${f.profile}${f.pointer}`, address: `setting:${f.key ?? f.profile + f.pointer}`,
  human: { label: f.human.label.pl, why: f.human.why.pl, effect: f.human.effect.pl, levels: f.human.levels ?? [], group: f.human.group, subgroup: f.human.subgroup },
  value: null, value_human: 'x', provenance, inheritance: [], edit_rule: 'optional', evidence: { class: 'declared', confidence: 1, provenance: {}, human: 'preset ROM' },
  flows_reading: (f.human.flows ?? []).map(id => ({ id, title: `T ${id}` })), proposals: [] });
const packFields = [...fieldsPack.fields, ...fieldsPack.profile_values];
const docFrom = (provenanceOf = () => baseline) => ({ groups: Object.entries(fieldsPack.groups).map(([id, g]) => ({ id, label: g.label.pl, why: g.why.pl, order: g.order, provenance: {},
  fields: packFields.filter(f => f.human.group === id).map(f => resolvedField(f, provenanceOf(f))) })) });

test('every word the screen asks for exists in both languages, and the feature says it is present', () => {
  const ids = ['title', 'intro', 'loading', 'error', 'retry', 'groups.aria', 'group.open', 'group.back', 'group.empty', 'total.line', 'count.line', 'count.yours',
    'yours.help', 'filter.changed', 'filter.help', 'filter.empty', 'subgroup.none', 'field.card', 'field.value', 'field.effect', 'field.flows', 'field.flows.none',
    'field.edit', 'planned.reason', 'edit.optional', 'edit.required', 'edit.requires_confirmation', 'edit.read_only', 'status.planned', 'chip.profile', 'chip.none',
    'hidden.line', 'proposals.title', 'all_fields.show', 'all_fields.hide', 'all_fields.why'];
  for (const id of ids) { assert.equal(typeof en[id], 'string', `en ${id}`); assert.equal(typeof pl[id], 'string', `pl ${id}`); }
  assert.equal(feature.status.state, 'present');
  assert.deepEqual(Object.keys(en).filter(k => !k.startsWith('count.fields.')).sort(), Object.keys(pl).filter(k => !k.startsWith('count.fields.')).sort(), 'en and pl have the same ids');
  // the edit rules the kernel sends all have a word (the pack's edit_rules are the ones the field can carry)
  for (const rule of ['optional', 'required', 'requires_confirmation', 'read_only']) assert.equal(typeof pl[`edit.${rule}`], 'string', rule);
});

test('the counts of the groups add up to the number of fields the pack declares; every field has a group', () => {
  const declared = fieldsPack.fields.length + fieldsPack.profile_values.length;
  assert.ok(declared > 0 && Object.keys(fieldsPack.groups).length > 0, 'the pack has fields and groups');
  for (const f of packFields) assert.ok(fieldsPack.groups[f.human.group], `${f.key ?? f.pointer} has a group of the pack`);
  const doc = docFrom();
  const groups = st.summaries(doc);
  assert.equal(groups.reduce((n, g) => n + g.fields, 0), declared);
  assert.equal(st.totals(doc).fields, declared);
  assert.deepEqual(groups.map(g => g.order), [...groups.map(g => g.order)].sort((a, b) => a - b), 'the groups keep the order of the data');
  assert.deepEqual(groups.map(g => g.id), Object.entries(fieldsPack.groups).sort((a, b) => a[1].order - b[1].order).map(([id]) => id));
});

test('"yours" is a value somebody gave on a level of a person; a preset, the preset kept in config.json, a secret and code are not', () => {
  const p = (scope, extra = {}) => ({ provenance: { scope, layer: 'x', chip: { id: scope, label: scope, ...(extra.same_as_preset ? { same_as_preset: true } : {}) }, ...extra } });
  assert.equal(st.isYours(p('person')), true);
  assert.equal(st.isYours(p('conversation')), true);
  assert.equal(st.isYours(p('project')), true);
  assert.equal(st.isYours(p('person', { same_as_preset: true })), false, 'the preset kept in your settings');
  for (const scope of ['installation', 'app_preset', 'engine', 'none', 'rom']) assert.equal(st.isYours(p(scope)), false, scope);
  // a document where the first field of every group is yours: M = number of groups, the others are not counted
  const doc = docFrom();
  const marked = { groups: doc.groups.map(g => ({ ...g, fields: g.fields.map((f, i) => i === 0 ? { ...f, provenance: { scope: 'person', layer: 'config.json', chip: { id: 'person', label: 'Ty' } } } : f) })) };
  assert.deepEqual(st.summaries(marked).map(g => g.yours), marked.groups.map(g => g.fields.length ? 1 : 0));
  assert.equal(st.totals(marked).yours, marked.groups.filter(g => g.fields.length).length);
  // the filter keeps exactly the fields that are yours
  for (const g of marked.groups) assert.equal(st.fieldsToShow(g.fields, true).length, g.fields.length ? 1 : 0);
  for (const g of marked.groups) assert.equal(st.fieldsToShow(g.fields, false).length, g.fields.length);
});

test('"N pól · M Twoje": the plural form comes from the language, the word from the catalogue', () => {
  const forms = [1, 2, 4, 5, 12, 22, 25].map(n => st.countWord('fields', n, 'pl', t));
  assert.deepEqual(forms, ['1 pole', '2 pola', '4 pola', '5 pól', '12 pól', '22 pola', '25 pól']);
  assert.equal(st.countWord('fields', 1, 'en', translator(en)), '1 field');
  assert.equal(st.countWord('fields', 3, 'en', translator(en)), '3 fields');
  assert.equal(st.countLine({ fields: 6, yours: 2 }, 'pl', t), '6 pól · 2 Twoje');
  assert.equal(st.countLine({ fields: 0, yours: 0 }, 'pl', t), '0 pól · 0 Twoje');
  assert.equal(st.countWord('fields', 5, 'pl', () => 'count.fields.many'), 'count.fields.many', 'a missing word shows its id');
  assert.equal(st.countWord('fields', 5, 'pl', (id) => id), 'count.fields.many', 'the id is the one of the plural form');
});

test('subgroups: in the order they appear, labelled by the pack, an unlabelled one shows its id, fields with none go to "Ogólne"', () => {
  const g = Object.entries(fieldsPack.groups).find(([, v]) => v.subgroups.length)[1];
  const id = g.subgroups[0].id;
  const labels = st.subgroupLabels({ groups: { g: g } }, 'g', 'pl');
  assert.equal(labels[id].label, g.subgroups[0].label.pl);
  assert.equal(st.subgroupLabels({ groups: { g: g } }, 'g', 'en')[id].label, g.subgroups[0].label.en);
  assert.deepEqual(st.subgroupLabels(null, 'g', 'pl'), {}, 'without the pack the labels are empty, never invented');
  const f = (key, subgroup) => ({ key, human: { subgroup } });
  const blocks = st.subgroupBlocks([f('a', id), f('b', undefined), f('c', 'nieznana'), f('d', id)], labels, t);
  assert.deepEqual(blocks.map(b => b.label), [g.subgroups[0].label.pl, pl['subgroup.none'], 'nieznana']);
  assert.deepEqual(blocks.map(b => b.fields.map(x => x.key)), [['a', 'd'], ['b'], ['c']]);
  const flat = st.subgroupBlocks([f('a', undefined), f('b', undefined)], {}, t);
  assert.equal(flat.length, 1); assert.equal(flat[0].label, '', 'a group with no subgroup has no heading');
});

test('a field row: the kernel\'s own chip, the evidence only when it is not a plain preset, "planowane" and "profil: …", the flows by title', () => {
  const base = resolvedField(fieldsPack.fields[0]);
  assert.deepEqual(st.fieldChips(base, t, graphT).map(c => c.id), ['provenance']);
  assert.equal(st.fieldChips(base, t, graphT)[0].text, 'Preset', 'the chip text is the kernel\'s label');
  const inferred = { ...base, evidence: { class: 'inferred', confidence: 0.5, provenance: {}, human: 'wniosek modelu' }, status: 'planned', profile_value: { profile: 'user', pointer: '/x' } };
  assert.deepEqual(st.fieldChips(inferred, t, graphT).map(c => c.id), ['provenance', 'evidence', 'status', 'profile']);
  assert.equal(st.fieldChips(inferred, t, graphT).find(c => c.id === 'status').text, 'planowane');
  assert.equal(st.fieldChips(inferred, t, graphT).find(c => c.id === 'profile').text, 'profil: user');
  const noChip = { ...base, provenance: { scope: 'none', layer: 'x' } };
  assert.equal(st.fieldChips(noChip, t, graphT)[0].text, pl['chip.none'], 'no chip from the kernel: the word of the catalogue');
  assert.equal(st.isPlanned(inferred), true); assert.equal(st.isPlanned(base), false);
  assert.deepEqual(st.flowTitles({ flows_reading: [{ id: 'a', title: 'Wyślij' }, { id: 'b', title: '' }] }), ['Wyślij', 'b']);
  assert.deepEqual(st.flowTitles({ flows_reading: [] }), []);
  assert.equal(st.editText({ edit_rule: 'read_only' }, t), pl['edit.read_only']);
  assert.equal(st.editText({ edit_rule: 'zagadka' }, t), 'edit.zagadka', 'an unknown rule shows its id');
});

test('the former settings screen hangs under the group the preset names, and that group exists in the pack', () => {
  assert.ok(fieldsPack.groups[st.ALL_FIELDS_GROUP_PRESET], 'the preset group id is a group of the pack');
});
