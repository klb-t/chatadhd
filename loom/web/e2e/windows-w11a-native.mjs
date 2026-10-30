#!/usr/bin/env node
// W11a-1: the window „Struktura projektu” (kind project_structure, panel structure) on the REAL loom-server, phone 390x844. No model and
// no network: a local fake provider is configured only so the chat has a provider (the window sentences are routed by the kernel without
// a model). TEST DATA (synthetic, in a temp data dir only): a unit „Alfa” with two conversations, defined through units.define /
// conversation.set_unit as a person would (asked, then confirmed), a unit „Pusty” with one conversation (a unit is a node of the graph only while a conversation names it), and a person's overlay of structure_templates with one
// template of little use (empty expects) that fits only „Pusty". Nothing of it ships.
// 1 the sentence „pokaż strukturę projektu Alfa” opens the window with root = the unit: conversations present (2), documents as a ghost
// 2 viewing writes nothing: the data dir is byte-identical before and after re-reads (show_expected off/on, a root typed again)
// 3 mapping change re-renders without a reload (a page marker survives; the ghost takes the dashed look, then the loud one)
// 4 a template of little use (root „Pusty”) -> the kernel's advisory with „nie pytaj więcej”; a look that hides the missing ones -> the
//   page's advisory; „nie pytaj więcej” hides it and the answer is remembered by the kernel (advisory.list)
// 5 the window parameters (root, mapping) come back after a reload (the last layout restores the windows and their values)
// 6 opened from the ⋮ menu „Struktura projektu” when no window is open: the suggestions of roots, a tap shows the unit
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { stableShot, trackRequests } from "./stable-shot.mjs";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-w11a-shots-", { evidence: true });
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[w11a] build loom-server and `npm run build` first"); process.exit(2); }

const provider = createServer(async (req, res) => {
  if (req.method !== "POST" || !req.url.endsWith("/chat/completions")) { res.writeHead(404).end(); return; }
  for await (const _ of req) { /* drain */ }
  res.writeHead(200, { "Content-Type": "application/json" });
  res.end(JSON.stringify({ choices: [{ message: { role: "assistant", content: "ok" } }], usage: { prompt_tokens: 1, completion_tokens: 1 } }));
});
await new Promise(r => provider.listen(0, "127.0.0.1", r));
const providerBase = `http://127.0.0.1:${provider.address().port}`;

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

// the test fixture: a person's overlay with a template of little use (empty expects) that fits only unit:project/pusty
const DATA = makeTmpDir("loom-w11a-");
mkdirSync(path.join(DATA, "profiles"), { recursive: true });
writeFileSync(path.join(DATA, "profiles/structure_templates.pack"), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain: "structure_templates",
  overrides: { templates: { pusty_test: { schema: "loom.structure_template/1", label: { pl: "Pusty wzorzec (test)", en: "Empty template (test)" }, match: { prefix: "unit:project/pusty" }, expects: [] } } } }));

// every file of the data dir and its hash (logs and SQLite's shared-memory index are not data)
function snapshot(dir) {
  const out = {};
  const walk = d => { for (const n of readdirSync(d)) { const p = path.join(d, n); const s = statSync(p); if (s.isDirectory()) { if (n !== "logs") walk(p); } else if (!n.endsWith("-shm")) out[path.relative(dir, p)] = createHash("sha256").update(readFileSync(p)).digest("hex"); } };
  walk(dir);
  return out;
}

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", DATA, "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[w11a] PASS ${name}`); } catch (e) { failed = true; console.error(`[w11a] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "?")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };
// an action that asks first, then the person's yes
const confirmed = async (p, body) => {
  const waiting = await api("POST", p, { ...body, locale: "pl" });
  assert.equal(waiting.status, "awaiting_confirmation", JSON.stringify(waiting));
  const done = await api("POST", "/api/actions/confirm", { run_id: waiting.id, approved: true, receipt_id: waiting.pending.receipt_id, ref: "w11a-e2e" });
  assert.equal(done.status, "completed", JSON.stringify(done));
};

