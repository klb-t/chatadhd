// W0 / P13: the registry of window kinds is complete: every component App.tsx / ChatShell.tsx mounts is a kind or `not_window` with a reason.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, access } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const reg = await loader.load('src/shell/windows/registry.ts');
const src = async f => readFile(new URL('../../src/' + f, import.meta.url), 'utf8');
const sources = { 'App.tsx': await src('App.tsx'), 'shell/ChatShell.tsx': await src('shell/ChatShell.tsx') };

/** Components of a source file: default-imported capitalised identifiers from ./components/ or ./shell/ plus capitalised JSX tags without a dot. */
function components(code) {
  const found = new Set();
  for (const m of code.matchAll(/^import\s+([A-Z]\w*)(?:\s*,\s*\{[^}]*\})?\s+from\s+"(\.{1,2}\/[^"]+)"/gm)) found.add(m[1]);
  const tags = new Set([...code.matchAll(/(?<![\w.])<([A-Z]\w*)[\s/>]/g)].map(m => m[1]));
  return { imported: found, tags };
}

test('P13: every mounted component is a registered kind or not_window (nothing skipped silently)', () => {
  const known = new Set([...reg.KINDS.map(k => k.component).filter(Boolean), ...reg.NOT_WINDOW.map(n => n.component)]);
  for (const [file, code] of Object.entries(sources)) {
    const { imported, tags } = components(code);
    assert.ok(imported.size >= 3, file + ': the import scan found components');
    for (const c of imported) assert.ok(known.has(c), `${file} imports ${c}, which is neither a window kind nor not_window`);
    for (const c of tags) if (!/^(Fragment|StrictMode)$/.test(c) && !imported.has(c) && !/^Shell|^Wrap/.test(c)) assert.ok(known.has(c), `${file} mounts <${c}>, unregistered`);
  }
});

test('P13: present kinds point at an existing component file; planned kinds carry a reason; ids are unique', async () => {
  const ids = reg.KINDS.map(k => k.id);
  assert.equal(new Set(ids).size, ids.length);
  for (const k of reg.KINDS) {
    assert.ok(['present', 'planned'].includes(k.status), k.id);
    assert.ok(typeof k.label.pl === 'string' && k.label.pl.length, k.id + ' label');
    if (k.status === 'planned') assert.ok(k.reason && k.reason.pl.length > 10, k.id + ' planned without reason');
    else {
      assert.ok(k.component, k.id + ' present without component');
      const found = await Promise.any(['components', 'shell'].map(d => access(new URL(`../../src/${d}/${k.component}.tsx`, import.meta.url))));
      void found;
    }
  }
  for (const n of reg.NOT_WINDOW) assert.ok(n.reason.pl.length > 10, n.component);
});

test('no component is both a kind and not_window; kinds have no enum-like closing (planned kinds are listed, not hidden)', () => {
  const nw = new Set(reg.NOT_WINDOW.map(n => n.component));
  for (const k of reg.KINDS) assert.ok(!k.component || !nw.has(k.component), k.id);
  assert.ok(reg.KINDS.filter(k => k.status === 'planned').length >= 1);
  const ctx = reg.kindsCtx();
  assert.equal(ctx.connection.status, 'planned');
});
