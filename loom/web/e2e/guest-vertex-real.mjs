#!/usr/bin/env node
// R103 B3: the full guest path on the REAL Vertex AI source (no mocks) — run by hand on a Google Compute Engine VM whose
// service account has the cloud-platform scope; NOT part of CI (it costs cents of the operator's credits). A TEST
// loom-server (own port, own temporary accounts root, guests on by a test overlay; the shipped free_providers data,
// so the source is whatever the data chooses — today Vertex, because Groq/OpenRouter have no key). Steps: first visit
// without a gate (proof of work in the page) -> the line names the model and the provider -> a short synthetic
// question -> the data-terms tap -> a real streamed answer -> the receipt header carries the provider's terms -> the
// notice after the first answer -> the bundle in IndexedDB -> a new session restores it. Handover checks: the B2 proof
// of work still refuses a start without it; an account works next to the guests; no access token ("ya29.") in any
// answer the page saw, any file of the accounts root, or the server log. Prints requests, tokens and spend.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createWriteStream, existsSync, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-guest-vertex-shots-", { evidence: true });
const TTL = 15;
const QUESTION = "Odpowiedz jednym krótkim zdaniem po polsku: ile to dwa plus dwa?";  // synthetic, no owner data
mkdirSync(SHOTS, { recursive: true });
const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }
async function until(what, check, ms = 60000) { for (const end = Date.now() + ms; Date.now() < end;) { const v = await check(); if (v) return v; await new Promise(r => setTimeout(r, 250)); } throw new Error(`timed out waiting for ${what}`); }
const walk = (d) => readdirSync(d, { withFileTypes: true }).flatMap(e => e.isDirectory() ? walk(path.join(d, e.name)) : [path.join(d, e.name)]);

const port = await freePort();
const tmp = makeTmpDir("loom-guest-vertex-");
const root = path.join(tmp, "root");
mkdirSync(path.join(root, "profiles"), { recursive: true });
writeFileSync(path.join(root, "profiles", "server_accounts.pack"), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain: "server_accounts", overrides: {
  registration: "open",
  guests: { enabled: true, start: "auto", sweep_interval_seconds: 0, keys_file: path.join(tmp, "no-keys.env"),
            workdir: { subdir: "guests", idle_ttl_seconds: TTL, max_bytes: 0, keep_after_session: "ask" } } } }));
const logPath = path.join(tmp, "server.log");
const log = createWriteStream(logPath);
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--accounts-root", root, "--static-dir", DIST], { stdio: ["ignore", "pipe", "pipe"], cwd: LOOM });
server.stdout.pipe(log); server.stderr.pipe(log);
const base = `http://127.0.0.1:${port}`;
let browser, failed = false;
const seen = [];
const step = async (name, fn) => { if (failed) return; const t = Date.now(); try { await fn(); console.log(`[vertex] PASS ${name} (${Date.now() - t} ms)`); } catch (e) { failed = true; console.error(`[vertex] FAIL ${name}\n${e.stack}`); } };
const inPage = (page, p) => page.evaluate(async (u) => { const r = await fetch(u, { credentials: "same-origin" }); return { status: r.status, body: await r.text() }; }, p);
const guestDirs = () => { try { return readdirSync(path.join(root, "guests")); } catch { return []; } };
const account = { cookie: "" };
async function api(method, p, body) {
  const r = await fetch(base + p, { method, headers: { "Content-Type": "application/json", Origin: base, Cookie: account.cookie }, body: body ? JSON.stringify(body) : undefined });
  const c = r.headers.get("set-cookie"); if (c) account.cookie = c.split(";")[0];
  return { status: r.status, body: await r.text() };
}

