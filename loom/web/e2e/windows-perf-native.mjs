#!/usr/bin/env node
// W10 of OKNA_SYSTEM (§8.3): a measurement harness for many windows, the REAL loom-server, headless Chromium (NOT WebView on a phone: those
// numbers belong to the cloud session S2 on the emulator). For each policy of window_keep_mounted (visible, all) and each count of windows
// (presets in e2e/windows-perf.presets.json: 1/4/8/16, panels of mixed kinds) it opens the windows floating, then measures:
//   - JS heap (CDP Performance.getMetrics JSHeapUsedSize after HeapProfiler.collectGarbage), DOM node count, layout/style/script time;
//   - open: time from the window step to two animation frames later, and the layout/script time it cost;
//   - switch: focus another window (rounds, average);
//   - hidden: every panel minimized - heap and nodes again (visible unmounts them, all keeps them mounted hidden);
//   - back: restoring one minimized window (remount under visible, show under all);
//   - tti: reload with the windows restored - navigation start to the chat input and every window drawn;
//   - close: closing each window (average).
// Results go as JSON to a temp dir (LOOM_PERF_OUT=<dir> to choose) and as a table to stdout. Deterministic checks (windows drawn, what is
// mounted under each policy, the scroll a remounted window comes back with, the advisory many_windows) fail the run; the numbers never do,
// unless LOOM_PERF_STRICT=1 and a `gross` preset is crossed.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { cpus, totalmem } from "node:os";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const P = JSON.parse(readFileSync(path.join(here, "windows-perf.presets.json"), "utf8"));
const COUNTS = (process.env.LOOM_PERF_COUNTS || "").split(",").map(Number).filter((n) => Number.isFinite(n) && n >= 1);
const counts = COUNTS.length ? COUNTS : P.counts;
const OUT = process.env.LOOM_PERF_OUT || makeTmpDir("loom-windows-perf-", { evidence: true });
const STRICT = process.env.LOOM_PERF_STRICT === "1";
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[windows-perf] build loom-server and `npm run build` first"); process.exit(2); }

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }
const round = (v, d = 1) => Math.round(v * 10 ** d) / 10 ** d;
const avg = (a) => (a.length ? a.reduce((x, y) => x + y, 0) / a.length : 0);

let failed = false;
const warnings = [];
const results = [];
const launch = await chromium();
const browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
const version = browser.version();

