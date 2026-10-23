// M1-G1b (2): attachment chips for every role, with the state only where the message carries data for it (the export's `resolved`).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const chips = await loader.load('src/content/attachment-chips.ts');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const locales = pack.entries.find(r => r.key === 'presentation.ui_shell').value.locales;
const translator = language => id => locales[language][id] ?? id;

// the attachments of the synthetic Claude.ai archive as the real kernel imported them (probe 2026-10-23): names kept, no bytes, makieta.png listed under /files and /files_v2
const exportAtt = [
  { source: 'attachments', path: '/attachments/0', id: 'att-1', name: 'notatki.txt', mime: 'text/plain', size: 22, has_extracted_content: true, resolved: false },
  { source: 'files', path: '/files/0', id: 'f-uuid-1', name: 'makieta.png', mime: 'image', has_extracted_content: false, resolved: false },
  { source: 'files_v2', path: '/files_v2/0', id: 'f-uuid-1', name: 'makieta.png', mime: 'image', has_extracted_content: false, resolved: false },
];
const imported = (role, attachments, refs = []) => ({ id: 'm_1', conv_id: 'c_1', role, text: 'x', status: 'active', created: null, attachments: refs,
  metadata: { export: { provider: 'anthropic', raw: {}, blocks: [], attachments, pointers: [], citations: [], artifacts: [] } } });

test('an imported user message shows one chip per attachment the kernel counted, each saying names only', () => {
  const list = chips.attachmentChips(imported('user', exportAtt));
  assert.deepEqual(list.map(c => [c.name, c.state]), [['notatki.txt', 'names_only'], ['makieta.png', 'names_only'], ['makieta.png', 'names_only']]);
  assert.equal(chips.chipStateText(list[0], translator('pl')), 'tylko nazwa, bez pliku');
  assert.equal(chips.chipStateText(list[0], translator('en')), 'name only, no file');
});

test('the role does not matter: an assistant message with attachments gets its chips too', () => {
  for (const role of ['user', 'assistant', 'system', 'tool']) assert.equal(chips.attachmentChips(imported(role, exportAtt)).length, 3, role);
});

test('the state is what the export says: bytes found = saved, not found = names only, nothing said = plain (no claim)', () => {
  const list = chips.attachmentChips(imported('assistant', [{ name: 'a.pdf', resolved: true }, { name: 'b.pdf', resolved: false }, { name: 'c.pdf' }]));
  assert.deepEqual(list.map(c => c.state), ['saved', 'names_only', 'plain']);
  assert.equal(chips.chipStateText(list[0], translator('pl')), 'plik zapisany w aplikacji');
  assert.equal(chips.chipStateText(list[2], translator('pl')), null, 'a chip with no data says nothing');
  assert.equal(chips.chipStateText(list[0], id => id), null, 'no catalogue: no id on the screen');
});

test('stored references of a message are chips with their names and no state claimed; the same name in the export joins them with the export\'s state', () => {
  const own = { id: 'm_2', conv_id: 'c_1', role: 'user', text: 'x', status: 'active', created: null, attachments: ['blob:abc123/raport.pdf', '/data/attachments/zdjecie.png'] };
  assert.deepEqual(chips.attachmentChips(own).map(c => [c.name, c.state]), [['raport.pdf', 'plain'], ['zdjecie.png', 'plain']]);
  const both = chips.attachmentChips(imported('user', [{ name: 'raport.pdf', resolved: true }, { name: 'inny.txt', resolved: false }], ['blob:abc123/raport.pdf']));
  assert.deepEqual(both.map(c => [c.name, c.state]), [['raport.pdf', 'saved'], ['inny.txt', 'names_only']]);
});

test('no attachments, no metadata, foreign values: no chips and no crash', () => {
  for (const m of [{}, { attachments: [] }, { attachments: 'x' }, { attachments: [1, null] }, { metadata: {} }, { metadata: { export: 5 } }, { metadata: { export: { attachments: 'no' } } }])
    assert.deepEqual(chips.attachmentChips(m), [], JSON.stringify(m));
});

test('property: refs and export attachments add up (seeded)', () => {
  let seed = 11;
  const next = n => { seed = (seed * 1103515245 + 12345) >>> 0; return seed % n; };
  for (let round = 0; round < 200; round++) {
    const names = ['a', 'b', 'c', 'd'];
    const refs = Array.from({ length: next(3) }, () => 'blob:h/' + names[next(4)]);
    const att = Array.from({ length: next(4) }, () => ({ name: names[next(4)], resolved: [true, false, null][next(3)] }));
    const list = chips.attachmentChips(imported('user', att, refs));
    const named = new Set(refs.map(r => r.slice(7)));
    const expected = refs.length + att.filter(a => !(named.has(a.name) && refs.length > 0)).length;
    assert.equal(list.length, expected, JSON.stringify({ refs, att }));
  }
});

test('the state words exist in both languages', () => {
  for (const language of ['pl', 'en']) for (const id of ['att_saved', 'att_names_only']) assert.ok(locales[language][id], `${language}: ${id}`);
});
