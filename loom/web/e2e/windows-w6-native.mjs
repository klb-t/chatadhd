#!/usr/bin/env node
// W6 of OKNA_SYSTEM (acceptance, the REAL loom-server, no model). Desktop 1280x800: ⋮ › Graf docked right of the chat, ⋯ › „Otwórz drugie
// takie okno obok” gives two graph windows. On the second one ⋯ › „Powiąż przybliżenie: ×2 okna «Graf»” → the chain on its title bar says
// it with words; + on the first map zooms the second ×2. Bound back the other way (first ×2 of the second) → the non-blocking chip
// coupling_cycle; „Zostaw, nie pytaj więcej” goes through advisory.answer (code + fingerprint) → no chip, also after a reload (the bindings
// came back with the layout: the chains are still there). ⋯ › „Rozwiąż przybliżenie” on the second → zooming the first leaves it as it is.
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
const SHOTS = process.env.M1_SHOTS_DIR || mkdtempSync(path.join(tmpdir(), "loom-w6-shots-"));
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[windows-w6] build loom-server and `npm run build` first"); process.exit(2); }

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }
const sleep = ms => new Promise(r => setTimeout(r, ms));

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-w6-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[windows-w6] PASS ${name}`); } catch (e) { failed = true; console.error(`[windows-w6] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };
const acks = async () => (await api("POST", "/api/actions/run", { action: "advisory.list", element: { class: "application" }, args: {} }))?.result?.data?.acks ?? [];

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  // the windows as they were left come back in front after a reload (setting window_restore_front; the preset puts the chat in front)
  await api("PUT", "/api/config/window_restore_front", { value: "saved" });
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  const desk = await browser.newContext({ viewport: { width: 1280, height: 800 } });
  const page = await desk.newPage();
  trackRequests(page);
  page.on("pageerror", e => errors.push(String(e)));
  dump = () => page.evaluate(() => document.body.innerText.slice(-1500));
  const shot = name => stableShot(page, path.join(SHOTS, `${name}.png`));
  const chip = code => page.locator(`[data-testid="window-advisory"][data-code="${code}"]`);
  const graphs = page.locator('[data-testid="window-navigator"]');
  const zoomOf = async i => Number(await graphs.nth(i).getByTestId("nav-map").getAttribute("data-zoom"));
  const couple = async (i, entry) => {
    await graphs.nth(i).getByTestId("window-menu-navigator").click();
    const item = graphs.nth(i).locator(`[data-testid="window-couple"][data-entry="${entry}"]`);
    await item.waitFor();
    await item.click();
  };
  const near = (a, b, what) => assert.ok(Math.abs(a - b) < 0.002, `${what}: ${a} vs ${b}`);

  await page.goto(base + "/");
  await page.getByTestId("chat-input").waitFor();
  let g1 = "", g2 = "";

  await step("1 two graph windows: ⋮ › Graf docked right, ⋯ › „Otwórz drugie takie okno obok”", async () => {
    await page.getByTestId("shell-menu-toggle").click(); await page.getByTestId("shell-menu-graph").click();
    await page.getByTestId("panel-navigator").waitFor();
    await page.getByTestId("panel-arrange-menu").click(); await page.getByTestId("panel-arrange-right").click();
    await graphs.first().waitFor();
    await graphs.first().getByTestId("window-menu-navigator").click();
    await page.getByTestId("window-arrange-duplicate").click();
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="window-navigator"] [data-testid="nav-map"]').length === 2, null, { timeout: 15000 });
    g1 = await graphs.nth(0).getAttribute("data-window"); g2 = await graphs.nth(1).getAttribute("data-window");
    assert.ok(g1 && g2 && g1 !== g2, `two windows ${g1} ${g2}`);
    assert.equal(await page.locator('[data-testid="window-chain-navigator"]').count(), 0, "nothing bound: no chain");
  });

  await step("2 the second bound ×2 to the first: the chain says it; + on the first map zooms the second ×2", async () => {
    await couple(1, `bind:zoom:${g1}:scale:2`);
    const chain = graphs.nth(1).getByTestId("window-chain-navigator");
    await chain.waitFor();
    assert.match(await chain.innerText(), /przybliżenie: ×2 okna «Graf»/);
    await page.waitForFunction(() => { const z = [...document.querySelectorAll('[data-testid="window-navigator"] [data-testid="nav-map"]')].map(e => Number(e.dataset.zoom)); return Math.abs(z[1] - 2 * z[0]) < 0.002; }, null, { timeout: 10000 });
    const z0 = await zoomOf(0);
    await graphs.nth(0).getByTestId("nav-map-zoom-in").click();
    await page.waitForFunction(z => Number(document.querySelectorAll('[data-testid="window-navigator"] [data-testid="nav-map"]')[0].dataset.zoom) > z + 0.01, z0);
    await page.waitForFunction(() => { const z = [...document.querySelectorAll('[data-testid="window-navigator"] [data-testid="nav-map"]')].map(e => Number(e.dataset.zoom)); return Math.abs(z[1] - 2 * z[0]) < 0.002; }, null, { timeout: 10000 });
    near(await zoomOf(1), 2 * await zoomOf(0), "second = 2 × first");
    assert.equal(await chip("coupling_cycle").count(), 0, "no loop yet");
    await shot("w6-1-bound-x2");
  });

  let fingerprint = "";
  await step("3 bound back the other way → chip coupling_cycle (blocks nothing); „Zostaw, nie pytaj więcej” remembered by the kernel", async () => {
    await couple(0, `bind:zoom:${g2}:scale:2`);
    await chip("coupling_cycle").waitFor();
    assert.match(await chip("coupling_cycle").innerText(), /w kółko[\s\S]*Czy to celowe\?/);
    fingerprint = await chip("coupling_cycle").getAttribute("data-fingerprint");
    assert.equal(fingerprint, "zoom|scale|navigator");
    await shot("w6-2-cycle-chip");
    await chip("coupling_cycle").getByTestId("window-advisory-dont_ask").click();
    await chip("coupling_cycle").waitFor({ state: "detached" });
    for (let i = 0; i < 50 && !(await acks()).some(a => a.code === "coupling_cycle" && a.fingerprint === fingerprint); i++) await sleep(100);
    assert.ok((await acks()).some(a => a.code === "coupling_cycle" && a.fingerprint === fingerprint), "advisory.answer stored code + fingerprint");
  });

  await step("4 after a reload: the bindings came back with the layout (both chains), the loop asks nothing", async () => {
    await page.reload();
    await page.getByTestId("chat-input").waitFor();
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="window-chain-navigator"]').length === 2, null, { timeout: 15000 });
    await page.waitForResponse(r => r.url().includes("/api/actions/run") && r.request().postData()?.includes("advisory.list"), { timeout: 15000 }).catch(() => {});
    await sleep(500);
    assert.equal(await chip("coupling_cycle").count(), 0, "remembered: no chip");
  });

  await step("5 ⋯ › „Rozwiąż przybliżenie” on the second → independent again: zooming the first leaves the second as it is", async () => {
    await couple(1, "unbind:zoom");
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="window-chain-navigator"]').length === 1);
    const z1 = await zoomOf(1), z0 = await zoomOf(0);
    await graphs.nth(0).getByTestId("nav-map-zoom-in").click();
    await page.waitForFunction(z => Number(document.querySelectorAll('[data-testid="window-navigator"] [data-testid="nav-map"]')[0].dataset.zoom) > z + 0.01, z0);
    await sleep(300);
    near(await zoomOf(1), z1, "the second keeps its zoom");
    await shot("w6-3-unbound");
  });

  await step("6 no page errors", async () => { assert.deepEqual(errors, []); });
} catch (e) {
  failed = true;
  console.error(`[windows-w6] FAIL setup\n${e.stack}`);
} finally {
  await browser?.close();
  server.kill();
}
console.log(`[windows-w6] shots in ${SHOTS}`);
process.exit(failed ? 1 : 0);
