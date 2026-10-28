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
const resolution = JSON.parse(await readFile(new URL('../../../data/runtime/element_resolution.pack', import.meta.url), 'utf8'));
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const { en, pl } = pack.entries.find(r => r.key === 'presentation.ui_shell').value.locales;
const appSource = await readFile(new URL('../../src/App.tsx', import.meta.url), 'utf8');
const panelsSource = await readFile(new URL('../../src/shell/panels.ts', import.meta.url), 'utf8');   // PANEL_LABELS and PANEL_TEXT_IDS live here (M1-G1b: a unit test walks them)

test('the menu is the entries of the data, in this order (R67: Diagnostyka added for the developer details that left the chat screen; W5: Układ ▾), and nothing else', () => {
  assert.deepEqual(menu.CHAT_MENU.map(e => e.id), ['graph', 'settings', 'me', 'this_conversation', 'windows', 'layouts', 'views', 'semantic', 'diagnostics', 'request', 'connection', 'workshop']);
  assert.deepEqual(menu.CHAT_MENU, layouts.layouts.czat.menu);   // menu.ts adds nothing of its own
});

test('no Pamięć and no Wiedza in the menu: neither an entry, nor an action, nor a word on a row', () => {
  const rows = menu.menuEntries(menu.CHAT_MENU, { androidShell: true });
  for (const e of rows) {
    assert.notEqual(e.id, 'memory'); assert.notEqual(e.id, 'knowledge');
    assert.ok(!['panel:memory', 'knowledge', 'panel:user-knowledge'].includes(e.action), e.action);   // Ty is the graph from the node party:self, not the old panel
    assert.ok(!/^Pamięć|^Wiedza/.test(pl[e.label] ?? ''), e.label);
    assert.ok(!/^Memory|^Knowledge/.test(en[e.label] ?? ''), e.label);
  }
});

test('every row has a Polish and an English word in presentation.ui_shell, and the Polish ones are the owner\'s', () => {
  const owner = { graph: 'Graf', settings: 'Ustawienia', me: 'Ty', this_conversation: 'Ta rozmowa w grafie', semantic: 'Analiza w tle', connection: 'Połączenie' };
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
  assert.deepEqual(menu.parseMenuAction('sheet:diagnostics'), { kind: 'sheet', sheet: 'diagnostics' });
  assert.equal(menu.parseMenuAction('sheet:nothing').kind, 'unknown');
  assert.equal(menu.parseMenuAction('layout:workshop').kind, 'workshop');
  assert.equal(menu.parseMenuAction('whatever').kind, 'unknown');
});

test('Połączenie only inside the Android shell; the other rows everywhere', () => {
  const web = menu.menuEntries(menu.CHAT_MENU, { androidShell: false }).map(e => e.id);
  const android = menu.menuEntries(menu.CHAT_MENU, { androidShell: true }).map(e => e.id);
  assert.deepEqual(web, ['graph', 'settings', 'me', 'this_conversation', 'windows', 'layouts', 'views', 'semantic', 'diagnostics', 'request', 'workshop']);
  assert.deepEqual(android, ['graph', 'settings', 'me', 'this_conversation', 'windows', 'layouts', 'views', 'semantic', 'diagnostics', 'request', 'connection', 'workshop']);
});

test('the old Memory and Knowledge panels stay reachable in the Workshop', () => {
  assert.ok(panels.SHELL_PANELS.includes('memory'));
  assert.match(appSource, /data-testid="nav-knowledge"/);
  assert.match(appSource, /Object\.keys\(PANEL_LABELS\)[\s\S]{0,200}data-testid=\{`nav-\$\{p\}`\}/);   // one nav button per panel, memory included
  assert.match(panelsSource, /memory: "Memory"/);
});

test('the drawer of a panel opened from the menu carries the menu\'s word, not an English label', () => {
  const ids = /export const PANEL_TEXT_IDS[^=]*= (\{[^}]*\})/.exec(panelsSource)[1];
  // r1:b5: the old graph entry lives in the Workshop as "Wejście do grafu (stare)"; the navigator carries the menu's "Graf".
  assert.match(ids, /navigator: "menu_graph"/); assert.match(ids, /"graph-entry": "workshop_graph_entry"/);
  // M1-G1b: the old map and the old "what the app knows" are NOT named like the menu entries that open the navigator (label-guard.test.mjs walks all labels).
  assert.match(ids, /"user-knowledge": "workshop_me_old"/); assert.match(ids, /graph: "workshop_graph_old"/);
});

test('Graf, Ty and Ta rozmowa open the same navigator at three foci (spec 3.1); conv:current becomes the open conversation, else the root', () => {
  const action = id => menu.CHAT_MENU.find(e => e.id === id).action;
  assert.deepEqual(menu.parseMenuAction(action('graph')), { kind: 'navigator', focus: '' });
  assert.deepEqual(menu.parseMenuAction(action('me')), { kind: 'navigator', focus: 'party:self' });
  assert.deepEqual(menu.parseMenuAction(action('this_conversation')), { kind: 'navigator', focus: menu.CURRENT_CONVERSATION_FOCUS });
  // The address form of a conversation is data (element_resolution.pack, projection_conv): c_<hex> in the database = conv:<hex> in the graph.
  const findRow = (node, id) => Array.isArray(node) ? node.map(n => findRow(n, id)).find(Boolean)
    : node && typeof node === 'object' ? (node.id === id ? node : Object.values(node).map(v => findRow(v, id)).find(Boolean)) : undefined;
  const convSource = findRow(resolution, 'projection_conv');
  assert.deepEqual(menu.CONVERSATION_ADDRESS, { prefix: convSource.prefix, dbPrefix: convSource.same_as_prefix }, 'the code follows the pack');
  assert.equal(menu.resolveNavigatorFocus('conv:current', 'c_12ab'), 'conv:12ab');
  assert.equal(menu.conversationAddress('12ab'), 'conv:12ab', 'an id without the database prefix is taken as it is');
  assert.equal(menu.resolveNavigatorFocus('conv:current', null), '', 'no conversation open: the whole graph, not a node that does not exist');
  assert.equal(menu.resolveNavigatorFocus('party:self', 'c_12ab'), 'party:self', 'any other focus passes as it is');
  assert.equal(menu.resolveNavigatorFocus('', null), '');
  // The old screens stay one tap away under Warsztat (spec 3.13), not in the menu.
  assert.ok(panels.SHELL_PANELS.includes('user-knowledge') && panels.SHELL_PANELS.includes('graph'));
});