try {
  await waitUp(`${base}/api/healthz`);
  await api("PATCH", "/api/config", { base_url: providerBase, semantic_analysis: false, stream: false, ui_language: "pl", default_model: "mock/w11a-model" });
  await api("POST", "/api/secrets/api_key", { value: "local-fake-provider-only" });
  await confirmed("/api/units/define", { kind: "project", id: "alfa", label: "Alfa" });
  await confirmed("/api/units/define", { kind: "project", id: "pusty", label: "Pusty" });
  for (const [title, unit] of [["Rozmowa jeden", "alfa"], ["Rozmowa dwa", "alfa"], ["Rozmowa w pustym", "pusty"]]) {
    const c = await api("POST", "/api/conversations", { title });
    await confirmed("/api/conversation/set_unit", { conv_id: c.id, kind: "project", id: unit });
  }
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true });
  const page = await ctx.newPage();
  trackRequests(page);
  page.on("pageerror", e => errors.push(String(e)));
  dump = async () => "[errors] " + errors.join(" | ") + "\n" + (await page.evaluate(() => document.body.innerText.slice(-900)));
  const shot = name => stableShot(page, path.join(SHOTS, `${name}.png`));
  const back = () => page.evaluate(() => window.__loomBack ? window.__loomBack() : (history.back(), true));
  const say = async (text, answer) => {
    await page.getByTestId("chat-input").fill(text);
    await page.getByTestId("send-chat").click();
    if (answer) await page.waitForFunction(re => new RegExp(re).test(document.body.innerText), answer.source, { timeout: 30000 });
  };
  const win = () => page.locator('[data-testid="project-structure"]:visible').first();
  const structureReads = [];
  page.on("request", r => { if (r.url().endsWith("/api/graph/structure")) structureReads.push(r.postDataJSON()); });
  await page.goto(base + "/");
  await page.getByTestId("chat-input").waitFor();

  await step("1 „pokaż strukturę projektu Alfa”: the window opens on the unit, conversations present, documents as a ghost", async () => {
    await say("pokaż strukturę projektu Alfa");
    await win().waitFor({ timeout: 15000 });
    await win().locator('[data-testid="ps-tree"]').waitFor({ timeout: 15000 });
    assert.equal(await win().getByTestId("ps-root-label").innerText(), "Alfa");
    const conv = win().locator('[data-testid="ps-expectation"][data-ref="unit_basic/conversations"]');
    assert.equal(await conv.getAttribute("data-state"), "present");
    assert.equal(await conv.locator('[data-testid="ps-node"]').count(), 2);
    const ghost = win().locator('[data-testid="ps-ghost"][data-address="unit:project/alfa#expect/unit_basic/documents"]');
    await ghost.waitFor();
    assert.match(await ghost.innerText(), /Dokumenty/);
    assert.match(await ghost.innerText(), /brakuje/);
    const op = await ghost.evaluate(e => getComputedStyle(e).opacity);
    assert.ok(Number(op) < 1, `the preset look (me_inferred_opacity) makes the ghost translucent: ${op}`);
    assert.equal(await win().locator('[data-testid="ps-advisory"]').count(), 0);
    const width = await page.evaluate(() => document.documentElement.scrollWidth);
    assert.ok(width <= 390, `no horizontal scroll at 390 px: ${width}`);
    await shot("1-alfa");
  });

  await step("2 viewing writes nothing: the data dir is identical before and after the re-reads", async () => {
    await page.waitForTimeout(500);
    const before = snapshot(DATA);
    const reads = structureReads.length;
    await win().getByTestId("ps-show-expected").click();
    await page.waitForFunction(() => !document.querySelector('[data-testid="project-structure"] [data-testid="ps-ghost"]'));
    await win().getByTestId("ps-show-expected").click();
    await win().locator('[data-testid="ps-ghost"]').first().waitFor();
    await win().getByTestId("ps-root-input").fill("unit:project/alfa");
    await win().getByTestId("ps-root-go").click();
    await page.waitForTimeout(800);
    assert.ok(structureReads.length >= reads + 2, `graph.structure was read again: ${structureReads.length - reads}`);
    assert.deepEqual(snapshot(DATA), before);
  });

  await step("3 the mapping changes the look without a reload", async () => {
    await page.evaluate(() => { window.__w11aMarker = "same-page"; });
    const ghost = () => win().locator('[data-testid="ps-ghost"]').first();
    await win().getByTestId("ps-mapping").selectOption("dashed");
    await page.waitForFunction(() => document.querySelector('[data-testid="project-structure"]')?.getAttribute("data-mapping") === "dashed");
    assert.equal(await ghost().evaluate(e => getComputedStyle(e).borderTopStyle), "dashed");
    assert.equal(await ghost().evaluate(e => getComputedStyle(e).opacity), "1");
    await win().getByTestId("ps-mapping").selectOption("loud");
    await page.waitForFunction(() => document.querySelector('[data-testid="project-structure"]')?.getAttribute("data-mapping") === "loud");
    assert.equal(await ghost().evaluate(e => getComputedStyle(e).color), "rgb(192, 57, 43)");
    assert.equal(await page.evaluate(() => window.__w11aMarker), "same-page", "no reload");
    await shot("3-loud");
  });

  await step("4 little use: an empty template (root „Pusty”) and a look hiding the missing ones raise advisories; „nie pytaj więcej” is remembered", async () => {
    await win().getByTestId("ps-root-input").fill("unit:project/pusty");
    await win().getByTestId("ps-root-go").click();
    const adv = win().locator('[data-testid="ps-advisory"][data-code="structure_empty_expects"]');
    await adv.waitFor({ timeout: 15000 });
    assert.match(await adv.innerText(), /Pusty wzorzec \(test\)/);
    assert.match(await adv.innerText(), /nie pytaj więcej/);
    await win().getByTestId("ps-mapping").selectOption("hide_missing");
    const hide = win().locator('[data-testid="ps-advisory"][data-code="structure_mapping_nothing_visible"]');
    await hide.waitFor();
    await shot("4-advisories");
    await hide.getByTestId("ps-advisory-dont-ask").click();
    await hide.waitFor({ state: "detached" });
    let acks = [];
    for (let i = 0; i < 40 && !acks.some(a => a.code === "structure_mapping_nothing_visible"); i++) {
      const r = await api("POST", "/api/actions/run", { action: "advisory.list", element: { class: "application" }, args: {} });
      acks = r.result?.data?.acks ?? [];
      if (!acks.some(a => a.code === "structure_mapping_nothing_visible")) await page.waitForTimeout(150);
    }
    assert.ok(acks.some(a => a.code === "structure_mapping_nothing_visible" && a.choice === "dont_ask"), JSON.stringify(acks));
    await adv.getByTestId("ps-advisory-keep").click();
    await adv.waitFor({ state: "detached" });
  });

  await step("5 root and mapping come back after a reload (the last layout restores the window and its values)", async () => {
    await win().getByTestId("ps-root-input").fill("unit:project/alfa");
    await win().getByTestId("ps-root-go").click();
    await win().getByTestId("ps-mapping").selectOption("dashed");
    await page.waitForFunction(() => document.querySelector('[data-testid="project-structure"]')?.getAttribute("data-root") === "unit:project/alfa"
      && document.querySelector('[data-testid="project-structure"]')?.getAttribute("data-mapping") === "dashed");
    await page.waitForTimeout(800);   // the last layout is written after a change settles
    await page.reload();
    await page.getByTestId("chat-input").waitFor();
    if (!(await win().count())) { await page.getByTestId("shell-menu-toggle").click(); await page.getByTestId("shell-menu-structure").click(); }
    await win().locator('[data-testid="ps-tree"]').waitFor({ timeout: 15000 });
    assert.equal(await win().getAttribute("data-root"), "unit:project/alfa");
    assert.equal(await win().getAttribute("data-mapping"), "dashed");
    await shot("5-restored");
  });

  await step("6 from the ⋮ menu with no window open: the suggestions of roots, a tap shows the unit", async () => {
    await back();
    await page.waitForTimeout(300);
    await say("zamknij wszystko poza czatem", /Zamkn|zamkn|Okn/);
    await page.waitForFunction(() => !document.querySelector('[data-testid="project-structure"]'), null, { timeout: 10000 }).catch(() => {});
    await page.getByTestId("shell-menu-toggle").click();
    await page.getByTestId("shell-menu-structure").click();
    await win().waitFor({ timeout: 15000 });
    const root = await win().getAttribute("data-root");
    if (root) {
      // a window kept by the layout comes forward with its root (no new one): the suggestions are shown for an empty root
      await win().getByTestId("ps-root-input").fill("");
      await win().getByTestId("ps-root-go").click();
    }
    const sug = win().locator('[data-testid="ps-suggestion"][data-address="unit:project/alfa"]');
    await sug.waitFor({ timeout: 15000 });
    await sug.click();
    await win().locator('[data-testid="ps-tree"]').waitFor();
    assert.equal(await win().getByTestId("ps-root-label").innerText(), "Alfa");
    await shot("6-menu");
  });
} finally {
  await browser?.close().catch(() => {});
  server.kill();
  provider.close();
}
if (failed) process.exit(1);
console.log(`[w11a] all steps passed; screenshots in ${SHOTS}`);
