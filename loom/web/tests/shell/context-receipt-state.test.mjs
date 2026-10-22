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
  guard: { ...base.guard, baseline: 585.5, ratio: 1.5303159692570452, growth_factor: 10, asked: false, status: 'allowed', unknown_reason: undefined, baseline_source: 'measured', baseline_samples: 5 },
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
  const kinds = ['instruction', 'rule', 'memory_node', 'graph_node', 'kb_claim', 'profile_slot', 'graph_packet', 'task', 'question', 'message:L0', 'message:L1', 'message:L2', 'message:L3', 'message:graph'];
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
  assert.deepEqual([slot.sections[0].items[0].text, slot.sections[0].items[0].chip, slot.sections[0].items[0].why], ['Język odpowiedzi', 'z profilu', pl['why.profile_slot']]);
  const node = st.sheetModel({ ...base, sections: [{ class: 'memory', items: [{ ref: 'n_1', label: 'text', source_kind: 'memory_node', why: 'an active node of the memory tree', tokens_est: 3 }], tokens_est: 3 }] }, t, 'pl');
  assert.equal(node.sections[0].items[0].why, 'Jeden z włączonych węzłów pamięci.', 'a memory node says why in the pack\'s words, not in the kernel\'s English');
  const none = st.sheetModel({ ...base, sections: [{ class: 'memory', items: [{ ref: 'n_1', label: 'text', source_kind: 'a_kind_of_a_later_kernel', why: 'a sentence of the kernel', tokens_est: 3 }], tokens_est: 3 }] }, t, 'pl');
  assert.equal(none.sections[0].items[0].why, 'a sentence of the kernel', 'a kind the pack has no word for keeps the kernel\'s sentence');
  assert.equal(m.profileLine, 'Profil: żaden nie wybrany — działają ustawienia');
  assert.equal(m.modifiersLine, 'Zmiany tej tury: brak');
  assert.equal(m.guardState, 'unknown');
  assert.equal(m.guardLine, 'strażnik ×10: nieznane — brak bazy');
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
  assert.equal(m.guardLine, 'strażnik ×10: 1.5× zwykłego zapytania do tego modelu (średnia z ostatnich zapytań do tego modelu (liczba zapytań: 5)) — bez pytania');
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
  assert.equal(st.guardView({ ...base, guard: { ratio: 12.34, asked: true, baseline_source: 'measured', baseline_samples: 8 } }, t).line, 'strażnik ×10: 12.3× zwykłego zapytania do tego modelu (średnia z ostatnich zapytań do tego modelu (liczba zapytań: 8)) — zapytano');
  assert.equal(st.guardView({ ...base, guard: null }, t).state, 'unknown');
  assert.equal(st.costLine({ ...base, cost_usd: 0.0123 }, t), 'koszt: $0.01');
  assert.equal(st.costLine({ ...base, cost_usd: 0 }, t), 'koszt nieznany — brak ceny');
  assert.ok(!/0\.0000/.test(st.costLine({ ...base, cost_usd: 0 }, t)));
});

