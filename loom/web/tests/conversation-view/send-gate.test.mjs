// R43 send gate: pure decision plus data texts. The rendered composer is covered by
// e2e/chat-send-gate.mjs (browser) and e2e/conversation-view-native.mjs (native).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const g = await loader.load('src/context/send-gate.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const generated = JSON.parse(await readFile(new URL('../../src/onboarding/generated/conversation-view.json', import.meta.url), 'utf8'));
const entry = key => pack.entries.find(row => row.key === key);
const en = entry('presentation.chat_send').value.locales.en, pl = entry('presentation.chat_send').value.locales.pl;
const linked = (id = 'c') => ({ schema: 'loom.conversation_view/1', conversation_id: id, view_id: 'view:linked', status: 'partial', messages: [],
  resources: [{ unit_id: 'unit', status: 'read_denied', current: false }], omissions: [],
  capabilities: { source_history_send: { available: false, reason: 'source_egress_not_bound' } } });
const plain = (id = 'c') => ({ ...linked(id), resources: [], capabilities: { source_history_send: { available: false, reason: 'not_applicable' } } });
const input = patch => ({ conversationId: 'c', readsView: true, view: linked(), failure: null, sourceReadInFlight: false,
  blockWithoutSourceHistory: false, ...patch });
const base = p.resolvePresentation();
const english = p.resolvePresentationFeature(base, 'chat_send');
const polish = p.resolvePresentationFeature(p.resolvePresentation(base.pack, 'pl'), 'chat_send');
const omittedReason = (catalog, code) => p.formatTemplate(catalog.reason, { reason: code, explanation: catalog[`reason.${code}`] ?? catalog['reason.unrecognized'] });

test('missing source-history capability informs and sends by default (no initiator, no block)', () => {
  const gate = g.chatSendGate(input());
  assert.deepEqual(gate, { blocked: null, viewLoading: false, viewFailure: null, sourceHistory: 'source_egress_not_bound' });
  const texts = g.sendGateTexts(english, gate);
  assert.equal(texts.blocked, null);
  assert.equal(texts.sourceHistory, en.source_history_omitted);
  assert.equal(texts.sourceReason, omittedReason(en, 'source_egress_not_bound'));
  assert.match(texts.sourceReason, /source_egress_not_bound/);
  // Reference rows alone also mark linked history; a plain conversation has nothing to say.
  assert.equal(g.chatSendGate(input({ view: { ...linked(), resources: [], messages: [{ id: 'r', storage: 'reference' }] } })).sourceHistory, 'source_egress_not_bound');
  assert.deepEqual(g.chatSendGate(input({ view: plain() })), { blocked: null, viewLoading: false, viewFailure: null, sourceHistory: null });
  const unnamed = linked(); delete unnamed.capabilities.source_history_send.reason;
  assert.equal(g.chatSendGate(input({ view: unnamed })).sourceHistory, 'unspecified');
});

test('only the user setting blocks, naming the user as initiator and how to undo it', () => {
  const gate = g.chatSendGate(input({ blockWithoutSourceHistory: true }));
  assert.deepEqual(gate.blocked, { initiator: 'user', reason: 'source_egress_not_bound', message: 'blocked_by_setting' });
  const texts = g.sendGateTexts(english, gate);
  assert.equal(texts.blocked, p.formatTemplate(en.blocked_by_setting, { setting: en.setting_label }));
  assert.ok(texts.blocked.includes(en.setting_label));
  assert.equal(texts.unblock, en.unblock);
  assert.equal(texts.sourceReason, omittedReason(en, 'source_egress_not_bound'));
  // The setting is scoped to linked history: a plain conversation still sends.
  assert.equal(g.chatSendGate(input({ view: plain(), blockWithoutSourceHistory: true })).blocked, null);
  // The user's choice outlasts a transient read, so it is the explanation shown.
  assert.equal(g.chatSendGate(input({ blockWithoutSourceHistory: true, sourceReadInFlight: true })).blocked.initiator, 'user');
});

