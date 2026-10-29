// M1-NAZWA-PRODUKTU: the product on screen is "ChatADHD" (one setting: the ui_shell word product_name, {{product}} in texts);
// "Loom" is the library and may be visible only in the allowed places: the About sheet (about_built_on, about_name_hint), documentation and the
// self-knowledge nodes of the graph (listed below by file). Technical identifiers (schema ids, file names, key names, JNI names) are not screen text.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
const read = async rel => await readFile(new URL(rel, import.meta.url), 'utf8');
const json = async rel => JSON.parse(await read(rel));

/** Every string value (not key) of a JSON value with its path. */
function* strings(v, path = '') {
  if (typeof v === 'string') yield [path, v];
  else if (Array.isArray(v)) for (let i = 0; i < v.length; i++) yield* strings(v[i], `${path}[${i}]`);
  else if (v && typeof v === 'object') for (const [k, x] of Object.entries(v)) yield* strings(x, `${path}.${k}`);
}
/** A text meant to be read: has a space or is a sentence, and is not a technical identifier (dotted/underscored/slash names, JNI symbols). */
const technical = s => /^[a-z0-9_.\/:-]+$/.test(s) || /loom[._/]|LoomNative|LoomBridge|loom-server|libloom/.test(s);
const ALLOWED_WORDS = new Set(['about_built_on', 'about_name_hint']);

test('presentation catalogs say ChatADHD; Loom only in the About words', async () => {
  const view = await json('../../../data/profiles/conversation_view.pack');
  const bad = [];
  for (const entry of view.entries) {
    for (const [path, s] of strings(entry.value)) {
      if (!/Loom/.test(s) || technical(s)) continue;
      const word = path.split('.').pop();
      if (!ALLOWED_WORDS.has(word)) bad.push(`${entry.key}${path}: ${s.slice(0, 80)}`);
    }
  }
  assert.deepEqual(bad, []);
});

test('product_name is a word of ui_shell in every language', async () => {
  const view = await json('../../../data/profiles/conversation_view.pack');
  const shell = view.entries.find(e => e.key === 'presentation.ui_shell').value.locales;
  for (const lang of Object.keys(shell)) assert.equal(shell[lang].product_name, 'ChatADHD', lang);
  for (const lang of Object.keys(shell)) for (const id of ['menu_about', 'about_title', 'about_version', 'about_built_on']) assert.equal(typeof shell[lang][id], 'string', `${lang}.${id}`);
  assert.match(shell.pl.about_built_on, /Loom/);
});

test('labels the person sees in the data packs name the app ChatADHD', async () => {
  for (const rel of ['../../../data/runtime/chat_context.pack', '../../../data/runtime/import_graph.pack', '../../../data/runtime/graph_entry.pack', '../../../data/graph/classes.pack', '../../../data/graph/devices.pack', '../../../data/graph/environments.pack']) {
    const bad = [...strings(JSON.parse(await read(rel)))].filter(([p, s]) => /(^|\.)(en|pl)$/.test(p) && /Loom/.test(s)).map(([p, s]) => `${p}: ${s.slice(0, 60)}`);
    assert.deepEqual(bad, [], rel);
  }
});

test('the page title and the profile labels are not Loom', async () => {
  assert.match(await read('../../index.html'), /<title>ChatADHD<\/title>/);
  for (const f of ['loom-default', 'loom-default-r2', 'nextchat-2.16.1', 'librechat-0.8.8']) {
    const bad = [...strings(await json(`../../src/profiles/data/${f}.json`))].filter(([p, s]) => /Loom/.test(s) && !technical(s)).map(([p, s]) => `${p}: ${s.slice(0, 60)}`);
    assert.deepEqual(bad, [], f);
  }
});

test('the menu has About and the shell code names the product through productName()', async () => {
  const layouts = await json('../../src/shell/data/layouts.json');
  assert.ok(layouts.layouts.czat.menu.some(e => e.id === 'about' && e.action === 'sheet:about'));
  const chat = await read('../../src/shell/ChatShell.tsx');
  assert.match(chat, /about-sheet/);
  const view = await read('../../src/components/ChatView.tsx');
  assert.doesNotMatch(view, /Message Loom/);
});
