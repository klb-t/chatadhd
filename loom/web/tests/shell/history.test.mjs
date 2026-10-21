// M1-W0 / §3.14: the stack of layers over the browser history. A fake browser models the one thing that matters: history.back() is
// asynchronous (the traversal runs later, relative to the entry the history stands on THEN), so a push made before it would be undone.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const h = await loader.load('src/shell/history.ts');

function fakeBrowser(initialState = null) {
  const entries = [{ state: initialState }];
  let index = 0;
  const listeners = {};
  const queue = [];
  const fire = (type, event = {}) => { for (const fn of listeners[type] ?? []) fn(event); };
  const win = {
    history: {
      get state() { return entries[index].state; },
      pushState(state) { entries.splice(index + 1); entries.push({ state }); index += 1; },
      replaceState(state) { entries[index].state = state; },
      back() { queue.push(() => { if (index > 0) { index -= 1; fire('popstate'); } }); },
    },
    addEventListener(type, fn) { (listeners[type] ??= []).push(fn); },
  };
  return {
    win,
    get index() { return index; },
    get length() { return entries.length; },
    pending: () => queue.length,
    /** The user presses Back (the same asynchronous traversal as history.back()). */
    userBack() { win.history.back(); },
    /** The user presses Forward. */
    userForward() { queue.push(() => { if (index < entries.length - 1) { index += 1; fire('popstate'); } }); },
    /** The history catches up: every queued traversal runs, including those that popstate handlers queue. */
    flush() { let guard = 0; while (queue.length) { queue.shift()(); if (++guard > 100) throw new Error('the history never settles'); } },
    key(key, extra = {}) { const event = { key, defaultPrevented: false, isComposing: false, preventDefault() { this.defaultPrevented = true; }, ...extra }; fire('keydown', event); return event; },
  };
}
function setup(options) {
  const b = fakeBrowser();
  const stack = h.installLayerStack(b.win, options);
  const closed = [];
  const open = id => stack.push(id, () => closed.push(id));
  return { b, stack, closed, open };
}

test('several layers: Back closes them one by one, topmost first, and the history ends at the base', () => {
  const { b, stack, closed, open } = setup();
  open('menu'); open('sheet'); open('card');
  assert.deepEqual(stack.ids, ['menu', 'sheet', 'card']);
  assert.equal(b.index, 3);
  for (const expected of ['card', 'sheet', 'menu']) {
    b.userBack(); b.flush();
    assert.equal(closed.at(-1), expected);
  }
  assert.deepEqual(closed, ['card', 'sheet', 'menu']);
  assert.equal(stack.size, 0); assert.equal(b.index, 0);
});

test('window.__loomBack() is synchronous, true only when it closed something, and the second call at the root falls through', () => {
  const { b, stack, closed, open } = setup();
  assert.equal(typeof b.win.__loomBack, 'function');
  assert.equal(b.win.__loomBack(), false);   // nothing open: the shell may go back in the WebView history or ask
  open('a'); open('b');
  assert.equal(b.win.__loomBack(), true);
  assert.deepEqual(closed, ['b']);           // closed at once, before the history caught up
  assert.equal(b.win.__loomBack(), true);    // a second Back before popstate closes the next layer
  assert.deepEqual(closed, ['b', 'a']);
  b.flush();
  assert.equal(b.index, 0); assert.equal(stack.size, 0);
  assert.equal(b.win.__loomBack(), false);   // at the root again: falls through
  assert.equal(b.pending(), 0);              // and it touched no history
});

test('closing one layer and opening another in the same moment reuses the entry (no back + push race)', () => {
  const { b, stack, closed, open } = setup();
  open('menu');
  stack.remove('menu'); open('panel');       // the ⋮ menu closes, a panel opens, in one React commit
  b.flush();
  assert.deepEqual(stack.ids, ['panel']);
  assert.equal(b.index, 1); assert.equal(b.length, 2);
  assert.deepEqual(closed, []);              // the panel was not undone by the pending back
  b.userBack(); b.flush();
  assert.deepEqual(closed, ['panel']); assert.equal(b.index, 0);
});

test('push, remove, push of the same layer in one commit (React StrictMode effects) leaves one open layer and one entry', () => {
  const { b, stack, closed, open } = setup();
  open('sheet'); stack.remove('sheet'); open('sheet');
  b.flush();
  assert.deepEqual(stack.ids, ['sheet']); assert.equal(b.index, 1); assert.deepEqual(closed, []);
});

test('the page closes a layer itself: its entry is given back, Back then does not land on a closed layer', () => {
  const { b, stack, closed, open } = setup();
  open('sheet');
  stack.remove('sheet');
  b.flush();
  assert.equal(b.index, 0); assert.equal(stack.size, 0);
  assert.deepEqual(closed, []);              // close() is for Back/Escape; the page already knew
  assert.equal(b.win.__loomBack(), false);
});

