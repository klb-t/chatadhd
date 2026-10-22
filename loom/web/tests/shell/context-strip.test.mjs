// M1-W1b: the pure parts of the context strip (src/shell/context-strip.ts), the model chip and its provenance (src/shell/model-chip.ts) and the lines
// the preview of the retrieval plan shows (src/context/retrieval-plan.ts). Words: presentation.context_receipt and presentation.ui_shell of
// conversation_view.pack (pl and en, a missing word shows its id). The kernel shapes below are the ones the real server answers (scope resolution of
// default_model, roles.resolved, the carrier content `waiting` of context.modify).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const strip = await loader.load('src/shell/context-strip.ts');
const st = await loader.load('src/components/context-receipt-state.ts');
const modifyApi = await loader.load('src/api/context-receipt.ts');
const chip = await loader.load('src/shell/model-chip.ts');
const plan = await loader.load('src/context/retrieval-plan.ts');
const pending = await loader.load('src/shell/turn-pending.ts');
const read = async rel => JSON.parse(await readFile(new URL(rel, import.meta.url), 'utf8'));
const view = await read('../../../data/profiles/conversation_view.pack');
const feature = key => view.entries.find(r => r.key === key).value;
const translator = words => (id, params = {}) => typeof words[id] === 'string' ? words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? '') : id;
const receipt = feature('presentation.context_receipt').locales, shell = feature('presentation.ui_shell').locales;
const tr = translator(receipt.pl), trEn = translator(receipt.en), ts = translator(shell.pl), tsEn = translator(shell.en);
const profiles = [{ id: 'loom', label: 'Loom (preset)', status: 'preset', why: '' }, { id: 'compat', label: 'Zgodność', status: 'calibration', why: '' }];

test('composer_context_strip: chips is the preset, a value the field does not list falls back to it', () => {
  assert.equal(strip.STRIP_PRESET, 'chips');
  assert.deepEqual(['chips', 'hidden', 'full', 'nonsense', undefined, 3].map(strip.stripMode), ['chips', 'hidden', 'full', 'chips', 'chips', 'chips']);
  const field = JSON.parse(readFileSyncText('../../../data/runtime/settings_fields.pack')).fields?.find?.(f => f.key === 'composer_context_strip')
    ?? findField(JSON.parse(readFileSyncText('../../../data/runtime/settings_fields.pack')), 'composer_context_strip');
  assert.deepEqual(field.values.map(v => v.value), ['chips', 'hidden', 'full'], 'the modes of the code are the values of the field');
});
import { readFileSync } from 'node:fs';
function readFileSyncText(rel) { return readFileSync(new URL(rel, import.meta.url), 'utf8'); }
function findField(node, key) { if (Array.isArray(node)) { for (const x of node) { const f = findField(x, key); if (f) return f; } return null; } if (node && typeof node === 'object') { if (node.key === key && node.values) return node; for (const v of Object.values(node)) { const f = findField(v, key); if (f) return f; } } return null; }

test('pending changes in words: steps counted, a profile named by its label, unknown steps keep their id', () => {
  const label = id => profiles.find(p => p.id === id)?.label ?? id;
  assert.deepEqual(strip.pendingParts({}, tr, label), []);
  assert.deepEqual(strip.pendingParts({ 'context.breadth_delta': ['wider'] }, tr, label), ['szerzej']);
  assert.deepEqual(strip.pendingParts({ 'context.breadth_delta': ['wider', 'wider'], 'context.depth_delta': ['deeper'] }, tr, label), ['szerzej ×2', 'głębiej']);
  assert.deepEqual(strip.pendingParts({ 'context.profile': 'compat' }, tr, label), ['profil Zgodność']);
  assert.deepEqual(strip.pendingParts({ 'context.breadth_delta': ['wider'] }, trEn, label), ['wider']);
  assert.deepEqual(strip.pendingParts({ 'context.breadth_delta': ['sideways'] }, tr, label), ['sideways'], 'a step with no word shows its id, never a guess');
});

