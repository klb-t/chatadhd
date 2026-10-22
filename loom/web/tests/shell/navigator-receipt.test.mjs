// M1-K1c3b: what the navigator shows of the perspective "Wysłane do modelu" (src/components/navigator-receipt.ts), without a browser. The
// answers below have the shape the real kernel gave (probe of loom-server on 2026-10-22: a conversation with the profile "Zgodność", the
// request "with the compatibility profile now?"; K1c-3a). The kernel's sentences (edge label, reason_human, derived_note, receipt.note,
// receipt.text) are shown verbatim; the pack words (presentation.graph_screen sent.*) say the tokens as an estimate, the mark "widok zapisu",
// the summary line and the fallback for a sentence the kernel did not send; a missing word shows its id; both languages have every sent.* word.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const st = await loader.load('src/components/navigator-receipt.ts');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const { pl, en } = pack.entries.find(r => r.key === 'presentation.graph_screen').value.locales;
const translator = words => (id, params = {}) => typeof words[id] === 'string' ? words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? '') : id;
const t = translator(pl), tEn = translator(en);

const NOTE = 'To jest widok zapisu żądania (ślad przy wiadomości), a nie zapisany węzeł grafu: nic nie trafiło do bazy, węzeł liczy się z zapisu przy każdym otwarciu.';
const REQ = 'msg:7d992485dd80';
const derivedSent = { address: 'sent:m_7d992485dd80:system_prompt', label: 'Instrukcja (prompt systemowy)', kind: 'instruction', derived: true, derived_note: NOTE, value: 19, value_human: '19 tokenów (szacunek)' };
const derivedDropped = { address: 'sent:m_7d992485dd80:dropped:memory:source_off', label: 'Pominięte: Pamięć (to źródło było wyłączone), 1', kind: 'left_out', derived: true, derived_note: NOTE };
const realSent = { address: 'msg:07e08968a1c7', label: 'Ty: the python code, database query and API bug in the server', kind: 'message' };
const realDropped = { address: 'msg:aaaaaaaaaaaa', label: 'Ty: an earlier message of another thread', kind: 'message' };
const edge = (dst, kind, extra) => ({ src: REQ, dst, kind, label: kind === 'sent_to_model' ? 'wysłane do modelu' : 'pominięte w żądaniu', meaning: null, derived: true, ...extra });
const edges = [
  edge(derivedSent.address, 'sent_to_model', { weight: 19 }),
  edge(realSent.address, 'sent_to_model', { weight: 15 }),
  edge(derivedDropped.address, 'left_out_of_model', { weight: 1, metadata: { group: 'memory', reason: 'source_off', reason_human: 'to źródło było wyłączone', group_count: 1 } }),
  edge(realDropped.address, 'left_out_of_model', { weight: 1, metadata: { reason: 'thread_filter', reason_human: 'wiadomość z innego wątku' } }),
];
const receipt = { present: true, counts: { nodes: 8, edges: 7, sent: 6, left_out: 1 }, receipt: { present: true, tokens_est: 82 }, note: 'To zapis żądania, które zbudowaliśmy i wysłaliśmy; nie jest dowodem, że dostawca dostał je dokładnie w tej postaci.' };
const answer = { edges, children: [derivedSent, derivedDropped, realSent, realDropped], receipt };

test('a sent row says the kernel name of the link and its tokens as an estimate; a derived row uses its own words', () => {
  const real = st.rowMarks(answer, realSent, t);
  assert.equal(real.sent, 'wysłane do modelu: 15 tokenów (szacunek)');
  assert.equal(real.weight, 15);
  assert.equal(real.derived, null);
  assert.equal(real.leftOut, null);
  const derived = st.rowMarks(answer, derivedSent, t);
  assert.equal(derived.sent, 'wysłane do modelu: 19 tokenów (szacunek)', 'the derived row keeps the kernel value_human');
  assert.equal(derived.derived, 'widok zapisu');
});

test('a left-out row says it in the kernel words and gives the reason only when the label does not already say it', () => {
  const group = st.rowMarks(answer, derivedDropped, t);
  assert.equal(group.leftOut, 'pominięte w żądaniu');
  assert.equal(group.reason, null, 'the label of the group already has "(to źródło było wyłączone)"');
  assert.equal(group.sent, null);
  const real = st.rowMarks(answer, realDropped, t);
  assert.equal(real.leftOut, 'pominięte w żądaniu');
  assert.equal(real.reason, 'wiadomość z innego wątku');
});

test('a link without a weight says no number (nothing is made up); an element sent twice adds up', () => {
  const none = st.rowMarks({ edges: [edge('msg:x', 'sent_to_model', {})] }, { address: 'msg:x', label: 'x' }, t);
  assert.equal(none.sent, 'wysłane do modelu');
  assert.equal(none.weight, null);
  const twice = st.rowMarks({ edges: [edge('msg:y', 'sent_to_model', { weight: 4 }), edge('msg:y', 'sent_to_model', { weight: 6 })] }, { address: 'msg:y', label: 'y' }, t);
  assert.equal(twice.weight, 10);
  assert.equal(twice.sent, 'wysłane do modelu: 10 tokenów (szacunek)');
});

