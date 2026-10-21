// M1-W0 r2:b2: every test id the Android driver's document (docs/reports/M1_W0_TESTIDS.md, the contract for S2's android-smoke
// steps) lists must exist in the web sources. A literal id (`nav-bar-wider`) must occur as a string in src/; a generated id
// (`nav-row-area:rom`, `shell-menu-graph`, `nav-view-both`, `panel-settings`) is accepted when some prefix of it opens a template
// literal in src/ (`nav-row-${…}`, `shell-menu-${…}`). A stale row (an id that nothing renders any more, like the `nav-bar-planned`
// of the first package) would make a smoke step wait for an element that never comes; this test says so first.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, readdir } from 'node:fs/promises';
import path from 'node:path';

const web = new URL('../../', import.meta.url);
const doc = await readFile(new URL('../../../../docs/reports/M1_W0_TESTIDS.md', import.meta.url), 'utf8');

async function sources(dir) {
  const files = (await readdir(dir, { recursive: true })).filter(f => /\.(tsx?|json)$/.test(f));
  return (await Promise.all(files.map(f => readFile(path.join(dir, f), 'utf8')))).join('\n');
}
const src = await sources(path.join(web.pathname, 'src'));

// A token in backticks that looks like a test id: lowercase words joined by hyphens (at least one), optionally with a placeholder
// (`<adres>`, `<i>`) or a generated tail (`area:rom`); not a data-/aria- attribute, not a word of the catalogue (`card.no_value`).
// Only the lines that name screen elements count: table rows (`| ekran | testid | … |`) and step chains (`a` → `b` → …); a prose line
// may hold a binary name in backticks (`loom-server`) that is not an id.
const ID = /^[a-z][a-z0-9]*(?:-(?:[a-z0-9_:]+|<[^>]+>))+$/;
const lines = doc.split('\n').filter(l => l.startsWith('|') || l.includes('→'));
const tokens = lines.flatMap(l => [...l.matchAll(/`([^`]+)`/g)].map(m => m[1])).filter(t => ID.test(t) && !/^(data|aria)-/.test(t));
const ids = [...new Set(tokens)];
// Limit: a generated id (`nav-bar-<x>`) is accepted by its prefix alone, so a stale tail is not seen here — the second test pins the
// one that was stale once (nav-bar-planned); a reviewer who removes a bar button adds its id there.

/** The id is in the sources: as a literal, or through a template literal that starts with one of its hyphen prefixes. */
function found(id) {
  const literal = id.replace(/-<[^>]+>.*$/, '').replace(/-[a-z0-9_]*:[a-z0-9_:]*$/, '');
  if (!id.includes('<') && !id.includes(':') && (src.includes(`"${id}"`) || src.includes(`'${id}'`) || src.includes('`' + id + '`'))) return true;
  const parts = literal.split('-');
  for (let n = parts.length; n >= 1; n--) {
    const prefix = parts.slice(0, n).join('-');
    if (src.includes('`' + prefix + '-${') || src.includes('`' + prefix + '${')) return true;
  }
  return false;
}

test('M1_W0_TESTIDS.md names only test ids that the web sources render (literal or generated)', () => {
  assert.ok(ids.length >= 150, `the document lists test ids (${ids.length} found)`);
  const missing = ids.filter(id => !found(id));
  assert.deepEqual(missing, [], `ids listed in docs/reports/M1_W0_TESTIDS.md that nothing in loom/web/src renders: ${missing.join(', ')}`);
});

test('the ids the finding named are in the document and in the sources', () => {
  for (const id of ['nav-actions-note', 'nav-view-note', 'nav-perspectives', 'nav-perspective-menu']) {
    assert.ok(ids.includes(id), `${id} is listed`);
    assert.ok(found(id), `${id} is rendered`);
  }
  assert.ok(!ids.includes('nav-bar-planned'), 'nav-bar-planned (gone since r1:b2) is not listed');
});
