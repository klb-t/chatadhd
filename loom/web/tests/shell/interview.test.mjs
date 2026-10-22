// M1-W2a: the pure rules of the interview in the conversation window (src/context/interview.ts) and the readers of profile.interview
// (src/api/profile-interview.ts). The messages and runs below have the shape the real kernel writes (captured from loom-server, K2b-2; the
// texts are synthetic). Words: presentation.interview of conversation_view.pack, both languages; the say-words of the quick replies must be
// control words of chat_ops.pack (profile.words), so a chip can never send a sentence the kernel does not take for a command.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
import { looksLikeDeveloperText } from '../../e2e/m1-audit.mjs';
const loader = await createModuleLoader();
const st = await loader.load('src/context/interview.ts');
const api = await loader.load('src/api/profile-interview.ts');
const read = async rel => JSON.parse(await readFile(new URL(rel, import.meta.url), 'utf8'));
const view = await read('../../../data/profiles/conversation_view.pack');
const feature = view.entries.find(r => r.key === 'presentation.interview').value;
const { en, pl } = feature.locales;
const translator = words => (id, params = {}) => typeof words[id] === 'string' ? words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? '') : id;
const t = translator(pl), tEn = translator(en);
const words = (await read('../../../data/runtime/chat_ops.pack')).defaults.profile.words;

const level = (id, label) => ({ id, label, preview: { asks: `asks ${id}`, stores: `stores ${id}`, model_sees: `sees ${id}` } });
const levels = [level('minimal', 'Minimalnie'), level('normal', 'Zwykle'), level('detailed', 'Szczegolowo')];
const levelMessage = { id: 'm_1', role: 'assistant', status: 'active', metadata: { routed: 'interview', interview: { kind: 'question', mode: 'form',
  question: { field: 'interview.detail', section: 'detail', topic: 'ile pytac', text: 'Ile pytan?', fallback: false, is_level_question: true, levels } } } };
const plainQuestion = { id: 'm_3', role: 'assistant', status: 'active', metadata: { routed: 'interview', interview: { kind: 'reply', mode: 'form',
  question: { field: 'communication.style', section: 'communication', topic: 'styl rozmowy', text: 'Jak mam do Ciebie mowic?', fallback: false, is_level_question: false } } } };
const proposal = { id: 'm_5', role: 'assistant', status: 'active', metadata: { routed: 'interview', interview: { kind: 'proposal', question: null, mode: 'form' } } };
const answer = { id: 'm_2', role: 'user', status: 'active', metadata: { routed: 'interview', interview: { kind: 'answer' } } };

test('a conversation of the interview is the one the kernel marked with metadata.flow', () => {
  assert.equal(st.isInterviewConversation({ flow: 'profile.interview', user_id: 'local', lang: 'pl', mode: 'form' }), true);
  for (const meta of [undefined, null, {}, { flow: 'other' }, { flow: 'profile.interview.x' }]) assert.equal(st.isInterviewConversation(meta), false);
});

test('level cards: only the question that asks the level and carries every level with its three previews', () => {
  assert.deepEqual(st.levelCards(levelMessage.metadata).map(l => l.id), ['minimal', 'normal', 'detailed']);
  assert.deepEqual(st.levelCards(levelMessage.metadata)[1].preview, { asks: 'asks normal', stores: 'stores normal', model_sees: 'sees normal' });
  assert.deepEqual(st.levelCards(plainQuestion.metadata), [], 'another question has no cards');
  assert.deepEqual(st.levelCards(undefined), []);
  const noLevels = structuredClone(levelMessage.metadata); delete noLevels.interview.question.levels;
  assert.deepEqual(st.levelCards(noLevels), [], 'no data: the plain text of the message stays');
  const brokenPreview = structuredClone(levelMessage.metadata); delete brokenPreview.interview.question.levels[0].preview.stores; delete brokenPreview.interview.question.levels[2].preview;
  assert.deepEqual(st.levelCards(brokenPreview), [], 'a level without its previews is dropped; fewer than two is no choice');
});

