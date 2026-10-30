#!/usr/bin/env node
// R101 D5 (PULPIT_R101_2026-10-28 §6, §13 P3/P4, §14 row D5; acceptance, the REAL loom-server, no model). Polish, a phone held sideways
// (844x390) and upright (390x844). (1) drag the first icon („Ty”) to the last column (the trash there takes its cell) → it stays there, desktop_arrangement holds it at both levels
// (this browser installation and the person); (2) turn the phone (viewport 390x844, another device class by width) → the position is
// snapped into the narrower grid (the last of 4 columns), read from the other class (class_fallback), nothing lost; (3) reload → it stays;
// turned back → the last column of the wide grid again; (4) a value at the level `installation` overrides `person`, and without it the
// person value is drawn; (5) „Uporządkuj ikony” › według nazwy → the icons in the order of the Polish alphabet, „Cofnij ułożenie” brings
// back the place before (journal), „Ponów” the arrangement again; (6) the sentence „uporządkuj ikony według rodzaju” (desktop.arrange)
// arranges the icons by kind. The data: a fresh data directory, the shipped preset pins; nothing synthetic beyond the e2e's own writes
// of desktop_arrangement through the API in (4). With M1_SHOTS_DIR=<dir> the screenshots go there.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-desktop-d5-shots-", { evidence: true });
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[desktop-d5] build loom-server and `npm run build` first"); process.exit(2); }

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", makeTmpDir("loom-desktop-d5-"), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[desktop-d5] PASS ${name}`); } catch (e) { failed = true; console.error(`[desktop-d5] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json(); };
const setKey = (key, value) => api("PUT", `/api/config/${key}`, { value });
const getKey = async key => { const cfg = await api("GET", "/api/config"); return (cfg.config ?? cfg)[key]; };
const parse = v => (typeof v === "string" ? JSON.parse(v) : v);
const until = async (what, fn, ms = 10000) => { let v; for (const end = Date.now() + ms; Date.now() < end; await new Promise(r => setTimeout(r, 150))) { v = await fn(); if (v) return v; } throw new Error(`never: ${what} (last ${JSON.stringify(v)})`); };