test('the strip says the profile and, only when this window knows of changes, the next request', () => {
  const base = { mode: 'chips', waiting: {}, profile: { id: 'loom', label: 'Loom (preset)', status: 'preset' }, profiles };
  let v = strip.stripView(base, tr, 'pl');
  assert.equal(v.profileText, 'Profil: Loom (preset)');
  assert.equal(v.nextText, '');
  assert.equal(v.profileKnown, true);
  v = strip.stripView({ ...base, waiting: { 'context.breadth_delta': ['wider'] } }, tr, 'pl');
  assert.equal(v.nextText, 'Następne zapytanie: szerzej');
  v = strip.stripView({ ...base, profile: null }, tr, 'pl');
  assert.equal(v.profileText, receipt.pl['strip.profile_settings'], 'no profile chosen: the settings apply, said so');
  assert.equal(v.profileKnown, false);
  v = strip.stripView({ ...base, mode: 'full', waiting: { 'context.breadth_delta': ['wider', 'wider'] } }, tr, 'pl');
  assert.match(v.fullText, /Zmiany tylko na nie: szerzej ×2\. Po jego wysłaniu wszystko wraca do ustawień\./);
  v = strip.stripView({ ...base, mode: 'full' }, tr, 'pl');
  assert.match(v.fullText, /nie ma\. Zmiany zrobione w innym oknie widać dopiero w śladzie/);
  assert.equal(strip.stripView({ ...base, profile: { id: 'compat', label: 'Zgodność', status: 'calibration' } }, tr, 'pl').profileText, 'Profil: Zgodność (kalibracja)', 'the status is added when the label does not say it');
  v = strip.stripView({ ...base, waiting: { 'context.profile': 'compat' } }, tr, 'pl');
  assert.equal(v.nextText, 'Następne zapytanie: profil Zgodność');
  assert.equal(strip.stripView(base, trEn, 'en').profileText, 'Profile: Loom (preset)');
});

test('the profile of the chain, else of the last receipt: the chosen profile and the last receipt are read, nothing is invented', () => {
  assert.deepEqual(strip.chosenProfile('compat', profiles), { id: 'compat', label: 'Zgodność', status: 'calibration' });
  assert.deepEqual(strip.chosenProfile('mine', profiles), { id: 'mine', label: 'mine', status: '' }, 'a profile the list does not have is shown by its id');
  assert.equal(strip.chosenProfile(null, profiles), null);
  assert.equal(strip.chosenProfile('', profiles), null);
  const rec = { schema: 'loom.context_receipt/1', sections: [], profile: { id: 'loom', label: { pl: 'Loom (preset)', en: 'Loom (preset)' }, status: 'preset', source: 'settings' } };
  const messages = [{ id: 'm_1', role: 'user', metadata: { context_trace: { receipt: rec } } }, { id: 'm_2', role: 'assistant', parent_id: 'm_1' }];
  assert.deepEqual(strip.lastReceiptProfile(messages, 'pl'), { id: 'loom', label: 'Loom (preset)', status: 'preset' });
  assert.equal(strip.lastReceiptProfile([{ id: 'm_1', role: 'user', metadata: {} }], 'pl'), null, 'a message with no receipt tells nothing');
  assert.equal(strip.lastReceiptProfile([], 'pl'), null);
});

test('the pending store: set, clear after a send, an empty answer clears, listeners are told', () => {
  let told = 0; const off = pending.subscribePendingTurn(() => told++);
  assert.deepEqual(pending.pendingTurn('c_1'), {});
  pending.setPendingTurn('c_1', { 'context.breadth_delta': ['wider'] });
  assert.deepEqual(pending.pendingTurn('c_1'), { 'context.breadth_delta': ['wider'] });
  assert.deepEqual(pending.pendingTurn('c_2'), {}, 'the carrier is per conversation');
  pending.clearPendingTurn('c_2', null);
  assert.deepEqual(pending.pendingTurn('c_1'), { 'context.breadth_delta': ['wider'] });
  pending.clearPendingTurn('c_1');
  assert.deepEqual(pending.pendingTurn('c_1'), {});
  pending.setPendingTurn(null, { 'context.profile': 'compat' });
  assert.deepEqual(pending.pendingTurn(null), { 'context.profile': 'compat' }, 'a conversation not opened yet is the key ""');
  pending.setPendingTurn(null, {});
  assert.deepEqual(pending.pendingTurn(null), {});
  assert.ok(told >= 4);
  off();
});

