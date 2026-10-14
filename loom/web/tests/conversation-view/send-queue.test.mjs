// P16: a send typed while the conversation is being made, or while its view loads, waits and goes once; it is never dropped.
// The rendered composer is covered by e2e/chat-send-gate.mjs (the view in flight, the view that fails) and e2e/first-steps-ui.mjs
// (a real server: new conversation, click Send at once, also at 390x800).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const q = await loader.load('src/context/send-queue.ts');
const g = await loader.load('src/context/send-gate.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const catalog = pack.entries.find(row => row.key === 'presentation.chat_send').value.locales;

const view = (id = 'c') => ({ schema: 'loom.conversation_view/1', conversation_id: id, view_id: 'v', status: 'complete', messages: [], resources: [], omissions: [],
  capabilities: { source_history_send: { available: false, reason: 'not_applicable' } } });
const gateOf = patch => g.chatSendGate({ conversationId: 'c', readsView: true, view: view(), failure: null, sourceReadInFlight: false, blockWithoutSourceHistory: false, ...patch });
const loading = gateOf({ view: null });
const open = gateOf({});
const failed = gateOf({ view: null, failure: { conversationId: 'c', reason: 'HTTP 503', lastKnown: null } });
const reading = gateOf({ sourceReadInFlight: true });
const now = (patch = {}) => ({ convId: 'c', creating: false, gate: open, ...patch });

test('only two transient states of the page queue a send; every other gate reason keeps its behaviour', () => {
  assert.equal(q.queueReason(now({ gate: loading })), 'conversation_view_loading');
  assert.equal(q.queueReason(now({ convId: null, creating: true, gate: gateOf({ conversationId: null }) })), 'conversation_creating');
  assert.equal(q.queueReason(now()), null);
  assert.equal(q.queueReason(now({ gate: reading })), null, 'a local source read is shown, not queued');
  assert.equal(q.queueReason(now({ gate: gateOf({ view: { ...view(), resources: [{ unit_id: 'u', status: 'read_denied', current: false }],
    capabilities: { source_history_send: { available: false, reason: 'source_egress_not_bound' } } }, blockWithoutSourceHistory: true }) })), null,
  'the user\'s own block is shown, not queued');
  assert.equal(q.queueReason(now({ gate: failed })), null, 'a failed view is not a wait');
  assert.equal(q.queueReason(now({ creating: true })), null, 'a conversation is already selected: the creation in flight is another one');
  assert.equal(q.VIEW_LOADING_REASON, loading.blocked.reason);
});

test('a queued send waits while anything is pending, then goes exactly when nothing waits', () => {
  const queued = { convId: 'c' };
  assert.equal(q.queueStep(queued, now({ gate: loading })), 'wait');
  assert.equal(q.queueStep(queued, now()), 'send');
  // another reason that blocks once the view is there: the send is attempted and the page shows why (current behaviour of Send)
  assert.equal(q.queueStep(queued, now({ gate: reading })), 'send');
  // the view failed: said, not sent, the text stays
  assert.equal(q.queueStep(queued, now({ gate: failed })), 'failed');
  // the person went elsewhere: nothing is sent into that other conversation
  assert.equal(q.queueStep(queued, now({ convId: 'other' })), 'drop');
  assert.equal(q.queueStep(queued, now({ convId: null, gate: gateOf({ conversationId: null }) })), 'drop');
});

test('a send typed while the conversation is being made waits for it, then for its view', () => {
  const queued = { convId: null };
  const blank = gateOf({ conversationId: null });
  assert.equal(q.queueStep(queued, now({ convId: null, creating: true, gate: blank })), 'wait');
  // the page was told which conversation it is, its view is not here yet: still waiting (the caller adopts the conversation)
  assert.equal(q.queueStep(queued, now({ convId: 'c', creating: false, gate: loading })), 'wait');
  assert.equal(q.queueStep({ convId: 'c' }, now({ gate: open })), 'send');
  // the creation ended and the page has no conversation (it failed): said, the text stays
  assert.equal(q.queueStep(queued, now({ convId: null, creating: false, gate: blank })), 'failed');
});

test('the creation in flight is counted per creation and ends once', () => {
  assert.equal(q.creationInFlight(), false);
  const first = q.beginCreation();
  const second = q.beginCreation();
  assert.equal(q.creationInFlight(), true);
  first(); first();  // ending twice is one end
  assert.equal(q.creationInFlight(), true);
  second();
  assert.equal(q.creationInFlight(), false);
});

test('the notes are data in both languages, and without a catalog the machine reason shows instead', () => {
  for (const language of ['en', 'pl']) {
    assert.ok(catalog[language].queued_waiting && catalog[language].queued_failed, language);
    const feature = p.resolvePresentationFeature(p.resolvePresentation(p.resolvePresentation().pack, language), 'chat_send');
    const texts = g.sendGateTexts(feature, open);
    assert.equal(texts.queuedWaiting, catalog[language].queued_waiting);
    assert.equal(texts.queuedFailed, catalog[language].queued_failed);
  }
  assert.notEqual(catalog.en.queued_waiting, catalog.pl.queued_waiting);
  assert.match(catalog.pl.queued_waiting, /Czekam na wczytanie rozmowy/);
  assert.equal(g.sendGateTexts(null, open).queuedWaiting, null);
});
