#!/usr/bin/env node
// M1-JEZYKI: the language of the page is the DEVICE's (R114: a phone set to Polish got English). Against the REAL loom-server with guests
// started automatically (test overlay, no model source needed: only the words are checked) and real Chromium contexts whose `locale` and
// `Accept-Language` differ: guest start and reload must show the device's language; a mismatching first Accept-Language (the thing the
// server used to freeze into the guest's ui_language) must not win; the person's own explicit setting still wins. The expected language is
// computed from the catalogs in the pack (first device tag the catalogs have, else en), so a language added to the data is covered with no
// change here. All profiles and settings are synthetic; catalogs and the server are the product's actual code/data.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { request, stopChild, suiteCompletionGuard } from "./harness-lifecycle.mjs";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[lang] build loom-server and `npm run build` first"); process.exit(2); }
const pack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const shell = pack.entries.find(r => r.key === "presentation.ui_shell").value.locales;
const available = new Set(Object.keys(shell));
const accounts = pack.entries.find(r => r.key === "presentation.accounts").value.locales;
// Static texts (no placeholders) of the guest layer and the store labels, per language: a page in language L must show none of the texts
// of another language M that differ from L's text for the same key (the owner's screenshot: "Store: Przeglądarka — baza SQLite").
const foreignTexts = (L) => { const own = new Set(Object.values(accounts[L] ?? {})); const out = [];
  for (const [M, words] of Object.entries(accounts)) if (M !== L) for (const [k, v] of Object.entries(words)) if ((k.startsWith("guest.") || k.startsWith("store.")) && !v.includes("{{") && v.length > 8 && !own.has(v)) out.push(v);
  return out; };
const expectFor = (tags) => { for (const tag of tags) { const code = tag.toLowerCase().split(/[-_]/)[0]; if (available.has(code)) return code; } return "en"; };

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await request(url, { timeoutMs: 3000 })).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error(`timed out waiting for ${url}`); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const tmp = makeTmpDir("loom-lang-");
const root = path.join(tmp, "root");
mkdirSync(path.join(root, "profiles"), { recursive: true });
writeFileSync(path.join(root, "profiles", "server_accounts.pack"), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain: "server_accounts",
  overrides: { guests: { enabled: true, start: "auto", keys_file: "", sweep_interval_seconds: 0, workdir: { subdir: "guests", idle_ttl_seconds: 600, max_bytes: 0, keep_after_session: "ask" }, protection: { per_session: { chat_per_minute: 1 } } } } }));
