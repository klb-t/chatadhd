#!/usr/bin/env node
// Android smoke, phone side. Drives the installed ChatADHD debug APK on an emulator, no dependencies (Node 22):
//   * the native connection screen with adb + UI Automator (`uiautomator dump` gives the widget tree and bounds,
//     `input tap` / `input text` act on it);
//   * the web app inside the WebView over the WebView DevTools socket (adb forward -> Chrome DevTools Protocol;
//     MainActivity opens it in debuggable builds only). The page has stable data-testid hooks, so this is far more
//     robust than tapping at pixels inside a WebView.
// Every wait is a poll on a condition with a timeout (never a bare sleep). Screenshots: adb screencap.
// Environment: SMOKE_OUT (output dir), SMOKE_URL (server as the phone sees it), SMOKE_LOGIN, SMOKE_PASS, SMOKE_KEY,
// optional SMOKE_PKG (default com.chatadhd.android), SMOKE_TIMEOUT_S (scale factor of waits, default 1).
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync, appendFileSync } from "node:fs";
import path from "node:path";

const need = n => process.env[n] || (() => { throw new Error("missing env " + n); })();
const OUT = need("SMOKE_OUT"), URL_ = need("SMOKE_URL"), LOGIN = need("SMOKE_LOGIN"), PASS = need("SMOKE_PASS"), KEY = need("SMOKE_KEY");
const PKG = process.env.SMOKE_PKG || "com.chatadhd.android";
const SCALE = Number(process.env.SMOKE_TIMEOUT_S || 1);
const SHOTS = path.join(OUT, "shots");
mkdirSync(SHOTS, { recursive: true });
const t0 = Date.now();
const log = m => { const l = `[${((Date.now() - t0) / 1000).toFixed(1).padStart(6)}s] ${m}`; console.error(l); appendFileSync(path.join(OUT, "drive.log"), l + "\n"); };
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ---------- adb ----------
const adb = (...a) => execFileSync("adb", a, { encoding: "utf8", maxBuffer: 64 << 20, timeout: 60000 });
let shotN = 0;
function shot(name) {
  try {
    const png = execFileSync("adb", ["exec-out", "screencap", "-p"], { maxBuffer: 64 << 20, timeout: 30000 });
    const f = path.join(SHOTS, String(++shotN).padStart(2, "0") + "-" + name + ".png");
    writeFileSync(f, png);
  } catch (e) { log("screencap failed: " + e.message); }
}

// ---------- waiting ----------
async function until(what, fn, timeoutS, every = 250) {
  const end = Date.now() + timeoutS * SCALE * 1000;
  let last;
  for (;;) {
    try { const v = await fn(); if (v) return v; } catch (e) { last = e; }
    if (Date.now() > end) throw new Error(`timeout (${timeoutS * SCALE}s) waiting for: ${what}` + (last ? ` (last error: ${last.message})` : ""));
    await sleep(every);
  }
}

// ---------- UI Automator ----------
const unesc = s => s.replace(/&quot;/g, '"').replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&apos;/g, "'").replace(/&amp;/g, "&");
export function parseNodes(xml) {
  const nodes = [];
  for (const m of xml.matchAll(/<node\s([^>]*?)\/?>/g)) {
    const a = {};
    for (const kv of m[1].matchAll(/([\w:-]+)="([^"]*)"/g)) a[kv[1]] = unesc(kv[2]);
    const b = /\[(\d+),(\d+)\]\[(\d+),(\d+)\]/.exec(a.bounds || "");
    if (b) { a.cx = (+b[1] + +b[3]) >> 1; a.cy = (+b[2] + +b[4]) >> 1; }
    nodes.push(a);
  }
  return nodes;
}
function dump() {
  adb("shell", "uiautomator", "dump", "/sdcard/ui.xml");
  return parseNodes(execFileSync("adb", ["exec-out", "cat", "/sdcard/ui.xml"], { encoding: "utf8", maxBuffer: 16 << 20 }));
}
const find = (nodes, pred) => nodes.find(n => n.cx !== undefined && pred(n));
async function waitNode(what, pred, timeoutS = 30) {
  return until("native widget: " + what, () => find(dump(), pred), timeoutS, 500);
}
const tap = n => adb("shell", "input", "tap", String(n.cx), String(n.cy));
const textOf = n => `${n.text || ""} ${n["content-desc"] || ""}`;

