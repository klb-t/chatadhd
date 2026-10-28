// W6 fix (CI 6ab55aae): src/shell/windows/param-echo.ts without a browser - a component that reports and follows a window parameter
// (the navigator's map: zoom, pan) consumes the echoes of its own reports and applies every value from elsewhere.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const { heard, wrote, sameParamValue, ECHO_QUEUE_LIMIT_PRESET } = await loader.load('src/shell/windows/param-echo.ts');

test('the CI race: the echo of the − step (1) rendered together with the pinch (2) is consumed, then the echo of 2 too', () => {
  let q = wrote([], 1.25, 1); assert.deepEqual(q, [1.25]);
  let h = heard(q, 1.25); assert.equal(h.apply, false); q = h.queue; assert.deepEqual(q, []);
  q = wrote(q, 1, 1.25);                         // − step reported, echo not yet rendered
  q = wrote(q, 2, 1.25);                         // pinch reported (the value rendered is still 1.25)
  h = heard(q, 1); assert.equal(h.apply, false, 'the old echo does not move the camera back'); q = h.queue; assert.deepEqual(q, [2]);
  h = heard(q, 2); assert.equal(h.apply, false); assert.deepEqual(h.queue, []);
});

test('a value from elsewhere (binding, channel, restore) is applied and drops the waiting reports', () => {
  assert.deepEqual(heard([], 3), { apply: true, queue: [] });
  assert.deepEqual(heard([1, 2], 0.5), { apply: true, queue: [] });
  // a restore at mount: the home report refused while the restored value waits never echoes; the restored value itself is applied
  let q = wrote([], 1, undefined); assert.deepEqual(q, [1]);
  assert.equal(heard(q, 1.7).apply, true);
});

test('an echo consumes every older report (batched renders skip intermediate values)', () => {
  assert.deepEqual(heard([1, 1.25, 1.5, 2], 1.5), { apply: false, queue: [2] });
  assert.deepEqual(heard([1, 2, 1], 1), { apply: false, queue: [] }, 'the newest equal report is the one heard');
});

test('a report equal to the value already there queues nothing (no echo will come); with reports waiting it is queued', () => {
  assert.deepEqual(wrote([], 1, 1), []);
  assert.deepEqual(wrote([], { x: 0, y: 0 }, { x: 0, y: 0 }), []);
  assert.deepEqual(wrote([2], 1, 1), [2, 1]);
});

test('the queue is bounded (preset, configurable)', () => {
  let q = [];
  for (let i = 0; i < ECHO_QUEUE_LIMIT_PRESET + 10; i++) q = wrote(q, i, -1);
  assert.equal(q.length, ECHO_QUEUE_LIMIT_PRESET);
  assert.equal(q[q.length - 1], ECHO_QUEUE_LIMIT_PRESET + 9);
  assert.deepEqual(wrote([1, 2, 3], 4, 0, 2), [3, 4]);
});

test('pan objects compare field by field, numbers within 1e-9', () => {
  assert.equal(heard([{ x: 10, y: -5 }], { y: -5, x: 10 }).apply, false);
  assert.equal(heard([{ x: 10, y: -5 }], { x: 10, y: -4 }).apply, true);
  assert.ok(sameParamValue(0.1 + 0.2, 0.3));
  assert.ok(!sameParamValue(1, '1'));
  assert.ok(!sameParamValue({ x: 1 }, { x: 1, y: 2 }));
  assert.ok(!sameParamValue([1], { 0: 1 }));
});
