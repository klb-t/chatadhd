#!/usr/bin/env node
// PH29 P1: the browser store plugin OPFS + SQLite-WASM, against the REAL loom-server (accounts mode, guests on by a TEST
// overlay, mock free provider - the setup of guest-bundle-native.mjs). Steps:
//  1. a guest conversation -> answer -> graph; the page chose "Przeglądarka — baza SQLite" (OPFS) by the store traits,
//     holds the Web Lock, saved the working copy into the database (nothing in IndexedDB);
//  2. close + idle TTL (the server's working directory is gone) -> a new visit restores everything FROM OPFS with
//     0 model requests and no analysis; the bundle rebuilt in the browser is accepted with its content_sha256 (D4)
//     and the database passes PRAGMA integrity_check;
//  3. two tabs: one leader (the lock is held once, the other tab waits), the second tab writes through the leader
//     (BroadcastChannel) and its write is in the store (proved by a restore after close + TTL);
//  4. a >= 50 MB bundle (SYNTHETIC rows made here, test only - never part of the app) -> time of the first save, then
//     bytes and time of one new message (incremental) vs the whole-bundle write the IndexedDB store does;
//  5. "Clear everything from this browser" removes the database files from OPFS (no file name survives, nothing left
//     to restore);
//  6. OPFS switched off in a new browser context -> the IndexedDB store is used, with an advisory (not an error).
// Measurements are printed as "[opfs-store] MEASURE ..." lines (machine, date) for the report.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { cpus, hostname } from "node:os";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { request } from "./harness-lifecycle.mjs";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-opfs-store-shots-", { evidence: true });
const BIG_MB = Number(process.env.OPFS_E2E_BIG_MB ?? 50);  // preset: the design's acceptance size
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[opfs-store] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });
const viewPack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const words = viewPack.entries.find(r => r.key === "presentation.accounts").value.locales.pl;
const say = (id, params = {}) => { assert.ok(typeof words[id] === "string", `the pack has the word ${id}`); return words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? ""); };
const TTL = 10;
const FAKE_KEY = "sk-opfs-e2e-not-a-real-key";
const LOCK = "loom-store-opfs-sqlite";

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await request(url, { timeoutMs: 3000 })).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error(`timed out waiting for ${url}`); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }
async function until(what, check, ms = 20000) { for (const end = Date.now() + ms; Date.now() < end;) { const v = await check(); if (v) return v; await new Promise(r => setTimeout(r, 150)); } throw new Error(`timed out waiting for ${what}`); }

