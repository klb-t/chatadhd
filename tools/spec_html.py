#!/usr/bin/env python3
"""spec_html.py — żywy, hierarchiczny widok specyfikacji (R73, właściciel 2026-10-23).

Czyta docs/spec/spec.json (wynik `tools/spec.py build`) i pisze jedną samodzielną stronę HTML:
tematy → pozycje → (cytat dosłowny ze źródłem, sens, status z datą, parafraza koordynatora i jej stan).
Wszystko zwinięte, poza gałązkami „w edycji”: pozycjami zmienionymi w ostatnich N godzinach i tymi podanymi
jako --focus. Strona jest publikowana jako artefakt w aplikacji Claude po każdej zmianie spec (koordynator);
docelowo ten sam widok to perspektywa grafu w aplikacji (spec jako węzły).

Użycie:  python3 tools/spec.py html [--focus R73 ON05 ...] [--recent-hours 48] [--out docs/spec/SPEC.html]
Wszystkie wartości (ile godzin, co rozwinięte, kolejność) to presety.
"""
from __future__ import annotations

import datetime as _dt
import html
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
STATUS_LABEL = {
    "JEST": "jest", "CZĘŚCIOWO": "częściowo", "BRAK": "brak", "NIEAKTUALNE": "nieaktualne", "SPRZECZNE": "sprzeczne",
}
UND_LABEL = {
    "none": None, "proposed": "parafraza czeka na Ciebie", "approved": "parafraza zatwierdzona",
    "corrected": "poprawione Twoimi słowami",
}


def _recent(node: dict, since: _dt.date) -> bool:
    dates = [node.get("date")]
    und = node.get("understanding") or {}
    dates.append(und.get("date"))
    for h in und.get("history") or []:
        dates.append(h.get("date"))
    st = node.get("status") or {}
    if isinstance(st, dict):
        dates.append(st.get("checked_on"))
    for d in dates:
        try:
            if d and _dt.date.fromisoformat(str(d)[:10]) >= since:
                return True
        except ValueError:
            continue
    return False


def build_html(spec: dict, focus: list[str], recent_hours: int, now: _dt.datetime, out_name: str) -> str:
    since = (now - _dt.timedelta(hours=recent_hours)).date()
    focus_set = {f.strip() for f in focus if f.strip()}
    nodes = spec["nodes"]
    for n in nodes:
        n["_focus"] = bool(focus_set & (set(n.get("aliases") or []) | {n["id"]}))
        n["_recent"] = _recent(n, since)
    themes = list(spec.get("themes") or [])
    known = {t["id"] for t in themes}
    extra = sorted({n.get("theme") or "none" for n in nodes} - known)
    for t in extra:
        themes.append({"id": t, "title": "Bez tematu" if t == "none" else t})
    counts = {
        "nodes": len(nodes),
        "und": {k: sum(1 for n in nodes if (n.get("understanding") or {}).get("state") == k) for k in UND_LABEL},
        "status": {k: sum(1 for n in nodes if isinstance(n.get("status"), dict) and n["status"].get("value") == k) for k in STATUS_LABEL},
        "recent": sum(1 for n in nodes if n["_recent"]),
        "focus": sum(1 for n in nodes if n["_focus"]),
    }
    payload = {
        "generated": now.strftime("%Y-%m-%d %H:%M UTC"),
        "recent_hours": recent_hours,
        "since": since.isoformat(),
        "focus": sorted(focus_set),
        "themes": themes,
        "nodes": nodes,
        "counts": counts,
        "status_policy": spec.get("status_policy"),
        "sources": spec.get("sources"),
        "out": out_name,
    }
    data = json.dumps(payload, ensure_ascii=False).replace("</", "<\\/")
    return TEMPLATE.replace("__DATA__", data)