async function run(policy, n) {
  const port = await freePort();
  const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", makeTmpDir("loom-perf-"), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
  const base = `http://127.0.0.1:${port}`;
  const ctx = await browser.newContext({ viewport: P.viewport });
  const tag = `${policy}/${n}`;
  const r = { policy, windows: n };
  try {
    await waitUp(`${base}/api/healthz`);
    const put = async (k, v) => { const res = await fetch(`${base}/api/config/${k}`, { method: "PUT", headers: { "content-type": "application/json" }, body: JSON.stringify({ value: v }) }); assert.ok(res.ok, `PUT ${k}: ${res.status}`); };
    await put("ui_language", "pl");
    await put("window_keep_mounted", policy);
    const page = await ctx.newPage();
    const errors = [];
    page.on("pageerror", (e) => errors.push(String(e)));
    const cdp = await ctx.newCDPSession(page);
    await cdp.send("Performance.enable");
    const metrics = async (gc = false) => {
      if (gc) await cdp.send("HeapProfiler.collectGarbage");
      const { metrics: m } = await cdp.send("Performance.getMetrics");
      const o = Object.fromEntries(m.map((x) => [x.name, x.value]));
      return { heap: o.JSHeapUsedSize, nodes: o.Nodes, layout: o.LayoutDuration * 1000, style: o.RecalcStyleDuration * 1000, script: o.ScriptDuration * 1000, task: o.TaskDuration * 1000 };
    };
    // one window step (the client step of a sentence) and two frames: the time a person waits for the screen
    const stepTimed = async (action, args) => {
      const before = await metrics();
      const ms = await page.evaluate(async ([action, args]) => {
        const t0 = performance.now();
        const detail = { action, args };
        window.dispatchEvent(new CustomEvent("loom:window-step", { detail }));
        await new Promise((res) => requestAnimationFrame(() => requestAnimationFrame(res)));
        if (detail.outcome?.state === "failed") throw new Error(`${action} ${JSON.stringify(args)}: ${JSON.stringify(detail.outcome)}`);
        return performance.now() - t0;
      }, [action, args]);
      const after = await metrics();
      return { ms, layout: after.layout - before.layout, style: after.style - before.style, script: after.script - before.script };
    };
    const frames = () => page.evaluate(() => ({ drawn: document.querySelectorAll("section.win-frame").length, parked: document.querySelectorAll("[data-window-parked]").length }));
    // the windows' content settles (panels that load their data say „Wczytuję…” first); not settling in time is reported, not failed
    const settle = async () => { try { await page.waitForFunction(() => ![...document.querySelectorAll("section.win-frame")].some((e) => /Wczytuj|Loading/.test(e.innerText)), null, { timeout: 15000, polling: 100 }); await page.waitForTimeout(200); return true; } catch { return false; } };
    await page.goto(base + "/");
    await page.getByTestId("chat-input").waitFor({ timeout: 30000 });
    const panels = P.panels.slice(0, n - 1);
    const opens = [];
    for (const p of panels) {
      const a = await stepTimed("window.open", { target: p, placement: "float" });
      const b = await stepTimed("window.float", { target: p });
      opens.push({ ms: a.ms + b.ms, layout: a.layout + b.layout, style: a.style + b.style, script: a.script + b.script });
    }
    r.settled = await settle();
    const f = await frames();
    assert.equal(f.drawn, n - 1, `${tag}: ${n - 1} floating frames drawn, got ${JSON.stringify(f)}`);
    const drawnM = await metrics(true);
    r.heap_mb = round(drawnM.heap / 1048576); r.nodes = drawnM.nodes;
    r.dom_elements = await page.evaluate(() => document.getElementsByTagName("*").length);
    if (process.env.LOOM_PERF_DEBUG) console.log(await page.evaluate(() => [...document.querySelectorAll('section.win-frame')].map(e => e.dataset.testid + ': ' + e.querySelectorAll('*').length + ' ' + e.innerText.slice(0, 120).replace(/\s+/g, ' ')).join('\n')));
    r.open_ms = round(avg(opens.map((o) => o.ms))); r.open_layout_ms = round(avg(opens.map((o) => o.layout + o.style))); r.open_script_ms = round(avg(opens.map((o) => o.script)));
    // the advisory many_windows: above the desktop preset (12) it is shown, at or below it is not
    const chip = await page.locator('[data-testid="window-advisory"][data-code="many_windows"]').count();
    assert.equal(chip > 0, n > 12, `${tag}: many_windows shown=${chip > 0}`);
    // switch between windows
    const sw = [];
    for (let k = 0; k < (P.switch_rounds ?? 3) && panels.length > 1; k++) for (const p of panels) sw.push(await stepTimed("window.focus", { target: p }));
    r.switch_ms = round(avg(sw.map((o) => o.ms))); r.switch_layout_ms = round(avg(sw.map((o) => o.layout + o.style))); r.switch_script_ms = round(avg(sw.map((o) => o.script)));
    // tti: reload with the windows restored
    await page.reload();
    await page.getByTestId("chat-input").waitFor({ timeout: 30000 });
    await page.waitForFunction((want) => document.querySelectorAll("section.win-frame").length >= want, n - 1, { timeout: 30000, polling: 16 });
    r.tti_ms = round(await page.evaluate(() => performance.now()));
    await settle();
    // the scroll of a remounted window (visible) / of a hidden mounted one (all) comes back
    let scrollKept = null;
    if (panels.includes("settings")) {
      const set = await page.evaluate(() => {
        const root = document.querySelector('[data-testid="window-settings"] .win-body');
        const el = root && [root, ...root.querySelectorAll("*")].find((e) => e.scrollHeight > e.clientHeight + 200 && /(auto|scroll)/.test(getComputedStyle(e).overflowY));
        if (!el) return null; el.scrollTop = 200; return el.scrollTop;
      });
      if (set) {
        await stepTimed("window.minimize", { target: "settings" });
        await stepTimed("window.restore", { target: "settings" });
        await settle();
        const kept = () => page.evaluate(() => {
          const root = document.querySelector('[data-testid="window-settings"] .win-body');
          const el = root && [root, ...root.querySelectorAll("*")].find((e) => e.scrollTop > 0);
          return el ? el.scrollTop : 0;
        });
        for (const end = Date.now() + 5000; Date.now() < end && Math.abs((scrollKept = await kept()) - set) > 1;) await page.waitForTimeout(100);
        assert.ok(Math.abs(scrollKept - set) <= 1, `${tag}: settings scroll ${set} came back as ${scrollKept}`);
      }
    }
    r.scroll_kept = scrollKept;
    // hidden: every panel minimized
    for (const p of panels) await stepTimed("window.minimize", { target: p });
    await page.waitForTimeout(300);
    const h = await frames();
    assert.equal(h.drawn, 0, `${tag}: nothing floating after minimizing, ${JSON.stringify(h)}`);
    if (policy === "all") assert.ok(h.parked >= n - 1, `${tag}: every hidden window parked mounted under all: ${JSON.stringify(h)}`);
    else assert.equal(h.parked, 0, `${tag}: nothing parked under ${policy}: ${JSON.stringify(h)}`);
    r.parked = h.parked;
    const hidM = await metrics(true);
    r.hidden_heap_mb = round(hidM.heap / 1048576); r.hidden_nodes = hidM.nodes;
    // back: restore one minimized window
    if (panels.length) { const b = await stepTimed("window.restore", { target: panels[panels.length - 1] }); r.back_ms = round(b.ms); r.back_script_ms = round(b.script); }
    // close every window
    const cl = [];
    for (const p of panels) cl.push(await stepTimed("window.close", { target: p }));
    r.close_ms = round(avg(cl.map((o) => o.ms)));
    r.page_errors = errors.length;
    if (errors.length) console.log(`[windows-perf] ${tag} page errors (reported, not failed): ${errors.slice(0, 3).join(" | ")}`);
    const g = P.gross ?? {};
    const over = [];
    if (n > 1 && g.heap_mb_per_window && r.heap_mb / n > g.heap_mb_per_window) over.push(`heap ${r.heap_mb} MB / ${n}`);
    if (g.open_ms && r.open_ms > g.open_ms) over.push(`open ${r.open_ms} ms`);
    if (g.switch_ms && r.switch_ms > g.switch_ms) over.push(`switch ${r.switch_ms} ms`);
    if (g.tti_ms && r.tti_ms > g.tti_ms) over.push(`tti ${r.tti_ms} ms`);
    if (g.nodes_per_window && r.nodes / n > g.nodes_per_window) over.push(`nodes ${r.nodes} / ${n}`);
    for (const o of over) warnings.push(`${tag}: gross preset crossed: ${o}`);
    console.log(`[windows-perf] PASS ${tag}`);
  } catch (e) {
    failed = true; console.error(`[windows-perf] FAIL ${tag}\n${e.stack}`);
  } finally {
    await ctx.close();
    server.kill();
  }
  results.push(r);
}

