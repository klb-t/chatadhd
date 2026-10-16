// P33: the + menu as data. Pure logic: the shipped entries, the user's operations (hide, add, move), what each entry says about itself,
// the labels in Polish and English, and the attachment reference.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
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

test('the shipped menu: the union of the popular chat apps, nothing invented as a limit', () => {
  const m = cm.composeMenu(base, entry('chat.composer_menu_ops').value);
  assert.deepEqual(m.issues, []);
  assert.deepEqual(ids(m.items), ['attach_file', 'attach_photo', 'model', 'tools', 'context', 'import']);
  assert.deepEqual(ids(m.items.find(i => i.id === 'tools').items), ['web_search', 'agent', 'thinking', 'image', 'music']);
  assert.deepEqual(ids(m.items.find(i => i.id === 'context').items), ['memory', 'knowledge', 'perspective']);
});

test('every entry has a word in Polish and English; an entry without a backend has a reason', () => {
  const feature = entry('presentation.ui_shell').value;
  assert.deepEqual(Object.keys(feature.locales.en).sort(), Object.keys(feature.locales.pl).sort());
  const m = cm.composeMenu(base, undefined);
  for (const item of flat(m.items)) {
    for (const lang of ['en', 'pl']) {
      assert.ok(feature.locales[lang][item.label], `${lang}: label ${item.label}`);
      if (item.kind === 'unavailable') assert.ok(feature.locales[lang][item.reason], `${lang}: reason ${item.reason}`);
    }
  }
  const t = text.shellTranslator(p.resolvePresentationFeature(p.resolvePresentation(), 'ui_shell'), 'pl');
  assert.equal(t('cm_attach_file'), 'Załącz plik');
  assert.match(t('cm_reason_image'), /dostawcy/);
});

test('overlay: hide, add (top, in a submenu, before another), move', () => {
  const ops = { ops: [
    { op: 'hide', id: 'import' },
    { op: 'hide', id: 'music' },
    { op: 'add', item: { id: 'translate', label: { en: 'Translate', pl: 'Tłumacz' }, kind: 'panel', panel: 'settings' }, before: 'model' },
    { op: 'add', item: { id: 'notes', label: 'Notes', kind: 'toggle', flag: 'memory' }, parent: 'tools' },
    { op: 'move', id: 'tools', before: 'attach_file' },
  ] };
  const m = cm.composeMenu(base, ops);
  assert.deepEqual(m.issues, []);
  assert.deepEqual(ids(m.items), ['tools', 'attach_file', 'attach_photo', 'translate', 'model', 'context']);
  assert.deepEqual(ids(m.items[0].items), ['web_search', 'agent', 'thinking', 'image', 'notes']);
  assert.equal(base.items.length, 6, 'the shipped data is not changed');
});

test('overlay mistakes are reported, never silently dropped, and do not break the rest', () => {
  const m = cm.composeMenu(base, { ops: [
    { op: 'hide', id: 'nope' }, { op: 'add', item: { id: 'x', kind: 'wat' } }, { op: 'add', item: { id: 'model', kind: 'model' } },
    { op: 'add', item: { id: 'y', kind: 'panel' }, parent: 'missing' }, { op: 'jump' }, 7, { op: 'hide', id: 'import' },
  ] });
  assert.equal(m.issues.length, 6);
  assert.deepEqual(ids(m.items), ['attach_file', 'attach_photo', 'model', 'tools', 'context']);
  assert.equal(cm.composeMenu(base, { nope: 1 }).issues.length, 1);
  assert.ok(cm.composeMenu(undefined, undefined).items.length === 0);
});

test('state of an entry: the camera entry only where there is a camera; missing abilities show a reason', () => {
  const m = cm.composeMenu(base, undefined);
  const find = (id) => flat(m.items).find(i => i.id === id);
  assert.equal(cm.itemState(find('attach_photo'), { camera: false, upload: true }).state, 'hidden');
  assert.equal(cm.itemState(find('attach_photo'), { camera: true, upload: true }).state, 'ok');
  const noUpload = cm.itemState(find('attach_file'), { camera: true, upload: false });
  assert.equal(noUpload.state, 'unavailable'); assert.equal(noUpload.reason, 'cm_reason_upload');
  for (const id of ['agent', 'image', 'music']) assert.equal(cm.itemState(find(id), { camera: true, upload: true }).state, 'unavailable');
  assert.equal(cm.itemState(find('web_search'), { camera: false, upload: false }).state, 'ok');
  assert.equal(cm.itemState(find('import'), { camera: false, upload: false }).state, 'ok');
});

test('attachment references: the kernel reads blob:<hash>/<name>; the name shows', () => {
  const hash = 'a'.repeat(64);
  assert.equal(cm.attachmentRef(hash, 'plan dnia.pdf'), `blob:${hash}/plan dnia.pdf`);
  assert.equal(cm.attachmentName(`blob:${hash}/plan dnia.pdf`), 'plan dnia.pdf');
  assert.equal(cm.attachmentName('/home/me/zdjęcie.jpg'), 'zdjęcie.jpg');
  assert.equal(cm.attachmentName('plain.txt'), 'plain.txt');
});

test('values from data: the thinking effort and the model setting are entries of the menu, a missing one falls back to the preset', () => {
  const m = cm.composeMenu(base, undefined);
  assert.equal(cm.toggleValue(m.items, 'thinking', 'fallback'), 'high');
  assert.equal(cm.modelSetting(m.items), 'default_model');
  const changed = cm.composeMenu(base, { ops: [
    { op: 'hide', id: 'thinking' }, { op: 'add', item: { id: 'think_max', label: 'Max', kind: 'toggle', flag: 'thinking', value: 'max' }, parent: 'tools' },
    { op: 'hide', id: 'model' }, { op: 'add', item: { id: 'model2', label: 'Model', kind: 'model', setting: 'chat_model' } },
  ] });
  assert.deepEqual(changed.issues, []);
  assert.equal(cm.toggleValue(changed.items, 'thinking', 'fallback'), 'max');
  assert.equal(cm.modelSetting(changed.items), 'chat_model');
  const bare = cm.composeMenu({ items: [{ id: 't', label: 't', kind: 'toggle', flag: 'thinking' }, { id: 'm', label: 'm', kind: 'model' }] }, undefined);
  assert.equal(cm.toggleValue(bare.items, 'thinking', cm.THINKING_EFFORT), cm.THINKING_EFFORT);
  assert.equal(cm.modelSetting(bare.items), cm.MODEL_SETTING);
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
