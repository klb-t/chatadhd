// W9 (OKNA §7.2, D14, §10): the style of a window / of a layout (style.ts). Examples first (inheritance window → layout → person →
// fallback, unknown references falling through with an advisory, one journal entry per choice and an exact undo, profile_layout_fields
// modes, profile_moves_dock and its fix), then a generative pass (seeded; LOOM_STYLE_RUNS raises the preset): random layouts, random
// references (also unknown, malformed and useless ones) and random sequences of choices - resolution always returns the first known level,
// a choice never touches anything of the layout but `profile`, undo restores the layout exactly, and the fix only mirrors one split.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const St = await loader.load('src/shell/windows/style.ts');
const S = await loader.load('src/shell/windows/session.ts');
const M = await loader.load('src/shell/windows/model.ts');
const sv = await loader.load('src/shell/start-view.ts');
const env = (k, d) => (Number.isFinite(Number(process.env[k])) && process.env[k] ? Number(process.env[k]) : d);
const CONFIG = { runs: env('LOOM_STYLE_RUNS', 400), seed: env('LOOM_STYLE_SEED', 20261028) };

const doc = (id, rev, side = 'left', label = id) => ({ id, profile_revision: rev, label, presentation: { sidebar: { side, width: 280 } } });
const DOCS = [doc('loom-default', 1), doc('loom-default', 2), doc('chatgpt-inspired', 1), doc('chatgpt-inspired', 2), doc('claude-inspired', 2), doc('righty', 1, 'right', 'Righty')];
const ref = (id, revision = null) => ({ id, revision });
const grid = () => S.layoutState(sv.LAYOUTS.siatka);
const chatOf = l => l.windows.find(w => w.kind === 'chat').id;
const listOf = l => l.windows.find(w => w.kind === 'conversation_list').id;
const strip = l => JSON.stringify({ ...l, profile: null, windows: l.windows.map(w => ({ ...w, profile: null })) });

test('refOf reads every stored shape and nothing else', () => {
  assert.deepEqual(St.refOf('chatgpt-inspired'), ref('chatgpt-inspired'));
  assert.deepEqual(St.refOf('chatgpt-inspired@2'), ref('chatgpt-inspired', 2));
  assert.deepEqual(St.refOf({ id: 'x', revision: 3 }), ref('x', 3));
  assert.deepEqual(St.refOf({ id: 'x', profile_revision: 1 }), ref('x', 1));
  assert.deepEqual(St.refOf({ id: 'x', revision: 1.5 }), ref('x', null));
  for (const junk of [null, undefined, '', '   ', 7, true, [], ['x'], {}, { id: '' }, { id: 3 }]) assert.equal(St.refOf(junk), null, JSON.stringify(junk));
});

test('inheritance: window → layout → person → fallback; newest revision when none is named', () => {
  let s = grid(); const chat = chatOf(s.layout);
  let r = St.resolveStyle(s.layout, chat, null, DOCS);
  assert.equal(r.source, 'default'); assert.equal(r.profile.id, 'loom-default'); assert.equal(r.profile.profile_revision, 2);
  r = St.resolveStyle(s.layout, chat, ref('claude-inspired', 2), DOCS);
  assert.equal(r.source, 'person'); assert.equal(r.profile.id, 'claude-inspired');
  s = St.setLayoutProfile(s, ref('chatgpt-inspired', 1));
  r = St.resolveStyle(s.layout, chat, ref('claude-inspired', 2), DOCS);
  assert.equal(r.source, 'layout'); assert.equal(r.profile.profile_revision, 1);
  s = St.setWindowProfile(s, chat, ref('righty'));
  r = St.resolveStyle(s.layout, chat, ref('claude-inspired', 2), DOCS);
  assert.equal(r.source, 'window'); assert.equal(r.profile.id, 'righty');
  // another window still inherits from the layout
  assert.equal(St.resolveStyle(s.layout, listOf(s.layout), null, DOCS).source, 'layout');
  // the layout level alone (windowId null)
  assert.equal(St.resolveStyle(s.layout, null, null, DOCS).profile.id, 'chatgpt-inspired');
  // no fallback and nothing named: none (the caller draws without a style)
  assert.equal(St.resolveStyle(grid().layout, null, null, DOCS, null).source, 'none');
});

