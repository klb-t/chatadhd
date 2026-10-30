// Language packs: one lookup (shellTranslator) over the catalogs; a language is data; a missing text comes from the fallback chain with an
// advisory, never as a key. SYNTHETIC data: the catalogs below are made up for the test (a language "xx" that exists nowhere else), which is
// the point - a new language works without a change in code.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const text = await loader.load('src/shell/text.ts');
const pack = await loader.load('src/shell/language-pack.ts');
const lang = await loader.load('src/shell/language.ts');

const feature = { feature: 'ui_shell', locale: 'en', catalog: { hello: 'Hello', only_en: 'Only English', product_name: 'P' },
  locales: { en: { hello: 'Hello', only_en: 'Only English', product_name: 'P' }, xx: { hello: 'Xhello' } } };

test('a new language is data only: present word from its catalog, missing word from the default language', () => {
  const t = text.shellTranslator(feature, 'xx');
  assert.equal(t('hello'), 'Xhello');
  assert.equal(t('only_en'), 'Only English');
  assert.equal(globalThis.window === undefined || window.__loomMissingText?.['xx:only_en'] === 'en', true);
});
test('no language has the word: the id (last resort), never a throw', () => {
  assert.equal(text.shellTranslator(feature, 'xx')('nowhere'), 'nowhere');
  assert.equal(text.shellTranslator(null, 'xx')('hello'), 'hello');
});
test('the available languages are the union of the catalogs; the page language walks the whole device list', () => {
  const available = pack.availableLanguages([feature]);
  assert.deepEqual(available.sort(), ['en', 'xx']);
  assert.equal(lang.shellLanguage('auto', undefined, ['de-DE', 'xx-YY', 'en'], available), 'xx');
  assert.equal(lang.shellLanguage('', undefined, ['pl-PL', 'en-US'], available), 'en');
  assert.equal(lang.shellLanguage('xx', undefined, ['pl'], available), 'xx');
  assert.equal(lang.shellLanguage('auto', undefined, ['de'], available), 'en');
});
test('the fallback chain starts with the language and ends with the default', () => {
  assert.deepEqual(pack.languageChain('xx'), ['xx', 'en']);
  assert.deepEqual(pack.languageChain('en'), ['en']);
});
