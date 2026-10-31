import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { urlImportDescriptor, urlImportFile } from '../src/components/url-import-request.mjs';

test('URL descriptor uses the existing multipart import file contract', async () => {
  const file = urlImportFile('https://example.invalid/share#fragment', 'share');
  assert.equal(file.name, 'source.loomurl');
  assert.deepEqual(JSON.parse(await file.text()), {
    schema: 'loom.url_import_request/1', url: 'https://example.invalid/share#fragment', mode: 'share',
  });
});
test('page mode stays separate from conversation parsing', () => {
  assert.equal(JSON.parse(urlImportDescriptor('https://example.invalid/doc', 'page')).mode, 'page');
});
test('credentials, control bytes and unsupported schemes are rejected in the descriptor UI', () => {
  for (const url of ['file:///tmp/a', 'https://user:secret@example.invalid/', 'https://example.invalid/\nsecret', 'https://x.invalid\\@127.0.0.1/', 'not a URL']) {
    assert.equal(urlImportDescriptor(url, 'page'), null);
  }
});
test('Polish and English status labels exist, with no success fallback for denied results', () => {
  const words = JSON.parse(readFileSync(new URL('../src/components/url-import-words.json', import.meta.url)));
  for (const id of ['complete', 'partial', 'denied', 'unsupported', 'missing', 'timeout', 'oversize', 'login_required']) {
    assert.ok(words[id].pl); assert.ok(words[id].en);
  }
  const component = readFileSync(new URL('../src/components/UrlImportPanel.tsx', import.meta.url), 'utf8');
  assert.match(component, /api\.importFile\(file/);
  assert.doesNotMatch(component, /fetch\(/);
  assert.match(component, /: "unsupported"/);
});
