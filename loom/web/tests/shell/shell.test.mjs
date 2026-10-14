// P20 (R56): the first screen is a chat app. Pure logic: start view order, disclosure, animation as data, language, first-run card, labels PL/EN.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const sv = await loader.load('src/shell/start-view.ts');
const dis = await loader.load('src/shell/disclosure.ts');
const Disclosure = (await loader.load('src/shell/Disclosure.tsx')).default;
const anim = await loader.load('src/shell/animation.ts');
const lang = await loader.load('src/shell/language.ts');
const text = await loader.load('src/shell/text.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const entry = pack.entries.find(r => r.key === 'presentation.ui_shell');
const fields = JSON.parse(await readFile(new URL('../../../data/runtime/settings_fields.pack', import.meta.url), 'utf8')).defaults.fields;

const probes = (o = {}) => ({ saved: () => null, conversationExists: async () => true, newest: async () => null, ...o });

test('start view: preset order last_saved, last_conversation, new_conversation; the first that yields wins', async () => {
  assert.deepEqual(sv.START_VIEW_PRESET, ['last_saved', 'last_conversation', 'new_conversation']);
  assert.deepEqual(sv.startViewOrder(undefined), sv.START_VIEW_PRESET);
  const order = sv.startViewOrder(undefined);
  let r = await sv.resolveStartView(order, probes({ saved: () => ({ layout: 'workshop', conversationId: 'c_1' }), newest: async () => 'c_2' }));
  assert.deepEqual(r, { source: 'last_saved', layout: 'workshop', conversationId: 'c_1' });
  r = await sv.resolveStartView(order, probes({ saved: () => ({ layout: 'chat', conversationId: 'c_gone' }), conversationExists: async () => false, newest: async () => 'c_2' }));
  assert.equal(r.source, 'last_conversation'); assert.equal(r.conversationId, 'c_2');
  r = await sv.resolveStartView(order, probes());
  assert.deepEqual(r, { source: 'new_conversation', layout: null, conversationId: null });
  r = await sv.resolveStartView(sv.startViewOrder(['new_conversation', 'last_saved']), probes({ saved: () => ({ layout: 'chat', conversationId: 'c_1' }) }));
  assert.equal(r.source, 'new_conversation');  // the user's own order
  r = await sv.resolveStartView(['nonsense'], probes());  // unknown names are skipped; a chat always opens
  assert.equal(r.source, 'new_conversation');
  r = await sv.resolveStartView(order, probes({ saved: () => { throw new Error('x'); }, newest: async () => 'c_9' }));
  assert.equal(r.conversationId, 'c_9');
});

test('layout_mode and deep links', () => {
  assert.equal(sv.layoutOfMode(undefined), 'chat'); assert.equal(sv.layoutOfMode('full_screen_chat'), 'chat');
  assert.equal(sv.layoutOfMode('views_grid'), 'workshop'); assert.equal(sv.layoutOfMode('windows'), 'workshop');
  assert.deepEqual(sv.parseShellHash('#warsztat'), { layout: 'workshop', conversationId: null });
  assert.deepEqual(sv.parseShellHash('#czat/c_ab12'), { layout: 'chat', conversationId: 'c_ab12' });
  assert.equal(sv.parseShellHash('#other'), null);
  const mem = new Map(); const store = { getItem: k => mem.get(k) ?? null, setItem: (k, v) => mem.set(k, v) };
  assert.equal(sv.readSaved(store), null);
  sv.writeSaved(store, { layout: 'chat', conversationId: 'c_1' });
  assert.deepEqual(sv.readSaved(store), { layout: 'chat', conversationId: 'c_1' });
});

test('settings are described in settings_fields.pack: start_view, layout_mode (windows planned with a reason), ui_language, animation_*', () => {
  const by = Object.fromEntries(fields.map(f => [f.key, f]));
  assert.match(by.start_view.description, /last_saved, last_conversation, new_conversation/);
  const windows = by.layout_mode.values.find(v => v.value === 'windows');
  assert.equal(windows.status, 'planned'); assert.ok(windows.reason);
  assert.equal(by.layout_mode.values.find(v => v.preset).value, 'full_screen_chat');
  for (const k of ['animation_styles', 'animations', 'animation_reduced_motion', 'animation_refresh_hz']) {
    assert.equal(by[k].section, 'Interfejs i wyświetlanie'); assert.equal(by[k].subsection, 'Animacje');
  }
  assert.ok(by.ui_language);
});

test('disclosure: state, aria-expanded and label change at once (no wait for the animation); target 44 px in CSS', async () => {
  const labels = { show: 'Pokaż myślenie', hide: 'Ukryj myślenie' };
  assert.deepEqual(dis.disclosureView(false, labels), { expanded: false, label: 'Pokaż myślenie', chevron: 'collapsed', ariaExpanded: 'false' });
  assert.deepEqual(dis.disclosureView(true, labels), { expanded: true, label: 'Ukryj myślenie', chevron: 'expanded', ariaExpanded: 'true' });
  const closed = renderToStaticMarkup(createElement(Disclosure, { labels }, 'treść'));
  assert.match(closed, /aria-expanded="false"/); assert.match(closed, /Pokaż myślenie/); assert.match(closed, /data-state="collapsed"/);
  const open = renderToStaticMarkup(createElement(Disclosure, { labels, defaultOpen: true }, 'treść'));
  assert.match(open, /aria-expanded="true"/); assert.match(open, /Ukryj myślenie/); assert.match(open, /data-state="expanded"/);
  const css = await readFile(new URL('../../src/shell/shell.css', import.meta.url), 'utf8');
  assert.match(css, /\.disclosure-head \{[^}]*min-height: 44px/);
  assert.match(css, /rotate\(90deg\)/);
});

test('animation as data: units, per-element styles, reduced motion, overlay style', () => {
  const d = anim.SHIPPED;
  assert.equal(anim.toMs({ value: 150, unit: 'ms' }, 60), 150);
  assert.equal(anim.toMs({ value: 0.25, unit: 's' }, 60), 250);
  assert.equal(anim.toMs({ value: 6, unit: 'frames' }, 60), 100);
  assert.equal(anim.toMs({ value: 6, unit: 'frames' }, 120), 50);
  assert.equal(anim.toMs({ value: -1, unit: 'ms' }, 60), 0);
  const dis1 = anim.resolveElement(d, 'disclosure', false);
  assert.deepEqual([dis1.style, dis1.kind, dis1.ms], ['grow+fade', 'grow+fade', 150]);
  assert.equal(anim.resolveElement(d, 'drawer', false).kind, 'slide');
  for (const el of ['menu', 'dialog', 'notice', 'message', 'proposal', 'view']) assert.equal(anim.resolveElement(d, el, false).kind, 'fade');
  const reduced = anim.resolveElement(d, 'disclosure', true);
  assert.deepEqual([reduced.style, reduced.ms], ['none', 0]);
  const vars = anim.cssVariables(d, true);
  assert.equal(vars['--anim-disclosure-ms'], '0ms');
  // the user's overlay: an own style in frames, applied to one element; reduced motion can keep some motion
  const mine = anim.mergeData({ animation_styles: { slow: { kind: 'grow+fade', duration: { value: 12, unit: 'frames' }, easing: 'cubic-bezier(0.1, 0.7, 0.1, 1)' } },
    animations: { disclosure: 'slow' }, animation_reduced_motion: 'fade', animation_refresh_hz: 120 });
  const r = anim.resolveElement(mine, 'disclosure', false);
  assert.deepEqual([r.style, r.ms, r.easing], ['slow', 100, 'cubic-bezier(0.1, 0.7, 0.1, 1)']);
  assert.equal(anim.resolveElement(mine, 'disclosure', true).style, 'fade');
  assert.equal(anim.resolveElement(mine, 'drawer', false).kind, 'slide');  // untouched elements keep the preset
  assert.equal(anim.resolveElement(anim.mergeData({ animations: { menu: 'none' } }), 'menu', false).ms, 0);
});

test('language: setting, then preference, then browser, then en', () => {
  const av = ['en', 'pl'];
  assert.equal(lang.shellLanguage('pl', undefined, ['en-US'], av), 'pl');
  assert.equal(lang.shellLanguage('auto', 'pl', ['en-US'], av), 'pl');
  assert.equal(lang.shellLanguage(undefined, 'auto', ['pl-PL', 'en'], av), 'pl');
  assert.equal(lang.shellLanguage(undefined, undefined, ['de-DE'], av), 'en');
});

test('labels: the shell words are data in PL and EN with the same keys; the translator follows the language', () => {
  const { en, pl } = entry.value.locales;
  assert.deepEqual(Object.keys(en).sort(), Object.keys(pl).sort());
  for (const k of ['menu_workshop', 'menu_views', 'new_chat', 'thinking', 'first_run_button']) assert.notEqual(en[k], pl[k]);
  assert.equal(pl.menu_workshop, 'Warsztat (narzędzia zaawansowane)');
  const feature = p.resolvePresentationFeature(p.resolvePresentation(), 'ui_shell');
  assert.equal(text.shellTranslator(feature, 'pl')('new_chat'), 'Nowa rozmowa');
  assert.equal(text.shellTranslator(feature, 'en')('new_chat'), 'New conversation');
  assert.equal(text.shellTranslator(feature, 'pl')('mic_failed', { reason: 'x' }), 'Dyktowanie nie powiodło się: x');
  assert.equal(text.shellTranslator(null, 'pl')('new_chat'), 'new_chat');
});

test('first run: the card shows only when the key is known to be missing', () => {
  assert.equal(text.needsProviderKey(false), true);
  assert.equal(text.needsProviderKey(true), false);
  assert.equal(text.needsProviderKey(null), false);
});
