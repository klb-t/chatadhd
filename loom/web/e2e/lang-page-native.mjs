#!/usr/bin/env node
// M1-JEZYKI: the language of the page is the DEVICE's (R114: a phone set to Polish got English). Against the REAL loom-server with guests
// started automatically (test overlay, no model source needed: only the words are checked) and real Chromium contexts whose `locale` and
// `Accept-Language` differ: guest start and reload must show the device's language; a mismatching first Accept-Language (the thing the
// server used to freeze into the guest's ui_language) must not win; the person's own explicit setting still wins. The expected language is
// computed from the catalogs in the pack (first device tag the catalogs have, else en), so a language added to the data is covered with no
// change here. Data: the real pack words; guests are real server sessions; nothing synthetic.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { request } from "./harness-lifecycle.mjs";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[lang] build loom-server and `npm run build` first"); process.exit(2); }
const pack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const shell = pack.entries.find(r => r.key === "presentation.ui_shell").value.locales;
const available = new Set(Object.keys(shell));
const expectFor = (tags) => { for (const tag of tags) { const code = tag.toLowerCase().split(/[-_]/)[0]; if (available.has(code)) return code; } return "en"; };

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await request(url, { timeoutMs: 3000 })).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error(`timed out waiting for ${url}`); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const tmp = makeTmpDir("loom-lang-");
const root = path.join(tmp, "root");
mkdirSync(path.join(root, "profiles"), { recursive: true });
writeFileSync(path.join(root, "profiles", "server_accounts.pack"), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain: "server_accounts",
  overrides: { guests: { enabled: true, start: "auto", sweep_interval_seconds: 0, workdir: { subdir: "guests", idle_ttl_seconds: 600, max_bytes: 0, keep_after_session: "ask" }, protection: { per_session: { chat_per_minute: 1 } } } } }));
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--accounts-root", root, "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false;
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[lang] PASS ${name}`); } catch (e) { failed = true; console.error(`[lang] FAIL ${name}\n${e.stack}`); } };
const shown = async (page) => {
  const input = page.getByTestId("chat-input");
  await input.waitFor({ timeout: 20000 });
  return { html: await page.evaluate(() => document.documentElement.lang), placeholder: await input.getAttribute("placeholder") };
};
async function visit(locale, languages, acceptLanguage) {
  const ctx = await browser.newContext({ viewport: { width: 420, height: 800 }, locale, ...(acceptLanguage ? { extraHTTPHeaders: { "Accept-Language": acceptLanguage } } : {}) });
  if (languages) await ctx.addInitScript((l) => Object.defineProperty(Navigator.prototype, "languages", { get: () => l }), languages);
  return { ctx, page: await ctx.newPage() };
}
async function check(label, locale, languages, acceptLanguage, want) {
  await step(label, async () => {
    const { ctx, page } = await visit(locale, languages, acceptLanguage);
    try {
      await page.goto(base);
      let s = await shown(page);
      assert.equal(s.html, want, `first visit html lang (${JSON.stringify(s)})`);
      assert.equal(s.placeholder, shell[want].composer_placeholder, "composer words are the device language's catalog");
      await page.reload();
      s = await shown(page);
      assert.equal(s.html, want, `after reload (${JSON.stringify(s)})`);
      assert.equal(s.placeholder, shell[want].composer_placeholder);
    } finally { await ctx.close(); }
  });
}
try {
  await waitUp(`${base}/api/healthz`);
  browser = await (await chromium()).launch({ executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || undefined });
  await check("pl-PL device: guest start and reload in pl", "pl-PL", ["pl-PL", "pl"], "pl-PL,pl;q=0.9", expectFor(["pl-PL"]));
  await check("en-US device: en", "en-US", ["en-US", "en"], "en-US,en;q=0.9", expectFor(["en-US"]));
  await check("nl-NL device: nl when the data has it, else en (computed from the pack)", "nl-NL", ["nl-NL", "nl"], "nl-NL,nl;q=0.9", expectFor(["nl-NL"]));
  await check("pl device whose request header says en first (Accept-Language is not the device list): pl", "pl-PL", ["pl-PL", "en-US"], "en-US,en;q=0.9", "pl");
  await check("device list de, pl, en: the first language the catalogs have (pl)", "de-DE", ["de-DE", "pl", "en"], "de-DE,pl;q=0.8,en;q=0.5", "pl");
  await check("device list with only unknown languages: en", "ja-JP", ["ja-JP"], "ja-JP,ja;q=0.9", "en");
  await step("the person's own setting wins over the device, and 'auto' gives the device back", async () => {
    const { ctx, page } = await visit("en-US", ["en-US"], "en-US,en;q=0.9");
    try {
      await page.goto(base);
      await shown(page);
      const put = (v) => page.evaluate(async (value) => (await fetch("/api/config/ui_language", { method: "PUT", headers: { "Content-Type": "application/json" }, credentials: "same-origin", body: JSON.stringify({ value }) })).status, v);
      assert.equal(await put("pl"), 200);
      await page.reload();
      assert.equal((await shown(page)).html, "pl");
      assert.equal(await put("auto"), 200);
      await page.reload();
      assert.equal((await shown(page)).html, "en");
    } finally { await ctx.close(); }
  });
} finally {
  await browser?.close();
  server.kill();
}
if (failed) process.exit(1);
console.log("[lang] all passed");