test('ratio words: no ratio or no base is unknown, a tiny one is "much less than the usual request", from 0.1 up a number with the sign — ONE phrase for the estimate and the guard, the base named next to it', () => {
  for (const [words, name] of [[t, 'pl'], [tEn, 'en']]) {
    const w = name === 'pl' ? pl : en;
    assert.deepEqual(st.ratioText(null, words), { kind: 'unknown', text: w['ratio.unknown'] }, `${name}: null`);
    assert.deepEqual(st.ratioText(undefined, words), { kind: 'unknown', text: w['ratio.unknown'] }, `${name}: undefined`);
    assert.deepEqual(st.ratioText(0.04, words), { kind: 'low', text: w['ratio.low'] }, `${name}: 0.04`);
    assert.deepEqual(st.ratioText(1.8, words), { kind: 'times', text: w['ratio.times'].replace('{{n}}', '1.8') }, `${name}: 1.8`);
    assert.equal(st.ratioText(0.1, words).kind, 'times', `${name}: 0.1 is the first number`);
    assert.equal(st.ratioText(0.1, words).text, w['ratio.times'].replace('{{n}}', '0.1'));
    assert.equal(st.ratioText(2, words).text, w['ratio.times'].replace('{{n}}', '2'), `${name}: a whole number has no ".0", as before`);
    assert.equal(st.ratioText(NaN, words).kind, 'unknown');
    for (const kind of ['unknown', 'low', 'times']) assert.ok(!/ratio\./.test(st.ratioText(kind === 'unknown' ? null : kind === 'low' ? 0.04 : 1.8, words).text), `${name}: ${kind} has its word`);
    // the base in words: what the number is measured against (the average of the last N requests, a starting value, a declared one), never an unlabeled "x"
    assert.equal(st.baseText({ source: 'measured', samples: 6 }, words), w['base.measured'].replace('{{n}}', '6'));
    assert.equal(st.baseText({ source: 'floor', samples: 3 }, words), w['base.floor'].replace('{{n}}', '3'));
    assert.equal(st.baseText({ source: 'declared', samples: 0 }, words), w['base.declared']);
    assert.equal(st.baseText({ source: null, samples: null }, words), null, `${name}: a base the guard did not name is not invented`);
    assert.equal(st.ratioWithBase(1.8, { source: 'measured', samples: 6 }, words).text, w['ratio.with_base'].replace('{{ratio}}', w['ratio.times'].replace('{{n}}', '1.8')).replace('{{base}}', w['base.measured'].replace('{{n}}', '6')));
    assert.equal(st.ratioWithBase(null, { source: 'measured', samples: 6 }, words).text, w['ratio.unknown'], `${name}: an unknown ratio has no base to name`);
  }
  assert.equal(st.ratioText(null, t).text, 'nieznane — brak bazy');
  assert.equal(st.ratioText(0.04, t).text, 'dużo mniej niż zwykłe zapytanie do tego modelu');
  assert.equal(st.ratioText(1.8, t).text, '1.8× zwykłego zapytania do tego modelu');
  assert.equal(st.ratioText(null, tEn).text, 'unknown — no base');
  assert.equal(st.ratioText(0.04, tEn).text, 'much less than the usual request to this model');
  assert.equal(st.ratioText(1.8, tEn).text, '1.8× the usual request to this model');
  // the guard line of the sheet: the same three cases, with the base named, and a ratio against a missing base is unknown (the kernel writes baseline: null)
  const measured = { baseline_source: 'measured', baseline_samples: 6 };
  assert.equal(st.guardView({ ...base, guard: { ratio: 1.8, baseline: 100, asked: false, ...measured } }, t).line, 'strażnik ×10: 1.8× zwykłego zapytania do tego modelu (średnia z ostatnich zapytań do tego modelu (liczba zapytań: 6)) — bez pytania');
  assert.equal(st.guardView({ ...base, guard: { ratio: 0.04, baseline: 100, asked: false, baseline_source: 'floor', baseline_samples: 2 } }, t).line, 'strażnik ×10: dużo mniej niż zwykłe zapytanie do tego modelu (wartość startowa; zmierzonych dotąd zapytań do tego modelu: 2) — bez pytania');
  assert.equal(st.guardView({ ...base, guard: { ratio: 0.04, baseline: 100, asked: false, ...measured } }, t).state, 'ok');
  assert.equal(st.guardView({ ...base, guard: { ratio: null, baseline: null, asked: false } }, t).line, 'strażnik ×10: nieznane — brak bazy');
  assert.equal(st.guardView({ ...base, guard: { ratio: 0, baseline: null, asked: false } }, t).line, 'strażnik ×10: nieznane — brak bazy', 'ratio 0 against no base is not "0× bazy"');
assert.equal(st.guardView({ ...base, guard: { ratio: 0, baseline: 0, asked: false } }, t).state, 'unknown', 'a base of zero is no base');
  assert.equal(st.guardView({ ...base, guard: { ratio: 0.04, baseline: 100, asked: false, ...measured } }, tEn).line, '×10 guard: much less than the usual request to this model (the average of the last requests to this model (count: 6)) — no question');
  assert.equal(st.guardView({ ...base, guard: { ratio: 1.8, baseline: 100, asked: true, baseline_source: 'declared', baseline_samples: 0 } }, tEn).line, '×10 guard: 1.8× the usual request to this model (a value declared in the settings) — asked');
  assert.equal(st.guardView({ ...base, guard: null }, tEn).line, '×10 guard: unknown — no base');
  for (const guard of [{ ratio: 0, baseline: null }, { ratio: 0.04, baseline: 100 }, { ratio: 1.8, baseline: 100 }, null]) assert.ok(!/(^|\D)0×/.test(st.guardView({ ...base, guard }, t).line), 'no "0×" in any case');
});

