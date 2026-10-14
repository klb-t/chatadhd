// P14 4: a new step for the device reaches the open conversation as an event of the kernel (no polling, no refresh by hand).
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const e = await loader.load('src/context/client-step-events.ts');
test.after(() => loader.close());

const envelope = (payload, event = 'action:client_step') => ({ event, payload });
const waiting = { conv_id: 'c_1', message_id: 'm_9', run_id: 'ar_3', state: 'waiting', receipt_id: 'arr_1', step: 'a1.s1', action: 'device.record_audio' };

test('the event is read from the envelope of the feed; other events and payloads without ids are not steps', () => {
  assert.equal(e.CLIENT_STEP_EVENT, 'action:client_step');
  assert.deepEqual(e.clientStepEvent(envelope(waiting)), { convId: 'c_1', messageId: 'm_9', runId: 'ar_3', state: 'waiting' });
  assert.deepEqual(e.clientStepEvent(envelope({ ...waiting, state: 'closed', status: 'cancelled' })), { convId: 'c_1', messageId: 'm_9', runId: 'ar_3', state: 'closed' });
  assert.equal(e.clientStepEvent(envelope(waiting, 'message:created')), null);
  assert.equal(e.clientStepEvent(envelope({ conv_id: 'c_1' })), null);
  assert.equal(e.clientStepEvent(envelope(null)), null);
  assert.equal(e.clientStepEvent(null), null);
  assert.equal(e.clientStepEvent('action:client_step'), null);
});

function feed() {
  const opened = [];
  const source = { subscribeEvents: (event, handlers) => { const entry = { event, handlers, closed: false }; opened.push(entry); return () => { entry.closed = true; }; } };
  return { opened, source };
}
function clock() {
  const waits = [];
  return { waits, wait: (run, ms) => { const entry = { run, ms, cancelled: false }; waits.push(entry); return () => { entry.cancelled = true; }; } };
}

test('a transport without an event feed is not watched (the step shows after a refresh, as before)', () => {
  assert.equal(e.watchClientSteps({}, () => assert.fail('no feed')), null);
});

test('one feed, the kernel event only; every step of the feed is handed on, the others are dropped', () => {
  const { opened, source } = feed();
  const seen = [];
  const stop = e.watchClientSteps(source, (event) => seen.push(event));
  assert.equal(opened.length, 1);
  assert.equal(opened[0].event, 'action:client_step');
  opened[0].handlers.onChunk(envelope(waiting));
  opened[0].handlers.onChunk(envelope(waiting, 'task:changed'));
  opened[0].handlers.onChunk(envelope({ ...waiting, state: 'closed' }));
  assert.deepEqual(seen.map(event => event.state), ['waiting', 'closed']);
  stop();
  assert.equal(opened[0].closed, true);
  assert.equal(opened.length, 1, 'nothing reopens after stop');
});

test('a feed that ends is opened again after a wait that doubles up to the cap; a feed that delivers starts the wait over', () => {
  const { opened, source } = feed();
  const time = clock();
  const seen = [];
  const stop = e.watchClientSteps(source, (event) => seen.push(event), { firstMs: 100, maxMs: 350, wait: time.wait });
  opened[0].handlers.onError('network');
  opened[0].handlers.onDone();  // one loss is one wait, whichever way it is reported
  assert.deepEqual(time.waits.map(w => w.ms), [100]);
  time.waits[0].run();
  assert.equal(opened.length, 2);
  opened[1].handlers.onDone();
  time.waits[1].run();
  opened[2].handlers.onDone();
  time.waits[2].run();
  opened[3].handlers.onDone();
  assert.deepEqual(time.waits.map(w => w.ms), [100, 200, 350, 350]);
  time.waits[3].run();
  opened[4].handlers.onChunk(envelope(waiting));
  opened[4].handlers.onDone();
  assert.equal(time.waits[4].ms, 100, 'a feed that delivered something starts the wait over');
  assert.equal(seen.length, 1);
  // stopping while waiting cancels the wait: the feed is not opened again
  stop();
  assert.equal(time.waits[4].cancelled, true);
  assert.equal(opened.length, 5);
});