test('an unknown reference falls through to the next level and is said, never dropped', () => {
  let s = grid(); const chat = chatOf(s.layout);
  s = St.setWindowProfile(s, chat, ref('gone', 4));
  s = St.setLayoutProfile(s, ref('chatgpt-inspired', 9));
  const r = St.resolveStyle(s.layout, chat, null, DOCS);
  assert.equal(r.source, 'default'); assert.deepEqual(r.missing, [ref('gone', 4), ref('chatgpt-inspired', 9)]);
  assert.deepEqual(St.refOf(s.layout.windows.find(w => w.id === chat).profile), ref('gone', 4), 'the reference stays in the layout');
  const adv = St.styleAdvisories(s.layout, null, DOCS, undefined).filter(a => a.code === 'profile_missing');
  assert.equal(adv.length, 2); assert.ok(adv[0].text.pl.includes('gone@4'));
});

test('a choice is one journal entry; the same choice none; undo restores the layout exactly', () => {
  const s0 = grid(); const chat = chatOf(s0.layout);
  const s1 = St.setWindowProfile(s0, chat, ref('righty', 1));
  assert.equal(s1.past.length, s0.past.length + 1); assert.equal(s1.past.at(-1).op, 'window.profile');
  assert.equal(St.setWindowProfile(s1, chat, ref('righty', 1)), s1);
  assert.equal(St.setWindowProfile(s1, 'no-such-window', ref('righty', 1)), s1);
  const s2 = St.setLayoutProfile(s1, ref('claude-inspired', 2));
  assert.equal(s2.past.at(-1).op, 'layout.profile');
  assert.equal(St.setLayoutProfile(s2, ref('claude-inspired', 2)), s2);
  const back = M.undo(M.undo(s2).state).state;
  assert.equal(JSON.stringify(back.layout), JSON.stringify(s0.layout));
  assert.equal(St.setLayoutProfile(St.setLayoutProfile(s0, ref('x')), null).layout.profile, null);
});

test('profile_layout_fields: open value, three known modes, unknown = preset with an advisory', () => {
  assert.deepEqual(St.layoutFieldsMode(undefined), { mode: 'layout', unknown: null });
  assert.deepEqual(St.layoutFieldsMode(' Window '), { mode: 'window', unknown: null });
  assert.deepEqual(St.layoutFieldsMode('off'), { mode: 'off', unknown: null });
  assert.deepEqual(St.layoutFieldsMode('sideways'), { mode: 'layout', unknown: 'sideways' });
  const a = St.styleAdvisories(grid().layout, null, DOCS, 'sideways');
  assert.equal(a.length, 1); assert.equal(a[0].code, 'profile_layout_fields_unknown');
});

test('profile_moves_dock: only an explicit style, per mode; the fix mirrors the split once and undoes exactly', () => {
  const s0 = grid(); const list = listOf(s0.layout); const leaf = s0.layout.windows.find(w => w.id === list).dock.leaf;
  assert.equal(St.leafSide(s0.layout.tree, leaf).side, 'left');
  // the person's profile or the fallback never asks to move a dock
  assert.equal(St.styleAdvisories(s0.layout, ref('righty'), DOCS, 'layout').length, 0);
  const sL = St.setLayoutProfile(s0, ref('righty'));
  let adv = St.styleAdvisories(sL.layout, null, DOCS, 'layout');
  assert.equal(adv.length, 1); assert.equal(adv[0].code, 'profile_moves_dock'); assert.equal(adv[0].evidence.want, 'right'); assert.equal(adv[0].window, list);
  assert.equal(St.styleAdvisories(sL.layout, null, DOCS, 'off').length, 0);
  // a style of the list window: only in mode `window`
  const sW = St.setWindowProfile(s0, list, ref('righty'));
  assert.equal(St.styleAdvisories(sW.layout, null, DOCS, 'layout').length, 0);
  assert.equal(St.styleAdvisories(sW.layout, null, DOCS, 'window').length, 1);
  // a style of another window never moves the list
  assert.equal(St.styleAdvisories(St.setWindowProfile(s0, chatOf(s0.layout), ref('righty')).layout, null, DOCS, 'window').length, 0);
  // the fix
  const fixed = St.applyStyleFix(sL, adv[0]);
  assert.equal(fixed.past.length, sL.past.length + 1);
  assert.equal(St.leafSide(fixed.layout.tree, leaf).side, 'right');
  assert.ok(Math.abs(St.leafSide(fixed.layout.tree, leaf).share - St.leafSide(sL.layout.tree, leaf).share) < 1e-12, 'the share is kept');
  assert.equal(St.styleAdvisories(fixed.layout, null, DOCS, 'layout').length, 0);
  assert.equal(St.applyStyleFix(fixed, adv[0]), fixed, 'a stale fix changes nothing');
  assert.equal(JSON.stringify(M.undo(fixed).state.layout), JSON.stringify(sL.layout));
  assert.deepEqual(M.check(fixed.layout), []);
  // a list collapsed to a handle (the drawer of `czat`) has no side to move: no advisory, no control that does nothing
  const czat = St.setLayoutProfile(S.layoutState(sv.LAYOUTS.czat), ref('righty'));
  adv = St.styleAdvisories({ ...czat.layout, tree: { ...czat.layout.tree, a: czat.layout.tree.b, b: czat.layout.tree.a } }, null, DOCS, 'layout');
  assert.equal(adv.filter(a => a.code === 'profile_moves_dock').length, 0);
});