test('the page closes a lower layer while an upper one stays: the entry count follows, Back still closes the upper first', () => {
  const { b, stack, closed, open } = setup();
  open('a'); open('b');
  stack.remove('a'); b.flush();
  assert.deepEqual(stack.ids, ['b']); assert.equal(b.index, 1);
  b.userBack(); b.flush();
  assert.deepEqual(closed, ['b']); assert.equal(b.index, 0);
});

test('two Back presses before popstate: the backs are serialised and the history ends at the base', () => {
  const { b, stack, closed, open } = setup();
  open('a'); open('b'); open('c');
  assert.equal(b.win.__loomBack(), true); assert.equal(b.win.__loomBack(), true);
  assert.equal(b.pending(), 1);              // only one history.back() in flight at a time
  b.flush();
  assert.deepEqual(closed, ['c', 'b']); assert.equal(b.index, 1); assert.deepEqual(stack.ids, ['a']);
});

test('opening the same layer twice adds no entry and keeps the latest close handler', () => {
  const { b, stack } = setup();
  const calls = [];
  stack.push('sheet', () => calls.push('old')); stack.push('sheet', () => calls.push('new'));
  assert.equal(stack.size, 1); assert.equal(b.index, 1);
  b.win.__loomBack();
  assert.deepEqual(calls, ['new']);
});

test('Escape closes the topmost layer and is consumed; at the root, with a default already prevented, or while composing text it is left alone', () => {
  const { b, stack, closed, open } = setup();
  assert.equal(b.key('Escape').defaultPrevented, false);        // nothing open
  open('menu'); open('card');
  assert.equal(b.key('Enter').defaultPrevented, false); assert.equal(stack.size, 2);
  assert.equal(b.key('Escape', { isComposing: true }).defaultPrevented, false); assert.equal(stack.size, 2);
  assert.equal(b.key('Escape', { defaultPrevented: true }).defaultPrevented, true); assert.equal(stack.size, 2);
  const event = b.key('Escape');
  assert.equal(event.defaultPrevented, true); assert.deepEqual(closed, ['card']);
  b.flush(); assert.equal(b.index, 1);
  b.key('Escape'); b.flush();
  assert.deepEqual(closed, ['card', 'menu']); assert.equal(b.index, 0);
});

test('a reload inside a stack starts at the base (the stale depth is rewritten) and installing twice returns the same stack', () => {
  const b = fakeBrowser({ loomDepth: 3 });
  const stack = h.installLayerStack(b.win);
  assert.equal(b.win.history.state.loomDepth, 0);
  assert.equal(stack.size, 0);
  assert.equal(h.installLayerStack(b.win), stack);   // idempotent
});

test('a hash link or another navigation that lands on the base closes every layer; Forward into a stale entry is undone', () => {
  const { b, stack, closed, open } = setup();
  open('a'); open('b');
  stack.popped(0);                                    // a popstate to an entry of depth 0 that we did not ask for
  assert.deepEqual(closed, ['b', 'a']); assert.equal(stack.size, 0);
  b.flush(); assert.equal(b.index, 0);
  const second = setup();
  second.open('a'); second.b.userBack(); second.b.flush();   // closed by Back
  second.b.userForward(); second.b.flush();                   // the entry of the closed layer is still ahead
  assert.equal(second.stack.size, 0);
  assert.equal(second.b.index, 0);                            // reconcile went back again: no layer is reopened by Forward
});

test('a close handler that throws does not stop the others', () => {
  const { stack } = setup();
  const closed = [];
  stack.push('a', () => closed.push('a'));
  stack.push('b', () => { throw new Error('gone'); });
  stack.popped(0);
  assert.deepEqual(closed, ['a']); assert.equal(stack.size, 0);
});

test('a back() that never answers (nothing behind the entry) stops blocking after the timeout', () => {
  let t = 0;
  const calls = [];
  let depth = 1;
  const host = { push: d => { depth = d; calls.push(`push${d}`); }, replace: () => {}, back: () => calls.push('back'), currentDepth: () => depth };
  const stack = new h.LayerStack(host, { backTimeoutMs: 500, now: () => t });
  stack.push('a', () => {});                  // depth 1 already (the entry exists): no push
  stack.remove('a');                          // have 1 > want 0: back(), no popstate ever comes
  assert.deepEqual(calls, ['back']);
  t = 100; stack.push('b', () => {});         // still waiting
  assert.deepEqual(calls, ['back']);
  t = 700; stack.push('c', () => {});         // gave up waiting: reconciles (have 1 < want 2)
  assert.deepEqual(calls, ['back', 'push2']);
  assert.equal(h.BACK_TIMEOUT_PRESET_MS, 1000);
});