const mockPort = await freePort(), port = await freePort();
const tmp = makeTmpDir("loom-opfs-store-");
const root = path.join(tmp, "root");
mkdirSync(path.join(root, "profiles"), { recursive: true });
writeFileSync(path.join(tmp, "keys.env"), `MOCK_FREE_KEY=${FAKE_KEY}\n`);
const overlay = (domain, overrides) => writeFileSync(path.join(root, "profiles", `${domain}.pack`), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain, overrides }));
overlay("server_accounts", {
  allow_private_hosts: ["127.0.0.1"],
  guests: { enabled: true, start: "auto", sweep_interval_seconds: 0, keys_file: path.join(tmp, "keys.env"),
            first_answer_notice: { on: false, ask_again: "per_device" },
            workdir: { subdir: "guests", idle_ttl_seconds: TTL, max_bytes: 0, keep_after_session: "ask" },
            secrets_routes: "write_only_session", model_sources: ["byok", "pool", "free", "local"],
            // the 50 MB measurement restores a big bundle; the guest body preset (1 MB) would refuse it (finding of this
            // package: a real guest history above 1 MB cannot be restored with the preset - reported as a gap)
            protection: { sizes: { max_body_bytes: 0, max_upload_bytes: 0 } } },
});
overlay("free_providers", {
  providers: [{ id: "mock_free", display_name: "Mock darmowy", base_url: `http://127.0.0.1:${mockPort}`, auth_env: "MOCK_FREE_KEY", auth_secret: "api_key",
                active_when: "key_present", enabled: true,
                data_terms: { retention: "none", human_review: false, training: "no", observed_at: "2026-10-28", source_url: "test", source_rating: "test",
                              summary: { pl: "Testowy dostawca nic nie zapisuje.", en: "The test provider keeps nothing." } } }],
  free_model_choice: { rank_source: "table", require: { training: "no" }, table: [{ model: "mock/e2e-model", provider: "mock_free", quality: 1 }] },
});
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--accounts-root", root, "--static-dir", DIST,
  "--max-body-bytes", "0", "--max-upload-bytes", "0"], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false;
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[opfs-store] PASS ${name}`); } catch (e) { failed = true; console.error(`[opfs-store] FAIL ${name}\n${e.stack}`); } };
const measure = (what, o) => console.log(`[opfs-store] MEASURE ${what} ${JSON.stringify({ ...o, machine: `${hostname()} ${cpus().length} cpu`, date: new Date().toISOString() })}`);
const guestDirs = () => { try { return readdirSync(path.join(root, "guests")); } catch { return []; } };
const api = (page, method, p, body) => page.evaluate(async ([m, u, b]) => {
  const r = await fetch(u, { method: m, credentials: "same-origin", headers: b === undefined ? {} : { "Content-Type": "application/json" }, body: b === undefined ? undefined : b });
  const text = await r.text();
  let json = null; try { json = JSON.parse(text); } catch {}
  return { status: r.status, text: text.length > 2e6 ? text.slice(0, 200) : text, json: text.length > 2e6 ? null : json };
}, [method, p, body === undefined ? undefined : (typeof body === "string" ? body : JSON.stringify(body))]);
const mockCount = async () => (await request(`http://127.0.0.1:${mockPort}/__count`)).json();
const idbBundle = (page) => page.evaluate(() => new Promise((res) => {
  const r = indexedDB.open("loom-guest-store", 1);
  r.onupgradeneeded = () => { r.result.createObjectStore("bundles"); };
  r.onsuccess = () => { const t = r.result.transaction("bundles", "readonly").objectStore("bundles").get("current"); t.onsuccess = () => { r.result.close(); res(t.result ?? null); }; t.onerror = () => res(null); };
  r.onerror = () => res(null);
}));
const opfsFiles = (page) => page.evaluate(async () => {
  const out = [];
  const walk = async (dir, pre) => { for await (const [name, h] of dir.entries()) { out.push(pre + name); if (h.kind === "directory") await walk(h, pre + name + "/"); } };
  try { await walk(await navigator.storage.getDirectory(), ""); } catch (e) { out.push(`!${e}`); }
  return out.sort();
});
const lockState = (page) => page.evaluate(async (name) => { const s = await navigator.locks.query(); return { held: s.held.filter((l) => l.name === name).length, pending: s.pending.filter((l) => l.name === name).length }; }, LOCK);
const storePanel = async (page) => {
  const el = page.getByTestId("guest-store");
  await el.waitFor({ timeout: 30000 });
  const attr = async (n) => (await el.getAttribute(n)) ?? "";
  const traits = Object.fromEntries(await page.$$eval('[data-testid="guest-store-traits"] li', (lis) => lis.map((li) => [li.getAttribute("data-trait"), li.textContent.replace(/^[^:]+:\s*/, "")])));
  return { store: await attr("data-store"), integrity: await attr("data-integrity"), fallback: await attr("data-fallback"), leader: await attr("data-leader"), traits };
};
// a save happened after `before` and no save is still running (closing a tab mid-save would lose that save)
const savedAfter = async (page, before) => until("a save", async () => {
  const el = page.getByTestId("guest-layer");
  const s = await el.getAttribute("data-saved-at");
  return (s && s !== before && (await el.getAttribute("data-saving")) === "0") ? s : null;
}, 300000);
const ask = async (page, q, n) => {
  await page.getByTestId("chat-input").fill(q);
  await page.getByTestId("send-chat").click();
  await until("the answer", async () => (await page.getByText("Hello from the mock model!").count()) >= n || null, 30000);
};
const sweep = async () => until("the sweep", async () => { await request(`${base}/api/healthz`); return guestDirs().length === 0 || null; }, (TTL + 15) * 1000);