test('the preview of the retrieval plan says the profile and the pending changes in the same words', () => {
  assert.deepEqual(plan.previewScopeLines([], { label: 'Loom (preset)', status: 'preset' }, tr),
    ['Profil następnego zapytania: Loom (preset)', 'Zmiany na następne zapytanie: brak (o których ten ekran wie)']);
  assert.deepEqual(plan.previewScopeLines(['szerzej'], null, tr), [receipt.pl['strip.profile_settings'], 'Zmiany na następne zapytanie: szerzej']);
});

// ---- the model chip ----
test('the chip shows a short name: the part after the vendor, whole when there is no vendor', () => {
  assert.equal(chip.shortModelName('anthropic/claude-sonnet-4-20250514'), 'claude-sonnet-4-20250514');
  assert.equal(chip.shortModelName('mock/e2e-model'), 'e2e-model');
  assert.equal(chip.shortModelName('llama3'), 'llama3');
  assert.equal(chip.shortModelName('openrouter/meta/llama-3'), 'llama-3');
  assert.equal(chip.shortModelName(''), '');
  assert.equal(chip.shortModelName('vendor/'), 'vendor/', 'a trailing slash leaves the id whole');
});

const chain = [
  { level: 'installation', label: { pl: 'Instalacja', en: 'Installation' }, role: 'supplied' },
  { level: 'person', label: { pl: 'Osoba', en: 'Person' }, role: 'overridden' },
  { level: 'conversation', label: { pl: 'Rozmowa', en: 'Conversation' }, role: 'supplied' },
];
const answerRole = { id: 'chat.default', model: 'mock/e2e-model', tier: { id: 'mid', label: 'średni', reason: '' }, cost: { per_million: { input: 3, output: 15, currency: 'USD' } } };
const lines = v => Object.fromEntries(v.lines.map(l => [l.id, l.text]));

test('provenance: the level of the chain that set the model, in words; the chain is listed as the kernel gave it', () => {
  const v = chip.provenanceView({ model: 'mock/e2e-model', source: 'conversation', resolution: { key: 'default_model', defined: true, value: 'mock/e2e-model', scope: 'conversation', chain },
    resolutionError: null, role: answerRole, info: undefined }, ts, 'pl');
  const l = lines(v);
  assert.equal(l.model, 'Model: mock/e2e-model');
  assert.equal(l['set-by'], 'Ustawiony na poziomie: Rozmowa');
  assert.equal(l.class, 'Klasa: średni');
  assert.deepEqual(v.chain.map(c => c.text), ['Instalacja — podał wartość', 'Osoba — przykryty bardziej szczegółowym', 'Rozmowa — podał wartość']);
  assert.equal(lines(chip.provenanceView({ model: 'mock/e2e-model', source: 'conversation', resolution: { key: 'default_model', defined: true, value: 'mock/e2e-model', scope: 'person', chain }, resolutionError: null, role: answerRole, info: undefined }, tsEn, 'en'))['set-by'], 'Set at the level: Person');
});

test('provenance: a model picked in this window is said so and is not a level; no level set it = the preset; an unreadable chain says why', () => {
  const resolution = { key: 'default_model', defined: false, chain };
  assert.equal(lines(chip.provenanceView({ model: 'a/b', source: 'window', resolution, resolutionError: null, role: null, info: undefined }, ts, 'pl'))['set-by'], shell.pl.mp_set_by_window);
  assert.equal(lines(chip.provenanceView({ model: '', source: 'chain', resolution, resolutionError: null, role: null, info: undefined }, ts, 'pl'))['set-by'], shell.pl.mp_set_by_none);
  assert.equal(lines(chip.provenanceView({ model: '', source: 'chain', resolution: null, resolutionError: 'timeout', role: null, info: undefined }, ts, 'pl'))['set-by'], 'Pochodzenia nie udało się odczytać: timeout');
  assert.equal(lines(chip.provenanceView({ model: '', source: 'chain', resolution, resolutionError: null, role: null, info: undefined }, ts, 'pl')).model, shell.pl.mp_model_default);
});