test('the header names: every order of chat_context.pack and every flow of the guardian profile has a short word in both languages; a name without a word shows itself', async () => {
  const guardian = (await read('../../../data/runtime/guardian.pack')).defaults;
  const orders = Object.keys(ctx.orders), flows = Object.keys(guardian.context_io.flows);
  assert.deepEqual(orders.sort(), ['legacy', 'stability', 'stable_first'], 'the orders of the data (a new one needs a word here)');
  assert.deepEqual(flows.sort(), ['graph_in', 'graph_io', 'graph_out', 'plain']);
  for (const id of orders) for (const [words, name] of [[pl, 'pl'], [en, 'en']]) assert.equal(typeof words[`order.${id}`], 'string', `order.${id} in ${name}`);
  for (const id of flows) for (const [words, name] of [[pl, 'pl'], [en, 'en']]) assert.equal(typeof words[`flow.${id}`], 'string', `flow.${id} in ${name}`);
  // the words of the data's own order: the graph comes after the conversation in the two orders that say "graf na końcu"
  for (const id of ['stable_first', 'stability']) assert.equal(ctx.orders[id].at(-1), 'graph', id);
  assert.ok(ctx.orders.legacy.indexOf('graph') < ctx.orders.legacy.indexOf('history'), 'legacy: graph before the conversation');
  assert.deepEqual(ctx.orders.stability.slice(0, 3), ['system', 'profile', 'rules'], 'stability: profile and rules first');
  const line = (order, flow, words) => { const m = st.sheetModel({ ...base, order: { name: order, classes: ctx.orders[order] ?? [] }, flow: flow ? { flow } : null }, words, words === t ? 'pl' : 'en'); return [m.orderLine, m.flowLine]; };
  assert.deepEqual(line('stability', 'plain', t), ['Kolejność: stabilna (profil i reguły na początku, graf na końcu)', 'Przepływ: zwykły tekst']);
  assert.deepEqual(line('legacy', 'graph_io', t), ['Kolejność: dawna (graf przed rozmową)', 'Przepływ: graf na wejściu i na wyjściu']);
  assert.deepEqual(line('stable_first', 'graph_in', tEn), ['Order: steady parts first (graph last)', 'Flow: a graph in, a text answer']);
  assert.deepEqual(line('x_mine', 'my_flow', t), ['Kolejność: x_mine', 'Przepływ: my_flow'], 'no word: the raw name, never a guess');
  assert.deepEqual(line('stability', null, t), ['Kolejność: stabilna (profil i reguły na początku, graf na końcu)', null]);
  const m = st.sheetModel({ ...base, flow: { flow: 'plain' } }, t, 'pl');
  assert.deepEqual([m.orderName, m.flowName], ['stability', 'plain'], 'the internal names stay available for a data attribute');
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

test('"Pokaż w grafie" asks for the node the graph really has: the message id m_<hex> is msg:<hex> (graph_sources.pack), never msg:m_<hex>', async () => {
  const sources = await read('../../../data/runtime/graph_sources.pack');
  const text = JSON.stringify(sources);
  assert.ok(text.includes('"db_prefix":"m_"') || /"db_prefix":\s*"m_"/.test(text), 'the pack maps the database prefix m_');
  assert.ok(/"address_prefix":\s*"msg:"/.test(text), 'to the address prefix msg:');
  assert.equal(st.messageAddress('m_7d992485dd80'), 'msg:7d992485dd80');
  assert.equal(st.messageAddress('msg:7d992485dd80'), 'msg:7d992485dd80', 'an address in the graph form is kept');
  assert.equal(st.messageAddress('7d992485dd80'), 'msg:7d992485dd80', 'a bare hex id gets the prefix');
  assert.ok(!st.messageAddress('m_abc').includes('m_'));
});

// M1-K1c4: the reason of an item is said in the pack's words (stable key: the fixed ref, else the source kind), the kernel's English sentence is the
// fallback for a key without a word. The list of keys is READ from the kernel source (chat_engine.cpp: every receipt_item / receipt_element call and
// the profile slot note), so a reason the kernel starts to write without a word here fails this test.
function callsOf(src) {
  const out = [];
  for (const m of src.matchAll(/receipt_(?:item|element)\(/g)) {
    let depth = 1, i = m.index + m[0].length, quote = false, cur = '';
    const args = [];
    for (; i < src.length && depth > 0; i++) {
      const c = src[i];
      if (quote) { cur += c; if (c === '\\') { cur += src[++i]; } else if (c === '"') quote = false; continue; }
      if (c === '"') { quote = true; cur += c; continue; }
      if (c === '(' || c === '{') depth++;
      if (c === ')' || c === '}') { depth--; if (depth === 0) break; }
      if (c === ',' && depth === 1) { args.push(cur.trim()); cur = ''; continue; }
      cur += c;
    }
    args.push(cur.trim());
    out.push(args);
  }
  return out;
}
const literal = a => /^"([^"]*)"$/.exec(a ?? '')?.[1] ?? null;

test('whyWords: every reason the kernel writes today has a word in both languages, chosen by the fixed ref or by the source kind', async () => {
  const src = await readFile(new URL('../../../src/chat/chat_engine.cpp', import.meta.url), 'utf8');
  const calls = callsOf(src);
  assert.ok(calls.length >= 12, 'the kernel source was read');
  const refs = new Set(), kinds = new Set();
  for (const args of calls) {
    const ref = literal(args[0]), kind = literal(args[2]);
    if (ref) refs.add(ref); else if (kind) kinds.add(kind);           // a dynamic ref (a message, a node, a packet) is named by its kind
    if (!ref && !kind && /^"message:"\s*\+/.test(args[2])) for (const l of ['L0', 'L1', 'L2', 'L3']) kinds.add('message:' + l);   // the history levels
  }
  for (const m of src.matchAll(/"source_kind",\s*"([a-z_:]+)"/g)) kinds.add(m[1]);   // the profile slots
  assert.deepEqual([...refs].sort(), ['active_task', 'graph_memory', 'graph_reply_instruction', 'guardian_rules', 'knowledge_context', 'memory_tree', 'question', 'system_prompt', 'unified_context'], 'the fixed refs of the kernel');
  assert.ok(['memory_node', 'graph_packet', 'profile_slot', 'message:graph', 'message:L0', 'message:L3'].every(k => kinds.has(k)), 'the kinds of the kernel: ' + [...kinds].join(', '));
  for (const [words, say] of [[pl, t], [en, tEn]]) {
    for (const ref of refs) assert.ok(words[`why.${ref}`], `why.${ref}`);
    for (const kind of kinds) if (!refs.has(kind) && !['instruction', 'question', 'task', 'rule', 'kb_claim', 'graph_node'].includes(kind)) assert.ok(words[`why.${kind.replace(':', '.')}`], `why.${kind}`);
    for (const ref of refs) assert.equal(st.whyWords({ ref, source_kind: 'x', why: 'the kernel sentence' }, say), words[`why.${ref}`]);
  }
});

test('whyWords: the word of a fixed ref wins over the kind, a dynamic ref is named by its kind, an unknown key falls back to the kernel sentence, nothing is made up', () => {
  assert.equal(st.whyWords({ ref: 'system_prompt', source_kind: 'instruction', why: 'the instruction of the application or of the conversation' }, t), 'Instrukcja dla modelu ustawiona w aplikacji albo w tej rozmowie.');
  assert.equal(st.whyWords({ ref: 'system_prompt', source_kind: 'instruction', why: 'the instruction of the application or of the conversation' }, tEn), 'The instruction for the model set in the application or in this conversation.');
  assert.equal(st.whyWords({ ref: 'memory_tree', source_kind: 'memory_node', why: 'the active nodes of the memory tree' }, t), 'Włączone węzły Twojej pamięci.', 'the ref memory_tree, not the kind memory_node');
  assert.equal(st.whyWords({ ref: 'n_8f2a', source_kind: 'memory_node', why: 'an active node of the memory tree' }, t), 'Jeden z włączonych węzłów pamięci.');
  assert.equal(st.whyWords({ ref: 'n_8f2a', source_kind: 'memory_node', why: 'an active node of the memory tree' }, tEn), 'One of the memory nodes that are switched on.');
  assert.equal(st.whyWords({ ref: 'm_1', source_kind: 'message:L1', why: 'a message of the conversation at the lossy level L1 chosen by the secretary' }, t), pl['why.message.L1']);
  assert.equal(st.whyWords({ ref: 'm_2', source_kind: 'message:graph', why: 'a message found by walking the graph from the question' }, t), 'Wiadomość znaleziona po drodze przez graf, od Twojego pytania.');
  assert.equal(st.whyWords({ ref: 'gp_1', source_kind: 'graph_packet', why: 'the selection as a graph packet (flow pack)' }, t), 'Wiadomości wybrane z grafu i wysłane jako jeden pakiet.');
  // a key without a word: the kernel's own sentence; a kernel that sent none: nothing
  assert.equal(st.whyWords({ ref: 'x_1', source_kind: 'a_kind_of_a_later_kernel', why: 'a sentence of the kernel' }, t), 'a sentence of the kernel');
  assert.equal(st.whyWords({ ref: 'x_1', source_kind: 'a_kind_of_a_later_kernel' }, t), '');
  assert.equal(st.whyWords({ ref: 'x_1', why: null }, t), '');
  assert.equal(st.whyWords({ ref: 'system_prompt', why: 'the kernel sentence' }, id => id), 'the kernel sentence', 'no words loaded: the kernel sentence, never a bare key');
  // every reason has its own words in each language (the Polish one is not the English one)
  for (const key of Object.keys(pl).filter(k => k.startsWith('why.'))) assert.notEqual(pl[key], en[key], `${key} is translated`);
});

test('"Brak śladu": the sentence under it is in plain words (no setting names); the raw names are only for the folded technical block', () => {
  for (const words of [pl, en]) {
    assert.doesNotMatch(words['missing.also'], /context_receipt_store|trace_context|[a-z]+_[a-z]+|=/, 'no raw setting name or "=" in the visible sentence');
    assert.ok(words['missing.technical']);
  }
  assert.equal(pl['missing.also'], 'Tak samo jest, gdy zapisywanie śladu żądań wyłączono w ustawieniach.');
  assert.equal(pl['missing.technical'], 'Szczegóły techniczne');
  const run = api.readReceiptRun({ id: 'ar_3', status: 'completed', result: { data: { msg_id: 'a0', request_msg_id: 'u0', missing_reason: 'before_m1', also_when: ['context_receipt_store=never', 'trace_context=false'] } } });
  assert.deepEqual(run.alsoWhen, ['context_receipt_store=never', 'trace_context=false'], 'the kernel\'s names stay as data for the folded block');
});
