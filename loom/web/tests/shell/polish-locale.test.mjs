// Synthetic person/state; actual catalogs, locale resolver and rendered product component.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { createModuleLoader } from '../product-perspective/loader.mjs';

const loader = await createModuleLoader();
test.after(() => loader.close());
const lang = await loader.load('src/shell/language.ts');
const languages = await loader.load('src/shell/language-pack.ts');
const text = await loader.load('src/shell/text.ts');
const presentation = await loader.load('src/onboarding/presentation.mjs');
const menu = await loader.load('src/shell/composer-menu.ts');
const ComposerMenu = (await loader.load('src/shell/ComposerMenu.tsx')).default;
const counts = await loader.load('src/components/settings-human-state.ts');
const card = await loader.load('src/components/node-card-state.ts');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const entry = key => pack.entries.find(row => row.key === key).value;
const feature = name => presentation.resolvePresentationFeature(presentation.resolvePresentation(), name);
const shell = feature('ui_shell');
const t = code => text.shellTranslator(shell, code);

test('real catalog locales: fresh Polish device, deliberate UI choice, profile preference and unsupported fallback', () => {
  const available = languages.availableLanguages([shell]);
  assert.equal(lang.shellLanguage(undefined, undefined, ['pl-PL', 'en-US'], available), 'pl');
  assert.equal(lang.shellLanguage('en', 'pl', ['pl-PL'], available), 'en');
  assert.equal(lang.shellLanguage('pl', 'en', ['en-US'], available), 'pl');
  assert.equal(lang.shellLanguage(undefined, 'en', ['pl-PL'], available), 'en');
  assert.equal(lang.shellLanguage('not-supported', undefined, ['de-DE', 'pl-PL'], available), 'pl');
  assert.equal(lang.shellLanguage('auto', 'auto', ['ja-JP'], available), 'en');
});

test('critical file keys resolve through the real translator; one missing PL word falls back without changing the other labels', () => {
  assert.equal(t('pl')('att_uploading', { name: 'Zażółć.pdf' }), 'Dodaję Zażółć.pdf…');
  assert.match(t('pl')('att_not_sent', { name: 'Zażółć.pdf', reason: t('pl')('att_unreadable') }), /Zażółć\.pdf/);
  assert.equal(t('pl')('att_saved'), 'plik zapisany w aplikacji');
  const sparse = structuredClone(shell);
  delete sparse.locales.pl.cm_attach_file;
  const fallback = text.shellTranslator(sparse, 'pl');
  assert.equal(fallback('cm_attach_file'), 'Attach a file');
  assert.equal(fallback('send'), 'Wyślij');
});

test('the actual composer menu renders Polish and English using the existing contexts and catalog', () => {
  const composed = menu.composeMenu(entry('chat.composer_menu'), entry('chat.composer_menu_ops'));
  assert.deepEqual(composed.issues, []);
  const noop = () => {};
  const props = { ...composed, ctx: { upload: true, settings: {}, blocked: false }, flags: { memory: true, knowledge: false }, switches: {},
    model: '', models: [], onAttach: noop, onFlag: noop, onSwitch: noop, onModel: noop, onPanel: noop, onClose: noop,
    modify: { clear: noop }, profiles: null, profilesError: null, onLoadProfiles: noop, usageFeature: null, waiting: false,
    onNode: noop, onPreview: noop, appliedText: '' };
  const render = code => renderToStaticMarkup(createElement(text.ShellLanguage.Provider, { value: code },
    createElement(text.ShellText.Provider, { value: t(code) }, createElement(ComposerMenu, props))));
  const pl = render('pl'), en = render('en');
  assert.match(pl, /Załącz plik/);
  assert.match(pl, /Wybierz model/);
  assert.doesNotMatch(pl, /Attach a file|Choose model|cm_attach_file/);
  assert.match(en, /Attach a file/);
  assert.doesNotMatch(en, /Załącz plik/);
});

test('actual graph/settings catalogs retain Polish number forms and decimal formatting', () => {
  const st = text.shellTranslator(feature('settings_human'), 'pl');
  assert.equal(counts.countWord('fields', 1, 'pl', st), '1 pole');
  assert.equal(counts.countWord('fields', 2, 'pl', st), '2 pola');
  assert.equal(counts.countWord('fields', 5, 'pl', st), '5 pól');
  assert.equal(counts.countWord('fields', 12, 'pl', st), '12 pól');
  assert.equal(counts.countWord('fields', 22, 'pl', st), '22 pola');
  const gt = text.shellTranslator(feature('graph_screen'), 'pl');
  assert.equal(card.numberText(0.75, gt), '0,75');
  assert.equal(gt('card.change'), 'Zmień');
  assert.notEqual(gt('card.saved'), 'card.saved');
});
