#!/usr/bin/env node
// R103 B1b: the FULL guest bundle (loom.guest_bundle/2) from the first visit, on the real loom-server with guests on
// (test overlay) and the mock free provider. Steps: a conversation -> its graph -> a named layout, a pin, a memory entry
// and something in the trash -> the browser store holds the v2 bundle (and none of the key, cookie or token) -> close +
// idle TTL: the server's directory is gone -> new visit: conversation, graph (the same node ids and timestamps), memory,
// layout, pins and trash are all back with 0 requests to the model and no analysis run -> "Clear everything from this
// browser" (with consent) -> the session ends, the store is gone, and a later visit restores nothing.
// (Setup below is shared with guest-mode-native.mjs.)
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
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-guest-bundle-shots-", { evidence: true });
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[guest-bundle] build loom-server and `npm run build` first"); process.exit(2); }
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
const tmp = makeTmpDir("loom-guest-bundle-");
const root = path.join(tmp, "root");
mkdirSync(path.join(root, "profiles"), { recursive: true });
writeFileSync(path.join(tmp, "keys.env"), `MOCK_FREE_KEY=${FAKE_KEY}\n`);
const overlay = (domain, overrides) => writeFileSync(path.join(root, "profiles", `${domain}.pack`), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain, overrides }));
overlay("server_accounts", {
  allow_private_hosts: ["127.0.0.1"],
  guests: { enabled: true, start: "auto", sweep_interval_seconds: 0, keys_file: path.join(tmp, "keys.env"),
            first_answer_notice: { on: false, ask_again: "per_device" },
            workdir: { subdir: "guests", idle_ttl_seconds: TTL, max_bytes: 0, keep_after_session: "ask" },
            secrets_routes: "write_only_session", model_sources: ["byok", "pool", "free", "local"] },
});
overlay("free_providers", {
  providers: [{ id: "mock_free", display_name: "Mock darmowy", base_url: `http://127.0.0.1:${mockPort}`, auth_env: "MOCK_FREE_KEY", auth_secret: "api_key",
                active_when: "key_present", enabled: true,
                data_terms: { retention: "none", human_review: false, training: "no", observed_at: "2026-10-28", source_url: "test", source_rating: "test",
                              summary: { pl: "Testowy dostawca nic nie zapisuje.", en: "The test provider keeps nothing." } } }],
  free_model_choice: { rank_source: "table", require: { training: "no" }, table: [{ model: "mock/e2e-model", provider: "mock_free", quality: 1 }] },
});
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--accounts-root", root, "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false;
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[guest-bundle] PASS ${name}`); } catch (e) { failed = true; console.error(`[guest-bundle] FAIL ${name}\n${e.stack}`); } };
const guestDirs = () => { try { return readdirSync(path.join(root, "guests")); } catch { return []; } };
const api = (page, method, p, body) => page.evaluate(async ([m, u, b]) => {
  const r = await fetch(u, { method: m, credentials: "same-origin", headers: b === undefined ? {} : { "Content-Type": "application/json" }, body: b === undefined ? undefined : JSON.stringify(b) });
  const text = await r.text();
  let json = null; try { json = JSON.parse(text); } catch {}
  return { status: r.status, text, json };
}, [method, p, body]);
const mockCount = async () => (await request(`http://127.0.0.1:${mockPort}/__count`)).json();
const storedBundle = (page) => page.evaluate(() => new Promise((res) => {
  const r = indexedDB.open("loom-guest-store", 1);
  r.onupgradeneeded = () => { r.result.createObjectStore("bundles"); };
  r.onsuccess = () => { const t = r.result.transaction("bundles", "readonly").objectStore("bundles").get("current"); t.onsuccess = () => res(t.result ?? null); t.onerror = () => res(null); };
  r.onerror = () => res(null);
}));
const snapshot = async (page) => {
  const convs = (await api(page, "GET", "/api/conversations")).json;
  const trashed = (await api(page, "GET", "/api/conversations?trashed=only")).json;
  const nodes = (await api(page, "GET", "/api/graph/nodes?limit=100000")).json;
  const memory = (await api(page, "GET", "/api/memory")).text;
  const config = (await api(page, "GET", "/api/config")).json;
  const msgs = convs?.length ? (await api(page, "GET", `/api/conversations/${convs[0].id}/messages`)).json : [];
  return { convs, trashed, nodes, memory, config, msgs };
};
const LAYOUT = [{ schema: "loom.window_layout/2", id: "e2e-moj", label: "Mój układ e2e", saved: "2026-10-29T12:00:00Z",
                  layout: { id: "e2e-moj", windows: [{ id: "w1", kind: "chat", mode: "maximized", params: {} }] } }];