function rng(seed) { let x = seed >>> 0; return () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; }; }
test(`generative: ${CONFIG.runs} random sequences of choices (seed ${CONFIG.seed})`, () => {
  const rand = rng(CONFIG.seed);
  const pick = a => a[Math.floor(rand() * a.length)];
  const refs = [null, ref('loom-default'), ref('loom-default', 1), ref('chatgpt-inspired', 2), ref('righty'), ref('righty', 1), ref('gone'), ref('claude-inspired', 7)];
  const layouts = [sv.LAYOUTS.czat, sv.LAYOUTS.siatka];
  for (let run = 0; run < CONFIG.runs; run++) {
    let s = S.layoutState(pick(layouts));
    if (rand() < 0.5) s = S.openPanel(s, pick(['settings', 'navigator', 'memory']));
    const s0 = s; const person = pick(refs); const docs = rand() < 0.1 ? [] : DOCS;
    const steps = 1 + Math.floor(rand() * 8);
    for (let k = 0; k < steps; k++) {
      const before = s;
      s = rand() < 0.5 ? St.setLayoutProfile(s, pick(refs)) : St.setWindowProfile(s, pick(s.layout.windows).id, pick(refs));
      assert.ok(s.past.length - before.past.length <= 1, 'at most one entry per choice');
      assert.equal(strip(s.layout), strip(before.layout), 'a choice touches only `profile`');
    }
    for (const w of s.layout.windows) {
      const r = St.resolveStyle(s.layout, w.id, person, docs);
      const chain = [St.refOf(w.profile), St.refOf(s.layout.profile), person, ref('loom-default')].filter(Boolean);
      const firstKnown = chain.find(c => St.findProfile(docs, c));
      if (firstKnown) { assert.deepEqual(r.ref, firstKnown); assert.equal(r.missing.length, chain.indexOf(firstKnown)); }
      else { assert.equal(r.source, 'none'); assert.equal(r.missing.length, chain.length); }
    }
    for (const mode of ['layout', 'window', 'off', 'junk']) {
      for (const a of St.styleAdvisories(s.layout, person, docs, mode)) {
        assert.ok(a.text.pl && a.text.en, 'every advisory is said in both languages');
        if (a.code !== 'profile_moves_dock') continue;
        assert.notEqual(mode, 'off');
        const f = St.applyStyleFix(s, a);
        assert.equal(f.past.length, s.past.length + 1);
        assert.deepEqual(M.check(f.layout), []);
        assert.equal(JSON.stringify({ ...f.layout, tree: null }), JSON.stringify({ ...s.layout, tree: null }), 'the fix changes only the tree');
      }
    }
    let back = s; while (back.past.length > s0.past.length) back = M.undo(back).state;
    assert.equal(JSON.stringify(back.layout), JSON.stringify(s0.layout), 'undo restores the layout exactly');
  }
});

