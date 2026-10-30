#!/usr/bin/env node
// A browser store whose bundle holds rows but no conversation (only desktop pins, say) left /api/conversations empty after
// the restore, so every page restored and reloaded again, for ever (production, 2d39b07c). On the real loom-server with
// guests on (test overlay), real web build: such a bundle gives exactly one reload, then none. The pin row is synthetic; the rest is the server's own export.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdirSync, writeFileSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const freePort = () => new Promise((res) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); });
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const root = makeTmpDir("loom-guest-noloop-");
mkdirSync(path.join(root, "profiles"), { recursive: true });
writeFileSync(path.join(root, "profiles/server_accounts.pack"), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain: "server_accounts", overrides: { guests: { enabled: true } } }));
const port = await freePort();
const base = `http://127.0.0.1:${port}`;
const srv = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--accounts-root", root, "--data-dir", root, "--static-dir", path.join(LOOM, "web/dist")], { stdio: "ignore" });
let browser;
try {
  for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(`${base}/api/healthz`)).ok) break; } catch {} await new Promise(r => setTimeout(r, 200)); }
  browser = await (await chromium()).launch();
  const page = await (await browser.newContext()).newPage();
  let loads = 0;
  page.on("framenavigated", (f) => { if (f === page.mainFrame()) loads += 1; });
  await page.goto(base);
  await page.waitForTimeout(3000);
  // A pins-only bundle: the server's own export (so its integrity hash is genuine) with the tables cut down to one row of `nodes` (synthetic,
  // id n_revfixpin1) and no conversation. The hash is recomputed here over the same compact JSON the server writes (checked below).
  const real = await page.evaluate(async () => await (await fetch("/api/working-copy/bundle", { credentials: "same-origin" })).text());
  const head = JSON.parse(real);
  const ctext = JSON.stringify(head.content);
  assert.ok(real.includes(`"content":${ctext}`), "the compact JSON of the content is what the server hashed");
  const cols = head.content.tables.nodes.columns;
  const row = cols.map((c) => ({ id: "n_revfixpin1", kind: "pin", label: "revfix pin", created: "2026-10-30T00:00:00Z" })[c] ?? "");
  const content = { ...head.content, tables: { nodes: { columns: cols, rows: [row] } }, memory: [], config: {} };
  const bundle = { ...head, content, integrity: { algorithm: "sha256", content_sha256: createHash("sha256").update(JSON.stringify(content)).digest("hex") } };
  const text = JSON.stringify(bundle);
  await page.evaluate((t) => new Promise((res, rej) => {
    const r = indexedDB.open("loom-guest-store", 1);
    r.onerror = () => rej(r.error);
    r.onsuccess = () => {
      const db = r.result, name = db.objectStoreNames[0], tx = db.transaction(name, "readwrite"), st = tx.objectStore(name);
      st.getAllKeys().onsuccess = (e) => { const key = e.target.result[0] ?? "bundle"; st.put({ schema: "loom.guest_bundle/2", text: t, saved_at: new Date().toISOString() }, key); };
      tx.oncomplete = () => res(); tx.onerror = () => rej(tx.error);
    };
  }), text);
  const visit = async (what, expect) => {
    const before = loads;
    if (what === "new session") await page.context().clearCookies();
    await page.reload();
    await page.waitForTimeout(9000);
    const n = loads - before;
    console.log(`[guest-noloop] ${what}: page loads ${n} (expected ${expect})`);
    assert.equal(n, expect, `${what}: exactly ${expect} page loads, saw ${n}`);
  };
  // 1. a new session, the bundle lacks one row: restored once = the visit and one reload
  await visit("new session", 2);
  const has = await page.evaluate(async () => (await (await fetch("/api/working-copy/bundle", { credentials: "same-origin" })).text()).includes("n_revfixpin1"));
  assert.ok(has, "the pin row came back into the working copy");
  // 2. later visits: the working copy has everything the stored bundle carries -> no restore, no reload, at any number of visits
  await visit("next visit", 1);
  await visit("and the one after", 1);
  console.log("[guest-noloop] PASSED");
} finally { await browser?.close(); srv.kill(); }