const WIDE = { width: 844, height: 390 }, TALL = { width: 390, height: 844 };
try {
  await waitUp(`${base}/api/healthz`);
  await setKey("ui_language", "pl");
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  const ctx = await browser.newContext({ viewport: WIDE, hasTouch: false });
  const p = await ctx.newPage();
  p.on("pageerror", e => errors.push(String(e)));
  dump = () => p.evaluate(() => document.body.innerText.slice(-900));
  const desk = () => p.getByTestId("desktop");
  const icon = a => p.locator(`[data-testid="desktop-icon"][data-address="${a}"]`);
  const cell = async a => [Number(await icon(a).getAttribute("data-col")), Number(await icon(a).getAttribute("data-row"))];
  const columns = async () => Number(await p.getByTestId("desktop-icons").getAttribute("data-columns"));
  const toDesktop = async () => {
    // after a reload the page may come back as it was left (the desktop with no window) or with the chat window open
    await p.waitForFunction(() => !!document.querySelector('[data-testid="chat-input"]') || document.querySelector('[data-testid="desktop"]')?.getAttribute("data-cover") === "none", null, { timeout: 20000 });
    if (await p.getByTestId("chat-input").count()) {
      await p.getByTestId("shell-menu-toggle").click();
      await p.getByTestId("shell-menu-windows").click();
      await p.locator('[data-testid="windows-entry"][data-window="w_chat"]').getByTestId("windows-entry-close").click();
    }
    await desk().waitFor({ state: "visible" });
    await p.waitForFunction(() => document.querySelector('[data-testid="desktop-icons"]')?.getAttribute("data-state") === "ok", null, { timeout: 15000 });
    await icon("trash:").waitFor();
  };
  const dragTo = async (a, col, row) => {
    const b = await icon(a).boundingBox(); const g = await p.getByTestId("desktop-icons").boundingBox();
    const cols = await columns(); const rows = Math.max(1, Number(await p.locator('[data-testid="desktop-icon"]').evaluateAll(els => Math.max(...els.map(e => Number(e.getAttribute("data-row")))))) + 1);
    const tx = g.x + (col + 0.5) * (g.width / cols), ty = g.y + (row + 0.5) * (g.height / rows);
    await p.mouse.move(b.x + b.width / 2, b.y + b.height / 2); await p.mouse.down();
    await p.mouse.move(tx, ty, { steps: 12 }); await p.mouse.up();
  };
  const shot = name => p.screenshot({ path: path.join(SHOTS, `d5-${name}.png`) });
  await p.goto(base + "/");
  await p.getByTestId("chat-input").waitFor();
  const inst = await p.evaluate(() => localStorage.getItem("loom.installation_id"));
  let wideCols = 0;

  const M = "party:self";   // the first icon; dragged to the last column (the trash there moves to its cell)
  await step("sideways (844x390): drag the „Ty” icon (party:self, first)  to the last column → it stays, written at the installation and the person level", async () => {
    assert.ok(inst, "the page has an installation id");
    await toDesktop();
    wideCols = await columns();
    assert.ok(wideCols > 4, `the wide grid has more than 4 columns (${wideCols})`);
    assert.equal(await p.getByTestId("desktop-arrange").getAttribute("data-from"), "preset");
    await dragTo(M, wideCols - 1, 0);
    await until("the icon in the last column", async () => JSON.stringify(await cell(M)) === JSON.stringify([wideCols - 1, 0]));
    assert.equal(await icon(M).getAttribute("data-pinned"), "true");
    const v = await until("desktop_arrangement written", async () => { const x = parse(await getKey("desktop_arrangement")); return x?.desktop?.positions?.[M] && x?.installations?.[inst]?.desktop?.positions?.[M] ? x : null; });
    assert.deepEqual([v.desktop.positions[M].col, v.desktop.positions[M].row], [wideCols - 1, 0], "person level");
    assert.deepEqual([v.installations[inst].desktop.positions[M].col, v.installations[inst].desktop.positions[M].row], [wideCols - 1, 0], "installation level");
    assert.equal(await p.getByTestId("desktop-arrange").getAttribute("data-from"), "installation");
    assert.equal(await p.getByTestId("desktop-arrange-undo").isDisabled(), false, "the move is a journal entry");
    await shot("1-wide-moved");
  });
  await step("turned upright (390x844): the position is snapped to the last of 4 columns, nothing lost", async () => {
    await p.setViewportSize(TALL);
    await until("4 columns", async () => (await columns()) === 4);
    await until("the icon snapped", async () => JSON.stringify(await cell(M)) === JSON.stringify([3, 0]));
    assert.equal(await p.getByTestId("desktop-arrange").getAttribute("data-inherited"), "desktop", "read from the other class (class_fallback)");
    assert.equal(await p.locator('[data-testid="desktop-icon"]').count(), 7, "every icon is there");
    const cells = await p.locator('[data-testid="desktop-icon"]').evaluateAll(els => els.map(e => `${e.getAttribute("data-col")},${e.getAttribute("data-row")}`));
    assert.equal(new Set(cells).size, cells.length, "no two icons on one cell");
    await shot("2-tall-snapped");
  });
  await step("reload upright → still snapped; turned back sideways → the last column of the wide grid again", async () => {
    await p.reload(); await toDesktop();
    assert.equal(await columns(), 4);
    assert.deepEqual(await cell(M), [3, 0]);
    await p.setViewportSize(WIDE);
    await until("the wide grid", async () => (await columns()) === wideCols);
    await until("the icon back in the last column", async () => JSON.stringify(await cell(M)) === JSON.stringify([wideCols - 1, 0]));
  });
  await step("the installation level overrides the person level; without it the person value is drawn", async () => {
    const v = parse(await getKey("desktop_arrangement"));
    v.desktop = { positions: { [M]: { col: 0, row: 1 } } };   // another device of the person put it elsewhere (a write through the API)
    await setKey("desktop_arrangement", v);
    await p.reload(); await toDesktop();
    assert.deepEqual(await cell(M), [wideCols - 1, 0], "this browser's own place wins");
    assert.equal(await p.getByTestId("desktop-arrange").getAttribute("data-from"), "installation");
    delete v.installations;
    await setKey("desktop_arrangement", v);
    await p.reload(); await toDesktop();
    assert.deepEqual(await cell(M), [0, 1], "the person value");
    assert.equal(await p.getByTestId("desktop-arrange").getAttribute("data-from"), "person");
  });
  await step("„Uporządkuj ikony” › według nazwy → Polish alphabetical order; undo brings the place before, redo the arrangement", async () => {
    await p.getByTestId("desktop-arrange-toggle").click();
    await p.locator('[data-testid="desktop-arrange-by"][data-by="label"]').click();
    const order = async () => p.locator('[data-testid="desktop-icon"]').evaluateAll(els => els.map(e => ({ l: e.querySelector(".desktop-icon-label")?.textContent ?? "", c: Number(e.getAttribute("data-col")), r: Number(e.getAttribute("data-row")) }))
      .sort((a, b) => a.r - b.r || a.c - b.c).map(x => x.l));
    const want = [...(await order())].sort((a, b) => a.localeCompare(b, "pl", { sensitivity: "base" }));
    await until("alphabetical", async () => JSON.stringify(await order()) === JSON.stringify(want));
    await shot("3-arranged");
    await p.getByTestId("desktop-arrange-undo").click();
    await until("undone", async () => JSON.stringify(await cell(M)) === JSON.stringify([0, 1]));
    await until("undo written", async () => { const x = parse(await getKey("desktop_arrangement")); return Object.keys(x?.desktop?.positions ?? {}).length === 1; });
    await p.getByTestId("desktop-arrange-redo").click();
    await until("redone", async () => JSON.stringify(await order()) === JSON.stringify(want));
  });
  await step("the sentence „uporządkuj ikony według rodzaju” (desktop.arrange) arranges by kind", async () => {
    const before = JSON.stringify(parse(await getKey("desktop_arrangement"))?.desktop?.positions);
    await p.getByTestId("desktop-sentence-input").fill("uporządkuj ikony według rodzaju");
    await p.getByTestId("desktop-sentence-send").click();
    const v = await until("desktop_arrangement rewritten", async () => { const x = parse(await getKey("desktop_arrangement")); const s = JSON.stringify(x?.desktop?.positions); return s && s !== before ? x : null; }, 15000);
    assert.equal(Object.keys(v.desktop.positions).length, 7, "every icon has a place");
    await until("the receipt in the chat", async () => (await p.evaluate(() => document.body.innerText)).includes("Ułożyłem ikony"));
  });
  await step("no page errors", async () => assert.deepEqual(errors, []));
} catch (e) { failed = true; console.error(e); }
finally { await browser?.close().catch(() => {}); server.kill(); }
console.log(failed ? "[desktop-d5] FAILED" : `[desktop-d5] OK (screenshots in ${SHOTS})`);
process.exit(failed ? 1 : 0);
