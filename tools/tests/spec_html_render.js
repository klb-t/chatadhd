#!/usr/bin/env node
'use strict';
// Pomocnik testów strony specyfikacji (R89 P2): otwiera wygenerowany HTML w bezgłowym Chromium na danej szerokości,
// opcjonalnie z atrapą bazy strony (window.claude.use('db')) i zapisaną wizytą, i wypisuje JSON z tym, co widać.
// Użycie: node spec_html_render.js '<opcje JSON>'   opcje: html (ścieżka), width, stubDb (lista dokumentów skrzynki),
//         visit (ISO), noStorage (bool), click ('approve'), shot (ścieżka PNG), shotSel (selektor elementu), typeRemark, search,
//         probe (lista selektorów: tekst widocznego elementu albo null)
// Playwright i Chromium: PLAYWRIGHT_MODULES / CHROMIUM_PATH albo znane miejsca na tej maszynie; brak = kod wyjścia 3 (test pomija).
const fs = require('fs');
const path = require('path');

function findPlaywright() {
  const cands = [process.env.PLAYWRIGHT_MODULES,
    path.join(__dirname, '../../loom/web/node_modules'), '/home/user/wt/m1d1/loom/web/node_modules'].filter(Boolean);
  for (const c of cands) { try { return require(require.resolve('playwright', { paths: [c] })); } catch (e) { /* następny */ } }
  return null;
}

(async () => {
  const opt = JSON.parse(process.argv[2] || '{}');
  const pw = findPlaywright();
  const exe = process.env.CHROMIUM_PATH || '/opt/pw-browsers/chromium';
  if (!pw || !fs.existsSync(exe)) { console.error('brak playwright albo chromium'); process.exit(3); }
  const html = fs.readFileSync(opt.html, 'utf8');
  const doc = '<!doctype html><html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"></head><body>' + html + '</body></html>';
  const browser = await pw.chromium.launch({ executablePath: exe, args: ['--no-sandbox'] });
  try {
    const page = await browser.newPage({ viewport: { width: opt.width || 400, height: 800 } });
    const errors = [];
    page.on('pageerror', e => errors.push(String(e)));
    await page.route('**/*', r => {
      const u = r.request().url();
      if (u === 'https://spec.test/') return r.fulfill({ contentType: 'text/html; charset=utf-8', body: doc });
      return r.abort();  // fonty z internetu nie są potrzebne do pomiaru układu
    });
    if (opt.visit && !opt.noStorage) await page.addInitScript(v => { try { if (!localStorage.getItem('spec.lastVisit')) localStorage.setItem('spec.lastVisit', v); } catch (e) {} }, opt.visit);
    if (opt.stubDb) {
      await page.addInitScript(initial => {
        window.__adds = [];
        const docs = initial.map((d, i) => ({ id: 'd' + i, data: () => d }));
        // stubDb: lista dokumentów spec_inbox albo {kolekcja: [dokumenty]}; zapisy (add/set) trafiają do window.__adds z nazwą kolekcji w __coll
        const byName = Array.isArray(initial) ? { spec_inbox: initial } : initial;
        const mk = name => {
          const docs = (byName[name] || []).map((d, i) => ({ id: d.__id || ('d' + i), data: () => d }));
          const q = { get: async () => ({ docs }) };
          return { orderBy: () => ({ limit: () => q }), limit: () => q, get: q.get,
            add: async e => { window.__adds.push(name === 'spec_inbox' ? e : { __coll: name, ...e }); return { id: 'new' }; },
            doc: id => ({ get: async () => { const d = (byName[name] || []).find(x => x.__id === id); return { exists: !!d, data: () => d }; },
                          set: async e => { window.__adds.push({ __coll: name, __id: id, ...e }); } }) };
        };
        window.claude = { use: async name => name === 'db' ? { collection: mk } : (name === 'user' ? { can: async () => true, id: 'owner' } : null) };
      }, opt.stubDb);
    }
    if (opt.probe) await page.addInitScript(p => { window.__probe = p; }, opt.probe);
    if (opt.noStorage) await page.setContent(doc, { waitUntil: 'domcontentloaded' });
    else await page.goto('https://spec.test/', { waitUntil: 'domcontentloaded' });
    await page.waitForTimeout(500);
    if (opt.click === 'approve') {
      await page.click('.qitem [data-k="approve"]');
      await page.waitForTimeout(300);
    }
    if (opt.typeRemark) {
      await page.click('.qitem [data-k="remark"]');
      await page.fill('.qitem textarea:visible', opt.typeRemark);
      await page.click('.qitem [data-send]:visible');
      await page.waitForTimeout(300);
    }
    if (opt.search !== undefined) { await page.fill('#q', opt.search); await page.waitForTimeout(200); }
    const out = await page.evaluate(() => {
      const t = s => { const e = document.querySelector(s); return e ? e.textContent.trim() : null; };
      const rows = Array.from(document.querySelectorAll('.qitem'));
      return {
        scrollWidth: document.documentElement.scrollWidth, clientWidth: document.documentElement.clientWidth,
        queueHidden: document.getElementById('queue').hidden,
        queueHeader: t('#queue h2'),
        rows: rows.map(r => ({ alias: r.querySelector('.alias').textContent.trim(), hidden: r.classList.contains('hidden'),
          silent: (r.querySelector('.silent') || {}).textContent, decide: !!r.querySelector('.decide button'),
          more: r.querySelector('[data-more]') ? !r.querySelector('[data-more]').hidden : null,
          newChip: !!r.querySelector('.chip.new:not([hidden])'), answered: r.classList.contains('answered'),
          criteria: Array.from(r.querySelectorAll('.crit li')).map(x => x.textContent.trim()),
          qtime: (r.querySelector('[data-qtime]') || {}).textContent || null,
          qclass: (r.querySelector('.chip.qclass') || {}).textContent || null })),
        probe: Object.fromEntries((window.__probe || []).map(sel => { const e = document.querySelector(sel); return [sel, e ? (e.hidden ? null : e.textContent.trim()) : null]; })),
        countsLine: t('#counts'), newInTree: document.querySelectorAll('details.node .chip.new:not([hidden])').length,
        qNew: t('#q-new'), dbNote: document.getElementById('q-dbnote') ? !document.getElementById('q-dbnote').hidden : null,
        adds: window.__adds || null,
      };
    });
    out.errors = errors;
    if (opt.shot) await (opt.shotSel ? page.locator(opt.shotSel).screenshot({ path: opt.shot }) : page.screenshot({ path: opt.shot }));
    console.log(JSON.stringify(out));
  } finally { await browser.close(); }
})().catch(e => { console.error(String(e && e.stack || e)); process.exit(2); });
