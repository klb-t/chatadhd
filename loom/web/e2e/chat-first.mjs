#!/usr/bin/env node
// P20 (R56): the start screen is a standard chat app. Real loom-server + mock LLM + Chromium, at 390x844 and 1280x800.
import { spawn } from "node:child_process";
import { mkdtempSync, existsSync, mkdirSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import net from "node:net";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { auditPage } from "./m1-audit.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = path.join(here, "screenshots");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
if (!existsSync(SERVER_BIN) || !existsSync(DIST)) { console.error("[chat-first] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const mockPort = await freePort();
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
let server, base = "";
// A fresh data directory (and server) per viewport: the first screen of a new installation.
async function freshServer() {
  server?.kill("SIGTERM");
  const port = await freePort();
  server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-chat-first-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
  base = `http://127.0.0.1:${port}`;
  await waitUp(`${base}/api/healthz`);
}
let browser, failed = false;
const step = async (name, fn) => { try { await fn(); console.log(`[chat-first] PASS ${name}`); } catch (e) { failed = true; console.error(`[chat-first] FAIL ${name}\n${e.stack}`); } };
const api = (m, p, b) => fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined });

try {
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  for (const [name, viewport, shot] of [["phone 390x844", { width: 390, height: 844 }, "phone"], ["desktop 1280x800", { width: 1280, height: 800 }, "desktop"]]) {
    await freshServer();
    const context = await browser.newContext({ viewport });
    const page = await context.newPage();
    const png = (n) => page.screenshot({ path: path.join(SHOTS, `chat-first-${shot}-${n}.png`) });

    await step(`${name}: the start screen is only the chat shell`, async () => {
      await page.goto(base, { waitUntil: "domcontentloaded" });
      await page.getByTestId("chat-shell").waitFor();
      const text = await page.locator("body").innerText();
      for (const forbidden of ["Add application view", "Workflow session snapshots", "Shared selection", "Show view conversations", "Import profile", "Graph reply mode", "Context for next message", "Expert", "Diagnostic details"])
        assert.ok(!text.includes(forbidden), `start screen must not show: ${forbidden}`);
      assert.ok(!/\{"pending"/.test(text), "no diagnostic JSON in the header");
      assert.equal(await page.locator("details").count(), 0, "no native details on the chat screen");
      await page.getByTestId("shell-empty").waitFor();
      assert.equal(await page.getByTestId("shell-starter").count(), 4);
      await page.getByTestId("first-run-card").waitFor();
      await page.getByTestId("shell-status-dot").waitFor();
      await png("01-start");
    });

    await step(`${name}: the first-run card opens Settings at the key; secrets are never typed into the chat`, async () => {
      await page.getByTestId("first-run-open-settings").click();
      await page.getByTestId("secret-api_key").waitFor();
      await page.fill('[data-testid="cfg-base-url"]', `http://127.0.0.1:${mockPort}`); await page.locator('[data-testid="cfg-base-url"]').blur();
      await page.fill('[data-testid="cfg-default-model"]', "mock/e2e-model"); await page.locator('[data-testid="cfg-default-model"]').blur();
      await page.fill('[data-testid="secret-api_key"]', "sk-e2e-mock-key"); await page.click('[data-testid="secret-api_key-save"]');
      for (const end = Date.now() + 10000; Date.now() < end;) { const h = await (await api("GET", "/api/secrets/api_key/has")).json(); if (h.has) break; await page.waitForTimeout(100); }
      await page.click('[data-testid="close-panel"]');
    });

    await step(`${name}: a first message is typed and sent on the first screen`, async () => {
      await page.goto(base, { waitUntil: "domcontentloaded" });
      await page.getByTestId("chat-input").fill("Hello from the first screen");
      await page.getByTestId("send-chat").click();
      await page.locator('[data-testid="message"][data-role="assistant"]').first().waitFor({ timeout: 20000 });
      assert.ok((await page.locator('[data-testid="message"]').count()) >= 2);
      await png("02-conversation");
    });

    // R67: nothing on the chat screen pretends. The model line is the model the kernel used, the connection dot is the real state of the last request,
    // no element carries the planned status, and the developer details (ids, checksums, raw traces) are not on the screen but in Diagnostyka.
    await step(`${name}: R67 - no planned element, the model line equals the kernel's model, no raw JSON on the chat screen`, async () => {
      assert.equal(await page.locator("[data-status='planned'], [data-planned], [data-testid$='-planned'], [data-testid*='planned']").count(), 0, "no element on the chat screen carries the planned status");
      const text = await page.locator("body").innerText();
      assert.ok(!/\bplanowane\b|\bplanned\b/i.test(text), "the chat screen does not list planned things");
      assert.ok(!/[{]\s*"[a-z_]+"\s*:/.test(text), "no raw JSON visible on the chat screen");
      assert.ok(!/\b(m|c)_[0-9a-f]{12}\b|\bsha256\b/i.test(text), "no developer ids or checksums on the chat screen");
      assert.equal(await page.getByTestId("chat-route-trace").count(), 0, "the route trace is not on the chat screen");
      const convs = await (await api("GET", "/api/conversations?limit=5")).json();
      const msgs = await (await api("GET", `/api/conversations/${convs[0].id}/messages`)).json();
      const used = msgs.filter(m => m.role === "assistant").at(-1)?.model;
      assert.equal(used, "mock/e2e-model", "the kernel used the model of the setting");
      const chip = page.getByTestId("model-chip");
      await chip.waitFor();
      assert.equal(await chip.getAttribute("data-model"), used, "the model line shows the model the kernel used");
      assert.equal(await page.getByTestId("model-chip-status").getAttribute("data-state"), "ok", "the connection dot shows the real state of the last request");
    });

    await step(`${name}: R67 - Diagnostyka (menu) holds the developer details, in Polish, off the chat screen`, async () => {
      await page.getByTestId("shell-menu-toggle").click();
      await page.getByTestId("shell-menu-diagnostics").click();
      await page.getByTestId("sheet-diagnostics").waitFor();
      assert.match(await page.getByTestId("sheet-diagnostics").locator("h2").innerText(), /Diagnostyka|Diagnostics/);
      await page.locator("[data-testid=diagnostics], [data-testid=diag-no-conv], [data-testid=diag-failed]").first().waitFor();
      assert.equal(await page.getByTestId("diag-failed").count(), 0, "the conversation is readable");
      await page.getByRole("button", { name: /×|Zamknij|Close/ }).first().click();
      await page.getByTestId("sheet-diagnostics").waitFor({ state: "detached" });
    });

    await step(`${name}: reload returns to the last conversation`, async () => {
      const before = await page.locator('[data-testid="message"]').count();
      await page.reload({ waitUntil: "domcontentloaded" });
      await page.locator('[data-testid="message"]').first().waitFor({ timeout: 10000 });
      assert.equal(await page.locator('[data-testid="message"]').count(), before);
      assert.match(await page.getByTestId("shell-title").innerText(), /\S/);
    });

    await step(`${name}: the drawer lists conversations and opens with a rotating chevron-free menu button`, async () => {
      await page.getByTestId("shell-drawer-toggle").click();
      await page.getByTestId("shell-drawer").waitFor();
      assert.ok((await page.getByTestId("shell-conv").count()) >= 1);
      await png("03-drawer");
      await page.getByTestId("shell-new-chat").click();
      await page.getByTestId("shell-empty").waitFor();
      await page.getByTestId("shell-drawer-toggle").click();
      await page.getByTestId("shell-conv").first().click();
      await page.locator('[data-testid="message"]').first().waitFor();
    });

    await step(`${name}: tapping a disclosure shows its content in the viewport and changes aria and label at once`, async () => {
      const first = page.locator('[data-testid="message"]').first();
      await first.hover(); await first.locator('[data-testid="msg-exclude"]').click({ force: true }).catch(async () => { await first.dispatchEvent("contextmenu"); await first.locator('[data-testid="msg-exclude"]').click(); });
      const disc = page.getByTestId("shell-excluded");
      await disc.waitFor();
      const head = disc.locator(".disclosure-head");
      assert.equal(await head.getAttribute("aria-expanded"), "false");
      const box = await head.boundingBox();
      assert.ok(box.height >= 44 && box.width >= 44, "touch target 44x44");
      const label = await head.innerText();
      await head.click();
      assert.equal(await head.getAttribute("aria-expanded"), "true");
      assert.notEqual(await head.innerText(), label, "the label changes");
      const inner = disc.locator("p");
      await inner.waitFor({ state: "visible" });
      const r = await inner.boundingBox();
      assert.ok(r.y >= 0 && r.y + r.height <= viewport.height, "the content is inside the viewport");
      await png("04-disclosure");
      await head.click();
      assert.equal(await head.getAttribute("aria-expanded"), "false");
    });

    await step(`${name}: the overflow menu opens Warsztat and the old workbench appears`, async () => {
      await page.getByTestId("shell-menu-toggle").click();
      for (const id of ["graph", "settings", "me", "this_conversation", "views", "semantic", "workshop"]) await page.getByTestId(`shell-menu-${id}`).waitFor();
      for (const id of ["memory", "knowledge"]) assert.equal(await page.getByTestId(`shell-menu-${id}`).count(), 0, `no ${id} in the menu (M1-W0)`);
      const rows = await page.getByRole("menuitem").evaluateAll(els => els.map(el => el.dataset.testid.replace("shell-menu-", "")));
      assert.deepEqual(rows.slice(0, 4), ["graph", "settings", "me", "this_conversation"]);
      await png("05-menu");
      await page.getByTestId("shell-menu-workshop").click();
      await page.getByTestId("add-profile-view").waitFor();
      await png("06-workshop");
      await page.getByTestId("toggle-nav").click().catch(() => {});
      await page.getByTestId("nav-chat").click();
      await page.getByTestId("chat-shell").waitFor();
    });

    await step(`${name}: Back and Escape close the layers in order; at the root Back falls through (window.__loomBack, popstate)`, async () => {
      const back = () => page.evaluate(() => window.__loomBack());
      const layers = () => page.evaluate(() => window.__loomLayers.ids);
      assert.equal(await back(), false, "nothing open: Back is not ours");
      // two layers: the ⋮ menu, then the conversation drawer opened over it (the header stays tappable); Back closes the drawer first
      await page.getByTestId("shell-menu-toggle").click();
      await page.getByTestId("shell-menu").waitFor();
      await page.getByTestId("shell-drawer-toggle").click();
      await page.getByTestId("shell-drawer").waitFor();
      assert.deepEqual(await layers(), ["shell-menu", "shell-drawer"]);
      assert.equal(await back(), true);
      await page.getByTestId("shell-drawer").waitFor({ state: "detached" });
      await page.getByTestId("shell-menu").waitFor();
      assert.equal(await back(), true);
      await page.getByTestId("shell-menu").waitFor({ state: "detached" });
      assert.equal(await back(), false, "the root again");
      // a panel opened from the menu is one layer (the menu's own entry is reused); the browser's Back closes it
      await page.getByTestId("shell-menu-toggle").click();
      await page.getByTestId("shell-menu-settings").click();
      await page.getByTestId("panel-settings").waitFor();
      assert.deepEqual(await layers(), ["panel"]);
      await page.waitForTimeout(150);
      assert.equal(await page.getByTestId("panel-settings").isVisible(), true, "the panel is not undone by the menu's history entry");
      // a panel is a modal dialog: the shell behind it is inert and Tab / Shift+Tab never leave the panel (they wrap inside it)
      assert.equal(await page.getByTestId("chat-shell").getAttribute("inert"), "", "the chat shell behind an open panel is inert");
      const focusHome = async () => page.evaluate(() => {
        const a = document.activeElement;
        return a?.closest('[data-testid="panel-settings"]') ? "panel" : `${a?.tagName ?? "?"}#${a?.getAttribute("data-testid") ?? ""}`;
      });
      for (let i = 0; i < 40; i++) { await page.keyboard.press("Tab"); assert.equal(await focusHome(), "panel", `Tab ${i + 1} stays in the panel`); }
      for (let i = 0; i < 12; i++) { await page.keyboard.press("Shift+Tab"); assert.equal(await focusHome(), "panel", `Shift+Tab ${i + 1} stays in the panel`); }
      await page.goBack();
      await page.getByTestId("panel-settings").waitFor({ state: "detached" });
      assert.deepEqual(await layers(), []);
      // Escape does the same as Back
      await page.getByTestId("shell-menu-toggle").click();
      await page.getByTestId("shell-menu-views").click();
      await page.getByTestId("sheet-views").waitFor();
      assert.equal(await page.getByTestId("shell-bar").getAttribute("inert"), "", "the bar behind an open sheet is inert");
      assert.equal(await page.locator(".shell-main").getAttribute("inert"), "", "the chat behind an open sheet is inert");
      await page.keyboard.press("Escape");
      await page.getByTestId("sheet-views").waitFor({ state: "detached" });
      // closing by the × button gives the entry back: Back then does not land on a closed layer
      await page.getByTestId("shell-menu-toggle").click();
      await page.getByTestId("shell-menu-this_conversation").click();   // layouts.json: navigator:conv:current → the navigator panel on the open conversation
      await page.getByTestId("panel-navigator").waitFor();
      assert.match(await page.getByTestId("graph-navigator").getAttribute("data-focus"), /^conv:/, "focus = the open conversation (conv:<id>)");
      await page.getByTestId("close-panel").click();
      await page.getByTestId("panel-navigator").waitFor({ state: "detached" });
      await page.waitForFunction(() => window.__loomLayers.size === 0 && (history.state?.loomDepth ?? 0) === 0);
      assert.equal(await back(), false);
    });

    await step(`${name}: Polish labels follow the setting`, async () => {
      await api("PUT", "/api/config/ui_language", { value: "pl" });
      await page.reload({ waitUntil: "domcontentloaded" });
      await page.getByTestId("shell-menu-toggle").click();
      assert.match(await page.getByTestId("shell-menu-workshop").innerText(), /Warsztat/);
      assert.deepEqual(await page.getByRole("menuitem").allInnerTexts(), ["Graf", "Ustawienia", "Ty", "Ta rozmowa w grafie", "Okna", "Układ ▾", "Widoki i okna", "Analiza w tle", "Diagnostyka", "Warsztat (narzędzia zaawansowane)"]);
      // R67: the text audit of Diagnostyka (the explanatory words are Polish; its ids, checksums and raw traces are marked as technical detail)
      await page.getByTestId("shell-menu-diagnostics").click();
      await page.locator("[data-testid=diagnostics]").waitFor();
      assert.match(await page.getByTestId("sheet-diagnostics").locator("h2").innerText(), /Diagnostyka/);
      const bad = (await auditPage(page)).filter(b => !String(b.where).includes("shell-title"));   // the title of the conversation is the person's own first sentence, content and not words of the app
      assert.deepEqual(bad, [], "the Polish Diagnostyka sheet shows no developer text: " + JSON.stringify(bad));
      await api("PUT", "/api/config/ui_language", { value: "auto" });
    });
    await context.close();
  }
} finally {
  await browser?.close(); server?.kill("SIGTERM"); mock.kill("SIGTERM");
}
if (failed) process.exit(1);
console.log("[chat-first] PASS all");
