#!/usr/bin/env node
// W4 of OKNA_SYSTEM (acceptance, the REAL loom-server, no model). Phone 390x844, touch: ⋮ › Graf, then ⋯ › „Dokuj z prawej” docks the
// graph next to the chat (nothing refused); both leaves are under the preset min_useful_width, so the non-blocking chip dock_narrow
// asks „Czy to celowe?”; „Zostaw, nie pytaj więcej” is answered through advisory.answer (code + fingerprint, the kernel remembers it);
// docking the same way again asks nothing, also after a reload. Settings ⋯ › „Pływające” floats over the chat; dragged mostly off the
// screen it raises float_offscreen, whose fix snaps it back. Desktop 1280x800: the split edge between chat and graph dragged changes the
// ratio, Ctrl+Shift+Z restores it; the graph's title bar dragged to the left edge of the chat docks it on the left (drop zone shown), undo
// puts it back. With M1_SHOTS_DIR=<dir> the screenshots go there (stable-shot.mjs), otherwise to a fresh temp directory.
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
const SHOTS = process.env.M1_SHOTS_DIR || mkdtempSync(path.join(tmpdir(), "loom-w4-shots-"));
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[windows-w4] build loom-server and `npm run build` first"); process.exit(2); }

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }
const sleep = ms => new Promise(r => setTimeout(r, ms));

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-w4-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[windows-w4] PASS ${name}`); } catch (e) { failed = true; console.error(`[windows-w4] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };
const acks = async () => (await api("POST", "/api/actions/run", { action: "advisory.list", element: { class: "application" }, args: {} }))?.result?.data?.acks ?? [];

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  const phone = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  let page = await phone.newPage();
  trackRequests(page);
  page.on("pageerror", e => errors.push(String(e)));
  dump = () => page.evaluate(() => document.body.innerText.slice(-1500));
  const shot = name => stableShot(page, path.join(SHOTS, `${name}.png`));
  const chip = code => page.locator(`[data-testid="window-advisory"][data-code="${code}"]`);
  const menu = async (entry) => { await page.getByTestId("shell-menu-toggle").tap(); await page.getByTestId(`shell-menu-${entry}`).tap(); };
  const arrangeOpen = async (panel, how) => {
    await page.getByTestId(`panel-${panel}`).waitFor();
    await page.getByTestId("panel-arrange-menu").tap();
    await page.getByTestId(`panel-arrange-${how}`).tap();
    await page.getByTestId(`panel-${panel}`).waitFor({ state: "detached" });
  };
  const box = async loc => (await loc.boundingBox()) ?? { x: 0, y: 0, width: 0, height: 0 };
  const listLoaded = () => page.waitForResponse(r => r.url().includes("/api/actions/run") && r.request().postData()?.includes("advisory.list"), { timeout: 15000 });

  await page.goto(base + "/");
  await page.getByTestId("chat-input").waitFor();

  await step("1 phone: ⋮ › Graf, ⋯ › Dokuj z prawej → graph docked next to the chat, both narrow, chip dock_narrow asks „Czy to celowe?”", async () => {
    await menu("graph");
    await arrangeOpen("navigator", "right");
    const graph = page.getByTestId("window-navigator"), chat = page.getByTestId("chat-shell");
    await graph.waitFor(); await chat.waitFor();
    const g = await box(graph), c = await box(chat);
    assert.ok(g.x >= c.x + c.width - 8, `graph right of the chat: chat ${JSON.stringify(c)} graph ${JSON.stringify(g)}`);
    assert.ok(c.width < 240 && g.width < 240, `both leaves narrow: ${c.width} ${g.width}`);
    await chip("dock_narrow").waitFor();
    assert.match(await chip("dock_narrow").innerText(), /Czy to celowe\?/);
    assert.equal(await chat.getAttribute("inert"), null, "the chip blocks nothing: the chat stays usable");
    await shot("w4-1-dock-narrow");
  });

  let fingerprint = "";
  await step("2 „Zostaw, nie pytaj więcej” → the kernel remembers dock_narrow with the configuration fingerprint", async () => {
    fingerprint = await chip("dock_narrow").getAttribute("data-fingerprint");
    assert.match(fingerprint, /^phone\|/);
    await chip("dock_narrow").getByTestId("window-advisory-dont_ask").tap();
    await chip("dock_narrow").waitFor({ state: "detached" });
    for (let i = 0; i < 40 && !(await acks()).some(a => a.code === "dock_narrow"); i++) await sleep(150);
    const ack = (await acks()).find(a => a.code === "dock_narrow");
    assert.ok(ack, "ack stored"); assert.equal(ack.fingerprint, fingerprint); assert.equal(ack.choice, "dont_ask");
  });

  await step("3 close the graph (chat back full-screen), dock it again → no chip", async () => {
    await page.getByTestId("window-close-navigator").tap();
    await page.getByTestId("window-navigator").waitFor({ state: "detached" });
    await page.getByTestId("dock-root").waitFor({ state: "detached" });
    await menu("graph");
    await arrangeOpen("navigator", "right");
    await page.getByTestId("window-navigator").waitFor();
    await sleep(400);
    assert.equal(await chip("dock_narrow").count(), 0);
    await page.getByTestId("window-close-navigator").tap();
    await page.getByTestId("window-navigator").waitFor({ state: "detached" });
  });

  await step("4 after a reload: docking the same way again asks nothing (the answer is the kernel's)", async () => {
    await page.reload();
    await page.getByTestId("chat-input").waitFor();
    await menu("graph");
    const loaded = listLoaded();   // the page reads the remembered answers when the arrangement first has something to advise
    await arrangeOpen("navigator", "right");
    await loaded;
    await page.getByTestId("window-navigator").waitFor();
    await sleep(400);
    assert.equal(await chip("dock_narrow").count(), 0);
    await shot("w4-4-no-chip-after-reload");
    await page.getByTestId("window-close-navigator").tap();
    await page.getByTestId("window-navigator").waitFor({ state: "detached" });
  });

  await step("5 ⋮ › Ustawienia, ⋯ › Pływające → settings float over the chat (the chat stays in front under it, not inert)", async () => {
    await menu("settings");
    await arrangeOpen("settings", "float");
    const win = page.getByTestId("window-settings");
    await win.waitFor();
    assert.equal(await page.getByTestId("window-float-layer").locator('[data-testid="window-settings"]').count(), 1);
    const w = await box(win), c = await box(page.getByTestId("chat-shell"));
    assert.ok(w.x >= 0 && w.x + w.width <= 390 + 1 && w.y >= 0 && w.y + w.height <= 844 + 1, `inside the screen: ${JSON.stringify(w)}`);
    assert.ok(w.x < c.x + c.width && w.y < c.y + c.height && w.width > 0, "over the chat");
    assert.equal(await page.getByTestId("chat-shell").getAttribute("inert"), null);
    assert.equal(await chip("float_offscreen").count(), 0);
    await shot("w4-5-settings-floating");
  });

  await step("6 the floating window dragged mostly off the screen → float_offscreen; its fix snaps it back", async () => {
    const t = await box(page.getByTestId("window-titlebar-settings"));
    await page.mouse.move(t.x + 30, t.y + t.height / 2);
    await page.mouse.down();
    await page.mouse.move(t.x + 130, t.y + t.height / 2, { steps: 4 });
    await page.mouse.move(t.x + 330, t.y + t.height / 2, { steps: 4 });
    await page.mouse.up();
    await chip("float_offscreen").waitFor();
    const w = await box(page.getByTestId("window-settings"));
    assert.ok(w.x + w.width / 2 > 390, `mostly off the screen: ${JSON.stringify(w)}`);
    await shot("w4-6-offscreen");
    await chip("float_offscreen").getByTestId("window-advisory-fix").tap();
    await chip("float_offscreen").waitFor({ state: "detached" });
    const back = await box(page.getByTestId("window-settings"));
    assert.ok(back.x + back.width <= 391, `snapped back: ${JSON.stringify(back)}`);
    await page.getByTestId("window-close-settings").tap();
    await page.getByTestId("window-settings").waitFor({ state: "detached" });
  });

  await step("7 no page errors on the phone", async () => { assert.deepEqual(errors, []); });
  await phone.close();

  const desk = await browser.newContext({ viewport: { width: 1280, height: 800 } });
  page = await desk.newPage();
  page.on("pageerror", e => errors.push(String(e)));
  dump = () => page.evaluate(() => document.body.innerText.slice(-1500));
  await page.goto(base + "/");
  await page.getByTestId("chat-input").waitFor();

  await step("8 desktop: graph docked right (no chip at 640 px), the split edge dragged → ratio changes; Ctrl+Shift+Z restores it", async () => {
    await page.getByTestId("shell-menu-toggle").click(); await page.getByTestId("shell-menu-graph").click();
    await page.getByTestId("panel-navigator").waitFor();
    await page.getByTestId("panel-arrange-menu").click(); await page.getByTestId("panel-arrange-right").click();
    await page.getByTestId("window-navigator").waitFor();
    assert.equal(await page.locator('[data-testid="window-advisory"]').count(), 0);
    const grip = page.getByTestId("dock-grip-b");
    assert.equal(await grip.getAttribute("aria-valuenow"), "50");
    const g = await box(grip);
    await page.mouse.move(g.x + g.width / 2, g.y + 200);
    await page.mouse.down();
    await page.mouse.move(600, g.y + 200, { steps: 5 });
    await page.mouse.move(384, g.y + 200, { steps: 5 });
    await page.mouse.up();
    const now = Number(await grip.getAttribute("aria-valuenow"));
    assert.ok(now >= 27 && now <= 33, `ratio after the drag ≈ 30: ${now}`);
    const chat = await box(page.getByTestId("chat-shell"));
    assert.ok(chat.width < 450, `the chat narrowed: ${chat.width}`);
    await shot("w4-8-split-dragged");
    await page.keyboard.press("Control+Shift+Z");
    await page.waitForFunction(() => document.querySelector('[data-testid="dock-grip-b"]')?.getAttribute("aria-valuenow") === "50");
  });

  await step("9 desktop: the graph's title bar dragged to the left edge of the chat → drop zone, docked left; undo puts it back", async () => {
    const t = await box(page.getByTestId("window-titlebar-navigator"));
    await page.mouse.move(t.x + 40, t.y + t.height / 2);
    await page.mouse.down();
    await page.mouse.move(t.x - 100, 400, { steps: 5 });
    await page.mouse.move(30, 400, { steps: 5 });
    assert.equal(await page.getByTestId("drop-zone").getAttribute("data-zone"), "left");
    await page.mouse.up();
    await page.getByTestId("drop-zone").waitFor({ state: "detached" });
    const g = await box(page.getByTestId("window-navigator")), c = await box(page.getByTestId("chat-shell"));
    assert.ok(g.x + g.width <= c.x + 8, `graph left of the chat: ${JSON.stringify(g)} ${JSON.stringify(c)}`);
    await shot("w4-9-docked-left");
    await page.keyboard.press("Control+Shift+Z");
    await page.waitForFunction(() => { const g = document.querySelector('[data-testid="window-navigator"]')?.getBoundingClientRect(); const c = document.querySelector('[data-testid="chat-shell"]')?.getBoundingClientRect(); return g && c && g.x >= c.x + c.width - 8; });
  });

  await step("10 no page errors", async () => { assert.deepEqual(errors, []); });
} catch (e) {
  failed = true;
  console.error(`[windows-w4] FAIL setup\n${e.stack}`);
} finally {
  await browser?.close();
  server.kill();
}
console.log(`[windows-w4] shots in ${SHOTS}`);
process.exit(failed ? 1 : 0);
