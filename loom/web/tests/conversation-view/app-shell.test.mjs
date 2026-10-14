// P18: the words of the application shell (the "Menu" button of the phone header) are data, in both languages, not literals of the page.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const p = await loader.load('src/onboarding/presentation.mjs');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const generated = JSON.parse(await readFile(new URL('../../src/onboarding/generated/conversation-view.json', import.meta.url), 'utf8'));
const entry = pack.entries.find(row => row.key === 'presentation.app_shell');
const source = await readFile(new URL('../../src/App.tsx', import.meta.url), 'utf8');

test('the shell words exist in both languages with the same keys, and the generated web catalog carries them', () => {
  const { en, pl } = entry.value.locales;
  assert.deepEqual(Object.keys(en).sort(), Object.keys(pl).sort());
  assert.deepEqual(Object.keys(en).sort(), ['menu', 'menu_label']);
  assert.equal(pl.menu_label, 'Otwórz menu');
  assert.notEqual(en.menu_label, pl.menu_label);
  assert.deepEqual(generated.entries.find(row => row.key === 'presentation.app_shell').value, entry.value);
});

test('the feature resolves like every other presentation feature', () => {
  const feature = p.resolvePresentationFeature(p.resolvePresentation(), 'app_shell');
  assert.equal(p.featureMessage(feature, 'menu'), entry.value.locales.en.menu);
  const polish = p.featureInLanguage(feature, 'pl');
  assert.equal(p.featureMessage(polish, 'menu_label'), 'Otwórz menu');
});

test('the page has no word of its own for the button: it asks the catalog', () => {
  assert.match(source, /shellText\("menu"\)/);
  assert.match(source, /shellText\("menu_label"\)/);
  assert.doesNotMatch(source, /aria-label="Menu"/);
  assert.doesNotMatch(source, />\s*Menu\s*</);
});
