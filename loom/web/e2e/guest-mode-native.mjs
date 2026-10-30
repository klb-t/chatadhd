#!/usr/bin/env node
// R103 B1 / R105: use without an account, against the REAL loom-server in accounts mode with guests switched on by a
// TEST overlay (<accounts_root>/profiles; the preset stays off) and a mock free provider (mock_llm_server.py; its
// key comes from a test keys file, in memory only). Steps: the first visit shows the full interface with no gate (no
// sign-in form, a guest session started by itself) and says which free model answers -> a question -> the answer of
// the mock free model -> one notice after the first answer whose period is computed from what the browser store
// reported (pack words, pl) -> the history bundle is in IndexedDB -> the session is closed (cookies cleared) and the
// idle TTL passes: the server's working directory is gone -> a new visit is a new guest whose working copy is filled
// back from the browser store: the conversation and its graph come back. Every word is taken from the pack.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { request } from "./harness-lifecycle.mjs";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-guest-shots-", { evidence: true });
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[guest] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });
const viewPack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const locales = viewPack.entries.find(r => r.key === "presentation.accounts").value.locales;
// The accounts texts follow the app's presentation locale (today its default); the test reads the words of whichever
// pack locale the page shows, so it checks the data, not one language.
let words = locales.pl;
const say = (id, params = {}) => { assert.ok(typeof words[id] === "string", `the pack has the word ${id}`); return words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? ""); };
const TTL = 10;  // margin for a loaded gate machine: a guest must not idle out between two steps
const FAKE_KEY = "sk-guest-e2e-not-a-real-key";

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await request(url, { timeoutMs: 3000 })).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error(`timed out waiting for ${url}`); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }
async function until(what, check, ms = 20000) { for (const end = Date.now() + ms; Date.now() < end;) { const v = await check(); if (v) return v; await new Promise(r => setTimeout(r, 150)); } throw new Error(`timed out waiting for ${what}`); }

