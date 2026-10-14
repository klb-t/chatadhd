// P14 3d: a failed model route is a quiet note in the chat, in the language of the page; texts are data (conversation_view.pack).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const r = await loader.load('src/context/route-issue.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const generated = JSON.parse(await readFile(new URL('../../src/onboarding/generated/conversation-view.json', import.meta.url), 'utf8'));
const locales = pack.entries.find(row => row.key === 'presentation.route_issue').value.locales;
const feature = p.resolvePresentationFeature(p.resolvePresentation(), 'route_issue');
const failed = { code: 'model_route_failed', message: 'API error 500', fallback: 'heuristic' };

test('generated web catalog carries the route-issue texts, pl and en', () => {
  assert.deepEqual(generated.entries.find(row => row.key === 'presentation.route_issue').value, pack.entries.find(row => row.key === 'presentation.route_issue').value);
  assert.deepEqual(Object.keys(locales).sort(), ['en', 'pl']);
  assert.deepEqual(Object.keys(locales.en).sort(), Object.keys(locales.pl).sort());
  assert.equal(r.ROUTE_ISSUE_CHUNK, 'route_issue');
  assert.equal(r.ROUTE_ISSUE_FEATURE, 'route_issue');
});
test('the issues of the stream are read as they come; what is not an issue is dropped', () => {
  assert.deepEqual(r.routeIssues([failed]), [failed]);
  assert.deepEqual(r.routeIssues([{ code: 'x' }]), [{ code: 'x', message: '', fallback: '' }]);
  assert.deepEqual(r.routeIssues([null, 3, 'text', {}, { message: 'no code' }]), []);
  assert.deepEqual(r.routeIssues(undefined), []);
  assert.deepEqual(r.routeIssues({ code: 'model_route_failed' }), []);
});
test('a failed model route is one quiet line with the reason, in the page language', () => {
  const notes = r.routeIssueNotes(feature, r.routeIssues([failed]));
  assert.equal(notes.length, 1);
  assert.equal(notes[0], locales.en.model_route_failed.replace('{{message}}', 'API error 500'));
  assert.match(notes[0], /API error 500/);
  assert.match(notes[0], /Nothing was lost/);
});
test('another problem gets the general line with its code; several keep their order', () => {
  const notes = r.routeIssueNotes(feature, r.routeIssues([failed, { code: 'odd_thing', message: 'details' }]));
  assert.equal(notes.length, 2);
  assert.match(notes[0], /API error 500/);
  assert.equal(notes[1], locales.en.other.replace('{{code}}', 'odd_thing').replace('{{message}}', 'details'));
});
test('without a catalog the machine names are shown', () => {
  assert.deepEqual(r.routeIssueNotes(null, r.routeIssues([failed])), ['model_route_failed: API error 500']);
  assert.deepEqual(r.routeIssueNotes(null, r.routeIssues([{ code: 'x' }])), ['x']);
  assert.deepEqual(r.routeIssueNotes(feature, []), []);
});