async function connectScreen() {
  log("native: connection screen (first run opens it by itself)");
  const radio = await waitNode("'Server account' radio", n => /RadioButton/.test(n.class) && /Server account/i.test(n.text), 60);
  shot("connection-screen");
  tap(radio);
  const edit = await waitNode("enabled URL field", n => /EditText/.test(n.class) && n.enabled === "true");
  tap(edit);
  adb("shell", "input", "text", URL_);
  await until("URL typed into the field", () => find(dump(), n => /EditText/.test(n.class) && n.text === URL_), 15, 500);
  // the soft keyboard may cover the button; BACK closes only the keyboard while it is shown
  if (/mInputShown=true/.test(adb("shell", "dumpsys", "input_method"))) adb("shell", "input", "keyevent", "KEYCODE_BACK");
  const box = await waitNode("plain-HTTP checkbox", n => /CheckBox/.test(n.class));
  if (box.checked !== "true") tap(box);
  await until("plain-HTTP checkbox ticked", () => find(dump(), n => /CheckBox/.test(n.class) && n.checked === "true"), 10, 500);
  shot("connection-filled");
  tap(await waitNode("'Check and save' button", n => /Button/.test(n.class) && /Check and save/i.test(n.text)));
  // success = the chooser finishes and MainActivity shows a WebView; failure = the status text stays on the screen
  await until("the connection screen to close (server address accepted)", () => {
    const nodes = dump();
    const err = nodes.find(n => /Cannot reach|does not look like a Loom|HTTPS is required|only for LAN|not a valid address/i.test(n.text || ""));
    if (err) throw new Error("connection screen refused the address: " + err.text);
    return nodes.some(n => n.class === "android.webkit.WebView");
  }, 60, 750);
  log("native: server mode saved, WebView is up");
  shot("webview-up");
}

// ---------- Chrome DevTools Protocol over the WebView socket ----------
const PORT = 9222;
async function openPage() {
  const sock = await until("the WebView DevTools socket", () => {
    const pid = adb("shell", "pidof", PKG).trim().split(/\s+/)[0];
    const unix = adb("shell", "cat", "/proc/net/unix");
    const m = pid && new RegExp(`@(webview_devtools_remote_${pid})\\b`).exec(unix);
    return m && m[1];
  }, 60, 500);
  adb("forward", `tcp:${PORT}`, `localabstract:${sock}`);
  const target = await until(`a page at ${URL_}`, async () => {
    const list = await (await fetch(`http://127.0.0.1:${PORT}/json`)).json();
    return list.find(t => t.type === "page" && t.url.startsWith(URL_) && t.webSocketDebuggerUrl);
  }, 60, 500);
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error("DevTools websocket failed")); });
  let id = 0; const pending = new Map();
  ws.onmessage = ev => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { const { res, rej } = pending.get(m.id); pending.delete(m.id); m.error ? rej(new Error(m.error.message)) : res(m.result); }
    else if (m.method === "Runtime.consoleAPICalled" && ["error", "warning"].includes(m.params.type))
      appendFileSync(path.join(OUT, "webview-console.txt"), `${m.params.type}: ${m.params.args.map(a => a.value ?? a.description ?? "").join(" ")}\n`);
    else if (m.method === "Runtime.exceptionThrown")
      appendFileSync(path.join(OUT, "webview-console.txt"), `exception: ${m.params.exceptionDetails.text} ${m.params.exceptionDetails.exception?.description ?? ""}\n`);
  };
  const send = (method, params = {}) => new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method, params })); });
  await send("Runtime.enable");
  return {
    async ev(expr) {
      const r = await send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true });
      if (r.exceptionDetails) throw new Error("page script: " + (r.exceptionDetails.exception?.description ?? r.exceptionDetails.text));
      return r.result.value;
    },
    close() { ws.close(); },
  };
}

const q = tid => `document.querySelector('[data-testid="${tid}"]')`;
let page;
const has = tid => page.ev(`!!${q(tid)}`);
const waitTid = (tid, s = 30) => until(`[data-testid=${tid}]`, () => has(tid), s);
const waitGone = (tid, s = 60) => until(`[data-testid=${tid}] to go away`, async () => !(await has(tid)), s);
const click = tid => page.ev(`(()=>{const e=${q(tid)};if(!e)return false;e.click();return true})()`);
const setVal = (tid, v) => page.ev(`(()=>{const e=${q(tid)};if(!e)return false;
  const proto=e.tagName==='TEXTAREA'?HTMLTextAreaElement.prototype:HTMLInputElement.prototype;
  Object.getOwnPropertyDescriptor(proto,'value').set.call(e,${JSON.stringify(v)});
  e.dispatchEvent(new Event('input',{bubbles:true}));return true})()`);
const assistantSays = needle => page.ev(`[...document.querySelectorAll('.msg.assistant .body')].some(b=>b.innerText.includes(${JSON.stringify(needle)}))`);