const mockPort = await freePort(), port = await freePort();
const tmp = makeTmpDir("loom-guest-");
const root = path.join(tmp, "root");
mkdirSync(path.join(root, "profiles"), { recursive: true });
writeFileSync(path.join(tmp, "keys.env"), `MOCK_FREE_KEY=${FAKE_KEY}\n`);
const overlay = (domain, overrides) => writeFileSync(path.join(root, "profiles", `${domain}.pack`), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain, overrides }));
overlay("server_accounts", {
  allow_private_hosts: ["127.0.0.1"],
  guests: { enabled: true, start: "auto", sweep_interval_seconds: 0, keys_file: path.join(tmp, "keys.env"),
            first_answer_notice: { on: true, ask_again: "per_device" },
            workdir: { subdir: "guests", idle_ttl_seconds: TTL, max_bytes: 0, keep_after_session: "ask" },
            secrets_routes: "write_only_session", model_sources: ["byok", "free", "local"],
            // R103 B2: the proof of work stays at its preset (solved by the page in a Web Worker); one model request per
            // minute so the second question meets the limit.
            protection: { per_session: { chat_per_minute: 1 } } },
});
overlay("free_providers", {
  providers: [{ id: "mock_free", display_name: "Mock darmowy", base_url: `http://127.0.0.1:${mockPort}`, auth_env: "MOCK_FREE_KEY", auth_secret: "api_key",
                tier: "free", active_when: "key_present", enabled: true,
                // R103 B3: retention other than "none…" -> the explicit tap before the first send (§4.3) is part of the path.
                data_terms: { retention: "per_test_terms", human_review: false, training: "no", observed_at: "2026-10-28", source_url: "test", source_rating: "test",
                              summary: { pl: "Testowy dostawca nic nie zapisuje.", en: "The test provider keeps nothing." } } }],
  free_model_choice: { rank_source: "table", require: { training: "no" }, table: [{ model: "mock/e2e-model", provider: "mock_free", quality: 1 }] },
});
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--accounts-root", root, "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false;
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[guest] PASS ${name}`); } catch (e) { failed = true; console.error(`[guest] FAIL ${name}\n${e.stack}`); } };
const guestDirs = () => { try { return readdirSync(path.join(root, "guests")); } catch { return []; } };
const inPage = (page, p) => page.evaluate(async (u) => { const r = await fetch(u, { credentials: "same-origin" }); return { status: r.status, body: await r.text() }; }, p);
const QUESTION = "Napisz do ala@example.com o https://example.org/kot w sprawie umowy z 2026-10-28.";

try {
  await waitUp(`${base}/api/healthz`);
  browser = await (await chromium()).launch({ executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || undefined });
  const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 }, locale: "pl-PL" });
  // PH29 P1: this test checks the IndexedDB bundle store, so it pins that plugin (the person's own choice, `loom.store.policy`);
  // the OPFS + SQLite store (the preset's first choice) is covered by opfs-store-native.mjs.
  await ctx.addInitScript(() => { try { localStorage.setItem("loom.store.policy", JSON.stringify({ targets: ["browser_indexeddb"] })); } catch {} });
  const page = await ctx.newPage();
  let firstGuest = null, convId = null;

  await step("first visit: full interface, no gate, a guest session started by itself, the free model named", async () => {
    await page.goto(base);
    await page.getByTestId("chat-input").waitFor({ timeout: 20000 });
    assert.equal(await page.getByTestId("auth-form").count(), 0, "no sign-in form");
    const me = JSON.parse((await inPage(page, "/api/auth/me")).body);
    firstGuest = me.guest?.id;
    assert.ok(firstGuest?.startsWith("g_"), JSON.stringify(me));
    assert.deepEqual(guestDirs(), [firstGuest]);
    const line = await page.getByTestId("guest-model-line").innerText();
    assert.ok(line.includes("mock/e2e-model") && line.includes("Mock darmowy") && line.includes("Testowy dostawca nic nie zapisuje."), line);
    assert.equal(await page.getByTestId("guest-notice").count(), 0, "no notice before the first answer");
    await page.screenshot({ path: path.join(SHOTS, "guest-1-first-visit.png") });
  });

  await step("question -> answer of the free model -> one notice with the period from the store", async () => {
    await page.getByTestId("chat-input").fill(QUESTION);
    await page.getByTestId("send-chat").click();
    // R103 B3 (§4.3): the first message waits for the tap; the dialog says what the provider does and where we know it from.
    const consent = page.getByTestId("guest-terms-consent");
    await consent.waitFor({ timeout: 10000 });
    const consentText = await consent.innerText();
    assert.ok(consentText.includes("Testowy dostawca nic nie zapisuje.") && consentText.includes("2026-10-28"), consentText);
    await page.screenshot({ path: path.join(SHOTS, "guest-1b-terms-consent.png") });
    await page.getByTestId("guest-terms-send").click();
    await page.getByText("Hello from the mock model!").first().waitFor({ timeout: 20000 });
    assert.equal(await consent.count(), 0, "the tap is asked once per provider and observation of its terms");
    const notice = page.getByTestId("guest-notice");
    await notice.waitFor({ timeout: 10000 });
    const kind = await notice.getAttribute("data-period");
    words = (await notice.innerText()).includes(locales.pl["guest.notice_title"]) ? locales.pl : locales.en;
    assert.ok(["guest.persisted", "guest.not_persisted", "guest.period_unknown"].includes(kind), kind);
    const body = await page.getByTestId("guest-notice-body").innerText();
    const ttl = `${TTL} s`;
    const periodPrefix = say(kind, { ttl, free: "\u0000" }).split("\u0000")[0];
    assert.ok(body.startsWith(say("guest.notice_body_browser", { period: "" }).split("{{")[0].slice(0, 40)), body);
    assert.ok(body.includes(periodPrefix) && body.includes(ttl), `${body} / ${periodPrefix}`);
    for (const id of ["guest.download", "guest.load", "guest.notice_sign_in", "guest.notice_never"])
      assert.ok((await notice.innerText()).includes(say(id)), id);
    await page.getByText("Hello from the mock model!").first().waitFor({ timeout: 10000 });  // the notice never hides the answer
    await page.screenshot({ path: path.join(SHOTS, "guest-2-notice.png") });
    await page.getByTestId("guest-notice-close").click();
    const convs = JSON.parse((await inPage(page, "/api/conversations")).body);
    assert.equal(convs.length, 1);
    convId = convs[0].id;
  });

  await step("R103 B2: over a limit -> the server's words, the wait in words and the alternatives, never an empty refusal", async () => {
    await page.getByTestId("chat-input").fill("Drugie pytanie w tej samej minucie.");
    await page.getByTestId("send-chat").click();
    const box = page.getByTestId("guest-limit");
    await box.waitFor({ timeout: 15000 });
    assert.equal(await box.getAttribute("data-mechanism"), "session_chat_minute");
    const wait = Number(await box.getAttribute("data-wait"));
    assert.ok(wait >= 1 && wait <= 60, `wait ${wait}`);
    const said = await page.getByTestId("guest-limit-text").innerText();
    assert.ok(said.length > 10 && !said.includes("{{") && /\d/.test(said), said);
    assert.ok(/\d+ (s|min)/.test(said), `the wait in words: ${said}`);
    await new Promise((r) => setTimeout(r, 2200));  // counted down in place
    const later = await page.getByTestId("guest-limit-text").innerText();
    assert.ok(later !== said && /\d+ (s|min)/.test(later), `${said} -> ${later}`);
    assert.ok((await page.getByTestId("guest-limit-alternatives").innerText()).length > 10);
    assert.equal(await page.getByTestId("guest-limit-own-key").count(), 1);
    await page.screenshot({ path: path.join(SHOTS, "guest-2b-limit.png") });
    await page.getByTestId("guest-limit-close").click();
  });

  await step("the bundle is in the browser store (IndexedDB) after the answer", async () => {
    await until("bundle saved", async () => (await page.getByTestId("guest-layer").getAttribute("data-saved-at")) || null);
    const stored = await page.evaluate(() => new Promise((res) => {
      const r = indexedDB.open("loom-guest-store", 1);
      r.onsuccess = () => { const t = r.result.transaction("bundles", "readonly").objectStore("bundles").get("current"); t.onsuccess = () => res(t.result); };
    }));
    // R103 B1b: the whole working copy (v2, the server's exact text); the full path is guest-bundle-native.mjs
    assert.equal(stored.schema, "loom.guest_bundle/2");
    assert.equal(JSON.parse(stored.text).content.tables.conversations.rows.length, 1);
    assert.ok(stored.text.includes("ala@example.com"));
    assert.ok(!stored.text.includes(FAKE_KEY), "the key never enters the bundle");
    await until("graph of the first session", async () => JSON.parse((await inPage(page, "/api/graph/nodes")).body)?.length > 0 || null);
  });

  await step("close: session gone; after the idle TTL the server's working directory is gone", async () => {
    await page.close();
    await ctx.clearCookies();
    await until("the sweep", async () => { await request(`${base}/api/healthz`); return guestDirs().length === 0 || null; }, (TTL + 10) * 1000);
  });

  await step("new visit: a new guest, the conversation and its graph come back from the browser store", async () => {
    const p2 = await ctx.newPage();
    await p2.goto(base);
    await p2.getByTestId("guest-restored").waitFor({ timeout: 30000 });
    assert.ok((await p2.getByTestId("guest-restored").innerText()).includes(say("guest.restored", { count: "1" })));
    const me = JSON.parse((await inPage(p2, "/api/auth/me")).body);
    assert.ok(me.guest?.id && me.guest.id !== firstGuest, "a new guest");
    const convs = JSON.parse((await inPage(p2, "/api/conversations")).body);
    assert.equal(convs.length, 1);
    const msgs = (await inPage(p2, `/api/conversations/${convs[0].id}/messages`)).body;
    assert.ok(msgs.includes("ala@example.com") && msgs.includes("Hello from the mock model!"), msgs.slice(0, 500));
    await until("graph restored", async () => JSON.parse((await inPage(p2, "/api/graph/nodes")).body)?.length > 0 || null);
    assert.equal(await p2.getByTestId("auth-form").count(), 0);
    await p2.screenshot({ path: path.join(SHOTS, "guest-3-restored.png") });
    void convId;
  });
} catch (e) {
  failed = true;
  console.error(`[guest] FAIL setup\n${e.stack}`);
} finally {
  await browser?.close();
  server.kill("SIGTERM");
  mock.kill("SIGTERM");
}
console.log(failed ? "[guest] FAILED" : `[guest] OK (screenshots: ${SHOTS})`);
process.exit(failed ? 1 : 0);
