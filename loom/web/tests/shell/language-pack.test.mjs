// Language packs: one lookup (shellTranslator) over the catalogs; a language is data; a missing text comes from the fallback chain with an
// advisory, never as a key. SYNTHETIC data: the catalogs below are made up for the test (a language "xx" that exists nowhere else), which is
// the point - a new language works without a change in code.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
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

test('sorting follows an added locale and falls back safely for custom catalog ids', () => {
  assert.ok(pack.languageCollator('sv').compare('zebra', 'ångström') < 0, 'Swedish places å after z');
  assert.ok(pack.languageCollator('en').compare('zebra', 'ångström') > 0, 'English treats å like a');
  for (const language of ['xx', 'not a BCP 47 tag', '']) {
    assert.equal(pack.languageCollator(language).compare('zebra', 'ångström'), pack.languageCollator('en').compare('zebra', 'ångström'));
  }
});

// Synthetic catalogs exercise an arbitrary added language, sparse legacy labels and inert input.
test('data labels use every language in data, preserving explicit empty labels and legacy sparse labels', () => {
  assert.equal(text.dataLabel({ en: 'Open', nl: 'Openen', xx: 'Xopen' }, 'nl', 'missing'), 'Openen');
  assert.equal(text.dataLabel({ en: 'Open', xx: 'Xopen' }, 'xx', 'missing'), 'Xopen');
  assert.equal(text.dataLabel({ en: 'Open', xx: '' }, 'xx', 'missing'), '');
  assert.equal(text.dataLabel({ pl: 'Otwórz' }, 'nl', 'missing'), 'Otwórz');
  assert.equal(text.dataLabel({ en: 3 }, 'nl', 'missing'), 'missing');
  assert.equal(text.dataLabel(['not a catalog'], 'en', 'missing'), 'missing');
  assert.equal(text.dataLabel(Object.create({ en: 'inherited' }), 'en', 'missing'), 'missing');
});

test('translation transforms and command aliases retain arbitrary language catalogs', () => {
  const words = { en: 'A {id}', nl: 'B {id}', xx: 'C {id}', empty: '', invalid: 3 };
  const before = JSON.stringify(words);
  assert.deepEqual(pack.mapLocalizedText(words, (line, language) => language + ':' + line.replace(/\{id\}/g, () => '$&{id}')),
    { en: 'en:A $&{id}', nl: 'nl:B $&{id}', xx: 'xx:C $&{id}', empty: 'empty:' });
  assert.equal(JSON.stringify(words), before);
  assert.deepEqual(pack.mapLocalizedText(Object.create({ en: 'inherited' }), line => line), {});
  assert.deepEqual(pack.localizedPhrases({ nl: ['volgorde'], zh: ['按名称'], xx: ['x', 3], bad: 'not a list' }), ['volgorde', '按名称', 'x']);
  assert.deepEqual(pack.localizedPhrases(null), []);
});

test('window style, arrangement, coupling and structure dictionaries have Dutch text with matching parameters', async () => {
  const parameters = text => [...text.matchAll(/\{\{?(\w+)\}?\}/g)].map(match => match[1]).sort();
  for (const file of ['styles', 'layouts', 'coupling', 'structure_mappings']) {
    const data = JSON.parse(await readFile(new URL(`../../src/shell/data/${file}.json`, import.meta.url), 'utf8'));
    let checked = 0;
    const walk = (value, path = file) => {
      if (!value || typeof value !== 'object') return;
      if (typeof value.en === 'string' && typeof value.pl === 'string') {
        assert.equal(typeof value.nl, 'string', path);
        assert.deepEqual(parameters(value.nl), parameters(value.en), path);
        checked += 1;
      } else for (const [key, child] of Object.entries(value)) walk(child, `${path}/${key}`);
    };
    walk(data);
    assert.ok(checked > 0, `${file}: the check actually visited translation maps`);
  }
});
