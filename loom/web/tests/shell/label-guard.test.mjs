// M1-G1b (3): one label, one screen. The shell's controls that name a screen (the ⋮ menu from layouts.json, the Workshop buttons from
// panels.ts) are walked in every language of the catalogue and no label may lead to two different screens. Includes the state the
// owner met ("Ta rozmowa w grafie" on the menu entry and on the Workshop button) as a case the guard must catch.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const guard = await loader.load('src/shell/label-guard.ts');
const panels = await loader.load('src/shell/panels.ts');
const menu = await loader.load('src/shell/menu.ts');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const locales = pack.entries.find(r => r.key === 'presentation.ui_shell').value.locales;

test('no label of the shell leads to two different screens, in any language of the catalogue', () => {
  assert.ok(['en', 'pl'].every((code) => code in locales), 'en and pl are present; further languages are data');
  for (const [language, words] of Object.entries(locales)) {
    const controls = guard.shellControls(words);
    assert.ok(controls.length >= menu.CHAT_MENU.length + panels.SHELL_PANELS.length, `${language}: every menu entry and every panel is walked`);
    const bad = guard.labelCollisions(controls);
    assert.deepEqual(bad.map(c => `${c.label}: ${c.controls.map(x => `${x.where}.${x.id}->${x.opens}`).join(' | ')}`), [], `${language}: no label with two screens`);
  }
});

test('every control shows a word of the catalogue, never its id (a missing word would show the id)', () => {
  for (const [language, words] of Object.entries(locales)) {
    for (const control of guard.shellControls(words)) {
      assert.ok(control.label.trim().length > 0, `${language}: ${control.id} has a label`);
      assert.ok(!/^[a-z]+(?:_[a-z]+)+$/.test(control.label), `${language}: ${control.where}.${control.id} shows the id "${control.label}"`);
    }
  }
});

test('the old state is caught: the menu entry and the Workshop button with the same words and different screens', () => {
  const owner = [
    { where: 'menu', id: 'this_conversation', label: 'Ta rozmowa w grafie', opens: 'navigator:conv:current' },
    { where: 'workshop', id: 'graph', label: 'Ta rozmowa w grafie', opens: 'panel:graph' },
  ];
  const found = guard.labelCollisions(owner);
  assert.equal(found.length, 1);
  assert.deepEqual(found[0].controls.map(c => c.id), ['this_conversation', 'graph']);
  // the same two controls with the old words on the real table: the guard fails on the real data too
  const words = { ...locales.pl, workshop_graph_old: locales.pl.menu_this_conversation };
  const real = guard.shellControls(words);
  assert.equal(guard.labelCollisions(real).length, 1, 'renaming the Workshop word back to the menu word is a collision');
});

test('the same words on two controls that open the SAME screen are fine (Ustawienia in the menu and in the Workshop; Graf and the navigator)', () => {
  const same = [
    { where: 'menu', id: 'settings', label: 'Ustawienia', opens: 'panel:settings' },
    { where: 'workshop', id: 'settings', label: 'Ustawienia', opens: 'panel:settings' },
  ];
  assert.deepEqual(guard.labelCollisions(same), []);
  const controls = guard.shellControls(locales.pl);
  const graph = controls.filter(c => c.label === locales.pl.menu_graph);
  assert.ok(graph.length >= 2 && new Set(graph.map(c => c.opens)).size === 1, 'Graf is the menu entry and the Workshop navigator: one screen');
});

test('labels compare as a person reads them: case and spaces do not make two words', () => {
  const c = [{ where: 'menu', id: 'a', label: 'Ty', opens: 'x' }, { where: 'workshop', id: 'b', label: '  ty ', opens: 'y' }, { where: 'workshop', id: 'c', label: 'T  y', opens: 'z' }];
  const found = guard.labelCollisions(c);
  assert.equal(found.length, 1);
  assert.deepEqual(found[0].controls.map(x => x.id), ['a', 'b']);
});

test('property: the guard agrees with the definition on random controls (seeded, so no run depends on order or speed)', () => {
  let seed = 20261023;
  const next = n => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed % n; };
  const labels = ['Graf', 'graf ', 'Ty', 'Ustawienia', 'Mapa', 'A  b', 'a b'], screens = ['s1', 's2', 's3'];
  for (let round = 0; round < 300; round++) {
    const controls = Array.from({ length: 1 + next(8) }, (_, i) => ({ where: next(2) ? 'menu' : 'workshop', id: `c${i}`, label: labels[next(labels.length)], opens: screens[next(screens.length)] }));
    const norm = s => s.trim().replace(/\s+/g, ' ').toLowerCase();
    const expected = new Set();
    for (const a of controls) for (const b of controls) if (norm(a.label) === norm(b.label) && a.opens !== b.opens) expected.add(norm(a.label));
    assert.deepEqual(guard.labelCollisions(controls).map(c => c.label).sort(), [...expected].sort(), JSON.stringify(controls));
  }
});

test('the Workshop buttons that open an OLD screen say so in their words (pl and en)', () => {
  for (const [language, words] of Object.entries(locales)) {
    for (const id of ['workshop_graph_old', 'workshop_me_old', 'workshop_graph_entry']) {
      assert.match(words[id], language === 'pl' ? /\(stary widok\)|\(stare\)/ : language === 'nl' ? /\(oude weergave\)|\(oud\)/ : /\(old view\)|\(old\)/, `${language}: ${id}`);
    }
  }
  assert.equal(locales.pl.workshop_graph_old, 'Mapa sił tej rozmowy (stary widok)');
});
