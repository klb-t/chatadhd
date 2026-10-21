// M1-W1a: the state of the sheet "Co wysłaliśmy" (src/components/context-receipt-state.ts) and the readers of context.receipt / context.modify
// (src/api/context-receipt.ts). The receipts below have the shape the real kernel answers (captured from loom-server, K1b): a receipt of the
// first message of a conversation, one with a graph section and a selected profile, and a run document of each action. Words: presentation.
// context_receipt of conversation_view.pack, both languages, a missing word shows its id. Cross-checks against data: the steps of "Następnym razem"
// are the steps of action_rules.pack (scoped_values), the profile choices are the rows of chat_context.pack, every source kind and reason the
// kernel writes today has a word.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const st = await loader.load('src/components/context-receipt-state.ts');
const api = await loader.load('src/api/context-receipt.ts');
const read = async rel => JSON.parse(await readFile(new URL(rel, import.meta.url), 'utf8'));
const view = await read('../../../data/profiles/conversation_view.pack');
const feature = view.entries.find(r => r.key === 'presentation.context_receipt').value;
const { en, pl } = feature.locales;
const translator = words => (id, params = {}) => typeof words[id] === 'string' ? words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? '') : id;
const t = translator(pl), tEn = translator(en);
const rules = (await read('../../../data/runtime/action_rules.pack')).defaults.scoped_values.keys;
const ctx = (await read('../../../data/runtime/chat_context.pack')).defaults;

const base = {
  schema: 'loom.context_receipt/1',
  order: { name: 'stability', classes: ['system', 'profile', 'rules', 'memory', 'project', 'history', 'graph'] },
  profile: { id: null, source: 'settings' },
  profile_prefix: { level: 'normal', slots: 0, unavailable: 'no_profile' },
  modifiers: { profile: null, breadth_delta: null, depth_delta: null, scope: null },
  guard: { resource: 'input_bytes', bytes: 282, baseline: null, ratio: null, growth_factor: null, asked: false, unknown_reason: 'no_usage_ledger_yet' },
  flow: { flow: 'plain', renderer: 'chat_turns', output: 'text', mode: '', model_class: 'unknown', why: 'class unknown' },
  renderer: 'chat_turns',
  sections: [
    { class: 'system', items: [{ ref: 'system_prompt', label: 'instruction', source_kind: 'instruction', why: 'the instruction of the application or of the conversation', tokens_est: 19 }], tokens_est: 19 },
    { class: 'profile', items: [], tokens_est: 0 }, { class: 'rules', items: [], tokens_est: 0 }, { class: 'memory', items: [], tokens_est: 0 },
    { class: 'project', items: [], tokens_est: 0 }, { class: 'history', items: [], tokens_est: 0 }, { class: 'graph', items: [], tokens_est: 0 },
    { class: 'question', items: [{ ref: 'question', label: 'the question', source_kind: 'question', why: 'what the person wrote in this turn', tokens_est: 9 }], tokens_est: 9 },
  ],
  dropped: [], tokens_est: 28, sha256: 'f880', truncated: false,
};
const rich = {
  ...base,
  profile: { id: 'compat', label: { pl: 'Zgodność', en: 'Compatibility' }, source: 'turn', status: 'preset', applied: {}, unapplied: [] },
  modifiers: { profile: 'compat', breadth_delta: ['wider', 'wider'], depth_delta: ['deeper'], scope: 'turn' },
  guard: { ...base.guard, baseline: 585.5, ratio: 1.5303159692570452, growth_factor: 10, asked: false, status: 'allowed', unknown_reason: undefined },
  sections: [
    base.sections[0],
    { class: 'history', items: [{ ref: 'm_1', label: 'user', source_kind: 'message:L0', why: 'earlier turn', tokens_est: 5 }, { ref: 'm_2', label: 'assistant', source_kind: 'message:L2', why: '', tokens_est: 7 }], tokens_est: 12 },
    { class: 'graph', items: [{ ref: 'graph_memory', label: 'graph context', source_kind: 'graph_node', why: 'nodes of the graph related to the question', tokens_est: 64 }], tokens_est: 64 },
    { class: 'memory', items: [], tokens_est: 0 },
    base.sections[7],
  ],
  dropped: [{ ref: 'memory', reason: 'source_off', count: 1, refs: [] }, { ref: 'history', reason: 'history_window', count: 3, refs: ['a', 'b', 'c'] }, { ref: 'history', reason: 'brand_new_reason', count: 2, refs: [] }],
  tokens_est: 100,
};