// The send button is briefly disabled while the conversation view loads (data-gate-reason). Same loop as
// deploy/first-steps-ui.mjs: wait until it is ready, click, and click again when the field did not empty.
async function send(text) {
  await waitTid("chat-input");
  await setVal("chat-input", text);
  for (let attempt = 0; attempt < 5; attempt++) {
    await until("send button ready", () => page.ev(`(()=>{const b=${q("send-chat")};return !!b&&!b.disabled&&!b.getAttribute('data-gate-reason')})()`), 30, 100);
    await click("send-chat");
    // "waiting for confirmation" (usage_pending) is a state, not an error: the text stays, the user confirms
    if (await until("usage notice (soft)", () => has("chat-usage-pending"), 1.5, 150).then(() => true, () => false)) {
      log("usage_pending shown, confirming");
      await click("chat-usage-pending-confirm");
      await waitGone("chat-usage-pending", 30).catch(() => {});
    }
    if (await until("input emptied", async () => (await page.ev(`${q("chat-input")}?.value ?? ''`)) === "", 3, 100).then(() => true, () => false)) return;
  }
  throw new Error("the message was not sent (the field never emptied): " + text);
}

// ---------- scenario ----------
const results = [];
async function step(name, fn) {
  const t = Date.now();
  try { await fn(); results.push({ step: name, ok: true, s: (Date.now() - t) / 1000 }); log("OK   " + name); }
  catch (e) {
    results.push({ step: name, ok: false, why: e.message });
    log("FAIL " + name + ": " + e.message);
    shot("FAIL-" + name.replace(/\W+/g, "-").slice(0, 40));
    if (page) {
      const body = await page.ev(`document.body.innerText.slice(0,3000)`).catch(() => "?");
      writeFileSync(path.join(OUT, "fail-page-text.txt"), String(body));
      writeFileSync(path.join(OUT, "fail-page-html.html"), String(await page.ev(`document.documentElement.outerHTML.slice(0,200000)`).catch(() => "?")));
    }
    try { writeFileSync(path.join(OUT, "fail-ui.xml"), execFileSync("adb", ["exec-out", "cat", "/sdcard/ui.xml"], { encoding: "utf8" })); } catch { /* none */ }
    throw e;
  }
}

try {
  adb("shell", "am", "start", "-W", "-n", `${PKG}/.MainActivity`);
  await step("1 native: choose server account (http://10.0.2.2, plain HTTP for LAN)", connectScreen);
  await step("2 attach to the WebView", async () => { page = await openPage(); await waitTid("auth-form", 60); shot("login-form"); });
  await step("3 log in to the account", async () => {
    await setVal("auth-login", LOGIN); await setVal("auth-password", PASS);
    await click("auth-submit");
    await waitTid("chat-view", 60);
    shot("logged-in");
  });
  await step("4 conversation ready", async () => {
    // a phone is narrow: the conversation list is hidden, a fresh conversation needs the sidebar
    if (!(await until("chat input (soft)", () => has("chat-input"), 8).then(() => true, () => false))) {
      const narrowToggle = await has("toggle-sidebar");
      if (narrowToggle && !(await page.ev(`!!${q("new-conversation")}&&${q("new-conversation")}.offsetParent!==null`))) await click("toggle-sidebar");
      await waitTid("new-conversation"); await click("new-conversation");
      await waitTid("chat-input");
    }
  });
  await step("5 send a message and see the reply", async () => {
    await send("Zespol Acme planuje wydac wersje 2.0 w piatek, jesli testy przejda. Odpowiedz dwoma zdaniami.");
    await until("the assistant reply", () => assistantSays("Odpowiedz testowego dostawcy"), 90, 300);
    shot("reply");
  });
  await step("6 'streść tę rozmowę' -> proposal -> 'tak' -> summary", async () => {
    await send("streść tę rozmowę");
    await waitTid("chat-action-proposal", 90);
    const shown = await until("proposal text", () => page.ev(`(${q("chat-action-proposal")}?.innerText||'')`).then(t => /summar|stresz|Streść/i.test(t) && t), 20, 200);
    log("proposal: " + shown.replace(/\s+/g, " ").slice(0, 160));
    shot("proposal");
    // the typed answer, as a user gives it (the chat recognises the yes words of the active language)
    await send("tak");
    await until("the summary", () => assistantSays("Streszczenie testowe"), 90, 300);
    if (await has("chat-error")) throw new Error("chat-error is visible: " + await page.ev(`${q("chat-error")}.innerText`));
    shot("summary");
  });
  await step("7 the provider got the key from the account settings", async () => {
    const calls = readFileSync(path.join(OUT, "provider.jsonl"), "utf8").split("\n").filter(Boolean).map(l => JSON.parse(l));
    if (!calls.some(c => c.authorization === "Bearer " + KEY)) throw new Error(`no provider call with the key (${calls.length} calls)`);
  });
  log("ALL STEPS OK");
} catch (e) {
  process.exitCode = 1;
} finally {
  writeFileSync(path.join(OUT, "results.json"), JSON.stringify(results, null, 1));
  try { page?.close(); } catch { /* closed */ }
}