test('the waiting question is the last message when it is an assistant message that carries one', () => {
  assert.equal(st.waitingQuestion([levelMessage]).messageId, 'm_1');
  assert.equal(st.waitingQuestion([levelMessage, answer]), null, 'the person answered: nothing waits');
  assert.equal(st.waitingQuestion([levelMessage, answer, plainQuestion]).question.field, 'communication.style');
  assert.equal(st.waitingQuestion([plainQuestion, answer, proposal]), null, 'a proposal waits for yes or no, not for an answer');
  assert.equal(st.waitingQuestion([]), null);
  assert.equal(st.waitingQuestion([{ id: 'm_9', role: 'assistant', status: 'active', metadata: {} }]), null, 'an ordinary message');
  assert.equal(st.waitingQuestion([plainQuestion, { ...answer, status: 'excluded' }]).messageId, 'm_3', 'an excluded message does not count');
});

test('the quick replies stand under the last message when it asks (or answers a status); never under a proposal or a person\'s message', () => {
  assert.deepEqual(st.repliesTarget([levelMessage]), { messageId: 'm_1', idle: false });
  assert.deepEqual(st.repliesTarget([levelMessage, answer, plainQuestion]), { messageId: 'm_3', idle: false });
  const statusReply = { id: 'm_7', role: 'assistant', status: 'active', metadata: { routed: 'interview', interview: { kind: 'reply', question: null, mode: 'form' } } };
  assert.deepEqual(st.repliesTarget([plainQuestion, answer, statusReply]), { messageId: 'm_7', idle: true });
  assert.equal(st.repliesTarget([plainQuestion, answer, proposal]), null);
  assert.equal(st.repliesTarget([plainQuestion, answer]), null, 'the person spoke last');
  assert.equal(st.repliesTarget([]), null);
});

test('the card of the empty chat: show + never started + not tapped away in this launch', () => {
  const status = kind => ({ kind });
  for (const setting of ['show', 'later', 'never', null])
    for (const s of [null, status('not_started'), status('active'), status('unavailable')])
      for (const hiddenNow of [false, true]) {
        const expected = setting === 'show' && s?.kind === 'not_started' && !hiddenNow;
        assert.equal(st.cardVisible({ setting, status: s, hiddenNow }), expected, JSON.stringify({ setting, status: s?.kind, hiddenNow }));
      }
  assert.equal(st.cardSettingOf('show'), 'show'); assert.equal(st.cardSettingOf('later'), 'later'); assert.equal(st.cardSettingOf('never'), 'never');
  assert.equal(st.cardSettingOf('maybe'), null); assert.equal(st.cardSettingOf(undefined), null);
});

test('"Później" lasts until the next launch: only later is turned back into show', () => {
  assert.equal(st.settingAtLaunch('later'), 'show');
  assert.equal(st.settingAtLaunch('never'), 'never');
  assert.equal(st.settingAtLaunch('show'), 'show');
  assert.equal(st.settingAtLaunch(null), null);
});

test('profile.interview start: the conversation to open, or the reason', () => {
  const run = { id: 'ar_1', status: 'completed', result: { parts: [{ kind: 'text', text: 'Otworzylem rozmowe.' }], data: { conv_id: 'c_ab', conversation: { conv_id: 'c_ab', title: 'Poznajmy sie', created: true } } } };
  assert.deepEqual(api.readInterviewStart(run), { kind: 'opened', convId: 'c_ab', text: 'Otworzylem rozmowe.' });
  assert.equal(api.readInterviewStart({ id: 'ar_2', status: 'failed', error: { code: 'unsupported', message: 'Stary scenariusz.' } }).message, 'Stary scenariusz.');
  assert.equal(api.readInterviewStart({ id: 'ar_3', status: 'completed', result: { data: {} } }).kind, 'failed');
  assert.equal(api.readInterviewStart(null).kind, 'failed');
});

test('profile.interview status: not started is the kernel\'s not_found; anything else that fails is unavailable', () => {
  assert.deepEqual(api.readInterviewStatus({ id: 'ar_1', status: 'failed', error: { code: 'not_found', message: 'Wywiad jeszcze sie nie zaczal.' } }), { kind: 'not_started' });
  assert.equal(api.readInterviewStatus({ id: 'ar_2', status: 'failed', error: { code: 'unsupported', message: 'x' } }).kind, 'unavailable');
  assert.deepEqual(api.readInterviewStatus({ id: 'ar_3', status: 'completed', result: { data: { level: 'minimal', level_choice_pending: false, remaining: 2 } } }),
    { kind: 'active', remaining: 2, levelChoicePending: false, level: 'minimal' });
  assert.equal(api.readInterviewStatus({ id: 'ar_4', status: 'completed', result: { data: { level_choice_pending: true, remaining: 1 } } }).levelChoicePending, true);
  assert.equal(api.readInterviewStatus(undefined).kind, 'unavailable');
});