try {
  await waitUp(`${base}/api/healthz`);
  browser = await (await chromium()).launch({ executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || undefined });
  const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 }, locale: "pl-PL" });
  ctx.on("page", (pg) => pg.on("console", (m) => { if (m.type() === "warning" || m.type() === "error") console.log(`[opfs-store] page ${m.type()}: ${m.text().slice(0, 300)}`); }));
  let page = await ctx.newPage();
  let before = null, convId = null, firstGuest = null;

  await step("1. conversation -> graph; OPFS + SQLite chosen by traits; the working copy saved into the database", async () => {
    await page.goto(base);
    await page.getByTestId("chat-input").waitFor({ timeout: 20000 });
    firstGuest = (await api(page, "GET", "/api/auth/me")).json?.guest?.id;
    const p = await storePanel(page);
    assert.equal(p.store, "browser_opfs_sqlite", JSON.stringify(p));
    assert.equal(p.leader, "true");
    assert.ok(p.traits.ops.includes("incremental_write") && p.traits.vfs === "opfs-sahpool", JSON.stringify(p.traits));
    measure("sqlite-wasm", { sqlite_version: p.traits.sqlite_version, fts5: p.traits.fts5, persisted: p.traits.persisted, quota_bytes: p.traits.quota_bytes, free_bytes: p.traits.free_bytes });
    assert.ok(say("guest.store", { label: "browser_opfs_sqlite" }).length > 0);
    assert.deepEqual(await lockState(page), { held: 1, pending: 0 });
    const saved0 = await page.getByTestId("guest-layer").getAttribute("data-saved-at");
    await ask(page, "Napisz do ala@example.com o https://example.org/kot w sprawie umowy z 2026-10-28.", 1);
    convId = (await api(page, "GET", "/api/conversations")).json[0].id;
    await until("analysis done", async () => {
      const m = (await api(page, "GET", `/api/conversations/${convId}/messages`)).json;
      return Array.isArray(m) && m.length >= 2 && m.every((x) => x.semantic_status === "done") || null;
    }, 30000);
    await savedAfter(page, saved0);
    // the analysis finishes after the answer; one more save catches the graph
    const s1 = await page.getByTestId("guest-layer").getAttribute("data-saved-at");
    await page.evaluate(() => window.dispatchEvent(new CustomEvent("loom:working-copy-changed")));
    await savedAfter(page, s1);
    assert.equal(await idbBundle(page), null, "nothing written to IndexedDB: the database is the store");
    const files = await opfsFiles(page);
    assert.ok(files.some((f) => f.startsWith(".loom-store")), files.join(","));
    before = { convs: (await api(page, "GET", "/api/conversations")).json, msgs: (await api(page, "GET", `/api/conversations/${convId}/messages`)).json,
               nodes: (await api(page, "GET", "/api/graph/nodes?limit=100000")).json, memory: (await api(page, "GET", "/api/memory")).text };
    assert.ok(before.nodes.length > 0, "the graph exists");
  });

  let requestsBefore = 0;
  await step("2. close + TTL -> new visit restores from OPFS: 0 model requests, no analysis, sha accepted, integrity ok", async () => {
    requestsBefore = (await mockCount()).requests;
    await page.close();
    await ctx.clearCookies();
    await sweep();
    page = await ctx.newPage();
    await page.goto(base);
    await page.getByTestId("guest-restored").waitFor({ timeout: 30000 });
    assert.ok((await page.getByTestId("guest-restored").innerText()).includes(say("guest.restored", { count: "1" })));
    assert.notEqual((await api(page, "GET", "/api/auth/me")).json.guest.id, firstGuest, "a new guest");
    await new Promise((r) => setTimeout(r, 3000));
    assert.deepEqual((await api(page, "GET", "/api/conversations")).json.map((c) => c.id), before.convs.map((c) => c.id));
    assert.deepEqual((await api(page, "GET", `/api/conversations/${convId}/messages`)).json, before.msgs, "messages with their analysis status");
    const key = (n) => `${n.id}|${n.created}|${n.label}`;
    assert.deepEqual((await api(page, "GET", "/api/graph/nodes?limit=100000")).json.map(key).sort(), before.nodes.map(key).sort());
    assert.equal((await api(page, "GET", "/api/memory")).text, before.memory);
    assert.equal((await mockCount()).requests, requestsBefore, "no request reached the model");
    const p = await storePanel(page);
    assert.equal(p.integrity, "ok", "PRAGMA integrity_check");
    measure("d4", { fallback: p.fallback || "none (the bundle rebuilt in the browser carried the server-identical content_sha256)" });
    assert.equal(p.fallback, "", "D4: the rebuilt bundle is canonical (no fallback text needed)");
    await page.screenshot({ path: path.join(SHOTS, "opfs-restored.png") });
  });

  await step("3. two tabs: one leader, the other writes through it", async () => {
    const tab2 = await ctx.newPage();
    await tab2.goto(base);
    await tab2.getByTestId("chat-input").waitFor({ timeout: 20000 });
    const p2 = await storePanel(tab2);
    assert.equal(p2.store, "browser_opfs_sqlite");
    assert.equal(p2.leader, "false", "the second tab is not the leader");
    assert.deepEqual(await lockState(tab2), { held: 1, pending: 1 }, "one holder, one waiting tab");
    const s0 = await tab2.getByTestId("guest-layer").getAttribute("data-saved-at");
    await ask(tab2, "Druga karta: notatka o kardamonie.", 1);
    await savedAfter(tab2, s0);  // the save went through the leader (tab 1 holds the database)
    const n = (await api(tab2, "GET", "/api/conversations")).json.length;
    const t2 = (await storePanel(tab2)).traits;
    console.log("[opfs-store] tab2 after its save", JSON.stringify({ rows: t2.last_write_rows, bytes: t2.last_write_bytes, n }));
    assert.ok(Number(String(t2.last_write_rows ?? "0")) > 0, "the second tab's save wrote rows through the leader");
    requestsBefore = (await mockCount()).requests;
    await page.close(); await tab2.close();
    await ctx.clearCookies();
    await sweep();
    page = await ctx.newPage();
    await page.goto(base);
    await page.getByTestId("guest-restored").waitFor({ timeout: 30000 });
    assert.equal((await api(page, "GET", "/api/conversations")).json.length, n, "the second tab's conversation is in the store");
    assert.ok((await api(page, "GET", "/api/conversations")).text.length > 0);
    assert.equal((await mockCount()).requests, requestsBefore);
    assert.equal((await storePanel(page)).integrity, "ok");
  });

  await step(`4. a ${BIG_MB} MB bundle (SYNTHETIC test rows): first save, then one new message incremental vs whole-bundle write`, async () => {
    const real = await page.evaluate(async () => (await (await fetch("/api/working-copy/bundle", { credentials: "same-origin" })).text()));
    const b = JSON.parse(real);
    const mt = b.content.tables.messages, ct = b.content.tables.conversations;
    const col = (t, n) => t.columns.indexOf(n);
    // SYNTHETIC DATA, TEST ONLY: one conversation of filler messages cloned from a real row; it never leaves this test.
    const conv = [...ct.rows[0]]; conv[col(ct, "id")] = "c_synth0e2e000"; conv[col(ct, "title")] = "SYNTETYCZNE DANE TESTOWE (e2e PH29 P1)";
    const filler = "Syntetyczny wiersz testowy PH29 (tylko test). ".repeat(110);  // ~5 KB
    const rows = [];
    let bytes = 0;
    for (let i = 0; bytes < BIG_MB * 1048576; i++) {
      const r = [...mt.rows[0]];
      r[col(mt, "id")] = `m_synth${String(i).padStart(7, "0")}`; r[col(mt, "conv_id")] = "c_synth0e2e000"; r[col(mt, "parent_id")] = i ? `m_synth${String(i - 1).padStart(7, "0")}` : null;
      r[col(mt, "text")] = `${i}: ${filler}`; r[col(mt, "version_group_id")] = `vg_synth${String(i).padStart(6, "0")}`;
      rows.push(r); bytes += JSON.stringify(r).length;
    }
    // Only conversations + messages; the one REAL column (weight) is written as the kernel writes doubles.
    const weightIx = col(mt, "weight");
    // the template's weight is a whole number (1.0); a whole double is written "1.0" by the kernel
    const formatDouble = (x) => (Number.isInteger(x) ? x.toFixed(1) : String(x));
    const dumpRow = (r) => "[" + r.map((v, i) => (i === weightIx ? formatDouble(Number(v)) : JSON.stringify(v))).join(",") + "]";
    const content = `{"tables":{"conversations":{"columns":${JSON.stringify(ct.columns)},"rows":[${JSON.stringify(conv)}]},"messages":{"columns":${JSON.stringify(mt.columns)},"rows":[${rows.map(dumpRow).join(",")}]}}}`;
    const sha = createHash("sha256").update(content).digest("hex");
    const synth = `{"schema":"loom.guest_bundle/2","saved_at":"2026-10-29T00:00:00Z","producer":"e2e-synthetic","content":${content},"integrity":{"algorithm":"sha256","content_sha256":"${sha}"}}`;
    const t0 = Date.now();
    // posted from here with the page's session cookie (50 MB through page.evaluate would cross the test protocol)
    const cookie = (await ctx.cookies(base)).map((c) => `${c.name}=${c.value}`).join("; ");
    const imp = await fetch(`${base}/api/working-copy/bundle?mode=merge`, { method: "POST", body: synth, headers: { "Content-Type": "application/json", Cookie: cookie, Origin: base } });
    assert.equal(imp.status, 200, (await imp.text()).slice(0, 300));
    const importMs = Date.now() - t0;
    const s0 = await page.getByTestId("guest-layer").getAttribute("data-saved-at");
    const t1 = Date.now();
    await page.evaluate(() => window.dispatchEvent(new CustomEvent("loom:working-copy-changed")));
    await savedAfter(page, s0);
    const firstSaveMs = Date.now() - t1;
    const first = (await storePanel(page)).traits;
    const s1 = await page.getByTestId("guest-layer").getAttribute("data-saved-at");
    const t2 = Date.now();
    await ask(page, "Jedna nowa wiadomość po dużym zapisie.", 1);
    await savedAfter(page, s1);
    const oneMsgMs = Date.now() - t2;
    const one = (await storePanel(page)).traits;
    // what the IndexedDB store writes for the same save: the whole bundle text, measured with the same browser
    const idb = await page.evaluate(async () => {
      const text = await (await fetch("/api/working-copy/bundle", { credentials: "same-origin" })).text();
      const t = performance.now();
      await new Promise((res, rej) => { const r = indexedDB.open("opfs-e2e-measure", 1); r.onupgradeneeded = () => r.result.createObjectStore("b");
        r.onsuccess = () => { const tx = r.result.transaction("b", "readwrite"); tx.objectStore("b").put({ text }, "k"); tx.oncomplete = () => { r.result.close(); res(); }; tx.onerror = () => rej(tx.error); }; r.onerror = () => rej(r.error); });
      const ms = performance.now() - t;
      await new Promise((res) => { const d = indexedDB.deleteDatabase("opfs-e2e-measure"); d.onsuccess = d.onerror = d.onblocked = () => res(); });
      return { bytes: text.length, ms: Math.round(ms) };
    });
    measure("big-bundle", { synthetic: true, synthetic_messages: rows.length, bundle_chars: idb.bytes, server_import_ms: importMs,
      opfs_first_save_ms: firstSaveMs, opfs_first_save_write_bytes: first.last_write_bytes, opfs_first_save_worker_ms: first.last_write_ms,
      one_message_save_ms: oneMsgMs, opfs_one_message_write_bytes: one.last_write_bytes, opfs_one_message_worker_ms: one.last_write_ms,
      indexeddb_one_message_write_bytes: idb.bytes, indexeddb_put_ms: idb.ms, fallback: (await storePanel(page)).fallback || "none" });
    assert.ok(Number(String(one.last_write_bytes).split(" ")[0]) < idb.bytes / 50, "one message writes a small fraction of the bundle");
  });

  await step("5. clear everything: the database files leave OPFS, nothing to restore", async () => {
    const old = (await opfsFiles(page)).filter((f) => f.startsWith(".loom-store/"));
    assert.ok(old.length > 0, "files before");
    await page.getByTestId("guest-wipe").click();
    const gone = page.waitForEvent("load");
    await page.getByTestId("guest-wipe-yes").click();
    await gone;
    await page.getByTestId("chat-input").waitFor({ timeout: 20000 });
    await storePanel(page);
    const now = await opfsFiles(page);
    // the new page opens a fresh, empty pool; none of the old files (random names of the SAH pool) survive
    // the SAH pool keeps its files under random names in .loom-store/.opaque/: none of the old ones may survive
    const poolFiles = old.filter((f) => /\.opaque\/./.test(f));
    assert.ok(poolFiles.length > 0, old.join(","));
    for (const f of poolFiles) assert.ok(!now.includes(f), `left after the wipe: ${f}`);
    assert.equal(await idbBundle(page), null);
    assert.equal((await api(page, "GET", "/api/conversations")).json.length, 0, "nothing restored");
    assert.equal(await page.getByTestId("guest-restored").count(), 0);
    measure("wipe", { opfs_before: old.length, opfs_after: now.filter((f) => f.startsWith(".loom-store/")).length });
  });

  await step("6. OPFS switched off -> the IndexedDB store, with an advisory", async () => {
    const ctx2 = await browser.newContext({ viewport: { width: 1280, height: 800 }, locale: "pl-PL" });
    await ctx2.addInitScript(() => { try { delete StorageManager.prototype.getDirectory; } catch {} });
    const p = await ctx2.newPage();
    await p.goto(base);
    await p.getByTestId("chat-input").waitFor({ timeout: 20000 });
    const panel = await storePanel(p);
    assert.equal(panel.store, "browser_indexeddb", JSON.stringify(panel));
    const adv = p.locator('[data-testid="guest-store-advisory"][data-plugin="browser_opfs_sqlite"]');
    await p.getByTestId("guest-store").locator("summary").click();
    assert.ok((await adv.innerText()).includes("opfs"), await adv.innerText());
    const s0 = await p.getByTestId("guest-layer").getAttribute("data-saved-at");
    await ask(p, "Bez OPFS.", 1);
    await savedAfter(p, s0);
    const stored = await idbBundle(p);
    assert.equal(stored?.schema, "loom.guest_bundle/2");
    await p.screenshot({ path: path.join(SHOTS, "opfs-off-indexeddb.png") });
    await ctx2.close();
  });
} catch (e) {
  failed = true;
  console.error(e);
} finally {
  await browser?.close();
  server.kill();
  mock.kill();
}
console.log(failed ? "[opfs-store] FAILED" : "[opfs-store] OK");
process.exit(failed ? 1 : 0);