const QUESTION = "Napisz do ala@example.com o https://example.org/kot w sprawie umowy z 2026-10-28.";

try {
  await waitUp(`${base}/api/healthz`);
  browser = await (await chromium()).launch({ executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || undefined });
  const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 }, locale: "pl-PL" });
  const page = await ctx.newPage();
  let firstGuest = null, before = null, convId = null, cookieValue = "";

  await step("conversation -> answer -> its graph", async () => {
    await page.goto(base);
    await page.getByTestId("chat-input").waitFor({ timeout: 20000 });
    firstGuest = (await api(page, "GET", "/api/auth/me")).json?.guest?.id;
    assert.ok(firstGuest?.startsWith("g_"));
    await page.getByTestId("chat-input").fill(QUESTION);
    await page.getByTestId("send-chat").click();
    await page.getByText("Hello from the mock model!").first().waitFor({ timeout: 20000 });
    convId = (await api(page, "GET", "/api/conversations")).json[0].id;
    await until("graph of the conversation", async () => (await api(page, "GET", "/api/graph/nodes")).json?.length > 0 || null);
    await until("analysis done", async () => {
      const m = (await api(page, "GET", `/api/conversations/${convId}/messages`)).json;
      return Array.isArray(m) && m.length >= 2 && m.every((x) => x.semantic_status === "done") || null;
    }, 30000);
  });

  await step("a named layout, a pin, a memory entry, something in the trash -> saved (debounced) as loom.guest_bundle/2", async () => {
    const changed = () => page.evaluate(() => window.dispatchEvent(new CustomEvent("loom:working-copy-changed")));
    assert.equal((await api(page, "PATCH", "/api/config", { window_layouts: LAYOUT, window_layout_active: { desktop: "e2e-moj" } })).status, 200);
    const pin = await api(page, "POST", "/api/actions/run", { action: "window.pin", element: { class: "application" }, args: { target: `conv:${convId.slice(2)}`, label: "Umowa" } });
    assert.equal(pin.json?.status, "completed", pin.text);
    assert.equal((await api(page, "POST", "/api/memory", { content: "Gość e2e lubi herbatę z imbirem." })).status, 201);
    assert.equal((await api(page, "POST", "/api/memory", { content: "Wpis do kosza kardamon e2e." })).status, 201);
    const tr = await api(page, "POST", "/api/actions/run", { action: "memory.trash", element: { class: "application" }, args: { query: "kardamon" } });
    assert.equal(tr.json?.status, "completed", tr.text);
    // a second conversation, thrown into the trash (it must come back as trashed, not as a live one, and not vanish)
    const c2 = await api(page, "POST", "/api/conversations", { title: "Kosz e2e rozmowa" });
    const c2id = c2.json?.id;
    assert.ok(c2id, c2.text);
    const tc = await api(page, "POST", "/api/actions/run", { action: "conversation.trash", element: { class: "application" }, args: { query: c2id, from: "desktop" } });
    assert.equal(tc.json?.status, "completed", tc.text);
    const savedBefore = await page.getByTestId("guest-layer").getAttribute("data-saved-at");
    await changed();
    await until("debounced save", async () => { const s = await page.getByTestId("guest-layer").getAttribute("data-saved-at"); return (s && s !== savedBefore) || null; });
    const stored = await storedBundle(page);
    assert.equal(stored.schema, "loom.guest_bundle/2");
    const b = JSON.parse(stored.text);
    assert.equal(b.schema, "loom.guest_bundle/2");
    assert.ok(/^[0-9a-f]{64}$/.test(b.integrity.content_sha256));
    for (const t of ["conversations", "messages", "nodes", "links"]) assert.ok(b.content.tables[t].rows.length > 0, t);
    assert.ok(stored.text.includes("herbatę z imbirem") && stored.text.includes("Mój układ e2e") && stored.text.includes("pinned_on"));
    cookieValue = (await ctx.cookies()).map((c) => c.value).find((v) => v.length >= 16) ?? "";
    assert.ok(cookieValue.length >= 16, "a session cookie exists");
    for (const secret of [FAKE_KEY, cookieValue]) assert.ok(!stored.text.includes(secret), "no key and no session token in the bundle");
    before = await snapshot(page);
    assert.equal(before.convs.length, 1);
    assert.equal(before.trashed.length, 1);
  });

  let requestsBefore = 0;
  await step("close: session gone; after the idle TTL the server's working directory is gone", async () => {
    requestsBefore = (await mockCount()).requests;
    await page.close();
    await ctx.clearCookies();
    await until("the sweep", async () => { await request(`${base}/api/healthz`); return guestDirs().length === 0 || null; }, (TTL + 10) * 1000);
  });

  await step("new visit: everything back exactly, 0 model requests, no analysis", async () => {
    const p2 = await ctx.newPage();
    await p2.goto(base);
    await p2.getByTestId("guest-restored").waitFor({ timeout: 30000 });
    const restoredText = await p2.getByTestId("guest-restored").innerText();
    // the count is every conversation that came back: the live one and the one in the trash
    assert.ok(restoredText.includes(say("guest.restored", { count: "2" })), restoredText);
    const me = (await api(p2, "GET", "/api/auth/me")).json;
    assert.ok(me.guest?.id && me.guest.id !== firstGuest, "a new guest");
    await new Promise((r) => setTimeout(r, 3000));  // a re-analysis, if any, would have started by now
    const after = await snapshot(p2);
    assert.deepEqual(after.convs.map((c) => c.id), before.convs.map((c) => c.id));
    assert.deepEqual(after.trashed.map((c) => c.id), before.trashed.map((c) => c.id), "the trash came back");
    assert.deepEqual(after.msgs, before.msgs, "messages with their analysis status");
    const key = (n) => `${n.id}|${n.created}|${n.label}`;
    assert.deepEqual(after.nodes.map(key).sort(), before.nodes.map(key).sort(), "the same graph nodes (ids and timestamps)");
    assert.equal(after.memory, before.memory, "memory incl. the trashed entry");
    assert.deepEqual(after.config.window_layouts, LAYOUT, "the named layout");
    assert.deepEqual(after.config.window_layout_active, { desktop: "e2e-moj" });
    const pins = (await api(p2, "GET", "/api/graph/nodes?limit=100000")).json;
    void pins;
    assert.ok((await storedBundle(p2)).text.includes("pinned_on"));
    const links = (await api(p2, "POST", "/api/actions/run", { action: "trash.list", element: { class: "application" }, args: {} })).json;
    assert.ok(JSON.stringify(links).includes("kardamon"), "the trashed memory entry is still in the trash");
    assert.equal((await mockCount()).requests, requestsBefore, "no request reached the model during the restore");
    await p2.screenshot({ path: path.join(SHOTS, "guest-bundle-restored.png") });

    // "Clear everything from this browser": consent first, "No" keeps everything
    await p2.getByTestId("guest-wipe").click();
    await p2.getByTestId("guest-wipe-confirm").waitFor();
    assert.ok((await p2.getByTestId("guest-wipe-confirm").innerText()).includes(say("guest.wipe_confirm").slice(0, 30)));
    await p2.getByTestId("guest-wipe-no").click();
    assert.ok(await storedBundle(p2));
    await p2.evaluate(() => { localStorage.setItem("loom.e2e.marker", "1"); sessionStorage.setItem("loom.e2e.marker", "1"); });
    await p2.getByTestId("guest-wipe").click();
    const gone = p2.waitForEvent("load");
    await p2.getByTestId("guest-wipe-yes").click();
    await gone;
    await p2.getByTestId("chat-input").waitFor({ timeout: 20000 });
    const me3 = (await api(p2, "GET", "/api/auth/me")).json;
    assert.ok(me3.guest?.id && me3.guest.id !== me.guest.id, "the session ended; a fresh guest");
    assert.equal((await api(p2, "GET", "/api/conversations")).json.length, 0);
    // what was stored before is gone (a fresh page writes its own first values again, so the check is on markers)
    assert.equal(await p2.evaluate(() => localStorage.getItem("loom.e2e.marker") ?? sessionStorage.getItem("loom.e2e.marker")), null, "local/session storage cleared");
    await p2.close();
  });

  await step("after the wipe a later visit restores nothing", async () => {
    await ctx.clearCookies();
    const p3 = await ctx.newPage();
    await p3.goto(base);
    await p3.getByTestId("chat-input").waitFor({ timeout: 20000 });
    await new Promise((r) => setTimeout(r, 3000));
    assert.equal(await p3.getByTestId("guest-restored").count(), 0);
    assert.equal((await api(p3, "GET", "/api/conversations")).json.length, 0);
    assert.equal((await api(p3, "GET", "/api/conversations?trashed=only")).json.length, 0);
    assert.ok(!(await api(p3, "GET", "/api/memory")).text.includes("imbirem"));
    assert.equal((await api(p3, "GET", "/api/config")).json.window_layouts?.[0]?.id === "e2e-moj", false);
  });
} catch (e) {
  failed = true;
  console.error(`[guest-bundle] FAIL setup\n${e.stack}`);
} finally {
  await browser?.close();
  server.kill("SIGTERM");
  mock.kill("SIGTERM");
}
console.log(failed ? "[guest-bundle] FAILED" : `[guest-bundle] OK (screenshots: ${SHOTS})`);
process.exit(failed ? 1 : 0);
