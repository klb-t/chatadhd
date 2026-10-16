#!/usr/bin/env node
// Android smoke, phone side. Drives the installed ChatADHD debug APK on an emulator, no dependencies (Node 22):
//   * first start = LOCAL full-screen chat (P20): attach CDP, menu -> "connection" (LoomBridge.openConnection) opens the
//     native connection screen (ConnectionActivity is not exported, so adb cannot start it); after saving, re-attach;
//   * the native connection screen with adb + UI Automator (`uiautomator dump` gives the widget tree and bounds,
//     `input tap` / `input text` act on it);
//   * the web app inside the WebView over the WebView DevTools socket (adb forward -> Chrome DevTools Protocol;
//     MainActivity opens it in debuggable builds only). The page has stable data-testid hooks, so this is far more
//     robust than tapping at pixels inside a WebView.
// Every wait is a poll on a condition with a timeout (never a bare sleep). Screenshots: adb screencap.
// Environment: SMOKE_OUT (output dir), SMOKE_URL (server as the phone sees it), SMOKE_LOGIN, SMOKE_PASS, SMOKE_KEY,
// optional SMOKE_PKG (default com.chatadhd.android), SMOKE_TIMEOUT_S (scale factor of waits, default 1).
// SMOKE_CDP=http://127.0.0.1:9333 (local check only): skip adb and the native screen and drive a desktop Chromium
// started with --remote-debugging-port (page already or soon at SMOKE_URL); used to test the web part off the emulator.
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync, appendFileSync } from "node:fs";
import path from "node:path";

const need = n => process.env[n] || (() => { throw new Error("missing env " + n); })();
const OUT = need("SMOKE_OUT"), URL_ = need("SMOKE_URL"), LOGIN = need("SMOKE_LOGIN"), PASS = need("SMOKE_PASS"), KEY = need("SMOKE_KEY");
const CDP_ONLY = process.env.SMOKE_CDP || "";
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
  if (CDP_ONLY) return;
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
    if (b) { a.l = +b[1]; a.t = +b[2]; a.r = +b[3]; a.b = +b[4]; a.cx = (+b[1] + +b[3]) >> 1; a.cy = (+b[2] + +b[4]) >> 1; }
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
  // a system picker left over by an earlier check would cover the native connection screen
  for (let k = 0; k < 4; k++) {
    let up = false; try { up = dump().some(n => /documentsui/i.test(n.package || "")); } catch {}
    if (!up) break;
    log("a picker is still open before the connection screen: BACK"); adb("shell", "input", "keyevent", "KEYCODE_BACK"); await sleep(1500);
  }
  log("native: connection screen (opened from the local chat menu)");
  const radio = await waitNode("'Server account' radio", n => /RadioButton/.test(n.class) && /Server account/i.test(n.text), 60);
  shot("connection-screen");
  tap(radio);
  const edit = await waitNode("enabled URL field", n => /EditText/.test(n.class) && n.enabled === "true");
  tap(edit);
  adb("shell", "input", "text", URL_);
  await until("URL typed into the field", () => find(dump(), n => /EditText/.test(n.class) && n.text === URL_), 15, 500);
  // No BACK to hide the keyboard: when the keyboard is already gone BACK finishes this screen, the app reopens it in local
  // mode and the URL field and the checkbox come back disabled. On a 2400 px screen the keyboard covers neither.
  // The checkbox label is long (several lines) and the box is its left edge: tap there, re-dump after every try,
  // scroll the form when the box is off-screen, and log what the dump says (state, bounds) so a failure explains itself.
  const [sw, sh] = (/(\d+)x(\d+)/.exec(adb("shell", "wm", "size")) || [0, 1080, 2400]).slice(1).map(Number);
  let ticked = false;
  for (let attempt = 1; attempt <= 5 && !ticked; attempt++) {
    const box = await waitNode("plain-HTTP checkbox", n => /CheckBox/.test(n.class));
    log(`native: checkbox try ${attempt}: checked=${box.checked} enabled=${box.enabled} bounds=${box.bounds} screen=${sw}x${sh}`);
    if (box.checked === "true") { ticked = true; break; }
    if (box.enabled !== "true") {  // the screen is in local mode (e.g. it was reopened): choose the server account again
      tap(await waitNode("'Server account' radio", n => /RadioButton/.test(n.class) && /Server account/i.test(n.text)));
      const field = await waitNode("enabled URL field", n => /EditText/.test(n.class) && n.enabled === "true");
      if (field.text !== URL_) { tap(field); adb("shell", "input", "text", URL_); }
      continue;
    }
    if (box.b > sh - 40 || box.t < 0) { adb("shell", "input", "swipe", String(sw >> 1), String((sh * 3) >> 2), String(sw >> 1), String(sh >> 2), "300"); continue; }
    adb("shell", "input", "tap", String(box.l + Math.min(60, (box.r - box.l) >> 1)), String(Math.min(box.cy, box.t + 60)));
    ticked = await until("plain-HTTP checkbox ticked", () => find(dump(), n => /CheckBox/.test(n.class) && n.checked === "true"), 4, 500).then(() => true, () => false);
  }
  if (!ticked) throw new Error("the plain-HTTP checkbox could not be ticked (see drive.log: bounds/state of every try)");
  shot("connection-filled");
  tap(await waitNode("'Check and save' button", n => /Button/.test(n.class) && /Check and save/i.test(n.text)));
  // success = the chooser finishes and MainActivity shows a WebView; failure = the status text stays on the screen
  await until("the connection screen to close (server address accepted)", () => {
    const nodes = dump();
    const err = nodes.find(n => /Cannot reach|does not look like a Loom|HTTPS is required|only for LAN|not a valid address/i.test(n.text || ""));
    if (err) throw new Error("connection screen refused the address: " + err.text);
    return nodes.some(n => n.class === "android.webkit.WebView");
  }, 60, 750);
  log("native: server mode saved, WebView is up (the activity may have restarted)");
  shot("webview-up");
}