test('the words: both languages have the same keys and every placeholder of one is in the other', () => {
  assert.deepEqual(Object.keys(pl).sort(), Object.keys(en).sort());
  for (const key of Object.keys(pl)) assert.deepEqual([...pl[key].matchAll(/\{\{(\w+)\}\}/g)].map(m => m[1]).sort(), [...en[key].matchAll(/\{\{(\w+)\}\}/g)].map(m => m[1]).sort(), key);
});

test('every source kind, section and reason the kernel writes today has a word', () => {
  const kinds = ['instruction', 'rule', 'memory_node', 'graph_node', 'kb_claim', 'profile_slot', 'graph_packet', 'task', 'question', 'message:L0', 'message:L1', 'message:L2', 'message:L3'];
  for (const k of kinds) assert.notEqual(st.itemChip(k, t), k, `chip for ${k}`);
  for (const c of ['system', 'profile', 'rules', 'memory', 'project', 'history', 'graph', 'task', 'question']) assert.notEqual(st.sectionTitle(c, t), c, `section ${c}`);
  for (const r of ['thread_filter', 'representation_level', 'budget', 'excluded', 'history_window', 'source_off', 'command_traffic', 'replaced_by_task', 'history_skip']) assert.ok(pl[`dropped.${r}`], `reason ${r}`);
  for (const ref of ['system_prompt', 'guardian_rules', 'memory_tree', 'graph_memory', 'unified_context', 'knowledge_context', 'active_task', 'graph_reply_instruction', 'question']) assert.ok(pl[`ref.${ref}`], `ref ${ref}`);
});

test('a receipt of the first message: the sheet names the empty sections in one line and lists the two that held something', () => {
  const m = st.sheetModel(base, t, 'pl');
  assert.deepEqual(m.sections.map(s => s.title), ['Instrukcja', 'Pytanie']);
  assert.deepEqual(m.emptyNames, ['Profil', 'Reguły o Tobie', 'Pamięć', 'Projekt', 'Rozmowa', 'Graf']);
  assert.match(m.emptyLine, /^Puste sekcje .*Profil, Reguły o Tobie/);
  assert.equal(m.sections[0].items[0].text, 'Instrukcja aplikacji lub rozmowy');
  assert.equal(m.sections[0].items[0].chip, 'instrukcja');
  assert.equal(m.sections[0].items[0].tokens, 19);
  assert.equal(m.sections[0].items[0].why, '', 'the kernel\'s English sentence is not shown for an item the pack names');
  const slot = st.sheetModel({ ...base, sections: [{ class: 'profile', items: [{ ref: 'slot_1', label: 'Język odpowiedzi', source_kind: 'profile_slot', why: 'said by the person', tokens_est: 3 }], tokens_est: 3 }] }, t, 'pl');
  assert.deepEqual([slot.sections[0].items[0].text, slot.sections[0].items[0].chip, slot.sections[0].items[0].why], ['Język odpowiedzi', 'z profilu', 'said by the person']);
  assert.equal(m.profileLine, 'Profil: żaden nie wybrany — działają ustawienia');
  assert.equal(m.modifiersLine, 'Zmiany tej tury: brak');
  assert.equal(m.guardState, 'unknown');
  assert.equal(m.guardLine, 'strażnik ×10: baza nieznana');
  assert.equal(m.costLine, 'koszt nieznany — brak ceny');
  assert.equal(m.tokensLine, '28 tokenów (szacunek)');
  assert.equal(m.prefixLine, 'prefiks stały: tak');   // the order puts the profile first and the kernel built the (empty) block: the start is the same every turn
  assert.equal(m.prefixDetail, 'profil pusty — nic o Tobie nie zapisano');
  assert.equal(m.dropped.length, 0);
  assert.equal(m.totalItems, 2);
  assert.equal(st.sheetModel(base, tEn, 'en').costLine, 'cost unknown — no price');
});

