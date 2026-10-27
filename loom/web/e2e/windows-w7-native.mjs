#!/usr/bin/env node
// W7 of OKNA_SYSTEM (acceptance, the REAL loom-server, no model). Desktop 1280x800, Polish. Sentences reach the window actions through the
// kernel's router and only this page carries them out: „zmaksymalizuj ustawienia” (settings in front, the reader of the screen hears
// „Okno: Ustawienia”), „zadokuj graf po lewej” (docked left of the chat), a key of keymap.json (Ctrl+Shift+F floats the focused window,
// Ctrl+Shift+Z undoes), „powiąż przybliżenie obu grafów” (two graph windows share the zoom), „usuń układ praca” (a proposal first, „tak”
// deletes). With M1_SHOTS_DIR=<dir> the screenshots go there (stable-shot.mjs), otherwise to a fresh temp directory.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { stableShot, trackRequests } from "./stable-shot.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const SHOTS = process.env.M1_SHOTS_DIR || mkdtempSync(path.join(tmpdir(), "loom-w7-shots-"));
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[windows-w7] build loom-server and `npm run build` first"); process.exit(2); }

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }
const sleep = ms => new Promise(r => setTimeout(r, ms));

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-w7-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[windows-w7] PASS ${name}`); } catch (e) { failed = true; console.error(`[windows-w7] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };
const acks = async () => (await api("POST", "/api/actions/run", { action: "advisory.list", element: { class: "application" }, args: {} }))?.result?.data?.acks ?? [];
const config = async () => { const c = await api("GET", "/api/config"); return c.config ?? c; };
const ownLayouts = async () => { const v = (await config()).window_layouts; const o = typeof v === "string" ? JSON.parse(v || "{}") : (v ?? {}); return Object.values(o).filter(Boolean).map(x => x.label); };

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  const desk = await browser.newContext({ viewport: { width: 1280, height: 800 } });
  const page = await desk.newPage();
  trackRequests(page);
  page.on("pageerror", e => errors.push(String(e)));
  dump = () => page.evaluate(() => "[announce] " + (document.querySelector('[data-testid="window-announce"]')?.textContent ?? "none") + "\n" + document.body.innerText.slice(-700));
  const shot = name => stableShot(page, path.join(SHOTS, `${name}.png`));
  const graphs = page.locator('[data-testid="window-navigator"]');
  const zooms = () => page.evaluate(() => [...document.querySelectorAll('[data-testid="window-navigator"] [data-testid="nav-map"]')].map(e => Number(e.dataset.zoom)));
  const say = async (text, answer) => {
    await page.getByTestId("chat-input").fill(text);
    await page.getByTestId("send-chat").click();
    if (answer) await page.waitForFunction(re => new RegExp(re).test(document.body.innerText), answer.source, { timeout: 30000 });
  };

  await page.goto(base + "/");
  await page.getByTestId("chat-input").waitFor();

  await step("1 „zmaksymalizuj ustawienia” by a sentence: the settings come in front, the receipt says it", async () => {
    await say("zmaksymalizuj ustawienia");
    await page.getByTestId("panel-settings").waitFor({ timeout: 15000 });
    await page.waitForFunction(() => /Okno: Ustawienia/.test(document.querySelector('[data-testid="window-announce"]')?.textContent ?? ""), null, { timeout: 10000 });
    await shot("w7-1-maximized-settings");
    await page.keyboard.press("Escape");
    await page.getByTestId("panel-settings").waitFor({ state: "detached" });
    await page.waitForFunction(() => /Ułożone: Ustawienia \(maximize\)/.test(document.body.innerText), null, { timeout: 10000 });
  });

  await step("2 „zadokuj graf po lewej”: the graph is docked left of the chat", async () => {
    await say("zadokuj graf po lewej", /Ułożone: Graf \(left\)/);
    await graphs.first().waitFor({ timeout: 15000 });
    assert.equal(await graphs.first().getAttribute("data-mode"), "docked");
    const g = await graphs.first().boundingBox(), c = await page.getByTestId("chat-input").boundingBox();
    assert.ok(g && c && g.x + g.width <= c.x + 2, `graph ${JSON.stringify(g)} left of the chat input ${JSON.stringify(c)}`);
    await shot("w7-2-docked-left");
  });

  await step("3 a key of keymap.json: Ctrl+Shift+F floats the focused graph, Ctrl+Shift+Z takes it back", async () => {
    await graphs.first().getByTestId("window-titlebar-navigator").click({ position: { x: 20, y: 10 } });
    await page.keyboard.press("Control+Shift+F");
    await page.waitForFunction(() => document.querySelector('[data-testid="window-navigator"]')?.getAttribute("data-mode") === "floating", null, { timeout: 10000 });
    await page.keyboard.press("Control+Shift+Z");
    await page.waitForFunction(() => document.querySelector('[data-testid="window-navigator"]')?.getAttribute("data-mode") === "docked", null, { timeout: 10000 });
  });

  await step("4 „powiąż przybliżenie obu grafów”: both graphs share the zoom - + on the first zooms the second the same", async () => {
    await graphs.first().getByTestId("window-menu-navigator").click();
    await page.getByTestId("window-arrange-duplicate").click();
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="window-navigator"] [data-testid="nav-map"]').length === 2, null, { timeout: 15000 });
    await say("powiąż przybliżenie obu grafów", /Powiązane, przybliżenie: Graf, Graf 2/);
    await page.waitForFunction(() => document.querySelectorAll('[data-testid^="window-chain-navigator"]').length === 2, null, { timeout: 10000 });
    const [z0] = await zooms();
    await graphs.nth(0).getByTestId("nav-map-zoom-in").click();
    await page.waitForFunction(z => { const v = [...document.querySelectorAll('[data-testid="window-navigator"] [data-testid="nav-map"]')].map(e => Number(e.dataset.zoom)); return v[0] > z + 0.01 && Math.abs(v[1] - v[0]) < 0.002; }, z0, { timeout: 10000 });
    await shot("w7-3-bound-zoom");
  });

  await step("5 „usuń układ praca” asks first; „tak” deletes it", async () => {
    await say("zapisz układ jako praca", /Zapisany układ: praca/);
    for (let i = 0; i < 50 && !(await ownLayouts()).includes("praca"); i++) await sleep(100);
    assert.ok((await ownLayouts()).includes("praca"), "saved");
    await say("usuń układ praca");
    await sleep(1500);
    assert.ok(!/Usunięty układ: praca/.test(await page.evaluate(() => document.body.innerText)), "nothing deleted before the yes");
    assert.ok((await ownLayouts()).includes("praca"), "still there before the yes");
    await shot("w7-4-delete-asks");
    await say("tak", /Usunięty układ: praca/);
    for (let i = 0; i < 50 && (await ownLayouts()).includes("praca"); i++) await sleep(100);
    assert.ok(!(await ownLayouts()).includes("praca"), "deleted after the yes");
  });

  await step("6 no page errors", async () => { assert.deepEqual(errors, []); });
} catch (e) {
  failed = true;
  console.error(`[windows-w7] FAIL setup\n${e.stack}`);
} finally {
  await browser?.close();
  server.kill();
}
console.log(`[windows-w7] shots in ${SHOTS}`);
process.exit(failed ? 1 : 0);