const t0 = Date.now();
try {
  for (const policy of P.policies) for (const n of counts) await run(policy, n);
} finally { await browser.close(); }
const report = { schema: "loom.windows_perf_result/1", date: new Date().toISOString(), machine: { cpus: cpus().length, cpu: cpus()[0]?.model, mem_gb: round(totalmem() / 2 ** 30), platform: process.platform }, browser: `headless Chromium ${version}`, viewport: P.viewport, note: "Dated observation of this machine and this browser; WebView on a phone is not measured here (S2, emulator).", seconds: round((Date.now() - t0) / 1000), results, warnings };
const file = path.join(OUT, "windows-perf.json");
writeFileSync(file, JSON.stringify(report, null, 2));
const cols = ["policy", "windows", "heap_mb", "nodes", "dom_elements", "open_ms", "open_layout_ms", "open_script_ms", "switch_ms", "switch_layout_ms", "tti_ms", "hidden_heap_mb", "hidden_nodes", "parked", "back_ms", "close_ms", "scroll_kept", "page_errors"];
console.log(cols.join("\t"));
for (const r of results) console.log(cols.map((c) => r[c] ?? "-").join("\t"));
for (const w of warnings) console.log(`[windows-perf] WARN ${w}`);
console.log(`[windows-perf] ${report.browser}, ${report.seconds} s, results in ${file}`);
process.exit(failed || (STRICT && warnings.length) ? 1 : 0);