test('class and price: from the role when it is the same model, from the model list first; otherwise "nieznany - brak ceny", never zero', () => {
  const same = { model: 'mock/e2e-model', source: 'chain', resolution: null, resolutionError: null, role: answerRole, info: undefined };
  assert.equal(lines(chip.provenanceView(same, ts, 'pl')).price, 'Cena za 1 mln tokenów: $3 wejście, $15 wyjście');
  const listed = chip.provenanceView({ ...same, info: { id: 'mock/e2e-model', pricing: { prompt: '0.000001', completion: '0.000002' } } }, ts, 'pl');
  assert.equal(lines(listed).price, 'Cena za 1 mln tokenów: $1 wejście, $2 wyjście', 'the model list wins');
  const none = chip.provenanceView({ ...same, role: null }, ts, 'pl');
  assert.equal(lines(none).price, shell.pl.mp_price_unknown);
  assert.equal(shell.pl.mp_price_unknown, 'Cena: nieznany — brak ceny');
  assert.equal(lines(chip.provenanceView({ ...same, role: null, info: { id: 'x', pricing: { prompt: '0', completion: '0' } } }, ts, 'pl')).price, shell.pl.mp_price_unknown, 'a zero is not a price');
  assert.equal(lines(chip.provenanceView({ ...same, role: { ...answerRole, tier: { id: 'none', label: '', reason: 'brak modelu' } } }, ts, 'pl')).class, 'Klasa: nieznana — brak modelu');
  // the role belongs to another model: its class and cost are not attributed to the one shown
  const other = chip.provenanceView({ ...same, model: 'a/other' }, ts, 'pl');
  assert.match(lines(other).class, /^Klasa: nieznana — ta klasa dotyczy modelu mock\/e2e-model/);
  assert.equal(lines(other).price, shell.pl.mp_price_unknown);
});

test('every word the strip, the menu help and the sheet use exists in both languages', () => {
  const ids = ['strip.aria', 'strip.profile', 'strip.profile_settings', 'strip.profile_with_status', 'strip.next', 'strip.full.profile', 'strip.full.profile_settings', 'strip.full.changes', 'strip.full.no_changes', 'preview.profile', 'preview.pending', 'preview.pending_none'];
  for (const id of ids) for (const words of [receipt.pl, receipt.en]) assert.equal(typeof words[id], 'string', id);
  for (const id of Object.keys(shell.pl).filter(k => /^(mp_|pv_)/.test(k))) assert.equal(typeof shell.en[id], 'string', `${id} in English`);
  for (const id of Object.keys(shell.en).filter(k => /^(mp_|pv_)/.test(k))) assert.equal(typeof shell.pl[id], 'string', `${id} in Polish`);
  assert.deepEqual(Object.keys(receipt.en).sort(), Object.keys(receipt.pl).sort());
});

test('the sentence after a change is the pack\'s, in the screen\'s language, not the kernel\'s; the measured size is added only when the kernel measured it', () => {
  const applied = { waiting: { 'context.breadth_delta': ['wider'] }, ratio: 1.12 };
  assert.equal(st.modifySentence(applied, 'szerzej', tr), 'Gotowe: tylko dla następnego zapytania — szerzej. Po jego wysłaniu wszystko wraca do ustawień. Szacunek: następne zapytanie będzie ok. 1.1× zwykłego.');
  assert.equal(st.modifySentence({ waiting: {}, ratio: null }, 'szerzej', tr), 'Gotowe: tylko dla następnego zapytania — szerzej. Po jego wysłaniu wszystko wraca do ustawień.');
  assert.equal(st.modifySentence({ waiting: {}, ratio: null }, '', tr), 'Cofnięte: następne zapytanie pójdzie bez zmian.', 'an empty carrier is a change taken back');
  assert.equal(st.modifySentence(applied, 'wider', trEn), 'Done: for the next request only — wider. Once it is sent everything is back to the settings. Estimate: the next request will be about 1.1× the usual one.');
  // the run document of the real kernel (K1b): completed, data.waiting and data.guard.ratio
  const run = { id: 'r1', status: 'completed', result: { parts: [{ text: 'Done: ...' }], data: { waiting: { 'context.breadth_delta': ['wider'] }, guard: { ratio: 1.12, asked: false } } } };
  assert.deepEqual(modifyApi.readModifyRun(run), { kind: 'applied', text: 'Done: ...', waiting: { 'context.breadth_delta': ['wider'] }, ratio: 1.12 });
  assert.equal(modifyApi.readModifyRun({ ...run, result: { parts: [], data: { waiting: {} } } }).ratio, null, 'no measurement: no ratio, never a guess');
});