test('a receipt with a profile, modifiers, a graph section and reasons for leaving out', () => {
  const m = st.sheetModel(rich, t, 'pl');
  assert.equal(m.profileLine, 'Profil: Zgodność — preset; wybór: na tę wiadomość');
  assert.equal(m.modifiersLine, 'Zmiany tej tury: szerzej ×2, głębiej, profil Zgodność (tylko ta wiadomość)');
  assert.equal(m.guardLine, 'strażnik ×10: 1.5× bazy — bez pytania');
  assert.equal(m.guardState, 'ok');
  assert.deepEqual(m.sections.map(s => [s.id, s.items.length, s.tokens]), [['system', 1, 19], ['history', 2, 12], ['graph', 1, 64], ['question', 1, 9]]);
  assert.deepEqual(m.sections[1].items.map(i => [i.text, i.chip]), [['Ty', 'wiadomość bez zmian'], ['Asystent', 'wiadomość bez dygresji']]);
  assert.equal(m.sections[2].items[0].text, 'Węzły grafu powiązane z pytaniem');
  assert.deepEqual(m.dropped.map(d => d.text), [
    'Pamięć — źródło wyłączone przez profil albo ustawienie (1)',
    'Rozmowa — okno historii: pominięto starsze wiadomości (3)',
    'Rozmowa — inny powód (brand_new_reason): 2']);
  assert.deepEqual(m.emptyNames, ['Pamięć']);
});

test('the guard: asked is said as asked, a missing ratio is unknown; the cost is an amount only when the receipt carries a price', () => {
  assert.equal(st.guardView({ ...base, guard: { ratio: 12.34, asked: true } }, t).line, 'strażnik ×10: 12.3× bazy — zapytano');
  assert.equal(st.guardView({ ...base, guard: null }, t).state, 'unknown');
  assert.equal(st.costLine({ ...base, cost_usd: 0.0123 }, t), 'koszt: $0.01');
  assert.equal(st.costLine({ ...base, cost_usd: 0 }, t), 'koszt nieznany — brak ceny');
  assert.ok(!/0\.0000/.test(st.costLine({ ...base, cost_usd: 0 }, t)));
});

test('the prefix is stable only when the order puts the profile before what changes and the kernel built the block', () => {
  assert.equal(st.prefixStable(base), true);   // an empty profile block is still the same every turn
  assert.equal(st.prefixStable({ ...base, profile_prefix: null }), false);   // the block was not built (no level)
  assert.equal(st.prefixStable({ ...base, profile_prefix: { level: 'normal', slots: 3 } }), true);
  assert.equal(st.prefixStable({ ...base, order: { name: 'legacy', classes: ['system', 'memory', 'graph', 'project', 'history'] }, profile_prefix: { level: 'normal', slots: 3 } }), false);
  assert.equal(st.prefixStable({ ...base, order: { name: 'x', classes: ['history', 'profile'] }, profile_prefix: { level: 'normal', slots: 3 } }), false);
});

test('the line under an answer: profile, number of items, tokens; no receipt says so in words', () => {
  assert.equal(st.lineText(rich, t, 'pl'), 'Kontekst: Zgodność · pozycji: 5 · ~100 tok. — zobacz');
  assert.equal(st.lineText(base, t, 'pl'), 'Kontekst: ustawienia · pozycji: 2 · ~28 tok. — zobacz');
  assert.equal(st.lineText(null, t, 'pl'), 'Kontekst: Brak śladu (zapisane przed M1) — zobacz');
  assert.equal(st.lineText(null, tEn, 'en'), 'Context: No receipt (saved before M1) — see');
});

test('the request of an answer is read from the loaded messages (parent first, else the nearest message of the person before it)', () => {
  const msgs = [
    { id: 'u1', role: 'user', parent_id: null, metadata: { context_trace: { receipt: base } } },
    { id: 'a1', role: 'assistant', parent_id: 'u1', metadata: {} },
    { id: 'u2', role: 'user', parent_id: 'a1', metadata: {} },
    { id: 'a2', role: 'assistant', parent_id: null, metadata: {} },
  ];
  assert.equal(st.requestOf(msgs, 'a1').id, 'u1');
  assert.equal(st.requestOf(msgs, 'a2').id, 'u2');        // no parent: the nearest message of the person before it
  assert.equal(st.requestOf(msgs, 'u2').id, 'u2');
  assert.equal(st.requestOf(msgs, 'nope'), null);
  assert.equal(st.recordedReceipt(st.requestOf(msgs, 'a1')).tokens_est, 28);
  assert.equal(st.recordedReceipt(st.requestOf(msgs, 'a2')), null);   // saved before M1 (or switched off): the sheet then asks the kernel for its reason
});

