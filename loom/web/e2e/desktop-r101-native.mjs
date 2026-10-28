#!/usr/bin/env node
// R101 P0 (PULPIT_R101_2026-10-28 §7, §10, §12; acceptance, the REAL loom-server, no model). Polish, a computer (1280x800) and a phone
// (390x844). The desktop is always under the windows: (1) close every window → the desktop stays, a window opens from it, a sentence
// written on it goes to the chat composer; (2) minimize all (Ctrl+Shift+D) → the desktop is seen → a minimized window comes back;
// (3) last_window_close=close_app → the end screen with its advisory → back to the desktop; (4) B3: with no section the emergency path
// is there; switched off, the desktop says so and brings it back on request. With M1_SHOTS_DIR=<dir> the screenshots go there.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { stableShot } from "./stable-shot.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const SHOTS = process.env.M1_SHOTS_DIR || mkdtempSync(path.join(tmpdir(), "loom-desktop-shots-"));
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[desktop-r101] build loom-server and `npm run build` first"); process.exit(2); }

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-desktop-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[desktop-r101] PASS ${name}`); } catch (e) { failed = true; console.error(`[desktop-r101] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };
const setKey = (key, value) => api("PUT", `/api/config/${key}`, { value });

try {
  await waitUp(`${base}/api/healthz`);
  await setKey("ui_language", "pl");
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  for (const [device, viewport] of [["computer", { width: 1280, height: 800 }], ["phone", { width: 390, height: 844 }]]) {
    const fresh = async () => {
      const ctx = await browser.newContext({ viewport });
      const p = await ctx.newPage();
      p.on("pageerror", e => errors.push(`${device}: ${e}`));
      dump = () => p.evaluate(() => document.body.innerText.slice(-900));
      await p.goto(base + "/");
      await p.getByTestId("chat-input").waitFor();
      return p;
    };
    const desk = p => p.getByTestId("desktop");
    const seen = async p => { await desk(p).waitFor({ state: "visible" }); assert.notEqual(await desk(p).getAttribute("data-cover"), "full"); };
    const covered = async p => { await desk(p).waitFor({ state: "hidden" }); assert.equal(await desk(p).getAttribute("data-cover"), "full"); };
    const closeChat = async p => {
      await p.getByTestId("shell-menu-toggle").click();
      await p.getByTestId("shell-menu-windows").click();
      await p.locator('[data-testid="windows-entry"][data-window="w_chat"]').getByTestId("windows-entry-close").click();
    };

    let p = await fresh();
    await step(`${device}: the desktop is mounted under the maximized chat, hidden and inert`, async () => {
      assert.equal(await desk(p).count(), 1);
      await covered(p);
    });
    await step(`${device}: close every window → the desktop stays with every kind of window (planned ones greyed with a reason)`, async () => {
      await closeChat(p);
      await seen(p);
      assert.equal(await p.getByTestId("chat-input").count(), 0);
      const kinds = p.getByTestId("desktop-open-kind");
      assert.ok(await kinds.count() >= 25, `kinds on the desktop: ${await kinds.count()}`);
      const planned = p.locator('[data-testid="desktop-open-kind"][data-status="planned"]');
      assert.ok(await planned.count() >= 1);
      assert.ok(await planned.first().isDisabled());
      assert.ok((await planned.first().getAttribute("title") ?? "").length > 5, "a planned kind carries its reason");
      await stableShot(p, path.join(SHOTS, `${device}-desktop-closed.png`));
    });
    await step(`${device}: a window opens from the desktop (Graf), closing it brings the desktop back`, async () => {
      await p.locator('[data-testid="desktop-open-kind"][data-kind="navigator"]').click();
      await covered(p);
      await p.getByTestId("close-panel").click();
      await seen(p);
    });
    await step(`${device}: a sentence on the desktop goes to the chat composer of a chat window opened for it`, async () => {
      await p.getByTestId("desktop-sentence-input").fill("pulpit mówi dzień dobry");
      await p.getByTestId("desktop-sentence-send").click();
      await covered(p);
      await p.getByTestId("chat-input").waitFor();
      await p.waitForFunction(() => document.body.innerText.includes("pulpit mówi dzień dobry") || document.querySelector('[data-testid="chat-input"]')?.value === "pulpit mówi dzień dobry", null, { timeout: 15000 });
    });
    await p.context().close();

    p = await fresh();
    await step(`${device}: minimize all (Ctrl+Shift+D) → the desktop is seen → the minimized chat comes back from it`, async () => {
      await p.locator("body").click({ position: { x: 5, y: Math.floor(viewport.height / 2) } });
      await p.keyboard.press("Control+Shift+D");
      await seen(p);
      const item = p.locator('[data-testid="desktop-restore-item"][data-window="w_chat"]');
      await item.waitFor();
      await stableShot(p, path.join(SHOTS, `${device}-desktop-minimized.png`));
      await item.click();
      await covered(p);
      await p.getByTestId("chat-input").waitFor();
    });
    await p.context().close();

    await setKey("last_window_close", "close_app");
    p = await fresh();
    await step(`${device}: last_window_close=close_app → the end screen with the advisory that the browser may refuse`, async () => {
      await closeChat(p);
      await p.getByTestId("app-ended").waitFor();
      assert.match(await p.getByTestId("app-ended-advisory").innerText(), /przeglądarka może odmówić/i);
      await stableShot(p, path.join(SHOTS, `${device}-ended.png`));
      await p.getByTestId("app-ended-back").click();
      await p.getByTestId("app-ended").waitFor({ state: "detached" });
      await seen(p);
    });
    await p.context().close();
    await setKey("last_window_close", null);

    await setKey("desktop_sections", []);
    p = await fresh();
    await step(`${device}: B3 - no section on the desktop: the emergency path is there and opens a chat window`, async () => {
      await closeChat(p);
      await seen(p);
      assert.equal(await p.getByTestId("desktop-open-kind").count(), 0);
      await p.getByTestId("desktop-emergency").click();
      await covered(p);
      await p.getByTestId("chat-input").waitFor();
    });
    await p.context().close();
    await setKey("desktop_emergency_path", false);
    p = await fresh();
    await step(`${device}: B3 - emergency path switched off: the desktop says there is no way and brings it back on request`, async () => {
      await closeChat(p);
      await seen(p);
      assert.equal(await p.getByTestId("desktop-emergency").count(), 0);
      const adv = p.locator('[data-testid="desktop-advisory"][data-code="desktop_no_path"]');
      await adv.waitFor();
      await p.getByTestId("desktop-advisory-fix").click();
      await p.getByTestId("desktop-emergency").waitFor();
      const cfg = await api("GET", "/api/config"); const c = cfg.config ?? cfg;
      assert.equal(c.desktop_emergency_path, true);
    });
    await p.context().close();
    await setKey("desktop_sections", null);
    await setKey("desktop_emergency_path", null);
  }
  await step("no page errors", async () => assert.deepEqual(errors, []));
} catch (e) { failed = true; console.error(e); }
finally { await browser?.close().catch(() => {}); server.kill(); }
console.log(failed ? "[desktop-r101] FAILED" : `[desktop-r101] OK (screenshots in ${SHOTS})`);
process.exit(failed ? 1 : 0);