TEMPLATE = r"""<title>Specyfikacja ChatADHD</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Sans:wght@400;500;600&family=IBM+Plex+Mono:wght@400;500&family=Source+Serif+4:opsz,wght@8..60,500;8..60,600&display=swap">
<style>
/* Układ: jedna kolumna tekstu (~72 znaki), drzewo <details>; nagłówek przyklejony z wyszukiwaniem i przyciskami. */
:root {
  --bg: #faf8f4; --fg: #1d1b17; --muted: #6b665c; --line: #e3ded4; --card: #ffffff;
  --accent: #2f5d50; --accent-fg: #ffffff; --focus: #fff3c4; --focus-line: #d9a400;
  --ok: #2d7a46; --part: #b06a00; --miss: #b23a3a; --old: #7a7a7a; --conf: #7a2f8a;
  --sans: "IBM Plex Sans", system-ui, -apple-system, "Segoe UI", sans-serif;
  --mono: "IBM Plex Mono", ui-monospace, SFMono-Regular, Menlo, monospace;
  --serif: "Source Serif 4", Georgia, "Times New Roman", serif;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #16171a; --fg: #ebe7df; --muted: #a39d92; --line: #2d2f35; --card: #1e2024;
  --accent: #7fc3ad; --accent-fg: #0f1a16; --focus: #3a3410; --focus-line: #e0b931;
  --ok: #6cc38a; --part: #e0a24a; --miss: #ef7b7b; --old: #9a9a9a; --conf: #cf9ae0; color-scheme: dark } }
:root[data-theme="dark"] {
  --bg: #16171a; --fg: #ebe7df; --muted: #a39d92; --line: #2d2f35; --card: #1e2024;
  --accent: #7fc3ad; --accent-fg: #0f1a16; --focus: #3a3410; --focus-line: #e0b931;
  --ok: #6cc38a; --part: #e0a24a; --miss: #ef7b7b; --old: #9a9a9a; --conf: #cf9ae0; color-scheme: dark }
body { background: var(--bg); color: var(--fg); font-family: var(--sans); line-height: 1.45; overflow-wrap: anywhere; }
.alias, .src, .id, a, code { overflow-wrap: anywhere; word-break: break-word; }
.wrap, main, details, .body { min-width: 0; }
.wrap { max-width: 56rem; margin: 0 auto; padding-block: 0 4rem;
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); }
body::before { content: ""; position: fixed; inset: 0 0 auto; height: env(safe-area-inset-top, 0px); z-index: 10; background: var(--bg); }
header.top { position: sticky; top: env(safe-area-inset-top, 0px); background: var(--bg); border-bottom: 1px solid var(--line); padding-block: .6rem; z-index: 5; }
h1 { font-family: var(--serif); font-weight: 600; font-size: 1.35rem; margin: 0 0 .2rem; text-wrap: balance; }
.meta { color: var(--muted); font-size: .85rem; }
.tools { display: flex; flex-wrap: wrap; gap: .5rem; align-items: center; margin-top: .5rem; }
.tools input[type=search] { flex: 1 1 14rem; min-width: 0; font: inherit; padding: .4rem .6rem; border: 1px solid var(--line); border-radius: .4rem; background: var(--card); color: var(--fg); }
button { font: inherit; font-size: .85rem; padding: .35rem .7rem; border-radius: .4rem; border: 1px solid var(--line); background: var(--card); color: var(--fg); cursor: pointer; }
button.primary { background: var(--accent); color: var(--accent-fg); border-color: var(--accent); }
button:focus-visible, input:focus-visible, summary:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
.counts { display: flex; flex-wrap: wrap; gap: .4rem .9rem; font-size: .85rem; color: var(--muted); margin-top: .4rem; font-variant-numeric: tabular-nums; }
.counts b { color: var(--fg); font-weight: 600; }
section.recent { margin: 1rem 0; padding: .7rem .9rem; background: var(--focus); border-left: 3px solid var(--focus-line); border-radius: .3rem; }
section.recent h2 { margin: 0 0 .3rem; font-size: 1rem; }
section.recent ul { margin: 0; padding-left: 1.1rem; }
section.recent a { color: inherit; }
details.theme { border-top: 1px solid var(--line); padding-block: .4rem; }
details.theme > summary { font-family: var(--serif); font-weight: 600; font-size: 1.1rem; cursor: pointer; list-style: none; display: flex; gap: .6rem; align-items: baseline; }
details.theme > summary::before { content: "▸"; color: var(--muted); font-family: var(--mono); }
details.theme[open] > summary::before { content: "▾"; }
details.theme > summary .n { color: var(--muted); font-size: .85rem; font-family: var(--mono); font-weight: 400; }
details.node { margin: .35rem 0 .35rem .6rem; border-left: 2px solid var(--line); padding-left: .7rem; }
details.node.recent { border-left-color: var(--focus-line); }
details.node.focus { background: var(--focus); border-left-color: var(--focus-line); border-radius: 0 .3rem .3rem 0; }
details.node > summary { cursor: pointer; list-style: none; display: flex; flex-wrap: wrap; gap: .35rem .5rem; align-items: baseline; font-size: .95rem; }
details.node > summary::before { content: "▸"; color: var(--muted); font-family: var(--mono); }
details.node[open] > summary::before { content: "▾"; }
.alias { font-family: var(--mono); font-size: .8rem; color: var(--accent); font-weight: 500; }
.chip { font-size: .72rem; padding: .05rem .45rem; border-radius: 1rem; border: 1px solid var(--line); color: var(--muted); white-space: nowrap; }
.chip.JEST { color: var(--ok); border-color: var(--ok); }
.chip.CZĘŚCIOWO { color: var(--part); border-color: var(--part); }
.chip.BRAK, .chip.SPRZECZNE { color: var(--miss); border-color: var(--miss); }
.chip.NIEAKTUALNE { color: var(--old); border-color: var(--old); }
.chip.proposed { color: var(--accent-fg); background: var(--accent); border-color: var(--accent); }
.chip.corrected, .chip.approved { color: var(--conf); border-color: var(--conf); }
.chip.kind { font-family: var(--mono); }
.date { color: var(--muted); font-size: .8rem; font-family: var(--mono); }
.body { padding: .3rem 0 .6rem; font-size: .92rem; }
.body h4 { margin: .6rem 0 .2rem; font-size: .78rem; text-transform: uppercase; letter-spacing: .06em; color: var(--muted); font-weight: 600; }
blockquote { margin: .2rem 0; padding: .4rem .7rem; background: var(--card); border-left: 3px solid var(--accent); border-radius: 0 .3rem .3rem 0; white-space: pre-wrap; font-family: var(--serif); }
.src { font-family: var(--mono); font-size: .75rem; color: var(--muted); }
.und { padding: .4rem .7rem; border: 1px dashed var(--line); border-radius: .3rem; }
.und.proposed { border-color: var(--accent); }
.und .by { font-size: .78rem; color: var(--muted); }
.hist { font-size: .85rem; color: var(--muted); margin-top: .3rem; }
.hist summary { cursor: pointer; }
.id { font-family: var(--mono); font-size: .72rem; color: var(--muted); }
.refs a { color: var(--accent); }
.hidden { display: none !important; }
.hit { background: var(--focus); }
.decide { margin-top: .6rem; padding: .5rem .7rem; border: 1px solid var(--line); border-radius: .4rem; background: var(--card); }
.decide .row { display: flex; flex-wrap: wrap; gap: .4rem; align-items: center; }
.decide textarea { width: 100%; min-height: 4.5rem; font: inherit; padding: .4rem .5rem; border: 1px solid var(--line); border-radius: .3rem; background: var(--bg); color: var(--fg); margin-top: .4rem; }
.decide .note { font-size: .8rem; color: var(--muted); margin-top: .3rem; }
.inbox { font-size: .85rem; margin-top: .4rem; padding: .3rem .6rem; border-left: 3px solid var(--accent); background: var(--card); }
.inbox b { color: var(--accent); }
#inbox-banner { margin: .6rem 0; font-size: .9rem; padding: .5rem .8rem; border-radius: .4rem; background: var(--card); border: 1px solid var(--accent); }
@media (prefers-reduced-motion: no-preference) { details.node { transition: background .2s; } }
</style>
<div class="wrap">
<header class="top">
  <h1>Specyfikacja ChatADHD — co pamiętam, na żywo</h1>
  <div class="meta" id="meta"></div>
  <div class="tools">
    <input type="search" id="q" placeholder="Szukaj: słowo, R72, ON05, temat…" aria-label="Szukaj w specyfikacji">
    <button type="button" id="b-recent" class="primary">Rozwiń zmienione</button>
    <button type="button" id="b-waiting">Czekające na Ciebie</button>
    <button type="button" id="b-all">Rozwiń wszystko</button>
    <button type="button" id="b-none">Zwiń wszystko</button>
  </div>
  <div class="counts" id="counts"></div>
</header>
<section class="recent" id="recent"></section>
<div id="inbox-banner" hidden></div>
<main id="tree"></main>
<footer class="meta" style="margin-top:2rem">Źródło: <code>docs/spec/spec.json</code> (<code>python3 tools/spec.py build</code>), strona: <code>python3 tools/spec.py html</code>. Status każdej pozycji to datowana obserwacja z pliku LUKI, nie ograniczenie (R60). Cytaty są dosłowne (strażnik <code>spec.py check</code>); parafrazy to słowa koordynatora i czekają na Twoje „zatwierdzam” albo poprawkę (R61).</footer>
</div>
<script type="application/json" id="data">__DATA__</script>
<script>
(function () {
  const D = JSON.parse(document.getElementById('data').textContent);
  const SL = {"JEST":"jest","CZĘŚCIOWO":"częściowo","BRAK":"brak","NIEAKTUALNE":"nieaktualne","SPRZECZNE":"sprzeczne"};
  const UL = {"proposed":"parafraza czeka na Ciebie","approved":"parafraza zatwierdzona","corrected":"poprawione Twoimi słowami"};
  const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const byTheme = {};
  for (const n of D.nodes) { const t = n.theme || 'none'; (byTheme[t] = byTheme[t] || []).push(n); }
  const aliasToId = {}; for (const n of D.nodes) for (const a of (n.aliases||[])) aliasToId[a] = n.id;
  function link(ref) { const id = aliasToId[ref] || (ref.startsWith('s_') ? ref : null); return id ? `<a href="#${esc(id)}">${esc(ref)}</a>` : esc(ref); }
  function chips(n) {
    let h = `<span class="chip kind">${esc(n.kind)}</span>`;
    const st = n.status && n.status.value; if (st) h += `<span class="chip ${esc(st)}" title="obserwacja z ${esc(n.status.checked_on||'?')}">${esc(SL[st]||st)}</span>`;
    const u = n.understanding && n.understanding.state; if (u && UL[u]) h += `<span class="chip ${esc(u)}">${esc(UL[u])}</span>`;
    if (n.date) h += `<span class="date">${esc(n.date)}</span>`;
    return h;
  }
  function body(n) {
    let h = '';
    if (n.quotes && n.quotes.length) { h += '<h4>Twoje słowa (dosłownie)</h4>'; for (const q of n.quotes) h += `<blockquote>${esc(q.text)}</blockquote><div class="src">${esc(q.src||'')}</div>`; }
    if (n.sense) h += `<h4>Sens</h4><div>${esc(n.sense)}</div>`;
    if (n.restatement && n.restatement.length) { h += '<h4>Jak to zapisano</h4>'; for (const r of n.restatement) h += `<div>${esc(r.text)} <span class="src">${esc(r.src||'')}</span></div>`; }
    const u = n.understanding || {};
    if (u.state && u.state !== 'none') {
      h += `<h4>Parafraza koordynatora (${esc(UL[u.state]||u.state)})</h4><div class="und ${esc(u.state)}">${esc(u.corrected_text || u.text || '')}<div class="by">${esc(u.by||'')} ${esc(u.date||'')}</div></div>`;
      if (u.history && u.history.length > 1) { h += '<details class="hist"><summary>Historia parafrazy</summary>'; for (const x of u.history) h += `<div><b>${esc(x.state)}</b> ${esc(x.by||'')} ${esc(x.date||'')}: ${esc(x.text||'')}</div>`; h += '</details>'; }
    }
    if (n.status && n.status.value) {
      h += `<h4>Stan (obserwacja z ${esc(n.status.checked_on||'?')})</h4><div>${esc(SL[n.status.value]||n.status.value)}${n.gap_text ? ' — ' + esc(n.gap_text) : ''}</div>`;
      if (n.evidence && n.evidence.length) { h += '<div class="src">dowody: ' + n.evidence.map(e => esc(typeof e === 'string' ? e : (e.text || e.src || JSON.stringify(e)))).join(' · ') + '</div>'; }
      if (n.next_step) h += `<div>następny krok: ${esc(n.next_step)}</div>`;
    }
    if (n.commentary && n.commentary.length) { h += '<h4>Komentarz</h4>'; for (const c of n.commentary) h += `<div>${esc(typeof c === 'string' ? c : (c.text||''))}</div>`; }
    if ((n.refs && n.refs.length) || (n.supersedes && n.supersedes.length) || (n.superseded_by && n.superseded_by.length)) {
      h += '<h4>Powiązania</h4><div class="refs">';
      if (n.refs && n.refs.length) h += 'odwołania: ' + n.refs.map(link).join(', ') + ' ';
      if (n.supersedes && n.supersedes.length) h += '· zastępuje: ' + n.supersedes.map(link).join(', ') + ' ';
      if (n.superseded_by && n.superseded_by.length) h += '· zastąpione przez: ' + n.superseded_by.map(link).join(', ');
      h += '</div>';
    }
    h += `<div class="inbox" data-inbox-for="${esc(n.id)}" hidden></div>`;
    h += `<div class="decide" data-decide-for="${esc(n.id)}" hidden></div>`;
    h += `<div class="id">${esc(n.id)}${n.aliases && n.aliases.length ? ' · ' + n.aliases.map(esc).join(', ') : ''}</div>`;
    return h;
  }
  function nodeHtml(n) {
    const cls = ['node']; if (n._recent) cls.push('recent'); if (n._focus) cls.push('focus');
    const open = (n._recent || n._focus) ? ' open' : '';
    const text = [n.title, (n.aliases||[]).join(' '), n.sense, ...(n.quotes||[]).map(q => q.text), (n.understanding||{}).text].join(' ').toLowerCase();
    return `<details class="${cls.join(' ')}" id="${esc(n.id)}" data-text="${esc(text)}"${open}><summary>${(n.aliases||[]).map(a => `<span class="alias">${esc(a)}</span>`).join('')}<span class="title">${esc(n.title||'(bez tytułu)')}</span>${chips(n)}</summary><div class="body">${body(n)}</div></details>`;
  }
  let tree = '';
  for (const t of D.themes) {
    const list = byTheme[t.id] || []; if (!list.length) continue;
    const anyOpen = list.some(n => n._recent || n._focus);
    const nRecent = list.filter(n => n._recent).length;
    tree += `<details class="theme" data-theme-id="${esc(t.id)}"${anyOpen ? ' open' : ''}><summary>${esc(t.title)} <span class="n">${list.length}${nRecent ? ' · zmienione ' + nRecent : ''}</span></summary>${list.map(nodeHtml).join('')}</details>`;
  }
  document.getElementById('tree').innerHTML = tree;
  const c = D.counts;
  document.getElementById('meta').textContent = `Wygenerowano ${D.generated} · ${c.nodes} pozycji · rozwinięte: zmienione od ${D.since} (${D.recent_hours} h)${D.focus.length ? ' i w edycji: ' + D.focus.join(', ') : ''}`;
  document.getElementById('counts').innerHTML =
    `<span>czekają na Ciebie <b>${c.und.proposed}</b></span><span>poprawione <b>${c.und.corrected}</b></span><span>zatwierdzone <b>${c.und.approved}</b></span>` +
    `<span>jest <b>${c.status['JEST']}</b></span><span>częściowo <b>${c.status['CZĘŚCIOWO']}</b></span><span>brak <b>${c.status['BRAK']}</b></span><span>nieaktualne <b>${c.status['NIEAKTUALNE']}</b></span><span>sprzeczne <b>${c.status['SPRZECZNE']}</b></span>`;
  const rec = D.nodes.filter(n => n._recent || n._focus);
  const recEl = document.getElementById('recent');
  if (rec.length) recEl.innerHTML = `<h2>W edycji i ostatnio zmienione (${rec.length})</h2><ul>` + rec.map(n => `<li><a href="#${esc(n.id)}">${(n.aliases||[]).map(esc).join(', ') || esc(n.id)}</a> — ${esc(n.title||'')}${n._focus ? ' <span class="chip">w edycji</span>' : ''}</li>`).join('') + '</ul>';
  else recEl.hidden = true;
  const all = () => Array.from(document.querySelectorAll('details.node'));
  const themesEl = () => Array.from(document.querySelectorAll('details.theme'));
  function setOpen(pred) { for (const d of all()) d.open = pred(d); for (const t of themesEl()) t.open = Array.from(t.querySelectorAll('details.node')).some(d => d.open); }
  document.getElementById('b-recent').onclick = () => setOpen(d => d.classList.contains('recent') || d.classList.contains('focus'));
  document.getElementById('b-waiting').onclick = () => setOpen(d => !!d.querySelector('.chip.proposed'));
  document.getElementById('b-all').onclick = () => { for (const d of all()) d.open = true; for (const t of themesEl()) t.open = true; };
  document.getElementById('b-none').onclick = () => { for (const d of all()) d.open = false; for (const t of themesEl()) t.open = false; };
  const q = document.getElementById('q');
  q.oninput = () => {
    const s = q.value.trim().toLowerCase();
    for (const d of all()) { const hit = !s || d.dataset.text.includes(s); d.classList.toggle('hidden', !hit); d.classList.toggle('hit', !!s && hit); }
    for (const t of themesEl()) { const vis = t.querySelectorAll('details.node:not(.hidden)').length; t.classList.toggle('hidden', vis === 0); if (s) t.open = vis > 0; }
  };
  function openHash() { const id = (location.hash || '').slice(1); if (!id) return; const d = document.getElementById(id); if (!d) return; d.open = true; const t = d.closest('details.theme'); if (t) t.open = true; d.scrollIntoView({ block: 'start' }); }
  window.addEventListener('hashchange', openHash); openHash();

  // --- R88 v1: decisions written by the owner next to each item; a cheap "secretary" task applies them to the spec ---
  const KIND_LABEL = { approve: 'Zatwierdzam', correct: 'Popraw', remark: 'Uwaga' };
  const STATE_LABEL = { new: 'czeka na sekretarza', applied: 'zapisane w spec', escalated: 'przekazane koordynatorowi', rejected: 'odrzucone' };
  function nodeOf(id) { return D.nodes.find(n => n.id === id); }
  async function wireDb() {
    if (!window.claude || typeof window.claude.use !== 'function') return;
    const db = await window.claude.use('db');
    if (!db) return;
    const user = await window.claude.use('user');
    let canWrite = true;
    try { if (user && typeof user.can === 'function') { const c = await user.can('data.write'); if (c === false) canWrite = false; } } catch (e) { /* keep inputs */ }
    const inbox = db.collection('spec_inbox');
    // existing entries (one read, newest first), rendered per node + banner
    const byNode = {};
    let waiting = 0;
    try {
      const snap = await inbox.orderBy('at', 'desc').limit(500).get();
      for (const d of snap.docs) { const x = d.data(); if (!x || !x.node) continue; (byNode[x.node] = byNode[x.node] || []).push({ id: d.id, ...x }); if (x.state === 'new') waiting++; }
    } catch (e) { /* render without history */ }
    for (const [nid, list] of Object.entries(byNode)) {
      const el = document.querySelector(`[data-inbox-for="${CSS.escape(nid)}"]`); if (!el) continue;
      el.hidden = false;
      el.innerHTML = list.slice(0, 5).map(x => `<div><b>${esc(KIND_LABEL[x.kind] || x.kind)}</b> ${esc((x.at || '').slice(0, 16).replace('T', ' '))} — ${esc(STATE_LABEL[x.state] || x.state)}${x.text ? ': ' + esc(x.text) : ''}</div>`).join('');
    }
    const banner = document.getElementById('inbox-banner');
    banner.hidden = false;
    banner.textContent = waiting ? `Skrzynka: ${waiting} Twoich decyzji czeka na sekretarza (zapisuje je do spec co godzinę; nietypowe trafiają do koordynatora).` : 'Skrzynka pusta: przy każdej pozycji możesz zatwierdzić parafrazę, poprawić ją swoimi słowami albo dopisać uwagę — sekretarz zapisze to do spec.';
    if (!canWrite) { banner.textContent += ' (To konto nie może zapisywać.)'; return; }
    for (const box of document.querySelectorAll('[data-decide-for]')) {
      const nid = box.dataset.decideFor; const n = nodeOf(nid); if (!n) continue;
      const und = (n.understanding && n.understanding.state) || 'none';
      box.hidden = false;
      box.innerHTML = `<div class="row"><span class="note">Twoja decyzja:</span>` +
        (und === 'proposed' || und === 'corrected' ? `<button type="button" data-k="approve">Zatwierdzam parafrazę</button>` : '') +
        `<button type="button" data-k="correct">Popraw swoimi słowami</button><button type="button" data-k="remark">Uwaga / dopisek</button></div>` +
        `<textarea hidden placeholder="Twoje słowa (zapisane dosłownie)"></textarea><div class="row" hidden><button type="button" class="primary" data-send>Wyślij</button><button type="button" data-cancel>Anuluj</button></div><div class="note" data-status></div>`;
      const ta = box.querySelector('textarea'); const row2 = box.querySelectorAll('.row')[1]; const status = box.querySelector('[data-status]');
      let kind = null;
      box.querySelectorAll('button[data-k]').forEach(b => b.onclick = () => {
        kind = b.dataset.k;
        if (kind === 'approve') { send(); return; }
        ta.hidden = false; row2.hidden = false; ta.focus();
      });
      box.querySelector('[data-cancel]').onclick = () => { ta.hidden = true; row2.hidden = true; kind = null; };
      box.querySelector('[data-send]').onclick = () => send();
      async function send() {
        const text = (ta.value || '').trim();
        if (kind !== 'approve' && !text) { status.textContent = 'Wpisz tekst.'; return; }
        status.textContent = 'Wysyłam…';
        try {
          await inbox.add({ node: nid, alias: (n.aliases || [])[0] || nid, kind, text, at: new Date().toISOString(), state: 'new', page_generated: D.generated });
          status.textContent = `Wysłane: ${KIND_LABEL[kind]} — czeka na sekretarza.`;
          ta.value = ''; ta.hidden = true; row2.hidden = true; kind = null;
        } catch (e) {
          status.textContent = 'Nie udało się zapisać (' + (e && e.code ? e.code : 'błąd') + '). Spróbuj za chwilę albo napisz w czacie.';
        }
      }
    }
  }
  wireDb();
})();
</script>
"""


def main(argv=None) -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--spec", default=str(REPO / "docs/spec/spec.json"))
    ap.add_argument("--out", default=str(REPO / "docs/spec/SPEC.html"))
    ap.add_argument("--focus", nargs="*", default=[], help="pozycje w edycji (id albo alias): rozwinięte i podświetlone")
    ap.add_argument("--recent-hours", type=int, default=48)
    ap.add_argument("--now", help="UTC ISO (testy)")
    a = ap.parse_args(argv)
    spec = json.loads(Path(a.spec).read_text(encoding="utf-8"))
    now = _dt.datetime.fromisoformat(a.now) if a.now else _dt.datetime.utcnow()
    out = Path(a.out)
    out.write_text(build_html(spec, a.focus, a.recent_hours, now, out.name), encoding="utf-8")
    n = len(spec["nodes"])
    print(f"zapisano {out} ({out.stat().st_size // 1024} KB, {n} pozycji; rozwinięte: zmienione od {a.recent_hours} h" + (f", w edycji {', '.join(a.focus)}" if a.focus else "") + ")")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
