// P15: a message that asks for secrets (connecting a storage) carries the keys; the page shows one button that asks the shell for Settings.
// Texts are data (conversation_view.pack, pl and en); a secret is never taken from the conversation.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const o = await loader.load('src/context/open-settings.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const generated = JSON.parse(await readFile(new URL('../../src/onboarding/generated/conversation-view.json', import.meta.url), 'utf8'));
const locales = pack.entries.find(row => row.key === 'presentation.open_settings').value.locales;
const feature = p.resolvePresentationFeature(p.resolvePresentation(), 'open_settings');
const meta = { action_run: { run_id: 'arun_1', status: 'completed', open_settings: { section: 'secrets', keys: ['storage_gdrive_refresh_token', 'storage_gdrive_client_id'] } } };

test('generated web catalog carries the texts, pl and en, with the same keys', () => {
  assert.deepEqual(generated.entries.find(row => row.key === 'presentation.open_settings').value, pack.entries.find(row => row.key === 'presentation.open_settings').value);
  assert.deepEqual(Object.keys(locales).sort(), ['en', 'pl']);
  assert.deepEqual(Object.keys(locales.en).sort(), Object.keys(locales.pl).sort());
  assert.equal(o.OPEN_SETTINGS_FEATURE, 'open_settings');
});
test('the keys a message asks for are read; a message without keys asks nothing', () => {
  assert.deepEqual(o.openSettingsHint(meta), { section: 'secrets', keys: ['storage_gdrive_refresh_token', 'storage_gdrive_client_id'] });
  assert.equal(o.openSettingsHint(undefined), null);
  assert.equal(o.openSettingsHint({}), null);
  assert.equal(o.openSettingsHint({ action_run: { open_settings: { keys: [] } } }), null);
  assert.equal(o.openSettingsHint({ action_run: { open_settings: 'settings' } }), null);
  assert.deepEqual(o.openSettingsHint({ action_run: { open_settings: { keys: ['a', 3, '', 'b'] } } }), { section: 'secrets', keys: ['a', 'b'] });
});
test('the note names the keys and the button opens Settings, in the page language', () => {
  const words = o.openSettingsTexts(feature, o.openSettingsHint(meta));
  assert.equal(words.button, locales.en.button);
  assert.equal(words.note, locales.en.note.replace('{{keys}}', 'storage_gdrive_refresh_token, storage_gdrive_client_id'));
  assert.deepEqual(o.openSettingsTexts(null, { section: 'secrets', keys: ['k'] }), { button: 'settings', note: 'k' });
});
test('the button asks the shell, not a panel it cannot know', () => {
  const seen = [];
  const target = { dispatchEvent(event) { seen.push(event); return true; } };
  assert.equal(o.requestOpenPanel(o.SETTINGS_PANEL, target), true);
  assert.equal(seen.length, 1);
  assert.equal(seen[0].type, o.OPEN_PANEL_EVENT);
  assert.equal(o.panelOfRequest(seen[0]), 'settings');
  assert.equal(o.panelOfRequest({}), null);
  assert.equal(o.panelOfRequest({ detail: { panel: '' } }), null);
  assert.equal(o.panelOfRequest(null), null);
});
