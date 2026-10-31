// R101 D4 (PULPIT_R101_2026-10-28 §5): the page's side of the trash as pure functions - what an icon dropped on the trash moves
// (drop_on_trash: target | pin [examples], an unknown value = the preset target), what brings an item back, the merge of the kernel's
// items with the page's own layouts, the badge, the groups by kind. Examples first, then a seeded generative pass (LOOM_TRASH_RUNS).
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const T = await loader.load('src/shell/windows/desktop-trash.ts');
const I = await loader.load('src/shell/windows/desktop-icons.ts');
const RUNS = Number(process.env.LOOM_TRASH_RUNS ?? 300);
let seed = Number(process.env.LOOM_TRASH_SEED ?? 4004);
const rnd = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
const pick = a => a[Math.floor(rnd() * a.length)];

test('drop_on_trash: target moves the thing, pin only the icon, an unknown value is the preset', () => {
  assert.deepEqual(T.trashStepOf('conv:c_0123456789ab', undefined), { action: 'conversation.trash', args: { query: 'c_0123456789ab', from: 'desktop' } });
  assert.deepEqual(T.trashStepOf('conv:c_0123456789ab', 'pin', 'desktop:main'), { action: 'pin.trash', args: { target: 'conv:c_0123456789ab', desktop: 'desktop:main' } });
  assert.equal(T.trashStepOf('memory:abc', 'target').action, 'memory.trash');
  assert.equal(T.trashStepOf('kind:chat', 'target').action, 'pin.trash');   // no trash of its own: the pin
  assert.equal(T.trashStepOf('conv:c_1', 'rubbish').action, 'conversation.trash');
  assert.equal(T.trashable('trash:'), false);
  assert.equal(T.trashable('conv:c_1'), true);
});

test('restore, merge, badge, groups', () => {
  assert.deepEqual(T.restoreStepOf({ kind: 'conversation', id: 'c_1', title: 'x', at: '' }), { action: 'conversation.untrash', args: { query: 'c_1' } });
  assert.equal(T.restoreStepOf({ kind: 'layout', id: 'u_a', title: 'a', at: '' }), null);
  const items = T.mergeTrash([{ kind: 'conversation', id: 'c_1', title: 'Plan', at: '2026-10-01T00:00:00Z', from: 'desktop' }, { bad: 1 }, null],
    [{ id: 'u_a', label: 'Praca', trashed_at: '2026-10-02T00:00:00Z' }, { id: 'u_b', label: 'Dom' }]);
  assert.deepEqual(items.map(i => i.id), ['u_a', 'c_1']);
  assert.equal(T.trashBadge(0), '');
  assert.equal(T.trashBadge(1), '1');
  assert.equal(T.trashBadge(250), '99+');
  assert.deepEqual(T.byKind(items).map(([k]) => k), ['layout', 'conversation']);
  assert.equal(T.retentionShort(3), true);
  assert.equal(T.retentionShort(null), false);
});

test('a trashed pin is not drawn on the desktop; the other pins keep their order', () => {
  const nodes = [{ id: 'n_d', label: 'desktop:main' }, { id: 'n_a', label: 'kind:chat' }, { id: 'n_b', label: 'trash:' }];
  const links = [{ src: 'n_a', dst: 'n_d', metadata: { order: 0, address: 'kind:chat', trash: { at: 'x' } } }, { src: 'n_b', dst: 'n_d', metadata: { order: 1, address: 'trash:' } }];
  assert.deepEqual(I.pinsFrom('n_d', nodes, links).map(r => r.address), ['trash:']);
});

test('generative: any address and mode give one known step; the merge never loses or invents an item', () => {
  const prefixes = ['conv:', 'memory:', 'kind:', 'party:', 'trash:', 'n_', '', 'area:', 'zz:'];
  for (let r = 0; r < RUNS; r++) {
    const addr = pick(prefixes) + (rnd() < 0.2 ? '' : 'x' + Math.floor(rnd() * 1000));
    const st = T.trashStepOf(addr, pick(['target', 'pin', undefined, 7, 'x']));
    assert.ok(['conversation.trash', 'memory.trash', 'pin.trash'].includes(st.action), addr);
    const k = Array.from({ length: Math.floor(rnd() * 6) }, (_, i) => ({ kind: pick(['conversation', 'memory', 'pin']), id: 'k' + i, title: 't', at: rnd() < 0.2 ? '' : `2026-10-${10 + i}T00:00:00Z` }));
    const l = Array.from({ length: Math.floor(rnd() * 4) }, (_, i) => ({ id: 'l' + i, label: 'L', ...(rnd() < 0.5 ? { trashed_at: `2026-09-${10 + i}T00:00:00Z` } : {}) }));
    const m = T.mergeTrash(k, l);
    assert.equal(m.length, k.length + l.filter(x => x.trashed_at).length);
    for (let i = 1; i < m.length; i++) assert.ok((m[i - 1].at || '') >= (m[i].at || ''));
  }
});

test('trash and undo require interpreter completion, not merely a successful HTTP response', async () => {
  // Synthetic transport results; no server, model or user data.
  const step = T.trashStepOf('conv:c_example', 'target');
  const requests = [];
  const success = { id: 'run_example', status: 'completed', result: { data: { changed: true } } };
  assert.equal(await T.runTrashAction({ run: async request => { requests.push(request); return success; } }, step), success);
  assert.deepEqual(requests, [{ action: 'conversation.trash', element: { class: 'application' }, args: { query: 'c_example', from: 'desktop' } }]);
  for (const status of ['failed', 'awaiting_confirmation', 'cancelled', 'future_state']) {
    await assert.rejects(T.runTrashAction({ run: async () => ({ id: 'run_example', status }) }, step), { message: status });
  }
  await assert.rejects(T.runTrashAction({ run: async () => ({ id: 'run_example', status: 'failed', error: { message: 'Permission denied' } }) }, step), { message: 'Permission denied' });
  await assert.rejects(T.runTrashAction({ run: async () => { throw new Error('Transport failed'); } }, step), { message: 'Transport failed' });
});
