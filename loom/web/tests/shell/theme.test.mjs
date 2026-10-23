// R67 (theme): the web reads the setting `theme`; any name is passed on, empty or non-text values mean no opinion.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const m = await loader.load('src/shell/theme.ts');

test('a name of the setting is passed on, also one without palette tokens', () => {
  assert.equal(m.themeFromConfig('amoled'), 'amoled');
  assert.equal(m.themeFromConfig('dark'), 'dark');
  assert.equal(m.themeFromConfig(' morski '), 'morski');
});
test('empty, missing and non-text values mean no opinion', () => {
  for (const v of [undefined, null, '', '   ', 3, {}, []]) assert.equal(m.themeFromConfig(v), null);
});
