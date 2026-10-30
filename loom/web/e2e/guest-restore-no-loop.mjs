#!/usr/bin/env node
// A browser store whose bundle holds rows but no conversation (only desktop pins, say) left /api/conversations empty after
// the restore, so every page restored and reloaded again, for ever (production, 2d39b07c). On the real loom-server with
// guests on (test overlay), real web build: such a bundle gives at most one reload. Bundle contents are synthetic.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
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
  // Whatever save the page made is rewritten so the bundle has rows and no conversation.
  await page.evaluate(() => new Promise((res, rej) => {
    const r = indexedDB.open("loom-guest-store", 1);
    r.onerror = () => rej(r.error);
    r.onsuccess = () => {
      const db = r.result, name = db.objectStoreNames[0], tx = db.transaction(name, "readwrite"), st = tx.objectStore(name);
      const text = JSON.stringify({ content: { tables: {}, memory: [{ id: "x", content: "test" }] } });
      st.getAllKeys().onsuccess = (e) => { const key = e.target.result[0] ?? "bundle"; st.put({ schema: "loom.guest_bundle/2", text, saved_at: new Date().toISOString() }, key); };
      tx.oncomplete = () => res(); tx.onerror = () => rej(tx.error);
    };
  }));
  const before = loads;
  await page.context().clearCookies();
  await page.reload();
  await page.waitForTimeout(12000);
  const n = loads - before;
  console.log(`[guest-noloop] page loads after the visit: ${n}`);
  assert.ok(n <= 2, `at most the visit and one reload, saw ${n}`);
  console.log("[guest-noloop] PASSED");
} finally { await browser?.close(); srv.kill(); }
