#!/usr/bin/env node
// M1-W0 (7), the LIST half (package P2): the graph navigator on a phone (390x844, touch) against the real loom-server, no model,
// no network. Everything asserted is compared with what the kernel answers (POST /api/graph/navigate) and with the words of the
// catalogue presentation.graph_screen (loom/data/profiles/conversation_view.pack): no literal of a label or a count lives here.
// Checks: menu ⋮ › Graf opens the navigator; the root rows are the kernel's rows with Polish labels, counts or "nieznane (powód)",
// provenance chips, "po co"; no page scroll wider than 390; every tappable >= 44 px; a tap on ROM drills in (crumbs, 13 Polish rows
// with status chips, the focus's why); search on the level; Szerzej/Węziej/Głębiej/Płycej change the budget and depth; the planned
// buttons say so; the node card (P3): a ROM item's card, Ustawienia › Rozmowa › Temperatura with the whole chain, Zmień → Dla kogo? →
// set for 'Ta rozmowa' (rules.set + the kernel's confirmation) and for 'Ty' (settings.set), 'Zdejmij wartość' (rules.disable), the chain
// compared with graph.card after every write; the map (P4): the same nodes as the list drawn through the view engine, a tap on a mark
// shows the label line and opens the same card, + / − and a two-finger pinch zoom, the exclusion rectangle for the Android shell,
// Widok ▾ saves navigator_layout; Back closes the layers in order (card → steps → panel) through window.__loomBack.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import net from "node:net";
import path from "node:path";
import { auditPage } from "./m1-audit.mjs";
import { fileURLToPath } from "node:url";
import { stableShot, trackRequests } from "./stable-shot.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = process.env.M1_SHOTS_DIR || path.join(here, "screenshots");   // M1_SHOTS_DIR = the report's folder (docs/reports/M1_CHECKPOINT_0/)
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[m1-navigator] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });
const pack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const words = pack.entries.find(r => r.key === "presentation.graph_screen").value.locales.pl;
const word = (id, params = {}) => words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? "");
// A number as the card writes it (node-card-state.ts numberText): the point replaced by the screen's word decimal_separator (pl ","),
// the same way the kernel's value_human writes "Wartość:" — one format on one card (r1:b4).
const num = v => { const s = String(v); const sep = words.decimal_separator; return sep && sep.length === 1 && sep !== "." ? s.replace(".", sep) : s; };
// r2:b1: "1 węzeł / 2 węzły / 5 węzłów" — the form from the language, the word from count.<kind>.<form> (shell/plural.ts does the same).
const plural = (kind, n) => word(`count.${kind}.${new Intl.PluralRules("pl").select(n)}`, { n: String(n) });

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-m1-navigator-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
const AUDIT = [];   // the lines a screenshot's screen shows that look like developer text (m1-audit.mjs)
let browser, failed = false;
const step = async (name, fn) => { try { await fn(); console.log(`[m1-navigator] PASS ${name}`); } catch (e) { failed = true; console.error(`[m1-navigator] FAIL ${name}\n${e.stack}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json(); };
const navigate = (request) => api("POST", "/api/graph/navigate", { lang: "pl", ...request });

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  const root = await navigate({}), rom = await navigate({ focus: "area:rom" });
  assert.ok(root.children.length > 0 && rom.children.length > 0, "the kernel answers rows");

  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  const page = await context.newPage();
  trackRequests(page);
  const errors = [];
  page.on("pageerror", e => errors.push(String(e)));
  const nav = () => page.getByTestId("graph-navigator");
  // a screenshot after the fade/slide animations end (opacity mid-animation would show the chat through the menu); viewport, not full page
  const shot = async name => { await stableShot(page, path.join(SHOTS, `${name}.png`)); await audit(name); };
  const audit = async name => { for (const f of await auditPage(page)) AUDIT.push({ shot: name, ...f }); };
  const rowsOf = () => page.getByTestId("nav-rows").locator("li").evaluateAll(els => els.map(li => ({
    address: li.querySelector(".gnav-row-main").dataset.testid.replace("nav-row-", ""), label: li.querySelector(".gnav-label").textContent,
    count: li.querySelector('[data-testid="nav-count"]')?.textContent ?? null, chip: li.querySelector('[data-testid="nav-chip"]').textContent,
    status: li.querySelector('[data-testid="nav-status"]')?.textContent ?? null, why: li.querySelector(".gnav-why")?.textContent ?? "" })));
  const noMissingWord = async () => {
    const texts = await nav().locator("button, span, p, input").evaluateAll(els => els.map(el => el.textContent.trim()));
    const leaked = texts.filter(t => /^(chip|count|bar|status|reason|evidence|card|search|no_|query|map|view|list|perspective|actions)\.[a-z_]+/.test(t));
    assert.deepEqual(leaked, [], "no catalogue id on screen (a missing word would show its id)");
    // A bare status id as the value ("Wartość: partial") is English on an owner's screen: the status ids are the pack's status.* keys.
    const statusIds = Object.keys(words).filter(k => k.startsWith("status.")).map(k => k.slice("status.".length));
    const values = await nav().locator('[data-testid="card-value"] strong').evaluateAll(els => els.map(el => el.textContent.trim()));
    assert.deepEqual(values.filter(v => statusIds.includes(v) || v === "unknown"), [], "no raw status id as a card's value");
  };
  const layoutFits = async () => {
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth) <= 390, "no horizontal page scroll");
    // the whole drawer, so the × that closes the screen (data-testid close-panel) is measured too; every tappable kind, not only buttons
    // (the search input nav-search-input, the editor's textarea card-editor-input, selects, role=button/switch, disclosure summaries)
    const small = await page.locator(".drawer button, .drawer input, .drawer textarea, .drawer select, .drawer [role=button], .drawer [role=switch], .drawer summary").evaluateAll(els => els.filter(el => el.offsetParent !== null).map(el => {
      const r = el.getBoundingClientRect(); return { id: el.dataset.testid, w: Math.round(r.width), h: Math.round(r.height) }; }).filter(b => b.w < 44 || b.h < 44));
    assert.deepEqual(small, [], "every tappable of the navigator screen (its × included) is at least 44 x 44 px");
  };

  await page.goto(base + "/");
  await page.getByTestId("chat-shell").waitFor();

  await step("menu ⋮ › Graf opens the navigator at the root, with the kernel's rows in Polish", async () => {
    await page.getByTestId("shell-menu-toggle").tap();
    await page.getByTestId("shell-menu-graph").tap();
    await nav().waitFor();
    // r2:b4: the document speaks the shell's language (index.html ships lang="en"; a screen reader picks its voice from here)
    assert.equal(await page.evaluate(() => document.documentElement.lang), "pl", "html lang follows ui_language");
    await page.getByTestId("nav-rows").locator("li").first().waitFor();
    await shot("m1-navigator-root-list");   // the root as the owner first sees it: the rows from the top (the map is below the fold)
    await page.getByTestId("nav-rows").waitFor();
    await page.getByTestId("nav-loading").waitFor({ state: "detached" });
    // r1:b6: the panel is a dialog for a reader of the screen — named by its title, focus inside it as soon as it opens.
    const drawer = page.getByTestId("panel-navigator");
    assert.equal(await drawer.getAttribute("role"), "dialog");
    assert.equal(await drawer.getAttribute("aria-modal"), "true");
    assert.equal(await drawer.getAttribute("aria-label"), await drawer.locator("h2").first().innerText(), "the dialog is named by its title");
    assert.ok(await drawer.evaluate(el => el.contains(document.activeElement)), "focus moved into the panel");
    // r1:b6: "nieznane (powód)" is a line of its own under the label, read from the left; a number stays on the label's line.
    const unknown = page.locator('[data-testid="nav-count"][data-known="false"]').first();
    assert.ok(await unknown.count() > 0, "the root has a row whose count is unknown (with its reason)");
    const geo = await unknown.evaluate(el => { const l = el.parentElement.querySelector(".gnav-label").getBoundingClientRect(), c = el.getBoundingClientRect();
      return { labelLeft: l.left, labelBottom: l.bottom, countLeft: c.left, countTop: c.top }; });
    assert.ok(geo.countTop >= geo.labelBottom - 1 && Math.abs(geo.countLeft - geo.labelLeft) < 2, `the unknown count sits under the label: ${JSON.stringify(geo)}`);
    const known = page.locator('[data-testid="nav-count"][data-known="true"]').first();
    const geoKnown = await known.evaluate(el => { const l = el.parentElement.querySelector(".gnav-label").getBoundingClientRect(), c = el.getBoundingClientRect();
      return { labelTop: l.top, countTop: c.top }; });
    assert.ok(Math.abs(geoKnown.countTop - geoKnown.labelTop) < 6, `a number stays on the label's line: ${JSON.stringify(geoKnown)}`);
    assert.equal(await nav().getAttribute("data-focus"), "");
    assert.equal(await page.getByTestId("nav-crumb-0").innerText(), words.root);
    const rows = await rowsOf();
    assert.deepEqual(rows.map(r => r.address), root.children.map(c => c.address), "the rows are the kernel's children, in its order");
    for (const [i, c] of root.children.entries()) {
      const r = rows[i];
      assert.equal(r.label, c.label);
      if (typeof c.count === "number") assert.equal(r.count, word("count.value", { count: String(c.count) }), c.address);
      else if (c.count_unknown_reason) assert.equal(r.count, word("count.unknown_reason", { reason: c.count_unknown_human }), c.address);
      assert.equal(r.chip, c.provenance.chip?.label ?? word(`chip.${c.provenance.layer}`), `chip of ${c.address}`);
      assert.equal(r.why, c.why ?? "");
    }
    assert.ok(rows.some(r => r.count?.startsWith(words["count.unknown"])), "an area without a source says unknown with the reason, never 0");
    assert.equal(await page.getByTestId("nav-budget").innerText(), `${root.limit.value === 0 ? word("bar.budget.unlimited", { hops: String(root.hops) }) : word("bar.budget", { nodes: plural("nodes", root.limit.value), hops: String(root.hops) })} · ${word("children", { shown: String(root.counts.shown), rows: plural("rows", root.counts.total) })}`);
    // Działania at the root: no node is in focus, so the bar says where actions live (no card opens, nothing says "planned").
    await page.getByTestId("nav-bar-actions").tap();
    assert.equal(await page.getByTestId("nav-actions-note").innerText(), words["actions.no_focus"]);
    assert.equal(await page.getByTestId("node-card").count(), 0);
    await page.getByTestId("nav-bar-actions").tap();
    await page.getByTestId("nav-actions-note").waitFor({ state: "detached" });
    await noMissingWord();
    await layoutFits();
    await shot("m1-navigator-root");
  });

  await step("a tap on ROM drills in: crumbs, the focus's why, the 13 rows with status chips in Polish", async () => {
    await page.getByTestId("nav-row-area:rom").tap();
    await page.locator('[data-testid="graph-navigator"][data-focus="area:rom"]').waitFor();
    await page.getByTestId("nav-loading").waitFor({ state: "detached" });
    assert.equal(await page.getByTestId("nav-crumb-1").innerText(), rom.focus.label);
    assert.equal(await page.getByTestId("nav-focus-why").innerText(), rom.focus.why);
    const rows = await rowsOf();
    assert.deepEqual(rows.map(r => r.address), rom.children.map(c => c.address));
    for (const [i, c] of rom.children.entries()) {
      assert.equal(rows[i].label, c.label);
      assert.equal(rows[i].status, c.status ? word(`status.${c.status}`) : null, `status of ${c.address}`);
      assert.equal(rows[i].count, null, "a leaf claims no count");
    }
    assert.ok(rows.every(r => /[ąćęłńóśźż]/i.test(r.label + r.why) || r.why === ""), "the ROM rows read in Polish");
    await noMissingWord();
    await layoutFits();
    await shot("m1-navigator-rom");
  });

  await step("the magnifier searches on the level (graph.navigate query), and closing it restores the level", async () => {
    const first = rom.children[0].label.slice(0, 3);
    const expected = await navigate({ focus: "area:rom", query: first });
    await page.getByTestId("nav-search-toggle").tap();
    const searchName = await page.getByTestId("nav-search-input").getAttribute("aria-label");   // r1:b6: the field is named in words, not only by its placeholder
    assert.ok(searchName && searchName !== "search", `the search field has a name: ${searchName}`);
    await page.getByTestId("nav-search-input").fill(first);
    await page.waitForFunction((n) => document.querySelectorAll('[data-testid="nav-rows"] li').length === n, expected.children.length);
    assert.deepEqual((await rowsOf()).map(r => r.address), expected.children.map(c => c.address));
    assert.ok(expected.children.length < rom.children.length, "the query narrows the level");
    await page.getByTestId("nav-search-toggle").tap();
    await page.waitForFunction((n) => document.querySelectorAll('[data-testid="nav-rows"] li').length === n, rom.children.length);
  });

  await step("Węziej halves the budget (collapsed rows are said, not hidden), Szerzej doubles it, Głębiej/Płycej change the depth", async () => {
    let limit = rom.limit.value, shown = rom.counts.shown;
    const budget = () => page.getByTestId("nav-budget").innerText();
    for (let i = 0; i < 3; i++) {
      limit = Math.max(1, Math.floor((limit > 0 ? limit : shown) / 2));
      await page.getByTestId("nav-bar-narrower").tap();
      await page.waitForFunction((text) => document.querySelector('[data-testid="nav-budget"]')?.textContent.includes(text), `: ${plural("nodes", limit)} `);
      shown = Number(await page.getByTestId("nav-rows").getAttribute("data-shown"));
    }
    const narrowed = await navigate({ focus: "area:rom", limit });
    assert.equal(shown, narrowed.counts.shown);
    assert.ok(shown < rom.children.length, `the budget ${limit} collapses rows`);
    assert.equal(await page.getByTestId("nav-collapsed").innerText(), word("collapsed", { nodes: plural("nodes", narrowed.counts.collapsed) }));
    await page.getByTestId("nav-bar-wider").tap();
    await page.waitForFunction((text) => document.querySelector('[data-testid="nav-budget"]')?.textContent.includes(text), `: ${plural("nodes", limit * 2)} `);
    await page.getByTestId("nav-bar-deeper").tap();
    await page.waitForFunction((h) => document.querySelector('[data-testid="nav-budget"]')?.textContent.includes(`głębia ${h}`), rom.hops + 1);
    await page.getByTestId("nav-bar-shallower").tap();
    await page.waitForFunction((h) => document.querySelector('[data-testid="nav-budget"]')?.textContent.includes(`głębia ${h}`), rom.hops);
    assert.ok((await budget()).includes(word("bar.budget", { nodes: plural("nodes", limit * 2), hops: String(rom.hops) })));
  });

  await step("Perspektywa ▾ lists the kernel's perspectives; ticks re-ask graph.navigate with `perspectives`; Działania opens the focus's card at the actions; ⋯ opens the node card of a ROM item from graph.card", async () => {
    const list = await api("GET", "/api/perspectives");
    const nameOf = id => { const l = list.perspectives.find(p => p.id === id)?.entry?.label; return (typeof l === "string" ? l : l?.pl ?? l?.en) || id; };
    const rowAddresses = () => page.evaluate(() => [...document.querySelectorAll('[data-testid="nav-rows"] .gnav-row-main')].map(el => el.dataset.testid.replace("nav-row-", "")));
    const waitRows = async addresses => {
      try { await page.waitForFunction(want => !document.querySelector('[data-testid="nav-loading"]') &&
        JSON.stringify([...document.querySelectorAll('[data-testid="nav-rows"] .gnav-row-main')].map(el => el.dataset.testid.replace("nav-row-", ""))) === want, JSON.stringify(addresses), { timeout: 10000 }); }
      catch (e) { throw new Error(`rows never became ${JSON.stringify(addresses)}; now ${JSON.stringify(await rowAddresses())}, perspectives '${await nav().getAttribute("data-perspectives")}', budget '${await page.getByTestId("nav-budget").innerText()}'`); }
    };
    await page.getByTestId("nav-bar-perspective").tap();
    await page.getByTestId("nav-perspective-none").waitFor();
    const options = await page.getByTestId("nav-perspective-menu").locator('[data-testid^="nav-perspective-"]:not([data-testid="nav-perspective-none"])')
      .evaluateAll(els => els.map(el => ({ id: el.dataset.testid.replace("nav-perspective-", ""), label: el.textContent.trim(), checked: el.getAttribute("aria-checked") })));
    assert.deepEqual(options.map(o => o.id), list.perspectives.map(p => p.id), "the options are the kernel's perspectives, in its order");
    for (const o of options) assert.equal(o.label, nameOf(o.id), `${o.id} named in Polish`);
    assert.ok(options.every(o => o.checked === "false") && await page.getByTestId("nav-perspective-none").getAttribute("aria-checked") === "true", "none ticked at first");
    const chosen = ["structure", "dependencies"].filter(id => list.perspectives.some(p => p.id === id));
    assert.ok(chosen.length > 0, "the kernel has the structure perspective");
    const limitNow = Number(/(\d+)/.exec(await page.getByTestId("nav-budget").innerText())?.[1] ?? 0);
    const reached = await navigate({ focus: "area:rom", perspectives: chosen, hops: rom.hops, ...(limitNow > 0 ? { limit: limitNow } : {}) });
    for (const id of chosen) await page.getByTestId(`nav-perspective-${id}`).tap();
    await page.locator(`[data-testid="graph-navigator"][data-perspectives="${chosen.join(" ")}"]`).waitFor();
    await waitRows(reached.children.map(c => c.address));   // the rows are what the kernel reaches through those relations
    assert.equal(await page.getByTestId("nav-bar-perspective").getAttribute("aria-pressed"), "true");
    assert.equal(await page.getByTestId("nav-perspectives").innerText(), word("perspective.active", { list: chosen.map(nameOf).join(" · ") }));
    if (reached.children.length === 0) assert.equal(await page.getByTestId("nav-empty").innerText(), words["perspective.empty"]);
    await noMissingWord();
    await layoutFits();
    await shot("m1-navigator-perspective");
    await page.getByTestId("nav-perspective-none").tap();   // back to the node's own children; the menu closes
    await page.getByTestId("nav-perspective-menu").waitFor({ state: "detached" });
    await page.locator('[data-testid="graph-navigator"][data-perspectives=""]').waitFor();
    const own = await navigate({ focus: "area:rom", hops: rom.hops, ...(limitNow > 0 ? { limit: limitNow } : {}) });   // the node's own children under the budget of the moment
    await waitRows(own.children.map(c => c.address));
    assert.equal(await page.getByTestId("nav-perspectives").count(), 0);

    // Działania: the focus is ROM, so its card (graph.card area:rom) opens with "Co możesz zrobić" in view, the kernel's actions in words
    const romCard = await api("POST", "/api/graph/card", { lang: "pl", address: "area:rom" });
    await page.getByTestId("nav-bar-actions").tap();
    await page.locator('[data-testid="node-card"][data-section="actions"]').waitFor();
    await page.getByTestId("card-loading").waitFor({ state: "detached" });
    assert.equal(await page.getByTestId("card-label").innerText(), romCard.label);
    assert.equal(await page.getByTestId("card-actions").locator("button").first().getAttribute("aria-expanded"), "true");
    await page.waitForFunction(() => { const r = document.querySelector('[data-testid="card-actions"]').getBoundingClientRect(); return r.top >= -1 && r.top < window.innerHeight; });
    for (const a of romCard.actions) assert.ok((await page.getByTestId(`card-action-${a.id}`).innerText()).includes(a.label), `action ${a.id} in the kernel's words`);
    await noMissingWord();
    await shot("m1-navigator-actions");
    await page.getByTestId("card-close").tap();
    await page.getByTestId("node-card").waitFor({ state: "detached" });

    const leaf = rom.children[0].address;
    const expected = await api("POST", "/api/graph/card", { lang: "pl", address: leaf });
    await page.getByTestId(`nav-row-card-${leaf}`).tap();
    await page.getByTestId("node-card").waitFor();
    await page.getByTestId("card-loading").waitFor({ state: "detached" });
    assert.equal(await page.getByTestId("card-label").innerText(), expected.label);
    assert.equal(await page.getByTestId("card-chip").innerText(), expected.provenance.chip?.label ?? word(`chip.${expected.provenance.layer}`));
    if (expected.why) assert.ok((await page.getByTestId("card-why").innerText()).includes(expected.why));
    // A ROM item's value IS its status id (the kernel sends value = status): the status chip says it in words, no "Wartość: partial".
    assert.equal(await page.getByTestId("card-status").innerText(), word(`status.${expected.status}`), "the status chip in Polish");
    if (expected.value === expected.status) assert.equal(await page.getByTestId("card-value").count(), 0, "no value line that would repeat the status in English");
    assert.equal(await page.getByTestId("card-layers").count(), 0, "one layer (rom) = the chip; no Warstwy line repeating it");
    assert.equal(await page.getByTestId("card-chain-rows").locator("li").count(), expected.inheritance?.length ?? 0, "the chain is the kernel's, row for row");
    // r2:b1: a ROM item has no chain of levels: no open "Skąd jest wartość" header with nothing under it, one line that says so.
    if (!expected.inheritance?.length) {
      assert.equal(await page.getByTestId("card-chain").count(), 0, "no chain header on a node without a chain");
      assert.equal(await page.getByTestId("card-no-chain").innerText(), words["card.no_value"]);
    }
    for (const a of expected.actions) {
      const row = page.getByTestId(`card-action-${a.id}`);
      assert.ok((await row.innerText()).includes(a.label), `action ${a.id} in the kernel's words`);
      if (a.implementation && a.implementation !== "implemented") assert.ok((await row.innerText()).includes(words["card.action.planned"]), `${a.id} says planned`);
    }
    const groups = Object.entries(expected.relations).filter(([, g]) => g.out.length + g.in.length > 0);
    for (const [kind, g] of groups) {
      // r1:b5: each direction is headed by its verb and names the concrete nodes; the generic meaning sentence stays off the card
      const text = await page.getByTestId(`card-relation-group-${kind}`).innerText();
      if (g.out.length > 0) assert.ok(text.includes(`${g.label}:`), `${kind}: the out-rows are headed by the kind's name`);
      if (g.in.length > 0) assert.ok(text.includes(`${g.inverse_label ?? g.label}:`), `${kind}: the in-rows are headed by the inverse kind's name`);
      for (const n of [...g.out, ...g.in]) assert.ok(text.includes(n.label), `${kind}: names the concrete node ${n.label}`);
      if (g.meaning) assert.ok(!text.includes(g.meaning), `${kind}: no generic meaning sentence on the card`);
    }
    if (groups.length === 0) assert.equal(await page.getByTestId("card-relations-none").innerText(), words["card.relations.none"]);
    await page.getByTestId("card-label").scrollIntoViewIfNeeded();   // the report's screenshot shows the card from its top
    await noMissingWord();
    await layoutFits();
    await shot("m1-navigator-card-rom");
    await page.getByTestId(`nav-row-${leaf}`).tap();   // a leaf's row toggles the same card
    await page.getByTestId("node-card").waitFor({ state: "detached" });
  });

  // The settings card needs an open conversation for the level "Ta rozmowa": the page is reopened on one (saved view).
  // r2:b1: the conversation has messages (multipart POST /api/import, the universal importer), so "Ta rozmowa w grafie" below shows
  // what the owner will see under a real conversation: its messages as rows, not "Nic pod tym węzłem."
  const MESSAGES = [{ role: "user", content: "Jak zaplanować tydzień?" }, { role: "assistant", content: "Zacznij od trzech priorytetów." }, { role: "user", content: "Dzięki, a co z budżetem?" }];
  const form = new FormData();
  form.append("file", new Blob([JSON.stringify(MESSAGES)]), "m1-card.json");
  form.append("title", "M1 card");
  const imported = await fetch(base + "/api/import", { method: "POST", body: form });
  assert.ok(imported.ok, `import: ${imported.status}`);
  const importEvents = (await imported.text()).split(/\r?\n/).filter(l => l.startsWith("data: ")).map(l => JSON.parse(l.slice(6)));
  assert.ok(importEvents.some(e => e.type === "done") && !importEvents.some(e => e.type === "error"), JSON.stringify(importEvents));
  const convList = await api("GET", "/api/conversations");
  const convRows = Array.isArray(convList) ? convList : convList.conversations ?? convList.items;
  // the importer names the conversation "[Import] <title>" (loom/src/import): the crumb will say so too — it was imported.
  const conv = convRows.find(c => c.title === "M1 card" || c.title === "[Import] M1 card")?.id;
  assert.ok(conv, `the imported conversation is listed (${JSON.stringify(convList).slice(0, 200)})`);
  const cardOf = () => api("POST", "/api/graph/card", { lang: "pl", address: "setting:temperature", conv_id: conv });
  // The kernel's real card as a fixture of the unit test (tests/shell/node-card-state.test.mjs): the chip, the chain and Ustawienia agree.
  // The id of the conversation is fresh in every run: the fixture keeps a fixed placeholder (the unit test reads no id from it), so a run does not dirty the tree.
  writeFileSync(path.join(here, "../tests/shell/fixtures/graph-card-temperature.json"), JSON.stringify(await cardOf(), null, 2).split(conv).join("c_000000000000") + "\n");
  const chainOnScreen = () => page.getByTestId("card-chain-rows").locator("li").evaluateAll(els => els.map(li => ({
    level: li.dataset.testid.replace("card-chain-", ""), winning: li.dataset.winning === "true", role: li.dataset.role,
    value: li.querySelector(".ncard-chain-value")?.textContent ?? "" })));
  const sameChain = async (tag) => {
    const expected = await cardOf();
    const shown = await chainOnScreen();
    assert.deepEqual(shown.map(r => r.level), expected.inheritance.map(l => l.level), `${tag}: every level of the chain, in the kernel's order`);
    assert.deepEqual(shown.filter(r => r.winning).map(r => r.level), expected.inheritance.filter(l => l.role === "supplied").map(l => l.level), `${tag}: the winning level`);
    for (const l of expected.inheritance) {
      const row = shown.find(r => r.level === l.level);
      assert.equal(row.role, l.role, `${tag}: role of ${l.level}`);
      if (l.value !== undefined) assert.equal(row.value, num(l.value), `${tag}: value of ${l.level}`);
      if (l.no_unit) assert.ok((await page.getByTestId(`card-chain-${l.level}`).innerText()).includes(word(`inheritance.no_unit.${l.level}`)), `${tag}: ${l.level} says it has no unit`);
      // r2:b4: the level's meaning (graph/scopes.pack chain[].meaning, through graph.card) is the row's second line — a term with its explanation
      const meaning = await page.getByTestId(`card-chain-${l.level}`).locator(".ncard-chain-meaning").allInnerTexts();
      if (l.meaning) assert.deepEqual(meaning, [l.meaning], `${tag}: ${l.level} explains itself with the kernel's words`);
      else assert.deepEqual(meaning, [], `${tag}: ${l.level} has no meaning line when the kernel sends none`);
    }
    assert.equal(await page.getByTestId("card-value").innerText(), `${words["card.value"]} ${expected.value_human}`, `${tag}: the value in words`);
    assert.equal(await page.getByTestId("card-chip").innerText(), expected.provenance.chip?.label ?? word(`chip.${expected.provenance.layer}`), `${tag}: the chip`);
    return expected;
  };
  const confirmGate = async () => {
    await page.getByTestId("card-gate").waitFor();
    assert.ok((await page.getByTestId("card-gate-reason").first().innerText()).length > 0, "the kernel's reason is shown");
    await page.getByTestId("card-gate-confirm").tap();
    await page.getByTestId("card-gate").waitFor({ state: "detached" });
    await page.getByTestId("card-notice").waitFor();
    assert.equal(await page.getByTestId("card-notice").innerText(), words["card.saved"]);
    await page.getByTestId("card-loading").waitFor({ state: "detached" });
  };

  await step("Ustawienia › Rozmowa › Temperatura: the card shows the value, why, effect, the chip with layers and the whole chain", async () => {
    // A #czat/<id> link names the conversation. Since r1:b2 a link start loads the config too, so the words below stay Polish (ui_language),
    // not the browser's English — the assertions against the Polish catalogue are the check.
    await page.goto("about:blank");
    await page.goto(`${base}/#czat/${conv}`);
    await page.getByTestId("chat-shell").waitFor();
    await page.getByTestId("shell-menu-toggle").tap();
    await page.getByTestId("shell-menu-graph").tap();
    await nav().waitFor();
    for (const focus of ["area:profiles_settings", "group:rozmowa"]) {
      await page.getByTestId(`nav-row-${focus}`).tap();
      await page.locator(`[data-testid="graph-navigator"][data-focus="${focus}"]`).waitFor();
      await page.getByTestId("nav-loading").waitFor({ state: "detached" });
    }
    await page.getByTestId("nav-row-setting:temperature").tap();
    await page.getByTestId("node-card").waitFor();
    await page.getByTestId("card-loading").waitFor({ state: "detached" });
    const expected = await sameChain("fresh");
    assert.ok((await page.getByTestId("card-why").innerText()).includes(expected.why));
    assert.ok((await page.getByTestId("card-effect").innerText()).includes(expected.effect));
    // One vocabulary for the provenance: the chain below names the levels in the kernel's words with their roles, so no "Warstwy:
    // Preset → Ty" above a chain that says "Preset aplikacji — nic tu nie ustawiono" (two names for one level read as a contradiction).
    assert.ok(expected.provenance.layers.length >= 2 && expected.inheritance.length > 0, "the kernel sends layers and a chain for a setting");
    assert.equal(await page.getByTestId("card-layers").count(), 0, "the chain is the single statement of the layers");
    const chainLabels = await page.getByTestId("card-chain-rows").locator(".ncard-row-label").evaluateAll(els => els.map(el => el.textContent));
    assert.deepEqual(chainLabels, expected.inheritance.map(l => l.label), "the levels in the kernel's words, in its order");
    assert.equal(await page.getByTestId("card-edit-rule").innerText(), `${words["card.edit_rule"]} ${word(`card.edit_rule.${expected.edit_rule}`)}`);
    assert.ok((await page.getByTestId("card-flows").innerText()).includes(expected.flows_reading[0].title), "the flows that read the field");
    await page.getByTestId("card-label").scrollIntoViewIfNeeded();   // the report's screenshot shows the card from its top
    await noMissingWord();
    await layoutFits();
    await shot("m1-navigator-card-setting");
  });

  await step("Zmień → Dla kogo?: the field's own levels, 'więcej poziomów' shows the whole chain with the reason a level cannot be written", async () => {
    const expected = await cardOf();
    await page.getByTestId("card-change").tap();
    await page.getByTestId("card-editor-input").fill("0,3");
    await page.getByTestId("card-next").tap();
    await page.getByTestId("card-levels").waitFor();
    const shown = () => page.getByTestId("card-levels").locator("button[data-testid^='card-level-']").evaluateAll(els => els.map(el => el.dataset.testid.replace("card-level-", "")));
    assert.deepEqual(await shown(), expected.levels, "first the levels the field declares (human.levels)");
    assert.equal(await page.getByTestId("card-level-turn").getAttribute("data-writable"), "false");
    assert.ok((await page.getByTestId("card-level-turn").innerText()).includes(words["card.level.turn_planned"]));
    await page.getByTestId("card-more-levels").tap();
    assert.deepEqual(await shown(), expected.inheritance.map(l => l.level), "'więcej poziomów' = the whole chain");
    assert.equal(await page.getByTestId("card-level-conversation").getAttribute("data-writable"), "true");
    assert.ok((await page.getByTestId("card-level-project").innerText()).includes(word("inheritance.no_unit.project")));
    assert.ok((await page.getByTestId("card-level-installation").innerText()).includes(words["card.level.baseline"]));
    // r2:b4: every level of the whole chain explains itself on the "Dla kogo?" row with the kernel's meaning (the same words as the chain)
    for (const l of (await cardOf()).inheritance) if (l.meaning) assert.deepEqual(await page.getByTestId(`card-level-${l.level}`).locator(".ncard-chain-meaning").allInnerTexts(), [l.meaning], `${l.level}: meaning on the "Dla kogo?" row`);
    await noMissingWord();
    await layoutFits();
    await shot("m1-navigator-card-for-whom");
  });

  await step("set for 'Ta rozmowa' (rules.set, the kernel asks, confirm) → the chain shows the conversation winning; set for 'Ty' (settings.set) → person", async () => {
    await page.getByTestId("card-level-conversation").tap();
    await confirmGate();
    const after = await sameChain("conversation 0.3");
    assert.equal(after.value, 0.3);
    assert.equal(after.provenance.scope, "conversation");
    await page.getByTestId("card-change").tap();
    await page.getByTestId("card-editor-input").fill("0.4");
    await page.getByTestId("card-next").tap();
    await page.getByTestId("card-level-person").tap();
    await page.getByTestId("card-notice").waitFor();
    await page.getByTestId("card-loading").waitFor({ state: "detached" });
    const person = await sameChain("person 0.4");
    assert.equal(person.inheritance.find(l => l.level === "person").value, 0.4, "config.json holds 0.4");
    assert.equal(person.value, 0.3, "the conversation still wins");
    assert.equal((await api("GET", "/api/config")).temperature, 0.4);
  });

  await step("'Zdejmij wartość: Ta rozmowa' (rules.disable, confirm) → the chain returns to 'Ty'; the person layer says why it cannot be taken off yet", async () => {
    const before = await cardOf();
    const convRow = before.inheritance.find(l => l.level === "conversation"), personRow = before.inheritance.find(l => l.level === "person");
    const unset = page.getByTestId("card-unset-conversation");
    assert.ok((await unset.innerText()).includes(word("card.unset", { layer: convRow.label, value: num(convRow.value) })));
    assert.ok((await unset.innerText()).includes(word("card.unset.returns", { layer: personRow.label, value: num(personRow.value) })));
    assert.equal(await page.getByTestId("card-unset-person").isDisabled(), true);
    assert.ok((await page.getByTestId("card-unset-person").innerText()).includes(words["card.unset.person_planned"]));
    await shot("m1-navigator-card-unset");
    await unset.tap();
    await confirmGate();
    const after = await sameChain("after unset");
    assert.equal(after.value, 0.4);
    assert.equal(after.provenance.scope, "person");
    assert.equal(await page.getByTestId("card-unset-conversation").count(), 0, "the conversation layer holds nothing now");
    const rules = await api("POST", "/api/rules/list", { scope: "conversation", scope_id: conv, include_disabled: true });
    assert.ok(rules.rules.every(r => r.disabled), "the rule is switched off, not deleted");
    await noMissingWord();
    await layoutFits();
  });

  await step("the map (P4): the same nodes as the list, a tap on a mark = label line + the same card, + / − and pinch zoom, the exclusion rect, Widok ▾ saves navigator_layout", async () => {
    // both disclosures open (preset both), the map drawn through the view engine (sector system) with one mark per list row plus the focus
    assert.equal(await nav().getAttribute("data-layout"), "both");
    assert.equal(await page.getByTestId("nav-list-disclosure").getAttribute("data-state"), "expanded");
    assert.equal(await page.getByTestId("nav-map-disclosure").getAttribute("data-state"), "expanded");
    assert.equal(await page.getByTestId("nav-map").getAttribute("data-system"), "sector");
    const rows = await rowsOf();
    const focus = await nav().getAttribute("data-focus");
    const markIds = await page.getByTestId("nav-map-mark").evaluateAll(els => els.map(el => el.dataset.id));
    assert.deepEqual(new Set(markIds), new Set([focus, ...rows.map(r => r.address)]), "the map draws exactly the rows of the list and the focus");
    assert.equal(await page.getByTestId("nav-map-hint").innerText(), word("map.hint"));
    // the visible rectangle of the map, as reported to the shell (CSS px, inside the phone, with the 24 px side strips free)
    // below the fold the map reports no rectangle (honest: nothing to protect); scrolled into view it reports its visible box
    assert.equal(await page.getByTestId("nav-map").getAttribute("data-exclusion"), "[]", "off screen = no exclusion");
    await page.getByTestId("nav-map").scrollIntoViewIfNeeded();
    await page.waitForFunction(() => JSON.parse(document.querySelector('[data-testid="nav-map"]').dataset.exclusion).length === 1);
    const rect = JSON.parse(await page.getByTestId("nav-map").getAttribute("data-exclusion"));
    assert.equal(rect.length, 1, "one rectangle while the map is open");
    assert.ok(rect[0].x >= 24 && rect[0].x + rect[0].w <= 390 - 24 && rect[0].y >= 0 && rect[0].y + rect[0].h <= 844 && rect[0].h > 100, JSON.stringify(rect));
    // + / − step the zoom; a two-finger pinch (pointer events, as a WebView delivers them) zooms by the finger distance
    await page.getByTestId("nav-map-zoom-in").tap();
    assert.equal(await page.getByTestId("nav-map").getAttribute("data-zoom"), "1.250");
    await page.getByTestId("nav-map-zoom-out").tap();
    assert.equal(await page.getByTestId("nav-map").getAttribute("data-zoom"), "1.000");
    await page.getByTestId("nav-map").evaluate(el => {
      const r = el.getBoundingClientRect();
      const ev = (type, id, x, y) => el.dispatchEvent(new PointerEvent(type, { pointerId: id, pointerType: "touch", clientX: r.left + x, clientY: r.top + y, bubbles: true, isPrimary: id === 1 }));
      ev("pointerdown", 1, 120, 150); ev("pointerdown", 2, 220, 150);
      ev("pointermove", 1, 70, 150); ev("pointermove", 2, 270, 150);
      ev("pointerup", 1, 70, 150); ev("pointerup", 2, 270, 150);
    });
    // the pinch was dispatched synchronously; React renders the new camera on its own turn, so wait for it (CI runners are slower than a dev box: this raced there)
    await page.waitForFunction(() => document.querySelector('[data-testid="nav-map"]')?.dataset.zoom === "2.000", null, { timeout: 5000 }).catch(() => {});
    assert.equal(await page.getByTestId("nav-map").getAttribute("data-zoom"), "2.000", "fingers 100 px apart to 200 px apart doubles the zoom");
    await page.getByTestId("nav-map-home").tap();   // ⌂: the camera home, so no node is lost after a pan or a pinch
    assert.equal(await page.getByTestId("nav-map").getAttribute("data-zoom"), "1.000");
    // a tap on the mark of the first row: the label line with the row's words and the same card as the row's ⋯ (graph.card of that address)
    await page.getByTestId("card-close").tap().catch(() => {});
    await page.getByTestId("node-card").waitFor({ state: "detached" });
    const first = rows[0];
    await page.getByTestId(`nav-map-node-${first.address}`).tap();
    const selected = page.getByTestId("nav-map-selected");
    assert.equal(await selected.getAttribute("data-address"), first.address);
    assert.ok((await selected.innerText()).includes(first.label));
    await page.getByTestId("node-card").waitFor();
    assert.equal(await page.getByTestId("node-card").getAttribute("data-address"), first.address, "the mark opens the card of the same node as the list row");
    await page.getByTestId("card-loading").waitFor({ state: "detached" });
    const expected = await api("POST", "/api/graph/card", { lang: "pl", address: first.address });
    assert.equal(await page.getByTestId("card-label").innerText(), expected.label);
    await layoutFits(); await noMissingWord();
    // Widok ▾: the three values of the field, chosen = applied at once and saved for you (settings.set navigator_layout)
    await page.getByTestId("nav-bar-view").tap();
    await page.getByTestId("nav-view-menu").waitFor();
    for (const id of ["both", "list", "map"]) assert.equal(await page.getByTestId(`nav-view-${id}`).innerText(), word(`view.${id}`));
    assert.equal(await page.getByTestId("nav-view-both").getAttribute("aria-checked"), "true");
    await page.getByTestId("nav-view-map").tap();
    await page.getByTestId("nav-view-menu").waitFor({ state: "detached" });
    assert.equal(await nav().getAttribute("data-layout"), "map");
    assert.equal(await page.getByTestId("nav-list-disclosure").getAttribute("data-state"), "collapsed");
    assert.equal(await page.getByTestId("nav-map-disclosure").getAttribute("data-state"), "expanded");
    await page.getByTestId("nav-view-note").waitFor();
    assert.equal(await page.getByTestId("nav-view-note").innerText(), word("view.saved", { layout: word("view.map") }));
    assert.equal((await api("GET", "/api/config")).navigator_layout, "map", "saved in config.json");
    // the folded list is one tap away; back to both (restores the preset for the steps after)
    await page.getByTestId("nav-list-disclosure").locator(".disclosure-head").tap();
    assert.equal(await page.getByTestId("nav-list-disclosure").getAttribute("data-state"), "expanded");
    await page.getByTestId("nav-bar-view").tap();
    await page.getByTestId("nav-view-both").tap();
    await page.getByTestId("nav-view-menu").waitFor({ state: "detached" });
    await page.getByTestId("nav-view-note").filter({ hasText: word("view.saved", { layout: word("view.both") }) }).waitFor();   // the save is asynchronous
    assert.equal((await api("GET", "/api/config")).navigator_layout, "both");
    await page.getByTestId("nav-map").scrollIntoViewIfNeeded();
    await shot("m1-navigator-map");
    await layoutFits(); await noMissingWord();
    assert.deepEqual(errors, [], "no page errors");
  });

  await step("Back closes the layers in order: the card, then each path step (Rozmowa → Profile i ustawienia → root), then the panel; at the root it falls through", async () => {
    const back = () => page.evaluate(() => window.__loomBack());
    assert.equal(await back(), true);
    await page.getByTestId("node-card").waitFor({ state: "detached" });
    assert.equal(await nav().getAttribute("data-focus"), "group:rozmowa", "the card closed, the path did not");
    assert.equal(await back(), true);
    await page.locator('[data-testid="graph-navigator"][data-focus="area:profiles_settings"]').waitFor();
    assert.equal(await back(), true);
    await page.locator('[data-testid="graph-navigator"][data-focus=""]').waitFor();
    assert.equal(await page.getByTestId("nav-crumb-1").count(), 0);
    assert.equal(await back(), true);
    await nav().waitFor({ state: "detached" });
    // r1:b6: focus goes back to the ⋮ button the panel was opened from (the menu item is gone with its menu).
    assert.equal(await page.evaluate(() => document.activeElement?.getAttribute("data-testid")), "shell-menu-toggle", "focus returned to ⋮");
    assert.equal(await back(), false);
    assert.deepEqual(errors, [], "no page errors");
  });

  await step("⋮ › Ta rozmowa w grafie and ⋮ › Ty open the same navigator at conv:<open conversation> and party:self (spec 3.1: four foci, one screen)", async () => {
    const back = () => page.evaluate(() => window.__loomBack());
    const openAt = async (menuId, focus) => {
      await page.getByTestId("shell-menu-toggle").tap();
      await page.getByTestId(`shell-menu-${menuId}`).tap();
      await page.locator(`[data-testid="graph-navigator"][data-focus="${focus}"]`).waitFor();
      await page.getByTestId("nav-loading").waitFor({ state: "detached" });
      // W2b-1: Ty is the page of the person (me-view); the generic rows of the same focus wait under a closed 'Wszystkie wiersze' - open them, the assertions below are about those rows
      if (await page.getByTestId("me-view").count()) {
        if (!(await page.getByTestId("nav-rows").isVisible())) await page.getByTestId("nav-list-disclosure").locator(".disclosure-head").first().tap();
        await page.getByTestId("nav-rows").waitFor({ state: "visible" });
      }
      const expected = await navigate({ focus });
      await shot(`m1-navigator-${menuId}`);
      const crumbs = await page.locator('[data-testid^="nav-crumb-"]').evaluateAll(els => els.map(el => el.textContent));
      assert.equal(crumbs[crumbs.length - 1], expected.focus.label, `${menuId}: the focus is the kernel's node (crumbs ${JSON.stringify(crumbs)}, focus ${JSON.stringify(expected.focus)})`);
      assert.deepEqual((await rowsOf()).map(r => r.address), expected.children.map(c => c.address), `${menuId}: the rows of the focus, in the kernel's order`);
      await noMissingWord();
      await layoutFits();
      return expected;
    };
    // The page is open on the conversation `conv` (saved view); its graph address is conv:<hex> for the database id c_<hex>
    // (loom/data/runtime/element_resolution.pack, projection_conv: prefix "conv:", same_as_prefix "c_").
    const thisConv = await openAt("this_conversation", "conv:" + conv.replace(/^c_/, ""));
    // r2:b1: under the conversation are its messages, each as a row ("Ty: …" / "Model: …"), in the kernel's order.
    assert.equal(thisConv.counts.total, MESSAGES.length, "every message of the conversation is a row");
    assert.deepEqual((await rowsOf()).map(r => r.label), thisConv.children.map(c => c.label), "the rows are the messages, labelled as the kernel labels them");
    for (const [i, c] of thisConv.children.entries()) assert.ok(c.label.includes(MESSAGES[i].content.slice(0, 12)), `row ${i} is the message text (${c.label})`);
    assert.equal(await page.getByTestId("nav-empty").count(), 0, "no 'Nic pod tym węzłem.' on a conversation with messages");
    assert.equal(await back(), true);
    await nav().waitFor({ state: "detached" });
    const me = await openAt("me", "party:self");
    assert.ok(me.children.length > 0, "Ty has rows (the parts)");
    // K2c-2: the children of Ty are PARTS (kind person_subgraph, address subgraph:self/<id>), each with the count of its entries; the interview
    // slots hang one level down, under the part(s) that hold them. Which part holds them is read from the kernel's answer (the parts table is
    // data: profiles/user.pack profile.subgraphs, a user layer may reorder or rename them), never from a Polish label.
    const parts = me.children.filter(c => c.kind === "person_subgraph");
    assert.ok(parts.length > 0, "Ty has parts");
    assert.deepEqual(me.children.map(c => c.address), parts.map(c => c.address).concat(me.children.filter(c => c.kind !== "person_subgraph").map(c => c.address)), "parts come first");
    const partViews = [];
    for (const part of parts) {
      assert.ok(part.address.startsWith("subgraph:self/"), `${part.address}: a part has the address subgraph:self/<id>`);
      const inside = await navigate({ focus: part.address });
      assert.equal(inside.focus.address, part.address, `${part.address}: the part is its own focus`);
      assert.equal(part.count, inside.counts.total, `${part.address}: the count on the part is the number of entries inside it`);
      assert.ok(inside.breadcrumbs.some(b => b.address === "party:self"), `${part.address}: the crumbs lead through Ty`);
      partViews.push({ part, inside });
    }
    const shownParts = await rowsOf();
    for (const { part } of partViews) {
      const row = shownParts.find(r => r.address === part.address);
      assert.ok(row, `${part.address}: the part is a row of Ty`);
      assert.equal(row.label, part.label, `${part.address}: the row is labelled as the kernel labels the part`);
      assert.ok(row.count !== null, `${part.address}: the row shows a count`);
    }
    const holders = partViews.filter(v => v.inside.children.some(c => c.kind === "profile_slot"));
    assert.ok(holders.length > 0, `some part of Ty holds the interview slots (parts ${JSON.stringify(partViews.map(v => [v.part.address, v.part.count]))})`);
    const { part: holder, inside: holderView } = holders[0];
    await page.getByTestId(`nav-row-${holder.address}`).tap();
    await page.locator(`[data-testid="graph-navigator"][data-focus="${holder.address}"]`).waitFor();
    // the focus changes at the tap, the rows when the kernel's answer arrives: wait for the rows of the part, not for the end of a spinner that may not have started
    await page.waitForFunction(want => !document.querySelector('[data-testid="nav-loading"]') &&
      JSON.stringify([...document.querySelectorAll('[data-testid="nav-rows"] .gnav-row-main')].map(el => el.dataset.testid.replace("nav-row-", ""))) === want,
      JSON.stringify(holderView.children.map(c => c.address)), { timeout: 10000 });
    await shot("m1-navigator-me-part");
    assert.deepEqual((await rowsOf()).map(r => r.address), holderView.children.map(c => c.address), "the rows of the part, in the kernel's order");
    await noMissingWord();
    await layoutFits();
    // r2:b1: a profile slot's chip says what its evidence says ("pytanie z wywiadu — bez odpowiedzi"), never "Preset (ROM)" on your own
    // profile; its card says where the answer is given (the interview) and has no empty "Skąd jest wartość" header.
    const slots = holderView.children.filter(c => c.kind === "profile_slot");
    assert.ok(slots.length > 0, "the part holds profile slots");
    const shownRows = await rowsOf();
    for (const s of slots) {
      const row = shownRows.find(r => r.address === s.address);
      if (s.evidence === null) {
        // an unanswered slot holds no value, so no evidence: the chip is its state in words (M1-K2c3)
        assert.ok(s.state, `${s.address}: the kernel says why there is no evidence`);
        assert.equal(row.chip, word(`me.state.${s.state}`), `${s.address}: the chip is the state in words`);
      } else {
        assert.ok(s.evidence?.human, `${s.address}: the kernel says the evidence in words`);
        assert.equal(row.chip, s.evidence.human, `${s.address}: the chip is the evidence`);
      }
      assert.notEqual(row.chip, word("chip.rom"), `${s.address}: not "Preset (ROM)" on the owner's profile`);
    }
    const slot = slots[0];
    const slotCard = await api("POST", "/api/graph/card", { lang: "pl", address: slot.address });
    await page.getByTestId(`nav-row-card-${slot.address}`).tap();
    await page.getByTestId("node-card").waitFor();
    await page.getByTestId("card-loading").waitFor({ state: "detached" });
    assert.equal(await page.getByTestId("card-label").innerText(), slotCard.label);
    assert.equal(await page.getByTestId("card-chip").innerText(), slotCard.evidence ? slotCard.evidence.human : word(`me.state.${slotCard.state}`));
    assert.equal(await page.getByTestId("card-profile-interview").innerText(), words[slotCard.status === "known" ? "card.profile.answered_in_interview" : "card.profile.answer_in_interview"]);
    assert.equal(await page.getByTestId("card-chain").count(), 0, "no empty chain header");
    assert.equal(await page.getByTestId("card-no-chain").innerText(), words["card.no_value"]);
    const cardText = await page.getByTestId("node-card").innerText();
    assert.ok(cardText.indexOf(words["card.profile.answer_in_interview"]) < cardText.indexOf(word("actions")), "the interview line comes before the generic actions");
    await page.getByTestId("card-label").scrollIntoViewIfNeeded();
    await noMissingWord();
    await layoutFits();
    await shot("m1-navigator-card-profile");
    await page.getByTestId(`nav-row-${slot.address}`).tap();
    await page.getByTestId("node-card").waitFor({ state: "detached" });
    assert.equal(await back(), true, "back from the part to Ty");
    await page.locator('[data-testid="graph-navigator"][data-focus="party:self"]').waitFor();
    assert.equal(await back(), true, "back from Ty closes the panel");
    await nav().waitFor({ state: "detached" });
    assert.deepEqual(errors, [], "no page errors");
  });

  await step("G3c: the map of a level whose marks look alike (13 rom_item, no counts) advises like the Widok bar; Zostaw, nie pytaj więcej is remembered by the kernel and survives a reload", async () => {
    if (await nav().count()) { await page.getByTestId("close-panel").tap(); await nav().waitFor({ state: "detached" }); }   // an earlier step may leave the screen open
    const openRom = async () => {
      await page.getByTestId("shell-menu-toggle").tap();
      await page.getByTestId("shell-menu-graph").tap();
      await nav().waitFor();
      await page.getByTestId("nav-loading").waitFor({ state: "detached" });
      await page.getByTestId("nav-row-area:rom").tap();
      await page.waitForFunction(() => document.querySelector('[data-testid="graph-navigator"]')?.dataset.focus === "area:rom");
      await page.getByTestId("nav-loading").waitFor({ state: "detached" });
    };
    const listCalls = [];
    page.on("request", r => { if (r.method() === "POST" && r.url().endsWith("/api/actions/run") && /"advisory\.list"/.test(r.postData() ?? "")) listCalls.push(1); });
    await openRom();
    const chips = page.getByTestId("view-advisory-chip");
    await chips.first().waitFor();
    const codes = await chips.evaluateAll(els => els.map(el => `${el.dataset.code}|${el.dataset.fingerprint}`));
    assert.equal(codes.length, 4, `hue, saturation, lightness and size are identical on the 13 marks: ${codes.join(" ")}`);
    assert.ok(codes.every(c => c.startsWith("marks_identical|navigator_level|")), codes.join(" "));
    assert.ok((await chips.first().innerText()).includes("Wszystkie 13 znaczniki"), "the sentence says how many marks");
    assert.equal(listCalls.length, 1, "the remembered answers are read once, when there is something to advise");
    assert.equal(await page.getByTestId("nav-map-mark").count() > 0, true, "the map is still drawn: an advisory blocks nothing");
    await layoutFits();
    const first = codes[0];
    await chips.first().getByTestId("view-advisory-choice-dont_ask").tap();
    await page.waitForFunction(n => document.querySelectorAll('[data-testid="view-advisory-chip"]').length === n, 3);
    await page.waitForTimeout(300);   // the answer reaches the kernel (advisory.answer) before the page goes away
    await page.reload();
    await page.getByTestId("chat-shell").waitFor();
    await openRom();
    await page.getByTestId("view-advisory-chip").first().waitFor();
    const after = await page.getByTestId("view-advisory-chip").evaluateAll(els => els.map(el => `${el.dataset.code}|${el.dataset.fingerprint}`));
    assert.equal(after.length, 3, "the remembered 'nie pytaj więcej' hides its chip after a reload; the others ask");
    assert.ok(!after.includes(first), `${first} stays quiet`);
    await shot("g3c-navigator-advisories");
    await page.getByTestId("close-panel").tap();
    await nav().waitFor({ state: "detached" });
    assert.deepEqual(errors, [], "no page errors");
  });

  await step("the dense root map (r2:b2): a tap on the centre of EVERY mark selects that mark, not its neighbour in DOM order (44 px targets overlap)", async () => {
    await page.getByTestId("shell-menu-toggle").tap();
    await page.getByTestId("shell-menu-graph").tap();
    await nav().waitFor();
    await page.getByTestId("nav-loading").waitFor({ state: "detached" });
    assert.equal(await nav().getAttribute("data-focus"), "", "the root");
    const ids = await page.getByTestId("nav-map-mark").evaluateAll(els => els.map(el => el.dataset.id));
    assert.ok(ids.length >= 20, `a dense level (${ids.length} marks)`);
    const overlapping = await page.locator(".gnav-map-hit").evaluateAll(els => {
      const r = els.map(e => e.getBoundingClientRect()); let n = 0;
      for (let i = 0; i < r.length; i++) for (let j = i + 1; j < r.length; j++) if (r[i].left < r[j].right && r[j].left < r[i].right && r[i].top < r[j].bottom && r[j].top < r[i].bottom) n++;
      return n;
    });
    assert.ok(overlapping > 0, `the hit buttons overlap on this level (${overlapping} pairs) — the case the finding describes`);
    let tapped = 0;
    for (const id of ids) {
      // a mark outside the plane has no button; the others are tapped at the centre of their circle, measured right before the tap
      if (await page.getByTestId(`nav-map-node-${id}`).count() === 0) continue;
      await page.getByTestId("nav-map").scrollIntoViewIfNeeded();
      const box = await page.locator(`[data-testid="nav-map-mark"][data-id="${id}"] circle`).boundingBox();
      await page.touchscreen.tap(box.x + box.width / 2, box.y + box.height / 2);
      assert.equal(await page.locator(`[data-testid="nav-map-mark"][data-id="${id}"]`).getAttribute("data-selected"), "true", `tap on the centre of ${id} selects ${id}`);
      assert.equal(await page.getByTestId("nav-map-mark").evaluateAll(els => els.filter(el => el.dataset.selected === "true").length), 1, "exactly one mark selected");
      if (id !== "root") {
        assert.equal(await page.getByTestId("nav-map-selected").getAttribute("data-address"), id, `the line under the map is ${id}`);
        await page.getByTestId("node-card").waitFor();
        assert.equal(await page.getByTestId("node-card").getAttribute("data-address"), id, `the card is ${id}`);
        await page.getByTestId("card-loading").waitFor({ state: "detached" });
      }
      tapped++;
    }
    assert.ok(tapped >= 20, `${tapped} marks tapped`);
    await page.getByTestId("close-panel").tap();
    await nav().waitFor({ state: "detached" });
    assert.deepEqual(errors, [], "no page errors");
  });
  await step("r2:b4: Rozwiń of an entry area in the Workshop opens the navigator at that area (kb.query → area:knowledge), not the old Wiedza sheet", async () => {
    await page.getByTestId("shell-menu-toggle").tap();
    await page.getByTestId("shell-menu-workshop").tap();
    if (!(await page.getByTestId("nav-graph-entry").isVisible())) await page.getByTestId("toggle-nav").tap();
    await page.getByTestId("nav-graph-entry").tap();
    await page.getByTestId("graph-entry-panel").waitFor();
    const expand = page.getByTestId("graph-entry-area-knowledge").locator(".graph-entry-expand");
    assert.equal(await expand.count(), 1, "the knowledge area has its Rozwiń (the opener kb.query is registered)");
    await expand.tap();
    await nav().waitFor();
    assert.equal(await nav().getAttribute("data-focus"), "area:knowledge", "the navigator opens at the evidence area");
    assert.equal(await page.getByTestId("nav-knowledge").getAttribute("aria-pressed"), "false", "the old Wiedza view did not open (its own state stays off)");
    await page.getByTestId("close-panel").tap();
    await nav().waitFor({ state: "detached" });
    if (!(await page.getByTestId("nav-chat").isVisible())) await page.getByTestId("toggle-nav").tap();   // the phone hides the Workshop nav behind ☰
    await page.getByTestId("nav-chat").tap();
    await page.getByTestId("chat-shell").waitFor();
    assert.deepEqual(errors, [], "no page errors");
  });
  await step("no screen shown so far has developer text: a raw key or an English sentence (m1-audit.mjs; the person's own messages and the fold Szczegóły techniczne are skipped)", async () => {
    assert.deepEqual(AUDIT.map(a => `${a.shot}: ${a.why} | ${a.line} | ${a.where}`), [], "every line of the screens is in the words of the pack");
  });
} finally {
  await browser?.close();
  server.kill("SIGTERM");
}
for (const a of AUDIT) console.log(`[m1-navigator] AUDIT ${a.shot}: ${a.why} | ${a.line} | ${a.where}`);
if (failed) process.exit(1);
