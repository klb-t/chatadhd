// M1-W0 (1): the ⋮ menu per layouts.json: Graf, Ustawienia, Ty, Ta rozmowa, Widoki, Analiza, Połączenie, Warsztat; no Pamięć, no Wiedza.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const menu = await loader.load('src/shell/menu.ts');
const panels = await loader.load('src/shell/panels.ts');
const layouts = JSON.parse(await readFile(new URL('../../src/shell/data/layouts.json', import.meta.url), 'utf8'));
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const { en, pl } = pack.entries.find(r => r.key === 'presentation.ui_shell').value.locales;
const appSource = await readFile(new URL('../../src/App.tsx', import.meta.url), 'utf8');

test('the menu is the eight entries of the spec, in this order, and nothing else', () => {
  assert.deepEqual(menu.CHAT_MENU.map(e => e.id), ['graph', 'settings', 'me', 'this_conversation', 'views', 'semantic', 'connection', 'workshop']);
  assert.deepEqual(menu.CHAT_MENU, layouts.layouts.czat.menu);   // menu.ts adds nothing of its own
});

test('no Pamięć and no Wiedza in the menu: neither an entry, nor an action, nor a word on a row', () => {
  const rows = menu.menuEntries(menu.CHAT_MENU, { androidShell: true });
  for (const e of rows) {
    assert.notEqual(e.id, 'memory'); assert.notEqual(e.id, 'knowledge');
    assert.ok(!['panel:memory', 'knowledge'].includes(e.action), e.action);   // (panel:user-knowledge is Ty, not Wiedza)
    assert.ok(!/^Pamięć|^Wiedza/.test(pl[e.label] ?? ''), e.label);
    assert.ok(!/^Memory|^Knowledge/.test(en[e.label] ?? ''), e.label);
  }
});

test('every row has a Polish and an English word in presentation.ui_shell, and the Polish ones are the owner\'s', () => {
  const owner = { graph: 'Graf', settings: 'Ustawienia', me: 'Ty', this_conversation: 'Ta rozmowa', semantic: 'Analiza w tle', connection: 'Połączenie' };
  for (const e of menu.CHAT_MENU) {
    assert.equal(typeof pl[e.label], 'string', `pl ${e.label}`); assert.equal(typeof en[e.label], 'string', `en ${e.label}`);
    if (owner[e.id]) assert.equal(pl[e.label], owner[e.id]);
  }
  assert.match(pl.menu_views, /^Widoki/); assert.match(pl.menu_workshop, /^Warsztat/);
});

test('every action of the data means something the shell can do; a panel is one the app has (no dead row)', () => {
  for (const e of menu.CHAT_MENU) {
    const target = menu.parseMenuAction(e.action);
    assert.notEqual(target.kind, 'unknown', `${e.id}: ${e.action}`);
    if (target.kind === 'panel') assert.ok(panels.SHELL_PANELS.includes(target.panel), `${e.id}: panel ${target.panel}`);
  }
  assert.deepEqual(menu.parseMenuAction('panel:graph-entry'), { kind: 'panel', panel: 'graph-entry' });
  assert.deepEqual(menu.parseMenuAction('sheet:semantic'), { kind: 'sheet', sheet: 'semantic' });
  assert.equal(menu.parseMenuAction('sheet:nothing').kind, 'unknown');
  assert.equal(menu.parseMenuAction('layout:workshop').kind, 'workshop');
  assert.equal(menu.parseMenuAction('whatever').kind, 'unknown');
});

test('Połączenie only inside the Android shell; the other rows everywhere', () => {
  const web = menu.menuEntries(menu.CHAT_MENU, { androidShell: false }).map(e => e.id);
  const android = menu.menuEntries(menu.CHAT_MENU, { androidShell: true }).map(e => e.id);
  assert.deepEqual(web, ['graph', 'settings', 'me', 'this_conversation', 'views', 'semantic', 'workshop']);
  assert.deepEqual(android, ['graph', 'settings', 'me', 'this_conversation', 'views', 'semantic', 'connection', 'workshop']);
});

test('the old Memory and Knowledge panels stay reachable in the Workshop', () => {
  assert.ok(panels.SHELL_PANELS.includes('memory'));
  assert.match(appSource, /data-testid="nav-knowledge"/);
  assert.match(appSource, /Object\.keys\(PANEL_LABELS\)[\s\S]{0,200}data-testid=\{`nav-\$\{p\}`\}/);   // one nav button per panel, memory included
  assert.match(appSource, /memory: "Memory"/);
});

test('the drawer of a panel opened from the menu carries the menu\'s word, not an English label', () => {
  const ids = /const PANEL_TEXT_IDS[^=]*= (\{[^}]*\})/.exec(appSource)[1];
  assert.match(ids, /"graph-entry": "menu_graph"/); assert.match(ids, /"user-knowledge": "menu_me"/); assert.match(ids, /graph: "menu_this_conversation"/);
});
