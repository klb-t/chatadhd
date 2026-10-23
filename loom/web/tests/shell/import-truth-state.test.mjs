// M1-G1b (1): the pure part of the import result page - the kernel's `truth` (G1a) as rows of words. The shape below is the answer of the real kernel for
// the synthetic Claude.ai archive (tests/fixtures/exports/anthropic_2026_full.zip), shortened to what the screen reads.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const state = await loader.load('src/components/import-truth-state.ts');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const locales = pack.entries.find(r => r.key === 'presentation.ui_shell').value.locales;
const translator = language => (id, params = {}) => locales[language][id] === undefined ? id : locales[language][id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? '');

const el = (kind, pl, en, found, imported, status, extra = {}) => ({ kind, label: { pl, en }, found, imported, status, notes: [], reason: null, shown: status !== 'empty', ...extra });
const claude = {
  type: 'done', messages: 8,
  truth: {
    schema: 'loom.import_truth/1', path: 'export', provider: 'anthropic',
    elements: [
      el('conversations', 'Rozmowy', 'Conversations', 2, 2, 'all'), el('messages', 'Wiadomości', 'Messages', 8, 8, 'all'),
      el('attachments', 'Załączniki', 'Attachments', 3, 0, 'none', { reason: { id: 'attachments.export_without_files', text: {
        pl: 'według obserwacji z 2026-10-22 eksport Claude.ai nie zawiera plików załączników – zapisano tylko ich nazwy i opisy',
        en: 'as observed on 2026-10-22, the Claude.ai export contains no attachment files – only their names and descriptions were saved' } } }),
      el('custom_instructions', 'Własne instrukcje', 'Custom instructions', 0, 0, 'empty'), el('feedback', 'Oceny wiadomości', 'Message feedback', 0, 0, 'empty'),
    ],
    summary: { pl: 'Import zakończony. Rozmowy: 2. Wiadomości: 8. Załączniki: znaleziono 3, zapisano 0 — według obserwacji z 2026-10-22 eksport Claude.ai nie zawiera plików załączników – zapisano tylko ich nazwy i opisy.', en: 'Import finished. Conversations: 2.' },
  },
};

test('the Polish summary and the per-kind rows come from the kernel and the screen words', () => {
  const view = state.importView(claude, 'pl', translator('pl'));
  assert.equal(view.hasTruth, true);
  assert.match(view.summary, /Załączniki: znaleziono 3, zapisano 0/);
  assert.deepEqual(view.rows.map(r => [r.kind, r.label, r.foundText, r.savedText, r.status]), [
    ['conversations', 'Rozmowy', '2', '2', 'all'], ['messages', 'Wiadomości', '8', '8', 'all'], ['attachments', 'Załączniki', '3', '0', 'none']]);
  const attachments = view.rows[2];
  assert.equal(attachments.statusText, locales.pl.import_status_none);
  assert.equal(attachments.reason, 'Powód: według obserwacji z 2026-10-22 eksport Claude.ai nie zawiera plików załączników – zapisano tylko ich nazwy i opisy');
  assert.equal(view.rows[0].reason, null);
  assert.equal(view.allSaved, false, 'one kind not saved: the summary is not green');
  assert.deepEqual(view.absent, ['Własne instrukcje', 'Oceny wiadomości'], 'the kinds with nothing found are named, not dropped');
  assert.equal(state.absentLine(view, translator('pl')), 'Nie było w archiwum: Własne instrukcje, Oceny wiadomości.');
});

test('the English screen reads the English texts of the same answer', () => {
  const view = state.importView(claude, 'en', translator('en'));
  assert.equal(view.summary, 'Import finished. Conversations: 2.');
  assert.equal(view.rows[2].label, 'Attachments');
  assert.match(view.rows[2].reason, /^Reason: as observed on 2026-10-22/);
  assert.equal(view.rows[2].statusText, locales.en.import_status_none);
});

test('a language the answer does not have falls back to English, then to any text; never to an empty row', () => {
  const view = state.importView(claude, 'de', translator('en'));
  assert.equal(view.rows[0].label, 'Conversations');
  assert.equal(state.pickText({ fr: 'x' }, 'de'), 'x');
  assert.equal(state.pickText(undefined, 'pl'), '');
});

test('an older kernel (no truth) keeps the one line, in the language of the screen; without a catalogue it stays English', () => {
  assert.equal(state.importView({ type: 'done', messages: 8 }, 'pl', translator('pl')).legacyLine, 'Import zakończony. Wiadomości: 8.');
  assert.equal(state.importView({ type: 'done', messages: 8 }, 'en', translator('en')).legacyLine, 'Import finished. Messages: 8.');
  const view = state.importView({ type: 'done', messages: 8 }, 'pl', id => id);
  assert.equal(view.legacyLine, 'Imported 8 messages');
  assert.equal(view.hasTruth, false); assert.deepEqual(view.rows, []); assert.equal(view.summary, null);
  assert.equal(state.importView(null, 'pl', translator('pl')).legacyLine, 'Import zakończony. Wiadomości: 0.');
});

test('not counted is not a zero; an unknown kind and an unknown status are shown as they come', () => {
  const view = state.importView({ truth: { elements: [
    el('attachments', 'Załączniki', 'Attachments', null, 0, 'not_counted', { reason: { text: { pl: 'ten czytnik nie czyta załączników' } } }),
    el('brand_new', 'Nowy rodzaj', 'New kind', 4, 4, 'strange'),
    { kind: 'no_label', found: 1, imported: 1, status: 'all' },
  ] } }, 'pl', translator('pl'));
  assert.equal(view.rows[0].foundText, locales.pl.import_not_counted);
  assert.equal(view.rows[0].found, null);
  assert.equal(view.rows[1].label, 'Nowy rodzaj'); assert.equal(view.rows[1].statusText, 'strange');
  assert.equal(view.rows[2].label, 'no_label', 'a kind with no label shows its own name, never an empty cell');
  assert.equal(view.summary, null, 'no sentence in the answer: none invented');
  assert.equal(view.allSaved, false);
});

test('property: every element the kernel counted is a row or named as absent; order is the kernel\'s (seeded, no timing)', () => {
  let seed = 7;
  const next = n => { seed = (seed * 1103515245 + 12345) >>> 0; return seed % n; };
  const statuses = ['all', 'some', 'none', 'not_counted', 'empty', 'x'];
  for (let round = 0; round < 200; round++) {
    const elements = Array.from({ length: next(9) }, (_, i) => {
      const status = statuses[next(statuses.length)];
      return el(`k${i}`, `Rodzaj ${i}`, `Kind ${i}`, status === 'not_counted' ? null : next(5), next(5), status, { shown: next(4) !== 0 });
    });
    const view = state.importView({ truth: { elements } }, next(2) ? 'pl' : 'en', translator('pl'));
    assert.equal(view.rows.length + view.absent.length, elements.length);
    assert.deepEqual(view.rows.map(r => r.kind), elements.filter(e => e.shown).map(e => e.kind));
    assert.equal(view.allSaved, view.rows.every(r => r.status === 'all' || r.status === 'empty'));
  }
});

test('every word the page uses exists in both languages', () => {
  for (const language of ['pl', 'en']) for (const id of ['import_summary_legacy', 'import_table', 'import_col_kind', 'import_col_found', 'import_col_saved', 'import_not_counted',
    'import_status_all', 'import_status_some', 'import_status_none', 'import_status_not_counted', 'import_status_empty', 'import_reason', 'import_absent', 'import_details_show', 'import_details_hide'])
    assert.ok(locales[language][id], `${language}: ${id}`);
});
