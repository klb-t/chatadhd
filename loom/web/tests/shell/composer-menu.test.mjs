// P33: the + menu as data. Pure logic: the shipped entries, the user's operations (hide, add, move), what each entry says about itself,
// the switches and the request fields they set, the problems said in plain words (Polish and English), and the attachment reference.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { readFileSync } from 'node:fs';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const cm = await loader.load('src/shell/composer-menu.ts');
const text = await loader.load('src/shell/text.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const entry = (key) => pack.entries.find(r => r.key === key);
const base = entry('chat.composer_menu').value;
const ids = (items) => items.map(i => i.id);
const flat = (items) => items.flatMap(i => [i, ...(i.items ? flat(i.items) : [])]);
const words = entry('presentation.ui_shell').value.locales;
const tIn = (lang) => text.shellTranslator(p.resolvePresentationFeature(p.resolvePresentation(), 'ui_shell'), lang);
const plT = tIn('pl');
const codes = (m) => m.issues.map(i => i.code);

test('the shipped menu: the union of the popular chat apps, nothing invented as a limit', () => {
  const m = cm.composeMenu(base, entry('chat.composer_menu_ops').value);
  assert.deepEqual(m.issues, []);
  assert.deepEqual(ids(m.items), ['attach_file', 'attach_photo', 'model', 'tools', 'context', 'import']);
  assert.deepEqual(ids(m.items.find(i => i.id === 'tools').items), ['web_search', 'deep_research', 'thinking', 'agent', 'image', 'music']);
  // M1-W1b: the context submenu gained the steps, the profile list, the flow, the preview; the three old entries are still there, in their place after them.
  assert.deepEqual(ids(m.items.find(i => i.id === 'context').items),
    ['wider', 'deeper', 'narrower', 'shallower', 'selector_profile', 'flow_text_graph', 'preview_request', 'memory', 'knowledge', 'perspective']);
  // The existing actions are the entries: the agent runs agent.run, a picture is depict; deep research is the request's own field.
  const find = (id) => flat(m.items).find(i => i.id === id);
  assert.deepEqual([find('agent').field, find('agent').value], ['action', 'agent.run']);
  assert.deepEqual([find('image').field, find('image').value, find('image').needs], ['action', 'depict', ['image_model', 'image_provider']]);
  assert.deepEqual([find('deep_research').field, find('deep_research').value], ['deep_research', true]);
  assert.equal(find('model').setting, undefined, 'the model goes to default_model, the key the kernel reads; not a setting of the data');
  assert.equal(cm.MODEL_SETTING, 'default_model');
});

test('every entry, reason and problem has a word in Polish and English', () => {
  assert.deepEqual(Object.keys(words.en).sort(), Object.keys(words.pl).sort());
  const m = cm.composeMenu(base, undefined);
  for (const item of flat(m.items)) {
    for (const lang of ['en', 'pl']) {
      assert.ok(words[lang][item.label], `${lang}: label ${item.label}`);
      if (item.reason) assert.ok(words[lang][item.reason], `${lang}: reason ${item.reason}`);
    }
  }
  for (const code of ['menu_not_effective', 'no_items', 'items_not_list', 'no_id', 'unknown_kind', 'toggle_target', 'no_panel', 'unknown_panel',
    'ops_shape', 'op_shape', 'hide_missing', 'no_submenu', 'exists', 'move_missing', 'move_into_itself', 'unknown_op', 'settings_unread'])
    for (const lang of ['en', 'pl']) assert.ok(words[lang][`cm_issue_${code}`], `${lang}: cm_issue_${code}`);
  for (const word of ['cm_reason_needs', 'cm_reason_unknown_action', 'cm_reason_upload', 'tool_chip_on', 'att_unreadable', 'att_missing', 'att_not_sent', 'att_scope', 'cm_action_no_files'])
    for (const lang of ['en', 'pl']) assert.ok(words[lang][word], `${lang}: ${word}`);
  assert.equal(plT('cm_attach_file'), 'Załącz plik');
  assert.equal(plT('cm_context'), 'Kontekst', 'no project item yet: the submenu does not promise one');
  assert.equal(plT('cm_deep_research'), 'Głębokie badanie');
  assert.match(plT('cm_reason_image'), /Ustawieniach/);
  assert.match(plT('cm_reason_music'), /nie ma takiej czynności/, 'the music reason says the truth: no provider would help');
});

test('overlay: hide, add (top, in a submenu, before another), move', () => {
  const ops = { ops: [
    { op: 'hide', id: 'import' },
    { op: 'hide', id: 'music' },
    { op: 'add', item: { id: 'translate', label: { en: 'Translate', pl: 'Tłumacz' }, kind: 'panel', panel: 'settings' }, before: 'model' },
    { op: 'add', item: { id: 'notes', label: 'Notes', kind: 'toggle', flag: 'memory' }, parent: 'tools' },
    { op: 'add', item: { id: 'canvas', label: 'Canvas', kind: 'toggle', field: 'flow', value: 'canvas' }, parent: 'tools' },
    { op: 'move', id: 'tools', before: 'attach_file' },
  ] };
  const m = cm.composeMenu(base, ops);
  assert.deepEqual(m.issues, []);
  assert.deepEqual(ids(m.items), ['tools', 'attach_file', 'attach_photo', 'translate', 'model', 'context']);
  assert.deepEqual(ids(m.items[0].items), ['web_search', 'deep_research', 'thinking', 'agent', 'image', 'notes', 'canvas']);
  assert.equal(base.items.length, 6, 'the shipped data is not changed');
  // A toggle added by an overlay names the request field it sets: no code change is needed for it to work.
  assert.deepEqual(cm.requestFields(m.items, { canvas: true }), { flow: 'canvas' });
});

test('overlay mistakes are reported by name, never silently dropped, and do not break the rest', () => {
  const m = cm.composeMenu(base, { ops: [
    { op: 'hide', id: 'nope' }, { op: 'add', item: { id: 'x', kind: 'wat' } }, { op: 'add', item: { id: 'model', kind: 'model' } },
    { op: 'add', item: { id: 'y', kind: 'panel', panel: 'import' }, parent: 'missing' }, { op: 'jump' }, 7, { op: 'hide', id: 'import' },
    { op: 'add', item: { id: 'z', label: 'Zet', kind: 'toggle', flag: 'nope' } },
    { op: 'add', item: { id: 'p1', label: 'P1', kind: 'panel' } },
    { op: 'add', item: { id: 'p2', label: 'P2', kind: 'panel', panel: 'nowhere' } },
    { op: 'add', item: { id: 'u1', label: 'U1', kind: 'unavailable', reason: 'cm_reason_music', panel: 'nowhere' } },
    { op: 'move', id: 'tools', parent: 'tools' },
    { op: 'move', id: 'ghost' },
  ] });
  assert.deepEqual(codes(m), ['hide_missing', 'unknown_kind', 'exists', 'no_submenu', 'unknown_op', 'op_shape', 'toggle_target', 'no_panel',
    'unknown_panel', 'unknown_panel', 'move_into_itself', 'move_missing']);
  assert.deepEqual(ids(m.items), ['attach_file', 'attach_photo', 'model', 'tools', 'context', 'u1']);
  assert.equal(m.items.at(-1).panel, undefined, 'an unavailable entry keeps its reason; a panel the app does not have is dropped and said');
  // A submenu cannot be moved into one of its own submenus either.
  const nested = cm.composeMenu({ items: [{ id: 'outer', label: 'Outer', kind: 'submenu', items: [{ id: 'inner', label: 'Inner', kind: 'submenu', items: [] }] }] },
    { ops: [{ op: 'move', id: 'outer', parent: 'inner' }] });
  assert.deepEqual(codes(nested), ['move_into_itself']);
  assert.deepEqual(ids(nested.items), ['outer']);
  // A missing or malformed menu value, and a malformed overlay, are said too.
  assert.deepEqual(codes(cm.composeMenu(undefined, undefined)), ['no_items']);
  assert.deepEqual(codes(cm.composeMenu({ items: 5 }, undefined)), ['items_not_list']);
  assert.deepEqual(codes(cm.composeMenu(base, { nope: 1 })), ['ops_shape']);
});

test('problems are said in plain Polish, entries named by the label they show, not by ids or op indices', () => {
  const m = cm.composeMenu(base, { ops: [{ op: 'hide', id: 'nope' }, { op: 'add', item: { id: 'z', label: { en: 'Zed', pl: 'Zet' }, kind: 'toggle', flag: 'nope' } },
    { op: 'move', id: 'tools', parent: 'tools' }] });
  const said = m.issues.map(i => cm.issueText(i, plT, 'pl'));
  assert.match(said[0], /^zmiana nr 1: nie ma pozycji „nope” do ukrycia$/);
  assert.match(said[1], /przełącznik „Zet” nie mówi, co włącza/);
  assert.match(said[2], /zmiana nr 3: nie da się przenieść „Narzędzia”/);
  for (const line of said) assert.ok(!/ops\[|cm_|undefined/.test(line), line);
  assert.match(cm.issueText({ code: 'menu_not_effective', params: {} }, tIn('en'), 'en'), /menu as shipped/);
});

test('a label that is not a word of the shell is shown as written; the id only when there is no label', () => {
  assert.equal(cm.labelText('cm_memory', plT, 'pl', 'memory'), 'Profil i pamięć (z węzła Ty)');
  assert.equal(cm.labelText('Moje notatki', plT, 'pl', 'notes'), 'Moje notatki');
  assert.equal(cm.labelText({ en: 'Translate', pl: 'Tłumacz' }, plT, 'pl', 'translate'), 'Tłumacz');
  assert.equal(cm.labelText('', plT, 'pl', 'translate'), 'translate');
  assert.equal(cm.labelText(undefined, plT, 'pl', 'translate'), 'translate');
  const added = cm.composeMenu(base, { ops: [{ op: 'add', item: { id: 'mine', label: 'Moja pozycja', kind: 'panel', panel: 'import' } }] });
  assert.equal(cm.labelText(added.items.at(-1).label, plT, 'pl', 'mine'), 'Moja pozycja');
});

test("the layered default's status: a menu that is not effective falls back to the shipped one and says so; changes apply only while effective", () => {
  const ops = { status: 'effective', value: { ops: [{ op: 'hide', id: 'import' }] } };
  const own = cm.effectiveMenu({ status: 'effective', value: { items: [{ id: 'only', label: 'Only', kind: 'attach' }] } }, { status: 'missing' }, base);
  assert.deepEqual(ids(own.items), ['only']);
  assert.deepEqual(own.issues, []);
  for (const status of ['disabled', 'excluded', 'missing']) {
    const m = cm.effectiveMenu({ status, value: { items: [] } }, ops, base);
    assert.equal(m.issues[0].code, 'menu_not_effective', status);
    assert.deepEqual(ids(m.items), ['attach_file', 'attach_photo', 'model', 'tools', 'context'], `${status}: shipped menu, changes applied`);
  }
  for (const status of ['disabled', 'excluded']) {
    const m = cm.effectiveMenu({ status: 'effective', value: base }, { ...ops, status }, base);
    assert.ok(ids(m.items).includes('import'), `${status} changes do not apply`);
    assert.deepEqual(m.issues, []);
  }
});

test('state of an entry: the photo entry wherever files can go; missing abilities and empty settings show a reason', () => {
  const m = cm.composeMenu(base, undefined);
  const find = (id) => flat(m.items).find(i => i.id === id);
  assert.equal(cm.itemState(find('attach_photo'), { upload: true }).state, 'ok', 'never hidden for a missing camera API (plain-HTTP phones)');
  const noUpload = cm.itemState(find('attach_photo'), { upload: false });
  assert.equal(noUpload.state, 'unavailable'); assert.equal(noUpload.reason, 'cm_reason_upload');
  assert.equal(cm.itemState(find('attach_file'), { upload: false }).reason, 'cm_reason_upload');
  assert.equal(cm.itemState(find('music'), { upload: true }).state, 'unavailable');
  for (const id of ['agent', 'web_search', 'deep_research', 'thinking', 'import']) assert.equal(cm.itemState(find(id), { upload: false }).state, 'ok', id);
  // Painting needs an image model and its provider: unavailable only while one of them is empty, pointing to Settings.
  assert.equal(cm.itemState(find('image'), { upload: true, settings: null }).state, 'ok', 'settings not read: not checked, the kernel says');
  const empty = cm.itemState(find('image'), { upload: true, settings: { image_model: '', image_provider: 'img' } });
  assert.deepEqual(empty, { state: 'unavailable', reason: 'cm_reason_image' });
  assert.equal(find('image').panel, 'settings');
  assert.equal(cm.itemState(find('image'), { upload: true, settings: { image_model: 'image-1', image_provider: 'img' } }).state, 'ok');
  const bare = { id: 'b', label: 'B', kind: 'toggle', field: 'x', needs: ['k'] };
  assert.equal(cm.itemState(bare, { upload: true, settings: {} }).reason, 'cm_reason_needs');
});

test('switches: each sets the field its entry names; one field is one choice; an action stands alone', () => {
  const { items } = cm.composeMenu(base, undefined);
  let on = {};
  for (const id of ['web_search', 'deep_research', 'thinking']) on = cm.switchToggle(items, on, id);
  assert.deepEqual(cm.requestFields(items, on), { web_search: true, deep_research: true, reasoning_effort: 'high' });
  assert.deepEqual(ids(cm.onSwitches(items, on)), ['web_search', 'deep_research', 'thinking']);
  on = cm.switchToggle(items, on, 'agent');
  assert.deepEqual(cm.requestFields(items, on), { action: 'agent.run' }, 'the agent takes only the words');
  on = cm.switchToggle(items, on, 'image');
  assert.deepEqual(cm.requestFields(items, on), { action: 'depict' }, 'one action per message');
  on = cm.switchToggle(items, on, 'web_search');
  assert.deepEqual(cm.requestFields(items, on), { web_search: true }, 'a tool turns the action off');
  on = cm.switchToggle(items, on, 'web_search');
  assert.deepEqual(cm.requestFields(items, on), {});
  assert.deepEqual(cm.switchToggle(items, {}, 'memory'), {}, 'memory is a switch of this view, not a request field');
  // An entry the menu no longer has (hidden by an overlay) sends nothing.
  const hidden = cm.composeMenu(base, { ops: [{ op: 'hide', id: 'web_search' }] });
  assert.deepEqual(cm.requestFields(hidden.items, { web_search: true }), {});
});

test('a refused send names the file it would not take', () => {
  const refs = ['blob:' + 'a'.repeat(64) + '/notatki.txt', 'blob:' + 'b'.repeat(64) + '/raport.pdf'];
  assert.deepEqual(cm.refusedAttachment('unsupported', `this kind of file cannot be sent (images and plain-text files can): ${refs[1]}`, refs), { ref: refs[1], why: 'unreadable' });
  assert.deepEqual(cm.refusedAttachment('not_found', `attachment not found: ${refs[0]}`, refs), { ref: refs[0], why: 'missing' });
  assert.equal(cm.refusedAttachment('network', 'the provider did not answer', refs), null);
  assert.equal(cm.refusedAttachment('unsupported', undefined, refs), null);
});

test('attachment references: the kernel reads blob:<hash>/<name>; the name shows', () => {
  const hash = 'a'.repeat(64);
  assert.equal(cm.attachmentRef(hash, 'plan dnia.pdf'), `blob:${hash}/plan dnia.pdf`);
  assert.equal(cm.attachmentName(`blob:${hash}/plan dnia.pdf`), 'plan dnia.pdf');
  assert.equal(cm.attachmentName('/home/me/zdjęcie.jpg'), 'zdjęcie.jpg');
  assert.equal(cm.attachmentName('plain.txt'), 'plain.txt');
});

test('the model of a conversation through the scope chain: only a level more specific than the person is its own', () => {
  const chain = ['installation', 'person', 'project', 'conversation'].map(level => ({ level }));
  assert.equal(cm.conversationModel({ defined: true, value: 'x/conv', scope: 'conversation', chain }), 'x/conv');
  assert.equal(cm.conversationModel({ defined: true, value: 'x/project', scope: 'project', chain }), 'x/project');
  assert.equal(cm.conversationModel({ defined: true, value: 'x/mine', scope: 'person', chain }), null, "the person's setting does not override the picker");
  assert.equal(cm.conversationModel({ defined: true, value: 'x/preset', scope: 'installation', chain }), null);
  assert.equal(cm.conversationModel({ defined: false, chain }), null);
  assert.equal(cm.conversationModel({ defined: true, value: 7, scope: 'conversation', chain }), null);
  assert.equal(cm.conversationModel(null), null);
});

test('writing the model at the conversation level keeps the rest of the metadata; the default model removes it', () => {
  const meta = { units: { project: 'p1' }, settings: { chat_flow: 'graph' } };
  const set = cm.withConversationModel(meta, 'default_model', 'x/conv');
  assert.deepEqual(set, { units: { project: 'p1' }, settings: { chat_flow: 'graph', default_model: 'x/conv' } });
  assert.deepEqual(meta.settings, { chat_flow: 'graph' }, 'the input is not changed');
  assert.deepEqual(cm.withConversationModel(set, 'default_model', ''), { units: { project: 'p1' }, settings: { chat_flow: 'graph' } });
  assert.deepEqual(cm.withConversationModel(undefined, 'default_model', 'm'), { settings: { default_model: 'm' } });
});

// ---- M1-W1b: the + menu gets the context entries; nothing is deleted ----
// The ids of the menu before M1-W1b (P33), written out: the test compares them with the data, so an entry that disappeared fails here.
const P33_IDS = ['attach_file', 'attach_photo', 'model', 'tools', 'web_search', 'deep_research', 'thinking', 'agent', 'image', 'music', 'context', 'memory', 'knowledge', 'perspective', 'import'];

test('M1-W1b: every entry of the old menu still exists; the new ones are in the context submenu, named by data', () => {
  const m = cm.composeMenu(base, entry('chat.composer_menu_ops').value);
  assert.deepEqual(m.issues, []);
  const have = ids(flat(m.items));
  for (const id of P33_IDS) assert.ok(have.includes(id), `the old entry ${id} is still in the menu`);
  const find = (id) => flat(m.items).find(i => i.id === id);
  for (const [id, step] of [['wider', 'wider'], ['deeper', 'deeper'], ['narrower', 'narrower'], ['shallower', 'shallower']]) {
    assert.deepEqual([find(id).kind, find(id).step], ['step', step]);
  }
  assert.equal(find('selector_profile').kind, 'profiles');
  assert.deepEqual([find('flow_text_graph').kind, find('flow_text_graph').focus], ['node', 'setting:chat_flow']);
  assert.equal(find('preview_request').kind, 'preview');
  // the toggles keep their behaviour (the flags), only the words changed
  assert.deepEqual([find('memory').kind, find('memory').flag], ['toggle', 'memory']);
  assert.deepEqual([find('knowledge').kind, find('knowledge').flag], ['toggle', 'knowledge']);
});

test('M1-W1b: the steps are the steps of the action rules (data), and every new entry has a label and a help word in both languages', () => {
  const rules = JSON.parse(readFileSync(new URL('../../../data/runtime/action_rules.pack', import.meta.url), 'utf8')).defaults.scoped_values.keys;
  const known = new Set(Object.values(rules).flatMap(k => Object.keys(k.steps ?? {})));
  const m = cm.composeMenu(base, undefined);
  for (const item of flat(m.items).filter(i => i.kind === 'step')) assert.ok(known.has(item.step), `${item.step} is a step of action_rules.pack`);
  const tEn = tIn('en');
  for (const item of flat(m.items).filter(i => ['step', 'profiles', 'node', 'preview'].includes(i.kind))) {
    for (const t of [plT, tEn]) {
      assert.notEqual(t(item.label), item.label, `${item.id} has a word`);
      if (item.help !== undefined) assert.notEqual(t(item.help), item.help, `${item.id} help has a word`);
    }
  }
  assert.equal(tEn('cm_memory'), 'Profile and memory (from the You node)');
  assert.equal(plT('cm_knowledge'), 'Twierdzenia z analizy');
});

test('M1-W1b: a step without its step, a node without its address are said, not dropped', () => {
  const m = cm.composeMenu({ items: [{ id: 'a', label: 'Szerzej', kind: 'step' }, { id: 'b', label: 'Karta', kind: 'node' }, { id: 'c', label: 'Ok', kind: 'step', step: 'wider' }] }, undefined);
  assert.deepEqual(codes(m), ['no_step', 'no_focus']);
  assert.deepEqual(ids(m.items), ['c']);
  for (const code of ['no_step', 'no_focus']) for (const [t, lang] of [[plT, 'pl'], [tIn('en'), 'en']]) assert.ok(!cm.issueText({ code, params: { name: 'X' } }, t, lang).startsWith('cm_issue_'), `${code} has a word in ${lang}`);
});

test('M1-W1b: an entry the screen cannot run keeps its row and says why (blocked), the others stay usable', () => {
  const m = cm.composeMenu(base, undefined);
  const find = (id) => flat(m.items).find(i => i.id === id);
  const ctx = { upload: true, settings: null, blocked: { preview_request: 'cm_reason_preview_empty' } };
  assert.deepEqual(cm.itemState(find('preview_request'), ctx), { state: 'unavailable', reason: 'cm_reason_preview_empty' });
  assert.deepEqual(cm.itemState(find('wider'), ctx), { state: 'ok' });
  assert.notEqual(plT('cm_reason_preview_empty'), 'cm_reason_preview_empty');
});
