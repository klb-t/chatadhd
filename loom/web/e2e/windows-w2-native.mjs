#!/usr/bin/env node
// W2 of OKNA_SYSTEM (acceptance, phone 390x844, touch, the REAL loom-server, no model): the sentence „otwórz graf obok” in the chat is
// routed by the kernel to the action window.open (a client step the page declared as arranges_windows), the page opens the graph window
// as the next window of the stack and answers with its receipt; Back walks the journal of visits back to the chat; the sheet „Okna”
// (⋮ › Okna) lists both windows; after a reload window_layout_last brings both back; „zamknij wszystko poza czatem” leaves the chat alone.
// With M1_SHOTS_DIR=<dir> the screenshots go there (stable-shot.mjs), otherwise to a fresh temp directory.
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
const SHOTS = process.env.M1_SHOTS_DIR || mkdtempSync(path.join(tmpdir(), "loom-w2-shots-"));
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[windows-w2] build loom-server and `npm run build` first"); process.exit(2); }

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-w2-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[windows-w2] PASS ${name}`); } catch (e) { failed = true; console.error(`[windows-w2] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  const page = await context.newPage();
  trackRequests(page);
  const errors = [];
  page.on("pageerror", e => errors.push(String(e)));
  dump = () => page.evaluate(() => document.body.innerText.slice(-1500));
  const shot = name => stableShot(page, path.join(SHOTS, `${name}.png`));
  const graph = page.getByTestId("panel-navigator");
  const chat = page.getByTestId("chat-shell");
  const windowsSheet = async () => {
    await page.getByTestId("shell-menu-toggle").tap();
    await page.getByTestId("shell-menu-windows").tap();
    await page.getByTestId("windows-sheet").waitFor();
    return page.getByTestId("windows-entry");
  };

  await page.goto(base + "/");
  await page.getByTestId("chat-input").waitFor();

  await step("1 „otwórz graf obok” → the kernel routes it to window.open, the page opens the graph window and answers", async () => {
    await page.getByTestId("chat-input").fill("otwórz graf obok");
    await page.getByTestId("send-chat").tap();
    await graph.waitFor({ timeout: 30000 });
    assert.equal(await page.evaluate(() => document.querySelectorAll('[data-testid="panel-navigator"]').length), 1);
    await shot("w2-1-graph");
  });

  await step("2 Back → the journal of visits brings the chat to the front (the graph waits in the switcher)", async () => {
    const closed = await page.evaluate(() => window.__loomBack ? window.__loomBack() : (history.back(), true));
    assert.ok(closed, "Back closed nothing");
    await graph.waitFor({ state: "detached" });
    await chat.waitFor();
    assert.equal(await chat.getAttribute("inert"), null, "the chat is in front and not inert");
    // the step's receipt reached the conversation (the run continued with the client's answer)
    await page.waitForFunction(() => /Otwarte:/.test(document.body.innerText), null, { timeout: 15000 });
    await shot("w2-2-back-chat");
  });

  await step("3 ⋮ › Okna lists 2 windows: Czat (in front) and the graph", async () => {
    const entries = await windowsSheet();
    assert.equal(await entries.count(), 2);
    assert.equal(await entries.nth(0).getAttribute("data-front"), "true");
    await shot("w2-3-sheet");
    await page.evaluate(() => window.__loomBack && window.__loomBack());
    await page.getByTestId("windows-sheet").waitFor({ state: "detached" });
  });

  await step("4 reload → window_layout_last restores both windows", async () => {
    const saved = await page.evaluate(() => JSON.parse(localStorage.getItem("loom.shell.last.v2") || "null"));
    assert.equal(saved?.schema, "loom.shell.last/2");
    assert.equal(saved?.window_layout_last?.windows?.filter(w => w.params && w.params.panel).length, 1);
    await page.reload();
    await page.getByTestId("chat-input").waitFor();
    const entries = await windowsSheet();
    assert.equal(await entries.count(), 2);
    await shot("w2-4-reload-sheet");
    // a tap on the graph entry brings it to the front
    await entries.nth(1).getByTestId("windows-entry-focus").tap();
    await graph.waitFor();
  });

  await step("5 „zamknij wszystko poza czatem” → only the chat is left", async () => {
    await page.evaluate(() => window.__loomBack && window.__loomBack());
    await graph.waitFor({ state: "detached" });
    await page.getByTestId("chat-input").fill("zamknij wszystko poza czatem");
    await page.getByTestId("send-chat").tap();
    await page.waitForFunction(() => /Zamknięte:/.test(document.body.innerText), null, { timeout: 30000 });
    const entries = await windowsSheet();
    assert.equal(await entries.count(), 1);
    await shot("w2-5-only-chat");
  });

  await step("6 no page errors", async () => { assert.deepEqual(errors, []); });
} catch (e) {
  failed = true;
  console.error(`[windows-w2] FAIL setup\n${e.stack}`);
} finally {
  await browser?.close();
  server.kill();
}
console.log(`[windows-w2] shots in ${SHOTS}`);
process.exit(failed ? 1 : 0);