test('"Następnym razem": the steps come from the words and are exactly the steps of action_rules.pack; every one has a label', () => {
  const fromData = ['context.breadth_delta', 'context.depth_delta'].flatMap(key => Object.keys(rules[key].steps));
  assert.deepEqual([...st.stepIds(t)].sort(), [...fromData].sort());
  for (const id of st.stepIds(t)) assert.notEqual(t(`action.${id}`), `action.${id}`);
  assert.deepEqual(st.stepIds(x => x), []);   // without the words there is nothing invented
});

test('the profile choices are the rows of the effective view (chat_context.pack), with label and status; disabled rows are skipped', () => {
  const data = { entries: ctx.selector_profiles.map(entry => ({ id: entry.id, entry })).concat([{ id: 'off', entry: { id: 'off', label: { pl: 'Wyłączony' }, status: 'preset' }, disabled: true }]) };
  const rows = st.profileChoices(data, 'pl');
  assert.deepEqual(rows.map(r => r.id), ctx.selector_profiles.map(p => p.id));
  const compat = rows.find(r => r.id === 'compat');
  assert.equal(compat.label, 'Zgodność');
  assert.equal(compat.status, 'preset');
  assert.deepEqual(new Set(rows.map(r => r.status)), new Set(ctx.selector_profiles.map(p => p.status)));
  for (const status of new Set(rows.map(r => r.status))) assert.notEqual(t(`status.${status}`), `status.${status}`);
  assert.deepEqual(st.profileChoices(null, 'pl'), []);
});

test('the readers of the two runs: a receipt, a missing one with the kernel reason, the x10 question, a decline, a failure', () => {
  const done = { id: 'ar_1', status: 'completed', result: { parts: [{ kind: 'text', text: 'ok' }], data: { msg_id: 'a1', request_msg_id: 'u1', receipt: base } } };
  const got = api.readReceiptRun(done);
  assert.equal(got.kind, 'receipt'); assert.equal(got.requestMsgId, 'u1');
  const missing = api.readReceiptRun({ id: 'ar_2', status: 'completed', result: { data: { msg_id: 'a0', request_msg_id: 'u0', missing_reason: 'before_m1', also_when: ['context_receipt_store=never', 'trace_context=false'] } } });
  assert.deepEqual([missing.kind, missing.reason, missing.alsoWhen.length], ['missing', 'before_m1', 2]);
  assert.equal(api.readReceiptRun({ id: 'ar_3', status: 'failed', error: { code: 'invalid_argument', message: 'Which message?' } }).message, 'Which message?');
  assert.equal(api.readReceiptRun(null).kind, 'failed');
  const applied = api.readModifyRun({ id: 'ar_4', status: 'completed', result: { parts: [{ kind: 'text', text: 'Done: wider' }], data: { waiting: { 'context.breadth_delta': ['wider'] } } } });
  assert.deepEqual([applied.kind, applied.text, Object.keys(applied.waiting)], ['applied', 'Done: wider', ['context.breadth_delta']]);
  const question = api.readModifyRun({ id: 'ar_5', status: 'awaiting_usage', pending: { kind: 'usage', receipt_id: 'usage_1', operation_id: 'ctxmod_1', usage_pending: { status: 'requires_confirmation', projected: {} } } });
  assert.deepEqual([question.kind, question.runId, question.receiptId, question.operationId], ['question', 'ar_5', 'usage_1', 'ctxmod_1']);
  assert.equal(api.readModifyRun({ id: 'ar_6', status: 'rejected', error: { message: 'no' } }).kind, 'declined');
  assert.equal(api.readModifyRun({ id: 'ar_7', status: 'failed', error: { message: 'unknown step' } }).kind, 'failed');
});

test.after(() => loader.close());