// W-gaps: a style named by a word (window.style / layout.style sentences), imported profiles on the list, sidebar.width as a preset
test('W-gaps: styleOfWord maps sentence words to profiles (newest revision), clear words to inherit, anything else to null', () => {
  const docs = [doc('chatgpt-inspired', 1, 'left', 'ChatGPT-inspired'), doc('chatgpt-inspired', 2, 'left', 'ChatGPT-inspired'), doc('claude-inspired', 2, 'left', 'Claude-inspired'), doc('loom-default', 2, 'left', 'Loom')];
  assert.equal(St.styleOfWord('Claude', docs).doc.id, 'claude-inspired');
  assert.equal(St.styleOfWord('ChatGPT', docs).doc.profile_revision, 2, 'the newest revision first');
  assert.equal(St.styleOfWord('loom', docs).doc.id, 'loom-default');
  assert.equal(St.styleOfWord('claude-inspired', docs).doc.id, 'claude-inspired');
  assert.deepEqual(St.styleOfWord('domyślny', docs), { clear: true });
  assert.deepEqual(St.styleOfWord('default', docs), { clear: true });
  assert.equal(St.styleOfWord('Copilot', docs), null, 'an unknown word is answered, not guessed');
  assert.equal(St.styleOfWord('', docs), null);
  assert.deepEqual(St.styleOfWord('xyz', docs, { clear_words: ['xyz'] }), { clear: true }, 'clear words are data');
});
test('W-gaps: importedStyleDocs reads the Workshop store, skips malformed docs, survives an unreadable store', () => {
  const store = JSON.stringify({ schema: 'loom.application.views/1', custom_profiles: [doc('mine', 3, 'right', 'Mine'), { id: 'bad' }, null], views: [] });
  assert.deepEqual(St.importedStyleDocs(() => store).map(d => d.id), ['mine']);
  assert.deepEqual(St.importedStyleDocs(() => null), []);
  assert.deepEqual(St.importedStyleDocs(() => '{not json'), []);
});
test('W-gaps: withSidebarWidth sets the list share from the profile width once, at the choice, in the same journal entry; off and false leave it', () => {
  const s0 = grid();
  const wide = { ...doc('wide', 1, 'left', 'Wide'), presentation: { sidebar: { side: 'left', width: 500 } } };
  const docs = [...DOCS, wide];
  const s1 = St.setLayoutProfile(s0, ref('wide', 1));
  const s2 = St.withSidebarWidth(s0, s1, null, docs, 'layout', 1000);
  const at = St.leafSide(s2.layout.tree, s2.layout.windows.find(w => w.kind === 'conversation_list').dock.leaf);
  assert.ok(Math.abs(at.share - 0.5) < 1e-9, 'share = 500 / 1000, got ' + at.share);
  assert.equal(s2.past.length, s1.past.length, 'no extra journal entry');
  assert.equal(JSON.stringify(M.undo(s2).state.layout), JSON.stringify(s0.layout), 'one undo takes the style and the width back');
  assert.equal(St.withSidebarWidth(s0, s1, null, docs, 'off', 1000), s1, 'profile_layout_fields off: nothing');
  assert.equal(St.withSidebarWidth(s0, s1, null, docs, 'layout', 1000, { apply_sidebar_width: false }), s1, 'apply_sidebar_width false: nothing');
  assert.equal(St.withSidebarWidth(s2, s2, null, docs, 'layout', 1000), s2, 'no change of style: the dragged split stays');
  const huge = St.withSidebarWidth(s0, St.setLayoutProfile(s0, ref('wide', 1)), null, docs, 'layout', 100);
  const hat = St.leafSide(huge.layout.tree, huge.layout.windows.find(w => w.kind === 'conversation_list').dock.leaf);
  assert.ok(hat.share <= 1 && hat.share >= 0, 'a degenerate width is clamped into 0..1 (dock_narrow then says it)');
});

test('advisories retain added catalogs, with the side words in that same language', () => {
  const original = St.STYLES.words;
  St.STYLES.words = { ...original, unknown_mode: { ...original.unknown_mode, xx: 'X{value}' },
    moves_dock: { ...original.moves_dock, xx: 'X{label}:{want}/{now}' },
    side_left: { ...original.side_left, xx: 'XL' }, side_right: { ...original.side_right, xx: 'XR' } };
  try {
    assert.equal(St.styleAdvisories(grid().layout, null, DOCS, '$&')[0].text.xx, 'X$&');
    const styled = St.setLayoutProfile(grid(), ref('righty'));
    const advice = St.styleAdvisories(styled.layout, null, DOCS, 'layout').find(x => x.code === 'profile_moves_dock');
    assert.ok(advice);
    assert.equal(advice.text.xx, 'XRighty:XR/XL');
  } finally { St.STYLES.words = original; }
});