test('the progress line: the level choice, the number left, or the end - in the words of the pack', () => {
  assert.equal(st.progressWord({ kind: 'active', remaining: 2, levelChoicePending: false, level: 'minimal' }, t), 'Pytań do zadania: 2');
  assert.equal(st.progressWord({ kind: 'active', remaining: 2, levelChoicePending: false, level: 'minimal' }, tEn), 'Questions left: 2');
  assert.equal(st.progressWord({ kind: 'active', remaining: 1, levelChoicePending: true, level: 'normal' }, t), pl['info.level_pending']);
  assert.equal(st.progressWord({ kind: 'active', remaining: 0, levelChoicePending: false, level: 'normal' }, t), pl['info.finished']);
  assert.equal(st.progressWord({ kind: 'active', remaining: null, levelChoicePending: false, level: null }, t), null);
  assert.equal(st.progressWord({ kind: 'not_started' }, t), null);
  assert.equal(st.progressWord(null, t), null);
});

test('the quick replies: ids and labels from the pack, and every say-word is a control word of the kernel (both languages)', () => {
  for (const [lang, tr] of [['pl', t], ['en', tEn]]) {
    const chips = st.chipSpecs(tr);
    assert.deepEqual(chips.map(c => c.id), ['skip', 'decline', 'never', 'review', 'status']);
    const control = Object.values(words).flatMap(w => w[lang]).map(x => x.toLowerCase());
    for (const chip of chips) {
      assert.ok(chip.label.length > 0 && chip.label !== `chip.${chip.id}.label`, `${lang}: ${chip.id} has a label`);
      assert.ok(control.includes(chip.say.toLowerCase()), `${lang}: "${chip.say}" is a control word of chat_ops profile.words`);
    }
    // each chip names its own operation: skip -> skip, decline -> decline, never -> never, review -> review, status -> status
    for (const chip of chips) assert.ok(words[chip.id][lang].map(x => x.toLowerCase()).includes(chip.say.toLowerCase()), `${lang}: "${chip.say}" is a word of ${chip.id}`);
  }
  assert.deepEqual(st.chipSpecs(id => id), [], 'without the words there are no chips');
  for (const [lang, tr] of [['pl', t], ['en', tEn]]) {
    const idle = st.chipSpecs(tr, true);
    assert.deepEqual(idle.map(c => c.id), ['next', 'review', 'status'], 'under an answer that asks nothing: go on, summary, how many are left');
    const control = Object.values(words).flatMap(w => w[lang]).map(x => x.toLowerCase());
    for (const chip of idle) assert.ok(words[chip.id][lang].map(x => x.toLowerCase()).includes(chip.say.toLowerCase()), `${lang}: "${chip.say}" is a word of ${chip.id}`);
    assert.ok(control.includes(idle[0].say.toLowerCase()));
  }
});

test('the words: pl and en have the same keys and the same {{holes}}; no raw key or English phrase on the Polish screen', () => {
  assert.deepEqual(Object.keys(pl).sort(), Object.keys(en).sort());
  const holes = text => (text.match(/\{\{\w+\}\}/g) ?? []).sort().join();
  for (const key of Object.keys(pl)) {
    assert.ok(pl[key].length > 0 && en[key].length > 0, key);
    assert.equal(holes(pl[key]), holes(en[key]), `${key}: the same holes`);
    if (/^(chips\.order|chip\.\w+\.say)$/.test(key)) continue;   // data for the code (the ids, the words the kernel takes), not a line of the screen
    assert.equal(looksLikeDeveloperText(pl[key]), null, `${key}: "${pl[key]}" looks like developer text`);
  }
  // the owner's sentence of the card
  assert.equal(pl['card.body'], 'Zbuduję Twój profil w rozmowie. Pierwsze pytanie: jak dokładnie mam Cię znać.');
  assert.match(pl['card.privacy'], /bez modelu/);
});

test.after(() => loader.close());