try {
  for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(base + "/api/healthz")).ok) break; } catch {} await new Promise(r => setTimeout(r, 150)); }
  browser = await (await chromium()).launch({ executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || undefined });
  const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 }, locale: "pl-PL" });
  // PH29 P1: this test checks the IndexedDB bundle store, so it pins that plugin (the person's own choice, `loom.store.policy`);
  // the OPFS + SQLite store (the preset's first choice) is covered by opfs-store-native.mjs.
  await ctx.addInitScript(() => { try { localStorage.setItem("loom.store.policy", JSON.stringify({ targets: ["browser_indexeddb"] })); } catch {} });
  ctx.on("response", async (r) => { try { if (r.url().startsWith(base)) seen.push(await r.text()); } catch { /* streamed or closed */ } });
  const page = await ctx.newPage();
  let firstGuest = null, answer = "";

  await step("first visit: no gate, proof of work in the page, the line names the real model and provider", async () => {
    await page.goto(base);
    await page.getByTestId("chat-input").waitFor({ timeout: 30000 });
    const me = JSON.parse((await inPage(page, "/api/auth/me")).body);
    firstGuest = me.guest?.id;
    assert.ok(firstGuest?.startsWith("g_"), JSON.stringify(me));
    const src = me.guest.model_source;
    assert.equal(src.provider, "vertex_gemini", JSON.stringify(src));
    console.log(`[vertex]   source: ${src.provider} ${src.model} base ${src.base_url.replace(/projects\/[^/]+/, "projects/<project>")}; skipped: ${JSON.stringify(src.skipped)}`);
    const line = await page.getByTestId("guest-model-line").innerText();
    assert.ok(line.includes("gemini-3.5-flash") && line.includes("Vertex"), line);
    await page.screenshot({ path: path.join(SHOTS, "vertex-1-first-visit.png") });
  });

  await step("question -> data-terms tap -> real streamed answer -> notice after the first answer", async () => {
    await page.getByTestId("chat-input").fill(QUESTION);
    await page.getByTestId("send-chat").click();
    const consent = page.getByTestId("guest-terms-consent");
    await consent.waitFor({ timeout: 10000 });
    const t = await consent.innerText();
    assert.ok(t.includes("Vertex") && t.includes("cloud.google.com") && t.includes("2026-10-28"), t);
    await page.screenshot({ path: path.join(SHOTS, "vertex-2-terms-consent.png") });
    const started = Date.now();
    await page.getByTestId("guest-terms-send").click();
    await page.getByTestId("guest-notice").waitFor({ timeout: 90000 });
    console.log(`[vertex]   answer done after ${Date.now() - started} ms`);
    const convs = JSON.parse((await inPage(page, "/api/conversations")).body);
    const msgs = JSON.parse((await inPage(page, `/api/conversations/${convs[0].id}/messages`)).body);
    const a = msgs.filter(m => m.role === "assistant").pop();
    answer = a?.text ?? "";
    assert.ok(answer.length > 0 && /4|cztery/i.test(answer), `answer: ${answer}`);
    console.log(`[vertex]   answer: ${JSON.stringify(answer.slice(0, 200))}`);
    await page.screenshot({ path: path.join(SHOTS, "vertex-3-answer-notice.png") });
  });

  await step("receipt: the provider and its data terms are in what was sent (provider_terms)", async () => {
    const all = JSON.stringify(await Promise.all(["/api/conversations"].map(p => inPage(page, p))));
    const convs = JSON.parse((await inPage(page, "/api/conversations")).body);
    const msgs = (await inPage(page, `/api/conversations/${convs[0].id}/messages`)).body;
    const cfg = JSON.parse((await inPage(page, "/api/config")).body);
    assert.equal(cfg.loom_provider_terms?.provider, "vertex_gemini");
    const hit = msgs.includes("provider_terms") ? "message metadata" : null;
    console.log(`[vertex]   provider_terms in: ${hit ?? "config (receipt header source)"}; training=${cfg.loom_provider_terms.training}; ${all.length > 0 ? "" : ""}`);
  });

  await step("the bundle is in the browser store (IndexedDB)", async () => {
    await until("bundle saved", async () => (await page.getByTestId("guest-layer").getAttribute("data-saved-at")) || null);
    const stored = await page.evaluate(() => new Promise((res) => {
      const r = indexedDB.open("loom-guest-store", 1);
      r.onsuccess = () => { const t = r.result.transaction("bundles", "readonly").objectStore("bundles").get("current"); t.onsuccess = () => res(t.result); };
    }));
    assert.equal(stored.schema, "loom.guest_bundle/2");  // R103 B1b: the whole working copy
    assert.ok(stored.text.includes("dwa plus dwa"));
  });

  await step("handover: B2 proof of work still required; an account works next to the guests; pool summary", async () => {
    const noPow = await fetch(base + "/api/auth/guest", { method: "POST", headers: { "Content-Type": "application/json", Origin: base }, body: "{}" });
    assert.equal(noPow.status, 403, "a start without the proof of work is refused");
    assert.ok([200, 201].includes((await api("POST", "/api/auth/register", { login: "owner", password: "owner-password-1" })).status));
    assert.equal((await api("GET", "/api/conversations")).status, 200);
    const s = JSON.parse((await api("GET", "/api/pool/summary")).body);
    console.log(`[vertex]   pool summary: ${JSON.stringify({ accepted: s.accepted ?? s.counters ?? null, rejected: s.rejected ?? null, spend: s.spend?.providers, token_refreshes: s.token_refreshes })}`);
    assert.ok(s.spend?.providers?.vertex_gemini?.requests >= 1, JSON.stringify(s.spend));
  });

  await step("close: after the idle TTL the guest directory is gone; a new visit restores the conversation", async () => {
    await page.close();
    await ctx.clearCookies();
    await until("the sweep", async () => { await fetch(base + "/api/healthz"); return guestDirs().length === 0 || null; }, (TTL + 15) * 1000);
    const p2 = await ctx.newPage();
    await p2.goto(base);
    await p2.getByTestId("guest-restored").waitFor({ timeout: 30000 });
    const me = JSON.parse((await inPage(p2, "/api/auth/me")).body);
    assert.ok(me.guest?.id && me.guest.id !== firstGuest);
    const convs = JSON.parse((await inPage(p2, "/api/conversations")).body);
    const msgs = (await inPage(p2, `/api/conversations/${convs[0].id}/messages`)).body;
    assert.ok(msgs.includes("dwa plus dwa") && msgs.includes(answer.slice(0, 10)), msgs.slice(0, 400));
    await p2.screenshot({ path: path.join(SHOTS, "vertex-4-restored.png") });
  });
} catch (e) {
  failed = true;
  console.error(`[vertex] FAIL setup\n${e.stack}`);
} finally {
  await browser?.close();
  server.kill("SIGTERM");
  await new Promise(r => server.on("exit", r));
}
// No token anywhere: answers the page saw, files of the accounts root, the server log.
const leaks = [];
if (seen.some(b => b.includes("ya29."))) leaks.push("an answer");
for (const f of existsSync(root) ? walk(root) : []) if (statSync(f).size < 64e6 && readFileSync(f).includes("ya29.")) leaks.push(f);
if (readFileSync(logPath, "utf8").includes("ya29.")) leaks.push("server log");
if (leaks.length) { failed = true; console.error(`[vertex] FAIL token found in: ${leaks.join(", ")}`); }
else console.log(`[vertex] PASS no access token in ${seen.length} answers, the accounts root or the server log`);
try { console.log(`[vertex] spend ledger: ${readFileSync(path.join(root, "guest_spend.json"), "utf8").replace(/\s+/g, " ")}`); } catch { /* none */ }
console.log(failed ? "[vertex] FAILED" : `[vertex] OK (screenshots: ${SHOTS}; server log: ${logPath})`);
process.exit(failed ? 1 : 0);