test('the lines under the list: the summary with the total as an estimate, the derived sentence once and verbatim, the kernel note', () => {
  const lines = st.receiptLines(answer, t);
  assert.deepEqual(lines.map(l => l.id), ['sent-summary', 'sent-derived-note', 'sent-note']);
  assert.equal(lines[0].text, 'Z tego żądania: wysłane 6, pominięte 1 · razem 82 tokenów (szacunek)');
  assert.equal(lines[1].text, NOTE, 'two derived rows, one sentence, verbatim from the kernel');
  assert.equal(lines[2].text, receipt.note);
  const handed = st.receiptLines({ ...answer, handed_from: 'msg:f952e9fdfb0f' }, t);
  assert.equal(handed[0].id, 'sent-handed');
  assert.equal(handed[0].text, 'z odpowiedzi do jej zapytania');
  assert.deepEqual(st.receiptLines({ children: [realSent], handed_from: 'area:x' }, t), [], 'no record in the answer: the area redirect is not this line');
});

test('a derived row from a kernel that sent no sentence gets the pack sentence; without a total the summary has no total', () => {
  const bare = { children: [{ ...derivedSent, derived_note: undefined }], receipt: { present: true, counts: { sent: 1, left_out: 0 } } };
  const lines = st.receiptLines(bare, t);
  assert.equal(lines.find(l => l.id === 'sent-derived-note').text, pl['sent.derived_note']);
  assert.equal(lines.find(l => l.id === 'sent-summary').text, 'Z tego żądania: wysłane 1, pominięte 0');
  assert.equal(st.derivedCard({ derived: true }, t).note, pl['sent.derived_note']);
});

test('an empty list says why when the record is missing, in the kernel sentence; the card of a derived node carries the mark and the sentence', () => {
  const missing = { receipt: { present: false, missing_reason: 'before_m1', text: 'Brak śladu (zapisane przed M1).' } };
  assert.equal(st.emptyText(missing, true, t), 'Brak śladu (zapisane przed M1).');
  assert.equal(st.emptyText({ receipt: { present: false, missing_reason: 'focus_not_a_message' } }, true, t), pl['perspective.empty'], 'no kernel sentence: the pack one');
  assert.equal(st.emptyText({}, true, t), pl['perspective.empty']);
  assert.equal(st.emptyText({}, false, t), pl.no_children);
  assert.deepEqual(st.derivedCard(derivedSent, t), { mark: 'widok zapisu', note: NOTE });
  assert.equal(st.derivedCard(realSent, t), null);
});

test('every sent.* word exists in both languages with the same placeholders; a missing word shows its id', () => {
  const keys = Object.keys(pl).filter(k => k.startsWith('sent.'));
  assert.ok(keys.length >= 6, keys.join());
  for (const k of keys) {
    assert.equal(typeof en[k], 'string', `en has ${k}`);
    assert.deepEqual([...pl[k].matchAll(/\{\{(\w+)\}\}/g)].map(m => m[1]).sort(), [...en[k].matchAll(/\{\{(\w+)\}\}/g)].map(m => m[1]).sort(), `${k}: same placeholders`);
  }
  assert.equal(tEn('sent.tokens', { n: '19' }), '19 tokens (estimate)');
  assert.equal(st.tokensText(19, id => id), 'sent.tokens');
});

// M1-K1c4: the reason line of a row/card of the record: the pack's words (presentation.context_receipt why.*), the kernel's English sentence only as the fallback.
const rw = pack.entries.find(r => r.key === 'presentation.context_receipt').value.locales;
const rt = translator(rw.pl), rtEn = translator(rw.en);
const sentRow = (ref, source_kind, why) => ({ derived: true, why, receipt: { ref, source_kind, class: 'x', why, tokens_est: 5 } });

test('a sent element of the record says why in the pack words, in both languages; the kernel sentence stays in the record', () => {
  const system = sentRow('system_prompt', 'instruction', 'the instruction of the application or of the conversation');
  assert.equal(st.rowWhy(system, rt), 'Instrukcja dla modelu ustawiona w aplikacji albo w tej rozmowie.');
  assert.equal(st.rowWhy(system, rtEn), 'The instruction for the model set in the application or in this conversation.');
  assert.equal(st.rowWhy(sentRow('graph_memory', 'graph_node', 'the heading line of the graph context'), rt), rw.pl['why.graph_memory']);
  assert.equal(st.rowWhy(sentRow('question', 'question', 'what the person wrote in this turn'), rt), 'Tekst Twojej wiadomości w tej turze.');
  assert.equal(st.rowWhy(sentRow('n_8f2a', 'memory_node', 'an active node of the memory tree'), rt), 'Jeden z włączonych węzłów pamięci.');
  assert.equal(system.why, 'the instruction of the application or of the conversation', 'the row itself is not changed');
});

test('a row the record does not say was sent keeps the kernel sentence: a left-out element, a row without the record facts, a stored node', () => {
  const left = { derived: true, why: 'Pamięć', receipt: { ref: 'system_prompt', source_kind: 'left_out', group: 'system', reason: 'source_off' } };
  assert.equal(st.rowWhy(left, rt), 'Pamięć', 'left out: the reason is the kernel\'s, never "why it was sent"');
  assert.equal(st.rowWhy({ derived: true, why: 'the sentence', receipt: {} }, rt), 'the sentence');
  assert.equal(st.rowWhy({ derived: false, why: 'Ustawienie języka', receipt: undefined }, rt), 'Ustawienie języka', 'a ROM or settings row is the kernel\'s own Polish');
  assert.equal(st.rowWhy({ derived: false, why: 'x', receipt: { ref: 'system_prompt', source_kind: 'instruction' } }, rt), 'x', 'only a derived node of the record is reworded');
  assert.equal(st.rowWhy({ derived: true, receipt: { ref: 'zzz', source_kind: 'new_kind' } }, rt), '', 'no word and no kernel sentence: nothing');
  assert.equal(st.rowWhy(sentRow('zzz', 'new_kind', 'a sentence of a later kernel'), rt), 'a sentence of a later kernel', 'a key without a word: the kernel sentence');
});