// ---------- Chrome DevTools Protocol over the WebView socket ----------
const PORT = 9222;
// The app opens in LOCAL mode (full-screen chat, page served from the APK assets); after "Check and save" the same
// WebView shows the server page. `prefix` is the URL the wanted page starts with. With `reattach` the socket is detected
// again on every poll: the activity restarts after saving and the socket name carries the pid (it can change).
const LOCAL_URL = process.env.SMOKE_LOCAL_URL || "https://appassets.androidx.startup/assets/web/index.html";
async function openPage(prefix = URL_, reattach = false) {
  if (!CDP_ONLY && !reattach) await attachSocket();
  const base = CDP_ONLY || `http://127.0.0.1:${PORT}`;
  const target = await until(`a page at ${prefix}`, async () => {
    if (!CDP_ONLY && reattach) await attachSocket(3);
    const list = await (await fetch(`${base}/json`)).json();
    return list.find(t => t.type === "page" && t.url.startsWith(prefix) && t.webSocketDebuggerUrl);
  }, 60, 500);
  return connect(target);
}
async function attachSocket(timeoutS = 60) {
  const sock = await until("the WebView DevTools socket", () => {
    const pid = adb("shell", "pidof", PKG).trim().split(/\s+/)[0];
    const unix = adb("shell", "cat", "/proc/net/unix");
    const m = pid && new RegExp(`@(webview_devtools_remote_${pid})\\b`).exec(unix);
    return m && m[1];
  }, timeoutS, 500);
  adb("forward", `tcp:${PORT}`, `localabstract:${sock}`);
}
async function connect(target) {
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
  // Every CDP call has a deadline: a page behind the system picker (or a dead WebView) must fail a step, not hang the job.
  const send = (method, params = {}) => new Promise((res, rej) => {
    const i = ++id;
    const t = setTimeout(() => { if (pending.delete(i)) rej(new Error(`CDP ${method} gave no answer in ${120 * SCALE}s`)); }, 120 * SCALE * 1000);
    pending.set(i, { res: v => { clearTimeout(t); res(v); }, rej: e => { clearTimeout(t); rej(e); } });
    ws.send(JSON.stringify({ id: i, method, params }));
  });
  await send("Runtime.enable");
  return {
    async ev(expr, gesture = false) {
      const r = await send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true, userGesture: gesture });
      if (r.exceptionDetails) throw new Error("page script: " + (r.exceptionDetails.exception?.description ?? r.exceptionDetails.text));
      return r.result.value;
    },
    send,
    // <input type=file data-testid=tid>: hand it files by device path, as if the user had picked them
    async setFiles(tid, paths) {
      await send("DOM.enable");
      const { root } = await send("DOM.getDocument", { depth: 0 });
      const { nodeId } = await send("DOM.querySelector", { nodeId: root.nodeId, selector: `[data-testid="${tid}"]` });
      if (!nodeId) throw new Error("no element [data-testid=" + tid + "]");
      await send("DOM.setFileInputFiles", { files: paths, nodeId });
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
// Two kinds of steps. step(): the set-up that everything else needs (attach, connect, log in, first reply); a failure stops
// the run (exit 1). check(): one function of the app, independent of the others: it is recorded as works / doesn't (with the
// reason) and the run goes on. The table is printed at the end to the job log (artifacts cannot always be downloaded).
const results = [];
const EXP = process.env.SMOKE_EXPORTS_DIR || "";                    // host copy of the synthetic exports (make-exports.py)
const DEV = process.env.SMOKE_DEVICE_DIR || `/data/data/${PKG}/cache/smoke`;  // copies readable by the app (run.sh)
const STRICT = process.env.SMOKE_STRICT === "1";                     // any "doesn't" fails the job
const short = e => String(e && e.message || e).replace(/\s+/g, " ").slice(0, 400);
const skip = why => Object.assign(new Error(why), { skip: true });

async function failureEvidence(name) {
  shot("FAIL-" + name.replace(/\W+/g, "-").slice(0, 40));
  if (page) {
    const body = await page.ev(`document.body.innerText.slice(0,3000)`).catch(() => "?");
    writeFileSync(path.join(OUT, "fail-page-text.txt"), String(body));
    writeFileSync(path.join(OUT, "fail-page-html.html"), String(await page.ev(`document.documentElement.outerHTML.slice(0,200000)`).catch(() => "?")));
  }
  if (!CDP_ONLY) try { writeFileSync(path.join(OUT, "fail-ui.xml"), execFileSync("adb", ["exec-out", "cat", "/sdcard/ui.xml"], { encoding: "utf8" })); } catch { /* none */ }
}
async function step(name, fn) {
  const t = Date.now();
  try { await fn(); results.push({ step: name, status: "works", why: "", s: (Date.now() - t) / 1000, core: true }); log("OK   " + name); }
  catch (e) {
    results.push({ step: name, status: "doesnt", why: short(e), s: (Date.now() - t) / 1000, core: true });
    log("FAIL " + name + ": " + e.message);
    await failureEvidence(name);
    throw e;
  }
}
async function check(name, fn) {
  const t = Date.now();
  try {
    const note = await fn();
    results.push({ step: name, status: "works", why: note || "", s: (Date.now() - t) / 1000 });
    log("OK   " + name + (note ? "  -- " + note : ""));
  } catch (e) {
    if (e && e.skip) { results.push({ step: name, status: "skipped", why: e.message, s: 0 }); log("SKIP " + name + ": " + e.message); return; }
    results.push({ step: name, status: "doesnt", why: short(e), s: (Date.now() - t) / 1000 });
    log("FAIL " + name + ": " + e.message);
    await failureEvidence(name).catch(() => {});
    // the next check starts from a clean screen: a panel or a menu left open by the failure is closed
    await page?.ev(`(()=>{for(const t of ['close-panel']){const e=${q("close-panel")};if(e)e.click()} const k=document.querySelector('[data-testid="panel-knowledge-sheet"] .icon-btn');if(k)k.click()})()`).catch(() => {});
  }
}
function report() {
  const w = Math.min(70, Math.max(10, ...results.map(r => r.step.length)));
  const label = r => (r.status === "works" ? "WORKS  " : r.status === "skipped" ? "SKIPPED" : "DOESN'T");
  const lines = ["", "=== ANDROID SMOKE REPORT (" + results.filter(r => r.status === "works").length + " works, " +
    results.filter(r => r.status === "doesnt").length + " don't, " + results.filter(r => r.status === "skipped").length + " skipped) ==="];
  for (const r of results) lines.push(`${label(r)}  ${r.step.padEnd(w)}  ${r.why || ""}`);
  lines.push("=== END OF REPORT ===", "");
  const text = lines.join("\n");
  console.error(text);
  try { writeFileSync(path.join(OUT, "report.txt"), text); } catch { /* the log has it */ }
}

// ---- helpers for the checks ----
const apiJson = (method, p, body) => page.ev(`(async()=>{const r=await fetch(${JSON.stringify(p)},{method:${JSON.stringify(method)},credentials:'same-origin',` +
  `headers:{'Content-Type':'application/json'}${body === undefined ? "" : ",body:JSON.stringify(" + JSON.stringify(body) + ")"}});` +
  `const t=await r.text();let j;try{j=JSON.parse(t)}catch{j=t}return {status:r.status,body:j}})()`);
const listOf = x => Array.isArray(x) ? x : (x && (x.conversations || x.nodes || x.items || x.data)) || [];
const convs = async () => listOf((await apiJson("GET", "/api/conversations?limit=500")).body);
const convTitled = async sub => (await convs()).find(c => String(c.title || "").includes(sub));
const messagesOf = async id => listOf((await apiJson("GET", `/api/conversations/${encodeURIComponent(id)}/messages?all=1`)).body);
const providerCalls = () => readFileSync(path.join(OUT, "provider.jsonl"), "utf8").split("\n").filter(Boolean).map(l => JSON.parse(l));
const clickText = (tid, re) => page.ev(`(()=>{const e=[...document.querySelectorAll('[data-testid="${tid}"]')].find(x=>${re}.test(x.innerText));if(!e)return false;e.click();return true})()`);
async function openMenu(entry) {
  if (!(await has("shell-menu"))) { await click("shell-menu-toggle"); await waitTid("shell-menu", 10); }
  await waitTid("shell-menu-" + entry, 10);
  await click("shell-menu-" + entry);
}
async function closePanel() {
  await click("close-panel").catch(() => {});
  await page.ev(`(()=>{const k=document.querySelector('[data-testid="panel-knowledge-sheet"] .icon-btn');if(k)k.click()})()`);
}
// a text field that saves on blur (settings): focus, set, leave
const setOnBlur = (tid, v) => page.ev(`(()=>{const e=${q(tid)};if(!e)return false;e.focus();
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(e,${JSON.stringify(v)});
  e.dispatchEvent(new Event('input',{bubbles:true}));e.blur();e.dispatchEvent(new FocusEvent('focusout',{bubbles:true}));return true})()`);
async function setConfigViaUi(tid, key, value) {
  await openMenu("settings"); await waitTid("panel-settings"); await waitTid(tid);
  await setOnBlur(tid, value);
  await until(`config ${key} = ${value}`, async () => (await apiJson("GET", "/api/config")).body?.[key] === value, 15, 300);
  await closePanel();
}
// the synthetic export as an upload straight to the server API (what the web client does with a picked File)
async function apiImport(hostFile, name) {
  const b64 = readFileSync(path.join(EXP, hostFile)).toString("base64");
  return page.ev(`(async()=>{const bin=atob(${JSON.stringify(b64)});const u=new Uint8Array(bin.length);for(let i=0;i<bin.length;i++)u[i]=bin.charCodeAt(i);
    const f=new FormData();f.append('file',new File([u],${JSON.stringify(name)}));
    const r=await fetch('/api/import',{method:'POST',body:f,credentials:'same-origin'});const t=await r.text();
    const ch=[...t.matchAll(/data:\s*(\{.*\})/g)].map(m=>{try{return JSON.parse(m[1])}catch{return null}}).filter(Boolean);
    return {status:r.status,progress:ch.filter(c=>c.type==='progress').length,last:ch[ch.length-1]||null,raw:t.slice(0,300)}})()`);
}

// ---- the Android system picker (DocumentsUI), driven with UI Automator ----
// file: finds the file by name (Downloads root first), taps it. folder: opens the folder by name and taps "Use this folder",
// then "Allow". Every try logs what the screen shows, so a failure says what the picker looked like.
async function drivePicker(target, { folder = false } = {}) {
  let seen = false, entered = false, lastTexts = "";
  for (let i = 0; i < 60; i++) {
    await sleep(1000);
    let nodes;
    try { nodes = dump(); } catch (e) { continue; }
    const inPicker = nodes.some(n => /documentsui/i.test(n.package || ""));
    lastTexts = nodes.map(n => n.text || n["content-desc"]).filter(Boolean).slice(0, 40).join(" | ");
    if (!inPicker) { if (seen) return "picker closed"; continue; }
    if (!seen) { seen = true; log("picker: " + lastTexts); shot("picker-" + target.replace(/\W+/g, "-")); }
    const btn = re => find(nodes, n => /Button/.test(n.class || "") && re.test(n.text || "") && n.enabled !== "false");
    const allow = btn(/^allow$/i); if (allow) { log("picker: Allow"); tap(allow); continue; }
    if (folder) {
      const use = btn(/use this folder/i);
      if (entered && use) { log("picker: Use this folder"); tap(use); continue; }
      const item = find(nodes, n => n.text === target && !/toolbar/i.test(n["resource-id"] || ""));
      if (item && !entered) { log("picker: open folder " + target); tap(item); entered = true; continue; }
      if (item && entered && use) { tap(use); continue; }
    } else {
      const file = find(nodes, n => n.text === target);
      if (file) { log("picker: tap " + target); tap(file); await sleep(800); const open = btn(/^(open|select)$/i); if (open) tap(open); continue; }
      const open = btn(/^(open|select)$/i); if (open) { tap(open); continue; }
    }
    const dl = find(nodes, n => /^downloads?$/i.test(n.text || "") && !/toolbar/i.test(n["resource-id"] || ""));
    if (dl && i % 3 === 0) { log("picker: Downloads"); tap(dl); continue; }
    const roots = find(nodes, n => /show roots|open drawer|navigate up/i.test(n["content-desc"] || ""));
    if (roots && i % 3 === 1) { log("picker: roots"); tap(roots); continue; }
  }
  // leave the picker, so the next check starts from the app and not from a stale picker
  for (let k = 0; k < 3 && dump().some(n => /documentsui/i.test(n.package || "")); k++) { adb("shell", "input", "keyevent", "KEYCODE_BACK"); await sleep(800); }
  throw new Error(`the system picker did not return ${target}; it showed: ${lastTexts}`);
}

// ---- LoomBridge streams from the page (local mode only: the bridge exists there) ----
const bridgeCall = (method, args = {}) => page.ev(`window.LoomBridge.call(${JSON.stringify(method)},${JSON.stringify(JSON.stringify(args))})`).then(JSON.parse);
const startBridge = (method, args) => page.ev(`(()=>{const id='smoke_'+Date.now()+'_'+Math.random().toString(36).slice(2);window.__smoke=window.__smoke||{};
  const st=window.__smoke[id]={chunks:[],done:false};window.__loomCallbacks=window.__loomCallbacks||{};
  window.__loomCallbacks[id]=(j,d)=>{try{st.chunks.push(JSON.parse(j))}catch(e){st.chunks.push({parse_error:String(e),raw:j})}if(d)st.done=true};
  window.LoomBridge.startStream(${JSON.stringify(method)},${JSON.stringify(JSON.stringify(args))},id);return id})()`);
const bridgeState = id => page.ev(`(()=>{const s=window.__smoke[${JSON.stringify(id)}];const l=s.chunks[s.chunks.length-1];
  return {n:s.chunks.length,done:s.done,last:l,progress:s.chunks.filter(c=>c.type==='progress').length}})()`);
const awaitBridge = (id, s) => until("the stream " + id + " to finish", async () => { const st = await bridgeState(id); return st.done && st; }, s, 400);
function wantDone(st, messages) {
  const l = st.last;
  if (!l) throw new Error("no final chunk");
  if (l.error) throw new Error(`${l.error.code}: ${l.error.message}`);
  if (l.type !== "done") throw new Error("the final chunk is not type=done: " + JSON.stringify(l).slice(0, 300));
  if (messages !== undefined && l.messages !== messages) throw new Error(`expected ${messages} messages, got ${l.messages} (${JSON.stringify(l).slice(0, 300)})`);
  return `${l.messages} messages, ${(l.conversations || []).length} conversation(s), ${st.progress} progress chunk(s)`;
}

// ---- the checks ----
const localChecks = [
  ["local: import a ChatGPT conversations.json by path (JNI import_file)", async () => {
    const st = await awaitBridge(await startBridge("import_file", { path: `${DEV}/chatgpt-conversations.json` }), 90);
    return wantDone(st, 4);
  }],
  ["local: import a Claude.ai ZIP by path (JNI import_file)", async () => {
    const st = await awaitBridge(await startBridge("import_file", { path: `${DEV}/claude-export.zip` }), 90);
    return wantDone(st, 6);
  }],
  ["local: imported conversations are in the app (LoomBridge list_conversations)", async () => {
    const all = JSON.stringify(await bridgeCall("list_conversations", { limit: 200 }));
    for (const t of ["Smoke ChatGPT JSON", "Smoke Claude ZIP"]) if (!all.includes(t)) throw new Error(`"${t}" is not listed: ${all.slice(0, 300)}`);
  }],
  ["local: import panel (+ -> Choose a file -> system picker)", async () => {
    // the Android shell has no browser file input: the panel opens the system picker (api.pickImport -> LoomBridge pick_import)
    await waitTid("shell-attach", 20); await click("shell-attach"); await waitTid("panel-import", 10); await waitTid("import-pick-file");
    await click("import-pick-file");
    await drivePicker("ui-local.json");
    await until("a result or an error of the import panel", async () => (await has("import-result")) || (await has("import-error")), 60, 300);
    if (await has("import-error")) throw new Error("import panel: " + (await page.ev(`${q("import-error")}.innerText`)));
    await closePanel();
    return await page.ev(`(${q("import-result")}?.innerText||'').replace(/\s+/g,' ').slice(0,100)`).catch(() => "");
  }],
  ["local: system picker, one file (SAF content:// -> copy -> kernel)", async () => {
    const id = await startBridge("pick_import", { kind: "file" });
    await drivePicker("saf-local.json");
    return wantDone(await awaitBridge(id, 90), 2);
  }],
  ["local: system picker, a folder (tree URI, only importable files)", async () => {
    const id = await startBridge("pick_import", { kind: "folder" });
    await drivePicker("smoke-exports", { folder: true });
    const st = await awaitBridge(id, 90);
    const note = wantDone(st, 4);
    const l = st.last;
    if (l.imported !== 2) throw new Error(`expected 2 files imported, got ${l.imported}: ${JSON.stringify(l.files)}`);
    return note + "; files: " + (l.files || []).map(f => f.name + (f.ok ? "" : " FAILED")).join(", ");
  }],
  ["local: a cancelled picker ends with an error chunk, not a hang", async () => {
    const id = await startBridge("pick_import", { kind: "file" });
    const pickerUp = () => { try { return dump().some(n => /documentsui/i.test(n.package || "")); } catch { return false; } };
    await until("the picker on screen", pickerUp, 30, 800);
    await sleep(1500); // a BACK sent while the picker is still opening goes to the app and the picker stays open
    for (let k = 0; k < 4 && pickerUp(); k++) { adb("shell", "input", "keyevent", "KEYCODE_BACK"); await sleep(1500); }
    if (pickerUp()) throw new Error("the picker did not close on BACK");
    const st = await awaitBridge(id, 30);
    if (!st.last.error || st.last.error.code !== "cancelled") throw new Error("expected a cancelled error, got " + JSON.stringify(st.last).slice(0, 200));
    return st.last.error.message;
  }],
];

const serverChecks = [
  ["new conversation (drawer -> New chat -> message -> listed)", async () => {
    const before = (await convs()).length;
    await click("shell-drawer-toggle"); await waitTid("shell-new-chat", 10); await click("shell-new-chat");
    await waitTid("shell-empty", 15);
    await send("Nowa rozmowa smoke: Kowalski jest w Krakowie.");
    await until("the reply in the new conversation", () => assistantSays("Odpowiedz testowego dostawcy"), 90, 300);
    await until("one more conversation on the server", async () => (await convs()).length === before + 1, 20, 500);
    return `${before} -> ${before + 1} conversations`;
  }],
  ["chat command: przetłumacz -> proposal -> confirm", async () => {
    await send("Przetłumacz tę rozmowę na niemiecki");
    await waitTid("chat-action-proposal", 90);
    const shown = await until("proposal text", () => page.ev(`(${q("chat-action-proposal")}?.innerText||'')`).then(t => /translat|tłumacz|Tłumacz/i.test(t) && t), 20, 200);
    await click("chat-action-proposal-confirm");
    await waitGone("chat-action-proposal", 60);
    if (await has("chat-error")) throw new Error("chat-error: " + await page.ev(`${q("chat-error")}.innerText`));
    return shown.replace(/\s+/g, " ").slice(0, 80);
  }],
  ["model: the picker and the default-model setting reach the provider", async () => {
    const options = await page.ev(`[...document.querySelectorAll('[data-testid="model-picker"] option')].map(o=>o.value)`);
    await setConfigViaUi("cfg-default-model", "default_model", "smoke/model-b");
    await send("Pytanie po zmianie modelu.");
    await until("the provider saw the new model", () => providerCalls().some(c => !c.classify && c.model === "smoke/model-b"), 60, 500);
    await setConfigViaUi("cfg-default-model", "default_model", "test/first-steps");
    return `picker options: ${options.length} (${options.slice(0, 3).join(", ") || "none"}); provider got smoke/model-b`;
  }],
  ["settings: a change persists (field -> server config -> reload -> field)", async () => {
    const val = "smoke/semantic-" + (Date.now() % 100000);
    await setConfigViaUi("cfg-semantic-model", "semantic_model", val);
    await page.ev(`location.reload()`);
    await waitTid("chat-input", 60);
    await openMenu("settings"); await waitTid("cfg-semantic-model");
    const shown = await until("the field shows the saved value", async () => (await page.ev(`${q("cfg-semantic-model")}.value`)) === val && val, 20, 300);
    await closePanel();
    await setConfigViaUi("cfg-semantic-model", "semantic_model", "");   // leave the account as it was
    return "saved " + shown;
  }],
  ["attachment: the + button opens the import panel", async () => {
    await waitTid("shell-attach", 20); await click("shell-attach"); await waitTid("panel-import", 10); await waitTid("import-file-input");
    await closePanel();
    return "the chat has no per-message attachment; + is the import panel";
  }],
  ["import (UI): ChatGPT conversations.json from a picked file", async () => {
    await click("shell-attach"); await waitTid("panel-import", 10); await waitTid("import-file-input");
    await page.setFiles("import-file-input", [`${DEV}/chatgpt-conversations.json`]);
    await until("the Import button enabled", () => page.ev(`!!${q("start-import")}&&!${q("start-import")}.disabled`), 10, 200);
    await click("start-import");
    await until("import result or error", async () => (await has("import-result")) || (await has("import-error")), 60, 300);
    if (await has("import-error")) throw new Error("import panel: " + (await page.ev(`${q("import-error")}.innerText`)));
    const c = await until("the conversation on the server", () => convTitled("Smoke ChatGPT JSON"), 20, 500);
    const n = (await messagesOf(c.id)).length;
    if (n !== 4) throw new Error(`expected 4 messages through the API, found ${n}`);
    await closePanel();
    return "4 messages through the API";
  }],
  ["import (UI): Claude.ai ZIP from a picked file", async () => {
    await click("shell-attach"); await waitTid("panel-import", 10); await waitTid("import-file-input");
    await page.setFiles("import-file-input", [`${DEV}/claude-export.zip`]);
    await until("the Import button enabled", () => page.ev(`!!${q("start-import")}&&!${q("start-import")}.disabled`), 10, 200);
    await click("start-import");
    await until("import result or error", async () => (await has("import-result")) || (await has("import-error")), 60, 300);
    if (await has("import-error")) throw new Error("import panel: " + (await page.ev(`${q("import-error")}.innerText`)));
    const a = await until("first conversation", () => convTitled("Smoke Claude ZIP"), 20, 500);
    const n = (await messagesOf(a.id)).length;
    if (n < 2) throw new Error(`the Claude conversation has ${n} messages through the API`);
    await closePanel();
    return `conversation "${a.title}" has ${n} messages`;
  }],
  ["import (UI): through the WebView file chooser = system picker (SAF)", async () => {
    await click("shell-attach"); await waitTid("panel-import", 10); await waitTid("import-file-input");
    await page.ev(`${q("import-file-input")}.click()`, true);   // a user gesture: Android opens the document picker
    await drivePicker("saf-server.json");
    await until("the page got the file", () => page.ev(`${q("import-file-input")}.files.length===1`), 20, 300);
    await click("start-import");
    await until("import result or error", async () => (await has("import-result")) || (await has("import-error")), 60, 300);
    if (await has("import-error")) throw new Error("import panel: " + (await page.ev(`${q("import-error")}.innerText`)));
    const c = await until("the conversation on the server", () => convTitled("Smoke SAF server"), 20, 500);
    await closePanel();
    return `conversation "${c.title}" imported from a content:// file`;
  }],
  ["graph: the panel opens and shows nodes", async () => {
    await openMenu("graph"); await waitTid("panel-graph", 10); await waitTid("graph-view", 20);
    const nodes = await until("graph nodes through the API", async () => { const n = listOf((await apiJson("GET", "/api/graph/nodes")).body); return n.length > 0 && n.length; }, 60, 1000);
    const counts = await until("graph counts text", () => page.ev(`(${q("graph-counts")}?.innerText||'').replace(/\s+/g,' ')`).then(t => /[1-9]/.test(t) && t), 30, 500);
    const canvas = await has("graph-canvas");
    shot("graph");
    await closePanel();
    return `${nodes} nodes (API); panel says "${counts}"; canvas ${canvas ? "present" : "absent"}`;
  }],
  ["memory: the panel opens, a note is added and stored", async () => {
    await openMenu("memory"); await waitTid("panel-memory", 10); await waitTid("memory-new-content", 20);
    const text = "Smoke notatka " + Date.now();
    await setVal("memory-new-content", text);
    await click("memory-add");
    await until("the note in the panel", () => page.ev(`[...document.querySelectorAll('[data-testid="memory-node"]')].some(n=>n.innerText.includes(${JSON.stringify(text)}))`), 20, 300);
    const stored = JSON.stringify((await apiJson("GET", "/api/memory")).body);
    if (!stored.includes(text)) throw new Error("the note is not in /api/memory");
    await closePanel();
    return "stored (API)";
  }],
  ["knowledge: the panel opens", async () => {
    await openMenu("knowledge"); await waitTid("panel-knowledge-sheet", 10); await waitTid("knowledge-workbench", 20);
    shot("knowledge");
    await closePanel();
  }],
  ["paging: a 520-message conversation loads in pages", async () => {
    const r = await apiImport("long-claude.json", "long-claude.json");
    if (!r.last || r.last.type !== "done") throw new Error("API import: " + JSON.stringify(r).slice(0, 300));
    const patch = await apiJson("PATCH", "/api/config", { loom_web_message_window: 40 });
    if (patch.status >= 400) throw new Error("cannot set loom_web_message_window: " + JSON.stringify(patch.body).slice(0, 200));
    try {
      const c = await until("the long conversation", () => convTitled("Smoke Long 520"), 20, 500);
      await click("shell-drawer-toggle"); await waitTid("shell-conv", 10);
      if (!(await clickText("shell-conv", "/Smoke Long 520/"))) throw new Error("the conversation is not in the drawer list");
      await waitTid("chat-older", 30);
      const first = await page.ev(`${q("chat-older-count")}.innerText`);
      await click("chat-load-older");
      const second = await until("a bigger page", async () => { const t = await page.ev(`${q("chat-older-count")}?.innerText||''`); return t !== first && t; }, 20, 300);
      const total = (await messagesOf(c.id)).length;
      return `API has ${total} messages; first page "${first}", after "load older" "${second}"`;
    } finally {
      await apiJson("PATCH", "/api/config", { loom_web_message_window: 500 });
    }
  }],
  ["agent proposals", async () => {
    throw skip("not reachable from the chat UI without an agent scenario (AgentProposalBlock renders only for agent runs); not driven here");
  }],
];

try {
  if (!CDP_ONLY) adb("shell", "am", "start", "-W", "-n", `${PKG}/.MainActivity`);
  if (!CDP_ONLY) {
    await step("1 attach to the local chat in the WebView", async () => {
      page = await openPage(LOCAL_URL);
      await waitTid("shell-menu-toggle", 60);
      shot("local-chat");
    });
    // the local app (JNI kernel on the phone) has the bridge: imports are checked here, before switching to the server
    for (const [name, fn] of localChecks) await check(name, fn);
    await step("2 menu -> connection (LoomBridge.openConnection)", async () => {
      if (await has("panel-import")) await closePanel();
      if (await has("shell-menu")) await click("shell-menu-toggle");   // a menu left open by a check
      await click("shell-menu-toggle");
      await waitTid("shell-menu", 10);
      // the entry is listed only when LoomBridge.openConnection exists (layouts.json: when=android_shell)
      await waitTid("shell-menu-connection", 10);
      await click("shell-menu-connection");
      page.close(); page = undefined;  // the native screen takes over; the old page goes away when the app reloads
    });
    await step("3 native: choose server account (http://10.0.2.2, plain HTTP for LAN)", connectScreen);
    await step("4 re-attach to the WebView (server page)", async () => { page = await openPage(URL_, true); await waitTid("auth-form", 60); shot("login-form"); });
  } else {
    // desktop check: there is no LoomBridge. If the page shows the shell menu, the toggle must open it; the connection
    // entry is listed only when LoomBridge exists, so its absence is the expected state here.
    await step("1 attach (desktop Chromium)", async () => {
      page = await openPage();
      await until("auth form or local chat", async () => (await has("auth-form")) || (await has("shell-menu-toggle")), 60);
      if (await has("shell-menu-toggle")) {
        await click("shell-menu-toggle");
        await waitTid("shell-menu", 10);
        log("desktop: connection menu entry " + ((await has("shell-menu-connection")) ? "present" : "absent (no LoomBridge, expected)"));
        await click("shell-menu-toggle");
      }
    });
  }
  await step("5 log in to the account", async () => {
    await setVal("auth-login", LOGIN); await setVal("auth-password", PASS);
    await click("auth-submit");
    await waitTid("chat-view", 60);
    shot("logged-in");
  });
  await step("6 conversation ready", async () => {
    // a phone is narrow: the conversation list is hidden, a fresh conversation needs the sidebar
    if (!(await until("chat input (soft)", () => has("chat-input"), 8).then(() => true, () => false))) {
      const narrowToggle = await has("toggle-sidebar");
      if (narrowToggle && !(await page.ev(`!!${q("new-conversation")}&&${q("new-conversation")}.offsetParent!==null`))) await click("toggle-sidebar");
      await waitTid("new-conversation"); await click("new-conversation");
      await waitTid("chat-input");
    }
  });
  await step("7 chat: send a message and see the reply", async () => {
    await send("Zespol Acme planuje wydac wersje 2.0 w piatek, jesli testy przejda. Odpowiedz dwoma zdaniami.");
    await until("the assistant reply", () => assistantSays("Odpowiedz testowego dostawcy"), 90, 300);
    shot("reply");
  });
  await step("8 chat command: 'streść tę rozmowę' -> proposal -> 'tak' -> summary", async () => {
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
  await step("9 the provider got the key from the account settings", async () => {
    const calls = providerCalls();
    if (!calls.some(c => c.authorization === "Bearer " + KEY)) throw new Error(`no provider call with the key (${calls.length} calls)`);
  });
  for (const [name, fn] of serverChecks) await check(name, fn);
  log("SET-UP STEPS OK");
} catch (e) {
  process.exitCode = 1;
  const ran = new Set(results.map(r => r.step));
  for (const [name] of [...(CDP_ONLY ? [] : localChecks), ...serverChecks])
    if (!ran.has(name)) results.push({ step: name, status: "skipped", why: "not run: a set-up step failed (" + short(e).slice(0, 120) + ")", s: 0 });
} finally {
  report();
  if (STRICT && results.some(r => r.status === "doesnt")) process.exitCode = 1;
  writeFileSync(path.join(OUT, "results.json"), JSON.stringify(results.map(r => ({ ...r, ok: r.status === "works" })), null, 1));
  try { page?.close(); } catch { /* closed */ }
}
