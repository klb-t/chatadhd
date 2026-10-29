#!/usr/bin/env node
// M1-FLAKE1: a LATE popstate of our own history.back() must not close a layer that was opened after it (phone 390x844, touch, the REAL
// loom-server, no model, no network). Evidence from the android-smoke runs: after ⋮ > Ustawienia the settings panel did not stay open,
// at random steps and profiles - the popstate of an earlier own back arrived later than backTimeoutMs on a loaded WebView, was read as
// the person's Back and closed the layers opened since.
// A fast desktop never delivers a popstate that late, so a test-only shim (page.addInitScript) wraps history.back: the real traversal
// runs M1_HISTORY_DELAY_MS (preset 2000, longer than the stack's backTimeoutMs of 1000) after the call, which makes the race reachable
// on purpose. The loop is the driver's shape, N times (M1_HISTORY_ITERATIONS, preset 30):
//   m1Reset = window.__loomBack() up to 8 times; ⋮ (JS click); Ustawienia (JS click); the panel and the groups (shum-groups);
//   then, while the delayed back of the previous round is STILL unanswered and already older than the wait (a layer change after the
//   wait is what used to drop it), Rozmowa opens as a second layer; the panel and the group must STAY open until every delayed back
//   has been answered and 1.5 s have passed (a condition polled every frame, not a sleep and a read); then Back twice.
// Every wait is a condition. M1_HISTORY_DIST=<dir> serves another build (how the failure on the old history.ts was shown).
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
const DIST = process.env.M1_HISTORY_DIST || path.join(LOOM, "web/dist");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const ITERATIONS = Number(process.env.M1_HISTORY_ITERATIONS || 30);
const DELAY_MS = Number(process.env.M1_HISTORY_DELAY_MS || 2000);
const WAIT_PASSED_MS = 1200;   // the stack's backTimeoutMs preset is 1000: a layer change after this is a change after the wait
const STAY_MS = 1500;
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[m1-history] build loom-server and `npm run build` first"); process.exit(2); }

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", makeTmpDir("loom-m1-history-"), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false;
const step = async (name, fn) => { try { await fn(); console.log(`[m1-history] PASS ${name}`); } catch (e) { failed = true; console.error(`[m1-history] FAIL ${name}\n${e.stack}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  const page = await context.newPage();
  const errors = [];
  page.on("pageerror", e => errors.push(String(e)));
  // TEST-ONLY: the traversal of every history.back() of the page runs `delay` ms after the call (the slow WebView of the evidence).
  await page.addInitScript(({ delay }) => {
    const late = { issued: 0, popstates: 0, lastIssuedAt: 0 };
    window.__lateBacks = late;
    const original = History.prototype.back;
    window.addEventListener("popstate", () => { late.popstates += 1; }, true);
    history.back = function () {
      late.issued += 1; late.lastIssuedAt = performance.now();
      setTimeout(() => original.call(history), delay);
    };
  }, { delay: DELAY_MS });
  const click = testid => page.evaluate(id => { const el = document.querySelector(`[data-testid="${id}"]`); if (!el) throw new Error(`no ${id}`); el.click(); }, testid);
  const m1Reset = () => page.evaluate(() => { for (let i = 0; i < 8 && window.__loomBack(); i += 1); return window.__loomLayers.size; });
  const back = () => page.evaluate(() => window.__loomBack());

  await page.goto(base + "/");
  await page.getByTestId("chat-shell").waitFor();

  await step(`${ITERATIONS} rounds of m1Reset > ⋮ > Ustawienia > Rozmowa, the history.back() delayed by ${DELAY_MS} ms: the layers stay open until the late popstates have come, then Back`, async () => {
    for (let round = 1; round <= ITERATIONS; round += 1) {
      const at = `round ${round}`;
      assert.equal(await m1Reset(), 0, `${at}: m1Reset leaves no layer`);
      await click("shell-menu-toggle");
      await page.getByTestId("shell-menu").waitFor();
      await click("shell-menu-settings");
      await page.getByTestId("shum-groups").waitFor();
      // the previous round's back is still unanswered (the shim holds it for DELAY_MS): let the stack's wait pass, then change the layers
      await page.waitForFunction(({ passed }) => { const l = window.__lateBacks; return l.issued === l.popstates || performance.now() - l.lastIssuedAt >= passed; }, { passed: WAIT_PASSED_MS }, { polling: "raf", timeout: 10000 });
      await click("shum-group-open-rozmowa");
      await page.getByTestId("shum-group-view-rozmowa").waitFor();
      // the condition fails the round when the group or the panel is gone at ANY frame; it holds when every delayed back has been
      // answered and STAY_MS have passed since the group opened
      const verdict = await page.evaluate(({ stay }) => new Promise(resolve => {
        const t0 = performance.now();
        const tick = () => {
          const group = document.querySelector('[data-testid="shum-group-view-rozmowa"]'), panel = document.querySelector('[data-testid="panel-settings"]');
          const l = window.__lateBacks;
          if (!group || !panel) return resolve({ gone: !panel ? "panel" : "group", after: Math.round(performance.now() - t0), size: window.__loomLayers.size, issued: l.issued, popstates: l.popstates });
          if (l.issued === l.popstates && performance.now() - t0 >= stay) return resolve({ gone: null, after: Math.round(performance.now() - t0) });
          requestAnimationFrame(tick);
        };
        tick();
      }), { stay: STAY_MS });
      assert.equal(verdict.gone, null, `${at}: the ${verdict.gone} closed by itself ${verdict.after} ms after Rozmowa opened (late popstate read as the person's Back): ${JSON.stringify(verdict)}`);
      assert.equal(await page.evaluate(() => window.__loomLayers.size), 2, `${at}: panel + group are the two open layers`);
      assert.equal(await back(), true, `${at}: Back closes the group`);
      await page.getByTestId("shum-group-view-rozmowa").waitFor({ state: "detached" });
      assert.equal(await back(), true, `${at}: Back closes the panel`);
      await page.getByTestId("shum-groups").waitFor({ state: "detached" });
    }
  });

  await step("after the last round every delayed back has been answered and the history stands on the base with no layer open", async () => {
    await page.waitForFunction(() => window.__lateBacks.issued === window.__lateBacks.popstates && window.__loomLayers.size === 0, null, { polling: "raf", timeout: 15000 });
    assert.equal(await page.evaluate(() => (history.state && history.state.loomDepth) || 0), 0, "history entry of depth 0");
    assert.equal(await back(), false, "at the root Back falls through (nothing to close)");
  });

  assert.deepEqual(errors, [], "no page errors");
} finally {
  await browser?.close();
  server.kill();
}
if (failed) process.exit(1);
