#!/usr/bin/env node
// M1-W0 (7), the LIST half (package P2): the graph navigator on a phone (390x844, touch) against the real loom-server, no model,
// no network. Everything asserted is compared with what the kernel answers (POST /api/graph/navigate) and with the words of the
// catalogue presentation.graph_screen (loom/data/profiles/conversation_view.pack): no literal of a label or a count lives here.
// Checks: menu ⋮ › Graf opens the navigator; the root rows are the kernel's rows with Polish labels, counts or "nieznane (powód)",
// provenance chips, "po co"; no page scroll wider than 390; every tappable >= 44 px; a tap on ROM drills in (crumbs, 13 Polish rows
// with status chips, the focus's why); search on the level; Szerzej/Węziej/Głębiej/Płycej change the budget and depth; the planned
// buttons say so; the card stub; Back closes the layers in order (step → panel) through window.__loomBack. P3/P4 add the card and the map.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = path.join(here, "screenshots");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[m1-navigator] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });
const pack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const words = pack.entries.find(r => r.key === "presentation.graph_screen").value.locales.pl;
const word = (id, params = {}) => words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? "");

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-m1-navigator-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
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
  const errors = [];
  page.on("pageerror", e => errors.push(String(e)));
  const nav = () => page.getByTestId("graph-navigator");
  const rowsOf = () => page.getByTestId("nav-rows").locator("li").evaluateAll(els => els.map(li => ({
    address: li.querySelector(".gnav-row-main").dataset.testid.replace("nav-row-", ""), label: li.querySelector(".gnav-label").textContent,
    count: li.querySelector('[data-testid="nav-count"]')?.textContent ?? null, chip: li.querySelector('[data-testid="nav-chip"]').textContent,
    status: li.querySelector('[data-testid="nav-status"]')?.textContent ?? null, why: li.querySelector(".gnav-why")?.textContent ?? "" })));
  const noMissingWord = async () => {
    const texts = await nav().locator("button, span, p, input").evaluateAll(els => els.map(el => el.textContent.trim()));
    const leaked = texts.filter(t => /^(chip|count|bar|status|reason|evidence|card|search|no_|query)\.[a-z_]+/.test(t));
    assert.deepEqual(leaked, [], "no catalogue id on screen (a missing word would show its id)");
  };
  const layoutFits = async () => {
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth) <= 390, "no horizontal page scroll");
    const small = await nav().locator("button").evaluateAll(els => els.filter(el => el.offsetParent !== null).map(el => {
      const r = el.getBoundingClientRect(); return { id: el.dataset.testid, w: Math.round(r.width), h: Math.round(r.height) }; }).filter(b => b.w < 44 || b.h < 44));
    assert.deepEqual(small, [], "every tappable of the navigator is at least 44 x 44 px");
  };

  await page.goto(base + "/");
  await page.getByTestId("chat-shell").waitFor();

  await step("menu ⋮ › Graf opens the navigator at the root, with the kernel's rows in Polish", async () => {
    await page.getByTestId("shell-menu-toggle").tap();
    await page.getByTestId("shell-menu-graph").tap();
    await nav().waitFor();
    await page.getByTestId("nav-rows").waitFor();
    await page.getByTestId("nav-loading").waitFor({ state: "detached" });
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
    assert.equal(await page.getByTestId("nav-budget").innerText(), `${word(root.limit.value === 0 ? "bar.budget.unlimited" : "bar.budget", { limit: String(root.limit.value), hops: String(root.hops) })} · ${word("children", { shown: String(root.counts.shown), total: String(root.counts.total) })}`);
    await noMissingWord();
    await layoutFits();
    await page.screenshot({ path: path.join(SHOTS, "m1-navigator-root.png") });
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
    await page.screenshot({ path: path.join(SHOTS, "m1-navigator-rom.png") });
  });

  await step("the magnifier searches on the level (graph.navigate query), and closing it restores the level", async () => {
    const first = rom.children[0].label.slice(0, 3);
    const expected = await navigate({ focus: "area:rom", query: first });
    await page.getByTestId("nav-search-toggle").tap();
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
      await page.waitForFunction((l) => document.querySelector('[data-testid="nav-budget"]')?.textContent.includes(`budżet ${l} `), limit);
      shown = Number(await page.getByTestId("nav-rows").getAttribute("data-shown"));
    }
    const narrowed = await navigate({ focus: "area:rom", limit });
    assert.equal(shown, narrowed.counts.shown);
    assert.ok(shown < rom.children.length, `the budget ${limit} collapses rows`);
    assert.equal(await page.getByTestId("nav-collapsed").innerText(), word("collapsed", { count: String(narrowed.counts.collapsed) }));
    await page.getByTestId("nav-bar-wider").tap();
    await page.waitForFunction((l) => document.querySelector('[data-testid="nav-budget"]')?.textContent.includes(`budżet ${l} `), limit * 2);
    await page.getByTestId("nav-bar-deeper").tap();
    await page.waitForFunction((h) => document.querySelector('[data-testid="nav-budget"]')?.textContent.includes(`głębia ${h}`), rom.hops + 1);
    await page.getByTestId("nav-bar-shallower").tap();
    await page.waitForFunction((h) => document.querySelector('[data-testid="nav-budget"]')?.textContent.includes(`głębia ${h}`), rom.hops);
    assert.ok((await budget()).includes(word("bar.budget", { limit: String(limit * 2), hops: String(rom.hops) })));
  });

  await step("Perspektywa / Działania / Widok say they are planned; ⋯ shows the card stub with the node's address", async () => {
    for (const id of ["perspective", "actions", "view"]) {
      await page.getByTestId(`nav-bar-${id}`).tap();
      assert.equal(await page.getByTestId("nav-bar-planned").innerText(), word("bar.planned", { what: word(`bar.${id}`) }));
      await page.getByTestId(`nav-bar-${id}`).tap();
      await page.getByTestId("nav-bar-planned").waitFor({ state: "detached" });
    }
    const leaf = rom.children[0].address;
    await page.getByTestId(`nav-row-card-${leaf}`).tap();
    assert.equal(await page.getByTestId("nav-card-stub").innerText(), word("card.stub", { address: leaf }));
    await page.getByTestId(`nav-row-${leaf}`).tap();   // a leaf's row toggles the same stub
    await page.getByTestId("nav-card-stub").waitFor({ state: "detached" });
  });

  await step("Back closes the layers in order: the step (ROM → root), then the panel; at the root it falls through", async () => {
    const back = () => page.evaluate(() => window.__loomBack());
    assert.equal(await back(), true);
    await page.locator('[data-testid="graph-navigator"][data-focus=""]').waitFor();
    assert.equal(await page.getByTestId("nav-crumb-1").count(), 0);
    assert.equal(await back(), true);
    await nav().waitFor({ state: "detached" });
    assert.equal(await back(), false);
    assert.deepEqual(errors, [], "no page errors");
  });
} finally {
  await browser?.close();
  server.kill("SIGTERM");
}
if (failed) process.exit(1);
