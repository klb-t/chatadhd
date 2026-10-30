// M1-G1b (4): the help "Co tu widzę?" of the graph screens - visibility state, the lines that exist, the words in both languages.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const help = await loader.load('src/components/graph-help-state.ts');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const locales = pack.entries.find(r => r.key === 'presentation.graph_screen').value.locales;
const shellLocales = pack.entries.find(r => r.key === 'presentation.ui_shell').value.locales;
const translator = language => id => locales[language][id] ?? id;
const appSource = await readFile(new URL('../../src/App.tsx', import.meta.url), 'utf8');
const navigatorSource = await readFile(new URL('../../src/components/GraphNavigator.tsx', import.meta.url), 'utf8');
const viewSource = await readFile(new URL('../../src/components/GraphView.tsx', import.meta.url), 'utf8');

test('the help is closed unless the setting says open: the preset is closed, junk is closed', () => {
  for (const open of [true, 1, 'true', 'TRUE', ' true ']) assert.equal(help.helpOpenOf(open), true, String(open));
  for (const closed of [false, 0, undefined, null, 'false', '', 'yes', 'open', {}, [], 2, NaN]) assert.equal(help.helpOpenOf(closed), false, String(closed));
});

test('the lines are the catalogue\'s, all of them, in both languages (nothing dropped, nothing counted in code)', () => {
  for (const [screen, { prefix }] of Object.entries(help.HELP_SCREENS)) {
    const counts = {};
    for (const language of Object.keys(locales)) {
      const present = Object.keys(locales[language]).filter(k => new RegExp(`^${prefix}\\.\\d+$`).test(k));
      const lines = help.helpLines(translator(language), prefix);
      assert.ok(lines.length > 0, `${screen}/${language}: has help`);
      assert.equal(lines.length, present.length, `${screen}/${language}: every ${prefix}.N is drawn`);
      counts[language] = lines.length;
    }
    assert.equal(counts.pl, counts.en, `${screen}: pl and en have the same lines`);
  }
  assert.match(help.helpLines(translator('pl'), 'help')[0], /^Dotknij wiersza, żeby wejść/);
  assert.match(help.helpLines(translator('en'), 'help')[0], /^Tap a row to go in/);
});

test('a gap in the numbering ends the list (a missing word is not drawn as its id); no catalogue: no lines', () => {
  assert.deepEqual(help.helpLines(id => ({ 'x.1': 'a', 'x.3': 'c' })[id] ?? id, 'x'), ['a']);
  assert.deepEqual(help.helpLines(id => id, 'help'), []);
  assert.deepEqual(help.helpLines(() => '', 'help'), []);
});

test('each screen has its own setting and its own words; the title and both disclosure labels exist in pl and en', () => {
  const settings = Object.values(help.HELP_SCREENS).map(s => s.setting);
  assert.equal(new Set(settings).size, settings.length);
  assert.deepEqual(settings.sort(), ['graph_view_help_open', 'navigator_help_open']);
  for (const language of Object.keys(locales)) for (const id of ['help.title', 'help.show', 'help.hide']) assert.ok(locales[language][id], `${language}: ${id}`);
  assert.equal(locales.pl['help.show'], 'Co tu widzę?');
});

test('the old map has its own help: it does not describe rows and cards it does not have', () => {
  for (const language of Object.keys(locales)) {
    const old = help.helpLines(translator(language), 'help_old').join(' ');
    assert.ok(!/wiersz|\brows?\b/i.test(old), `${language}: the old view has no rows`);
    assert.ok(old.includes(`⋮ › ${shellLocales[language].menu_graph}`), `${language}: it points to the navigator by the word of that language's menu (menu_graph)`);   // nl says Graaf, as its menu does
  }
});

test('the screens draw it and remember it through the settings path, not the browser', () => {
  assert.match(navigatorSource, /<GraphHelp screen="navigator"[^>]*helpSetting|<GraphHelp screen="navigator" t=\{t\} setting=\{helpSetting\}/);
  assert.match(viewSource, /<GraphHelp screen="old_graph"/);
  assert.match(appSource, /helpSetting=\{config\.navigator_help_open\}/);
  assert.match(appSource, /helpSetting=\{config\.graph_view_help_open\}/);
  const component = 'src/components/GraphHelp.tsx';
  return readFile(new URL('../../' + component, import.meta.url), 'utf8').then(source => {
    assert.match(source, /api\.setConfigKey\(/);
    assert.ok(!/localStorage|sessionStorage|indexedDB/.test(source), 'not browser storage');
  });
});