test('a failed view request allows sending with a notice; a known linked view keeps the user block', () => {
  const failure = { conversationId: 'c', reason: 'HTTP 500: synthetic failure', lastKnown: null };
  for (const blockWithoutSourceHistory of [false, true]) {
    const gate = g.chatSendGate(input({ view: null, failure, blockWithoutSourceHistory }));
    assert.deepEqual(gate, { blocked: null, viewLoading: false, viewFailure: failure.reason, sourceHistory: null });
  }
  const texts = g.sendGateTexts(english, g.chatSendGate(input({ view: null, failure })));
  assert.equal(texts.viewFailure, p.formatTemplate(en.view_unavailable, { reason: failure.reason }));
  const known = { ...failure, lastKnown: linked() };
  assert.equal(g.chatSendGate(input({ view: null, failure: known, blockWithoutSourceHistory: true })).blocked.initiator, 'user');
  const informed = g.chatSendGate(input({ view: null, failure: known }));
  assert.deepEqual(informed, { blocked: null, viewLoading: false, viewFailure: failure.reason, sourceHistory: 'source_egress_not_bound' });
  // A failure recorded for another conversation does not describe the selected one.
  assert.equal(g.chatSendGate(input({ view: null, failure: { ...failure, conversationId: 'other' } })).blocked.reason, 'conversation_view_loading');
  assert.equal(g.chatSendGate(input({ view: null, failure: { ...known, lastKnown: linked('other') }, blockWithoutSourceHistory: true })).blocked, null);
});

test('in-flight view and source reads are transient integrity gates with data explanations', () => {
  for (const view of [null, linked('previous')]) {
    const gate = g.chatSendGate(input({ view }));
    assert.deepEqual(gate.blocked, { initiator: 'invariant:integrity', reason: 'conversation_view_loading', message: 'view_loading' });
    assert.equal(gate.viewLoading, true);
    assert.equal(g.sendGateTexts(english, gate).blocked, en.view_loading);
  }
  assert.equal(g.chatSendGate(input({ view: null, readsView: false })).blocked, null, 'hosts without the view reader are not gated');
  assert.equal(g.chatSendGate(input({ view: null, conversationId: null })).blocked, null, 'a new conversation has no view to wait for');
  const reading = g.chatSendGate(input({ sourceReadInFlight: true }));
  assert.deepEqual(reading.blocked, { initiator: 'invariant:integrity', reason: 'source_read_in_flight', message: 'source_read_in_flight' });
  assert.equal(g.sendGateTexts(english, reading).blocked, en.source_read_in_flight);
});

test('gate texts are localized pack data and degrade to machine codes without a usable catalog', () => {
  assert.deepEqual(generated, pack, 'web catalog is generated from the canonical pack');
  assert.equal(pack.entries[0].key, 'presentation.conversation_view', 'install prompt keeps reading the source-view entry');
  assert.deepEqual(Object.keys(pl).sort(), Object.keys(en).sort());
  assert.equal(entry('chat.send.block_without_source_history').value, false, 'preset informs and sends');
  const gate = g.chatSendGate(input({ blockWithoutSourceHistory: true }));
  const polishTexts = g.sendGateTexts(polish, gate);
  assert.equal(polishTexts.blocked, p.formatTemplate(pl.blocked_by_setting, { setting: pl.setting_label }));
  assert.equal(polishTexts.sourceReason, omittedReason(pl, 'source_egress_not_bound'));
  assert.equal(polishTexts.setting, pl.setting_label);
  const machine = g.sendGateTexts(null, gate);
  assert.deepEqual(machine, { blocked: 'source_egress_not_bound', sourceHistory: 'source_egress_not_bound', sourceReason: null,
    viewFailure: null, setting: null, settingHelp: null, unblock: null });
  assert.equal(g.sendGateTexts(null, g.chatSendGate(input({ view: null, failure: { conversationId: 'c', reason: 'raw', lastKnown: null } }))).viewFailure, 'raw');
  const unknown = linked(); unknown.capabilities.source_history_send.reason = 'future_reason';
  assert.equal(g.sendGateTexts(english, g.chatSendGate(input({ view: unknown }))).sourceReason, omittedReason(en, 'future_reason'));
  const broken = { ...english, catalog: { ...english.catalog, blocked_by_setting: 'Broken {{template' } };
  assert.equal(g.sendGateTexts(broken, gate).blocked, 'source_egress_not_bound', 'a malformed user template never takes the composer down');
});

test('profiles holding the revision-1 source-view catalog stay complete; the new catalog is separately optional', () => {
  const legacy = { status: 'effective', enabled: true, excluded: false, value: pack.entries[0].value };
  assert.ok(p.resolvePresentationFeature(base, 'conversation_view', legacy));
  assert.throws(() => p.resolvePresentationFeature(base, 'chat_send', null), p.PresentationError);
  const custom = structuredClone(entry('presentation.chat_send').value); custom.locales.en.unblock = 'Synthetic owner label';
  const effective = { status: 'effective', enabled: true, excluded: false, value: custom };
  assert.equal(g.sendGateTexts(p.resolvePresentationFeature(base, 'chat_send', effective), g.chatSendGate(input({ blockWithoutSourceHistory: true }))).unblock, 'Synthetic owner label');
});
