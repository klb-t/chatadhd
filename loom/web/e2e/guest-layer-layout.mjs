#!/usr/bin/env node
// The guest layer (model line, notice, store) has its own place in the layout and never covers the chat or its controls
// (owner's screenshot 2026-10-30, 390 px wide: "tekst na tekście"). Real loom-server in accounts mode with guests on by a
// TEST overlay, a mock free provider (mock_llm_server.py; the key is a fake one from a test keys file; nothing real).
// At 390x844 the bounding rectangles of the layer and of the chat, the input and the send button must not intersect:
// on the first look (layer open), with the notice after the first answer, after folding it, after a reload (the folded
// state is remembered) and after unfolding it again; every notice button is the element at its own centre (not covered).
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { request } from "./harness-lifecycle.mjs";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[layer] build loom-server and `npm run build` first"); process.exit(2); }
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-guest-layer-shots-", { evidence: true });
mkdirSync(SHOTS, { recursive: true });
const FAKE_KEY = "sk-guest-layer-not-a-real-key";
const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await request(url, { timeoutMs: 3000 })).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error(`timed out waiting for ${url}`); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const mockPort = await freePort(), port = await freePort();
const tmp = makeTmpDir("loom-guest-layer-");
const root = path.join(tmp, "root");
mkdirSync(path.join(root, "profiles"), { recursive: true });
writeFileSync(path.join(tmp, "keys.env"), `MOCK_FREE_KEY=${FAKE_KEY}\n`);
const overlay = (domain, overrides) => writeFileSync(path.join(root, "profiles", `${domain}.pack`), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain, overrides }));
overlay("server_accounts", { allow_private_hosts: ["127.0.0.1"], guests: { enabled: true, start: "auto", sweep_interval_seconds: 0, keys_file: path.join(tmp, "keys.env"),
  first_answer_notice: { on: true, ask_again: "per_device" }, workdir: { subdir: "guests", idle_ttl_seconds: 600, max_bytes: 0, keep_after_session: "ask" },
  secrets_routes: "write_only_session", model_sources: ["byok", "free", "local"] } });
overlay("free_providers", { providers: [{ id: "mock_free", display_name: "Mock darmowy", base_url: `http://127.0.0.1:${mockPort}`, auth_env: "MOCK_FREE_KEY", auth_secret: "api_key",
  tier: "free", active_when: "key_present", enabled: true,
  data_terms: { retention: "none_by_default", human_review: false, training: "no", observed_at: "2026-10-30", source_url: "test", source_rating: "test", summary: { pl: "Testowy dostawca nic nie zapisuje.", en: "The test provider keeps nothing." } } }],
  free_model_choice: { rank_source: "table", require: { training: "no" }, table: [{ model: "mock/e2e-model", provider: "mock_free", quality: 1 }] } });
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--accounts-root", root, "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false;
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[layer] PASS ${name}`); } catch (e) { failed = true; console.error(`[layer] FAIL ${name}\n${e.stack}`); } };

const box = async (page, id) => { const b = await page.getByTestId(id).first().boundingBox(); assert.ok(b, `${id} has a box`); return b; };
const meets = (a, b) => a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
async function noOverlap(page, label) {
  const layer = await box(page, "guest-layer");
  assert.ok(layer.width > 0 && layer.height > 0, `${label}: the layer is visible`);
  for (const id of ["guest-body", "chat-input", "send-chat"]) {
    const b = await box(page, id);
    assert.ok(!meets(layer, b), `${label}: the layer ${JSON.stringify(layer)} meets ${id} ${JSON.stringify(b)}`);
  }
  // inside the viewport, no horizontal spill
  assert.ok(layer.x >= 0 && layer.x + layer.width <= 390 + 0.5 && layer.y + layer.height <= 844 + 0.5, `${label}: the layer stays on the screen ${JSON.stringify(layer)}`);
  // every control of the layer is the element at its own centre (nothing lies over it)
  const covered = await page.evaluate(() => [...document.querySelectorAll('[data-testid="guest-layer"] button, [data-testid="guest-layer"] summary')].filter(el => {
    const r = el.getBoundingClientRect(); if (!r.width || !r.height) return false;
    const at = document.elementFromPoint(r.x + r.width / 2, r.y + r.height / 2);
    return !(at === el || el.contains(at));
  }).map(el => el.getAttribute("data-testid") || el.textContent));
  assert.deepEqual(covered, [], `${label}: covered controls`);
}

try {
  await waitUp(`${base}/api/healthz`);
  browser = await (await chromium()).launch({ executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || undefined });
  const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, locale: "pl-PL" });
  await ctx.addInitScript(() => { try { localStorage.setItem("loom.store.policy", JSON.stringify({ targets: ["browser_indexeddb"] })); } catch {} });
  const page = await ctx.newPage();

  await step("first look: the layer is open, has its own place, covers nothing", async () => {
    await page.goto(base);
    await page.getByTestId("chat-input").waitFor({ timeout: 20000 });
    await page.getByTestId("guest-model-line").waitFor();
    assert.equal(await page.getByTestId("guest-layer").getAttribute("data-open"), "true");
    await noOverlap(page, "open");
    await page.screenshot({ path: path.join(SHOTS, "layer-1-open.png") });
  });
  await step("after the first answer the notice with its buttons is in the layer, nothing covered", async () => {
    await page.getByTestId("chat-input").fill("Hej, jak się masz?");
    await page.getByTestId("send-chat").click();
    await page.getByTestId("guest-notice").waitFor({ timeout: 30000 });
    for (const id of ["guest-notice-close", "guest-never", "guest-sign-in"]) await page.getByTestId(id).waitFor();
    await noOverlap(page, "notice");
    await page.screenshot({ path: path.join(SHOTS, "layer-2-notice.png") });
    await page.getByTestId("guest-notice-close").click();
  });
  await step("fold it: one line, remembered after a reload; unfold again: still no overlap", async () => {
    await page.getByTestId("guest-layer-toggle").click();
    assert.equal(await page.getByTestId("guest-layer").getAttribute("data-open"), "false");
    await noOverlap(page, "folded");
    await page.reload();
    await page.getByTestId("chat-input").waitFor({ timeout: 20000 });
    assert.equal(await page.getByTestId("guest-layer").getAttribute("data-open"), "false", "folded state remembered");
    await noOverlap(page, "folded after reload");
    await page.getByTestId("guest-layer-toggle").click();
    assert.equal(await page.getByTestId("guest-layer").getAttribute("data-open"), "true");
    await noOverlap(page, "unfolded again");
    await page.screenshot({ path: path.join(SHOTS, "layer-3-unfolded.png") });
  });
  await step("the model line says the source is free (tier free) in the page's words", async () => {
    const line = await page.getByTestId("guest-model-line").innerText();
    assert.ok(line.includes("mock/e2e-model") && (line.includes("darmowych modeli") || line.includes("free models")), line);
  });
} catch (e) {
  failed = true;
  console.error(`[layer] FAIL setup\n${e.stack}`);
} finally {
  await browser?.close();
  server.kill("SIGTERM");
  mock.kill("SIGTERM");
}
console.log(failed ? "[layer] FAILED" : `[layer] OK (screenshots: ${SHOTS})`);
process.exit(failed ? 1 : 0);