writeFileSync(path.join(root, "profiles", "free_providers.pack"), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain: "free_providers",
  overrides: { providers: [], free_model_choice: { rank_source: "table", table: [] } } }));
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--accounts-root", root, "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
console.log(`[lang] own server pid ${server.pid}`);
const base = `http://127.0.0.1:${port}`;
let browser, failed = false;
const groups = [];
const receipt = { choices: [], profilePreference: null };
const completion = suiteCompletionGuard("lang", 9, groups);
const step = async (name, fn) => { if (failed) return; try { await fn(); groups.push(name); console.log(`[lang] PASS ${name}`); } catch (e) { failed = true; console.error(`[lang] FAIL ${name}\n${e.stack}`); } };
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
      const store = page.getByTestId("guest-store");
      await store.waitFor({ timeout: 15000 });   // the guest layer's store block must be there: the mixed-language check below is not optional
      {
        await store.locator("summary").click();
        const seen = await page.locator("body").innerText();
        assert.ok(seen.includes(accounts[want]["guest.store_choice"] ?? accounts.en["guest.store_choice"]), "the store block is in the page language");
        const foreign = foreignTexts(want).filter(t => seen.includes(t));
        assert.deepEqual(foreign, [], `no text of another language on a ${want} page`);
      }
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
  await step("PL device: explicit EN/PL via the settings card persists on disk and survives reload and reopen without changing model settings", async () => {
    const { ctx, page: firstPage } = await visit("pl-PL", ["pl-PL", "pl"], "pl-PL,pl;q=0.9");
    let page = firstPage;
    const config = () => page.evaluate(async () => {
      const r = await fetch("/api/config");
      if (!r.ok) throw new Error(`config read: ${r.status}`);
      const c = await r.json();
      // Only the non-secret settings needed by this assertion leave the page.
      return { ui_language: c.ui_language, system_prompt: c.system_prompt, default_model: c.default_model, language: c.language };
    });
    const languageOnDisk = async () => {
      const id = await page.getByTestId("guest-layer").getAttribute("data-guest-id");
      assert.match(id, /^[a-zA-Z0-9_-]+$/);
      return JSON.parse(readFileSync(path.join(root, "guests", id, "config.json"), "utf8")).ui_language;
    };
    const checkPage = async want => {
      await page.waitForFunction(({ want, placeholder }) => document.documentElement.lang === want
        && document.querySelector('[data-testid="chat-input"]')?.getAttribute("placeholder") === placeholder, { want, placeholder: shell[want].composer_placeholder });
      assert.equal((await shown(page)).placeholder, shell[want].composer_placeholder);
    };
    try {
      await page.goto(base);
      await checkPage("pl");
      assert.equal(await page.evaluate(async () => (await fetch("/api/config", { method: "PATCH", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ language: "en", system_prompt: "Synthetic preference: reply in English." }) })).status), 200);
      const before = await config();
      assert.equal(before.language, "en");
      assert.equal(before.system_prompt, "Synthetic preference: reply in English.");
      assert.ok(before.ui_language === undefined || before.ui_language === "auto" || before.ui_language === "", "a fresh guest has no forced UI locale");
      for (const choice of ["en", "pl"]) {
        await page.getByTestId("shell-menu-toggle").click();
        await page.getByTestId("shell-menu-settings").click();
        await page.getByTestId("shum-group-open-interfejs").click();
        await page.getByTestId("shum-card-btn-ui_language").click();
        await page.getByTestId("card-change").click();
        await page.getByTestId("card-editor-input").fill(choice);
        await page.getByTestId("card-next").click();
        const saved = page.waitForResponse(r => r.url().endsWith("/api/actions/run") && r.request().method() === "POST");
        await page.getByTestId("card-level-person").click();
        assert.equal((await saved).status(), 200, "the existing settings action acknowledged the write");
        await checkPage(choice); // without closing Settings or reloading
        assert.equal((await config()).ui_language, choice, "actual server value, not a local preset");
        const onDisk = await languageOnDisk();
        assert.equal(onDisk, choice, "actual config.json in this synthetic guest's own directory");
        await page.reload();
        await checkPage(choice);
        assert.equal((await config()).ui_language, choice, "reload reads the choice");
        await page.close();
        page = await ctx.newPage(); // same person's cookies/storage; new page and React state
        await page.goto(base);
        await checkPage(choice);
        const after = await config();
        assert.equal(after.ui_language, choice, "reopen reads the choice");
        assert.deepEqual({ ...after, ui_language: undefined }, { ...before, ui_language: undefined }, "UI language does not rewrite model or response settings");
        receipt.choices.push({ choice, onDisk, afterReopen: after.ui_language, html: (await shown(page)).html, language: after.language, systemPromptUnchanged: after.system_prompt === before.system_prompt });
      }
    } finally { await ctx.close(); }
  });
  await step("an acknowledged native profile language preference is restored; an explicit UI language overrides it", async () => {
    const { ctx, page } = await visit("pl-PL", ["pl-PL", "pl"], "pl-PL,pl;q=0.9");
    try {
      await page.goto(base);
      await shown(page);
      const persisted = await page.evaluate(async () => {
        const call = async body => {
          const r = await fetch("/api/onboarding", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
          if (!r.ok) throw new Error(`native profile: ${r.status}`);
          return r.json();
        };
        const user_id = "synthetic-polish-ui-preference";
        const opened = await call({ operation: "open", user_id });
        await call({ operation: "apply", user_id, expected_revision: opened.revision,
          action: { target: "layers", op: "override", key: "preference.language", value: "en", id: "locale-choice", time: "2026-10-31T00:00:00Z", source_refs: [] } });
        const read = await call({ operation: "read", user_id });
        localStorage.setItem("loom.user-profile.identity.v1", JSON.stringify({ schema: "loom.user_profile_identity/1", user_id }));
        return read.snapshot.effectiveDefaults.find(row => row.key === "preference.language");
      });
      assert.equal(persisted.value, "en", "read back the actual native value");
      assert.equal(persisted.layer, "user", "a deliberate user override, not the builtin auto preset");
      receipt.profilePreference = { value: persisted.value, layer: persisted.layer };
      await page.reload();
      await page.waitForFunction(placeholder => document.documentElement.lang === "en"
        && document.querySelector('[data-testid="chat-input"]')?.getAttribute("placeholder") === placeholder, shell.en.composer_placeholder);
      assert.equal((await shown(page)).placeholder, shell.en.composer_placeholder);
      const r = await page.evaluate(async () => (await fetch("/api/config/ui_language", { method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ value: "pl" }) })).status);
      assert.equal(r, 200);
      await page.reload();
      await page.waitForFunction(placeholder => document.documentElement.lang === "pl"
        && document.querySelector('[data-testid="chat-input"]')?.getAttribute("placeholder") === placeholder, shell.pl.composer_placeholder);
      assert.equal((await shown(page)).placeholder, shell.pl.composer_placeholder);
    } finally { await ctx.close(); }
  });
} finally {
  await browser?.close();
  await stopChild(server);
  if (process.env.P0_LOCALE_EVIDENCE) {
    mkdirSync(process.env.P0_LOCALE_EVIDENCE, { recursive: true });
    writeFileSync(path.join(process.env.P0_LOCALE_EVIDENCE, "lang-page-receipt.json"), JSON.stringify({ groups, ...receipt }, null, 2));
  }
}
if (failed) process.exit(1);
completion.complete();
console.log(`[lang] ${groups.length}/9 groups passed`);
