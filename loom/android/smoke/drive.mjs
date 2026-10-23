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
// A slow emulator now and then shows "<app> isn't responding" (seen: Pixel Launcher with font_scale 1.3). The dialog
// takes the screen and the WebView stops answering CDP; "Wait" lets the app (and the run) go on.
function dismissSystemDialogs(nodes) {
  try { nodes = nodes || dump(); } catch { return false; }
  const title = nodes.find(n => /isn't responding|nie odpowiada/i.test(n.text || ""));
  if (!title) return false;
  // the launcher is restarted (Close app): Wait keeps a stuck launcher stuck and the dialog comes back every few
  // seconds; any other app (ours included) gets Wait, and the text is logged so an ANR of the app itself is visible
  const launcher = /launcher/i.test(title.text);
  const pick = launcher ? /^(close app|zamknij aplikację)$/i : /^(wait|czekaj)$/i;
  const id = launcher ? "android:id/aerr_close" : "android:id/aerr_wait";
  const button = nodes.find(n => n.cx !== undefined && (n["resource-id"] === id || pick.test(n.text || "")));
  if (!button) return false;
  log(`system: "${title.text}", tapping ${button.text || id}`); tap(button);
  return true;
}
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
  const p = await connect(target);
  p.prefix = prefix;
  return p;
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
    // a page that stopped answering (a system dialog over it, a stalled renderer) is re-attached once, so one stuck
    // check does not turn every later check into a 120 s timeout
    if (/gave no answer/.test(e.message || "") && page?.prefix) {
      dismissSystemDialogs();
      try { page.close(); } catch { /* closed */ }
      try { page = await openPage(page.prefix, true); log("re-attached to the page after a CDP timeout"); } catch (r) { log("re-attach failed: " + short(r)); }
    }
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
// an element that is still moving (a menu opening) may drop a click: wait until two reads 120 ms apart give the same box
async function clickSettled(tid) {
  const box = () => page.ev(`(()=>{const e=${q(tid)};if(!e)return '';const r=e.getBoundingClientRect();return [r.x,r.y,r.width,r.height].map(Math.round).join(',')})()`);
  let prev = await box();
  for (let k = 0; k < 25; k++) { await sleep(120); const now = await box(); if (now && now === prev) break; prev = now; }
  if (!(await click(tid))) throw new Error(`[data-testid=${tid}] is gone before the tap`);
}
const clickText = (tid, re) => page.ev(`(()=>{const e=[...document.querySelectorAll('[data-testid="${tid}"]')].find(x=>${re}.test(x.innerText));if(!e)return false;e.click();return true})()`);
async function openMenu(entry) {
  if (!(await has("shell-menu"))) { await click("shell-menu-toggle"); await waitTid("shell-menu", 10); }
  await waitTid("shell-menu-" + entry, 10);
  await click("shell-menu-" + entry);
}
// P33: + opens the composer menu; its "Import conversations" entry opens the import panel (M1 §3.14 rewrites the old step).
async function openImportPanel() {
  await waitTid("shell-attach", 20); await click("shell-attach");
  await waitTid("composer-menu", 10); await waitTid("composer-menu-import", 10); await click("composer-menu-import");
  await waitTid("panel-import", 10);
}
async function closePanel() {
  await click("close-panel").catch(() => {});
  await page.ev(`(()=>{const k=document.querySelector('[data-testid="panel-knowledge-sheet"] .icon-btn');if(k)k.click()})()`);
}
// M1-W0 (layouts.json since df6a97c2): ⋮ has no "Pamięć" / "Wiedza" entries and ⋮ › Graf opens the navigator (panel-navigator),
// so the old panels are reached through the Workshop: ⋮ › Warsztat (shell-menu-workshop → layout "workshop", header with
// toggle-nav + app-nav) → nav-<panel> (nav-graph, nav-memory, nav-graph-entry; nav-knowledge toggles the knowledge sheet).
// The nav buttons stay in the DOM when app-nav is folded on a phone and click() is element.click(), so the ≡ toggle is not
// needed; an open panel makes the header inert, hence closePanel() first. nav-chat (workshop_back) brings the chat shell back.
async function openWorkshopPanel(entry) {
  await closePanel();
  if (!(await has("toggle-nav"))) { await openMenu("workshop"); await waitTid("toggle-nav", 10); }
  await waitTid("nav-" + entry, 10);
  await click("nav-" + entry);
}
async function backToChat() {
  await closePanel();
  if (await has("nav-chat")) { await click("nav-chat"); await waitTid("shell-menu-toggle", 10); }
}
// a text field that saves on blur (settings): focus, set, leave
const setOnBlur = (tid, v) => page.ev(`(()=>{const e=${q(tid)};if(!e)return false;e.focus();
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(e,${JSON.stringify(v)});
  e.dispatchEvent(new Event('input',{bubbles:true}));e.blur();e.dispatchEvent(new FocusEvent('focusout',{bubbles:true}));return true})()`);
// M1-W0: ⋮ › Ustawienia shows the human groups; the old field table (cfg-*) sits in the group "silnik" under the "all fields" disclosure
async function openSettingsField(tid) {
  await openMenu("settings"); await waitTid("panel-settings");
  if (!(await has(tid))) {
    await waitTid("shum-groups", 30);   // the groups come from the kernel after the panel opens
    if (await has("shum-group-open-silnik")) await click("shum-group-open-silnik");
    await waitTid("shum-all-fields", 10);
    if (!(await has(tid))) await page.ev(`${q("shum-all-fields")}.querySelector('button').click()`);
  }
  await waitTid(tid);
}
async function setConfigViaUi(tid, key, value) {
  await openSettingsField(tid);
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
    const ch=[...t.matchAll(/data:\\s*(\\{.*\\})/g)].map(m=>{try{return JSON.parse(m[1])}catch{return null}}).filter(Boolean);
    return {status:r.status,progress:ch.filter(c=>c.type==='progress').length,last:ch[ch.length-1]||null,raw:t.slice(0,300)}})()`);
}

// ---- the Android system picker (DocumentsUI), driven with UI Automator ----
// file: finds the file by name (Downloads root first), taps it. folder: opens the folder by name and taps "Use this folder",
// then "Allow". Every try logs what the screen shows, so a failure says what the picker looked like.
async function drivePicker(target, { folder = false } = {}) {
  let seen = false, entered = false, lastTexts = "", fileTaps = 0, places = 0, moved = false;
  for (let i = 0; i < 60; i++) {
    await sleep(1000);
    let nodes;
    try { nodes = dump(); } catch (e) { continue; }
    const inPicker = nodes.some(n => /documentsui/i.test(n.package || ""));
    if (dismissSystemDialogs(nodes)) continue;
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
      // A tap on the name does not always choose the file (seen with font_scale 1.3 in the Recent view): the second try
      // taps the row start, then the picker is moved to Downloads and the same is tried there. Bounds go to the log.
      const file = find(nodes, n => n.text === target);
      if (file && fileTaps < 2 * (places + 1) && (places === 0 || moved)) {
        const atStart = fileTaps % 2 === 1;
        log(`picker: tap ${target} ${atStart ? "row start" : "name"} bounds=${file.bounds} clickable=${file.clickable}`);
        if (atStart) adb("shell", "input", "tap", String(Math.max(file.l - 120, 10)), String(file.cy)); else tap(file);
        fileTaps++; await sleep(800); const open = btn(/^(open|select)$/i); if (open) tap(open); continue;
      }
      if (file && places === 0) {
        places = 1; log("picker: the file did not open from here, trying Downloads");
        const r = find(nodes, n => /show roots|open drawer/i.test(n["content-desc"] || "")); if (r) tap(r); continue;
      }
      const open = btn(/^(open|select)$/i); if (open) { tap(open); continue; }
    }
    const dl = find(nodes, n => /^downloads?$/i.test(n.text || "") && !/toolbar/i.test(n["resource-id"] || ""));
    if (dl && (i % 3 === 0 || (places === 1 && !moved))) { log("picker: Downloads"); tap(dl); if (places === 1) moved = true; continue; }
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
// ---------- M1 Gate 0: declarative steps from the W0 testid file ----------
// Format (first ```json block of docs/reports/M1_W0_TESTIDS.md):
//   {"gate0": [{"name": "Graf > ROM: 13 rows, chips 4/8/1",
//               "do":     [{"click": "<tid>"}, {"clickText": ["<tid>", "<regex source>"]}, {"wait": "<tid>"}, {"back": 1}],
//               "expect": [{"tid": "<tid>", "count": 13} | {"tid", "min": 1} | {"tid", "text": "<regex source>"} |
//                          {"tid", "absent": true} | {"exitQuestion": true}],
//               "reset": true}]}          // reset (default true): window.__loomBack() until nothing is left open
// No file or no block = every Gate 0 step is "skipped" with the reason (W0 has not published its testids yet).
function m1GateSpec() {
  const file = process.env.SMOKE_M1_TESTIDS || path.join(import.meta.dirname, "../../../docs/reports/M1_W0_TESTIDS.md");
  let text;
  try { text = readFileSync(file, "utf8"); } catch { return { missing: `no ${path.basename(file)} yet (W0 publishes it)` }; }
  const m = /```json\s*\n([\s\S]*?)\n```/.exec(text);
  if (!m) return { missing: `${path.basename(file)} has no json block` };
  try { return { steps: JSON.parse(m[1]).gate0 || [] }; } catch (e) { return { missing: `${path.basename(file)}: ${e.message}` }; }
}
const countTid = tid => page.ev(`document.querySelectorAll('[data-testid="${tid}"]').length`);
const textTid = tid => page.ev(`[...document.querySelectorAll('[data-testid="${tid}"]')].map(e=>e.innerText).join(' | ')`);
async function m1Reset() {
  for (let k = 0; k < 8; k++) if (!(await page.ev(`!!(window.__loomBack&&window.__loomBack())`).catch(() => false))) return;
}
// what a person sees: the visible text of the page (overlays first: the last dialog/sheet in the DOM is on top)
const m1Visible = () => page.ev(`(()=>{const v=e=>{const r=e.getBoundingClientRect();return r.width>0&&r.height>0&&getComputedStyle(e).visibility!=='hidden'};
  const top=[...document.querySelectorAll('[role=dialog],[data-testid$=-sheet],[data-testid=role-card]')].filter(v).pop();
  return ((top&&top.innerText)||document.body.innerText||'').replace(/\\s+/g,' ').slice(0,900)})()`).catch(() => "");
function m1GateChecks() {
  const spec = m1GateSpec();
  if (spec.missing) return [["M1 Gate 0 (screens of W0)", async () => { const e = new Error(spec.missing); e.skip = true; throw e; }]];
  return spec.steps.map(st => [`M1 Gate 0: ${st.name}`, async () => {
    const notes = [];
    // one navigation attempt; a tap the WebView dropped (settings panel never opened after ⋮ › Ustawienia, seen on 2 of 3
    // profiles at random) gets one more attempt from the chat root, and the report says so
    const navigate = async () => {
      for (const a of st.do || []) {
        if (a.click) { await waitTid(a.click, 20); await clickSettled(a.click); }
        else if (a.clickText) { await waitTid(a.clickText[0], 20); if (!(await clickText(a.clickText[0], `/${a.clickText[1]}/i`))) throw new Error(`no ${a.clickText[0]} matching /${a.clickText[1]}/`); }
        else if (a.wait) { const t0 = Date.now(); await waitTid(a.wait, a.timeout || 30); notes.push(`${a.wait} after ${((Date.now() - t0) / 1000).toFixed(1)}s`); }
        else if (a.back) for (let k = 0; k < a.back; k++) { adb("shell", "input", "keyevent", "KEYCODE_BACK"); await sleep(1200); }
        await sleep(300);
      }
    };
    try {
      try { await navigate(); }
      catch (e) { notes.push(`second attempt after: ${String(e.message).split(" || ")[0]}`); await m1Reset(); await navigate(); }
      for (const x of st.expect || []) {
        if (x.exitQuestion) {
          const d = await until("the exit question", () => find(dump(), n => n["resource-id"] === "android:id/button2"), 15, 600);
          tap(d); await sleep(800); notes.push("exit question shown, Stay"); continue;
        }
        if (x.absent) { await until(`[data-testid=${x.tid}] gone`, async () => !(await has(x.tid)), 15); notes.push(`${x.tid} gone`); continue; }
        await waitTid(x.tid, 30);
        if (x.count !== undefined) { const n = await countTid(x.tid); if (n !== x.count) throw new Error(`${x.tid}: ${n} on screen, expected ${x.count}`); notes.push(`${x.tid}=${n}`); }
        if (x.min !== undefined) { const n = await countTid(x.tid); if (n < x.min) throw new Error(`${x.tid}: ${n} on screen, expected at least ${x.min}`); notes.push(`${x.tid}>=${x.min} (${n})`); }
        if (x.text) { const t = await textTid(x.tid); if (!new RegExp(x.text, "i").test(t)) throw new Error(`${x.tid} says "${t.slice(0, 160)}", expected /${x.text}/`); notes.push(`${x.tid} ~ /${x.text}/: "${t.replace(/\s+/g, " ").slice(0, 300)}"`); }
      }
      shot("m1-" + st.name.replace(/\W+/g, "-").slice(0, 40));
      const visible = await m1Visible();
      return notes.join("; ") + (visible ? ` || on screen: ${visible}` : "");
    } catch (e) {
      // a failed step says what the screen showed, so the cause (app or driver) is readable from the report
      shot("m1-fail-" + st.name.replace(/\W+/g, "-").slice(0, 40));
      const visible = await m1Visible();
      if (visible) e.message += ` || on screen: ${visible}`;
      throw e;
    } finally {
      if (st.reset !== false) await m1Reset();
    }
  }]);
}

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
  ["local: import panel (+ menu -> Import conversations -> Choose a file -> system picker)", async () => {
    // the Android shell has no browser file input: the panel opens the system picker (api.pickImport -> LoomBridge pick_import)
    await openImportPanel(); await waitTid("import-pick-file");
    await click("import-pick-file");
    await drivePicker("ui-local.json");
    await until("a result or an error of the import panel", async () => (await has("import-result")) || (await has("import-error")), 60, 300);
    if (await has("import-error")) throw new Error("import panel: " + (await page.ev(`${q("import-error")}.innerText`)));
    await closePanel();
    return await page.ev(`(${q("import-result")}?.innerText||'').replace(/\\s+/g,' ').slice(0,100)`).catch(() => "");
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
  // M1 Gate 0 (spec §3.14, M1-A0): the steps are DATA from docs/reports/M1_W0_TESTIDS.md (its first ```json block),
  // written by the web side (W0) with its data-testid values, so new screens get smoke steps without a driver change.
  ...m1GateChecks(),
  // M1 §3.14: system Back never ends the app silently. The page closes its own overlays first (window.__loomBack),
  // then the WebView history; at the root the shell asks (android_back_exit_confirm, preset ask). "Stay" keeps the
  // app and the draft; Back again while asked leaves (the task goes to the back, the draft stays in the WebView).
  ["local: Back at the chat root asks before leaving; Stay keeps the app and the draft", async () => {
    if (await has("panel-import")) await closePanel();
    await waitTid("chat-input", 20);
    const draft = "szkic M1 wstecz";
    await setVal("chat-input", draft);
    const exitDialog = () => find(dump(), n => n["resource-id"] === "android:id/button2");
    let dialog;
    for (let k = 0; k < 5 && !dialog; k++) {
      dismissSystemDialogs();   // a system dialog would take this Back instead of the app
      adb("shell", "input", "keyevent", "KEYCODE_BACK"); await sleep(1200);
      if (!resumed()) throw new Error(`Back #${k + 1} left the app without asking`);
      dialog = exitDialog();
      if (!dialog) log(`Back #${k + 1}: handled by the page or the WebView history`);
    }
    if (!dialog) throw new Error("no exit question after 5 Backs");
    shot("back-exit-question");
    const question = textOf(find(dump(), n => n["resource-id"] === "android:id/message") || {}).trim();
    tap(dialog); await sleep(1000);
    if (!resumed()) throw new Error("Stay did not keep the app");
    const kept = await page.ev(`${q("chat-input")}?.value`);
    if (kept !== draft) throw new Error(`the draft is gone after Stay: ${JSON.stringify(kept)}`);
    return `asked: "${question}"`;
  }],
  ["local: Back twice at the root leaves to the launcher; reopening shows the draft", async () => {
    const draft = await page.ev(`${q("chat-input")}?.value`);
    dismissSystemDialogs();
    adb("shell", "input", "keyevent", "KEYCODE_BACK"); await sleep(1200);
    await until("the exit question", () => find(dump(), n => n["resource-id"] === "android:id/button2"), 15, 600);
    adb("shell", "input", "keyevent", "KEYCODE_BACK"); await sleep(1500);
    if (resumed()) throw new Error("Back on the question did not leave the app");
    adb("shell", "am", "start", "-W", "-n", `${PKG}/.MainActivity`); await sleep(1500);
    if (!resumed()) throw new Error("the app did not come back");
    const kept = await until("the draft after reopening", () => page.ev(`${q("chat-input")}?.value`).then(v => v === draft && v), 20, 500);
    await setVal("chat-input", "");
    return `draft kept: "${kept}"`;
  }],
];
const resumed = () => new RegExp(`(mResumedActivity|topResumedActivity)[^\\n]*${PKG.replace(/\./g, "\\.")}/\\.MainActivity`).test(
  execFileSync("adb", ["shell", "dumpsys", "activity", "activities"], { encoding: "utf8", maxBuffer: 16 << 20 }));

const serverChecks = [
  // M1-W2a: an empty chat offers "Poznajmy się"; the interview is a form in a conversation, so the model sees no request
  // M1-W2a (S1's path, one interview per account): the remark is understood with a question waiting and with none
  ["interview: Poznajmy się -> level -> remark with a question waiting -> Tak; skip to the end -> remark again -> Tak; no model request", async () => {
    const before = providerCalls().length;
    const lastAnswer = () => page.ev(`(()=>{const b=[...document.querySelectorAll('.msg.assistant .body')].pop();return (b&&b.innerText||'').replace(/\\s+/g,' ').slice(0,240)})()`);
    // the remark is understood when the proposal comes, or (the same rule already set by the first remark) when the kernel
    // answers that it understood it and nothing changes ("This is how I understood it: … It is already set that way.")
    const remark = async when => {
      const n = await page.ev(`document.querySelectorAll('.msg.assistant .body').length`);
      await send("nie pytaj o zdrowie");
      const got = await until(`the proposal 'Tak zrozumiałem' or 'already set' (${when})`, async () => {
        if (await has("chat-action-proposal")) return "proposal";
        if ((await page.ev(`document.querySelectorAll('.msg.assistant .body').length`)) > n && /understood it|zrozumiałem/i.test(await lastAnswer()) && !(await has("chat-action-proposal"))) return "already";
        return false;
      }, 60, 400).catch(async e => { throw new Error(`${e.message}; the last answer: "${await lastAnswer()}"`); });
      if (got === "already") { await sleep(1500); if (!(await has("chat-action-proposal"))) return `understood, already set: "${await lastAnswer()}"`; }
      const text = (await textTid("chat-action-proposal")).replace(/\s+/g, " ").slice(0, 160);
      await click("chat-action-proposal-confirm"); await waitGone("chat-action-proposal", 60);
      return text;
    };
    await click("shell-drawer-toggle"); await waitTid("shell-new-chat", 10); await click("shell-new-chat");
    await waitTid("shell-empty", 15);
    const shown = await until("the empty chat knows whether to offer the interview", () => page.ev(`${q("shell-empty")}?.getAttribute('data-interview')`).then(v => v && v !== "checking" && v), 30, 300);
    if (shown !== "shown") throw new Error(`the empty chat does not offer the interview (data-interview=${shown})`);
    await clickSettled("interview-card-start");
    await waitTid("interview-levels", 30);
    await page.ev(`(document.querySelector('[data-testid="interview-level"][data-level="detailed"]')||document.querySelector('[data-testid="interview-level"]')).click()`);
    await waitTid("interview-chips", 30);
    await page.ev(`document.querySelector('[data-testid="interview-chip"][data-chip="skip"]')?.click()`);
    const skipChip = () => page.ev(`!!document.querySelector('[data-testid="interview-chip"][data-chip="skip"]')`);
    await until("a question waiting", skipChip, 30, 300);
    const first = await remark("a question waiting");
    // skip until no question waits (bounded: the detailed level has a dozen or so)
    let skips = 0;
    for (; skips < 40; skips++) {
      await sleep(800);
      if (!(await skipChip())) break;
      await page.ev(`document.querySelector('[data-testid="interview-chip"][data-chip="skip"]')?.click()`);   // it may go away between the check and the tap
    }
    if (await skipChip()) throw new Error(`a question still waits after ${skips} skips`);
    const second = await remark("no question waiting");
    shot("interview");
    const calls = providerCalls().slice(before);
    if (calls.length) throw new Error(`the model got ${calls.length} request(s) during the interview`);
    return `with a question: "${first}"; after ${skips} skips, none waiting: "${second}"; 0 model requests`;
  }],
  ["new conversation (drawer -> New chat -> message -> listed)", async () => {
    const before = (await convs()).length;
    await click("shell-drawer-toggle"); await waitTid("shell-new-chat", 10); await click("shell-new-chat");
    await waitTid("shell-empty", 15);
    await send("Nowa rozmowa smoke: Kowalski jest w Krakowie.");
    await until("the reply in the new conversation", () => assistantSays("Odpowiedz testowego dostawcy"), 90, 300);
    try {
      await until("one more conversation on the server", async () => (await convs()).length === before + 1, 20, 500);
    } catch (e) {
      const now = await convs();
      const newest = now.slice().sort((a, b) => String(b.updated || b.created).localeCompare(String(a.updated || a.created))).slice(0, 3);
      const sizes = await Promise.all(newest.map(async c => `${c.title || c.id} (${(await messagesOf(c.id)).length} msgs)`));
      throw new Error(`${e.message}; before ${before}, now ${now.length}; newest: ${sizes.join(" | ")}`);
    }
    return `${before} -> ${before + 1} conversations`;
  }],
  // M1-W1a: under an answer the line "Kontekst: … — zobacz" opens "Co wysłaliśmy"; one step "Szerzej" changes only the next message
  ["context: the line under an answer opens Co wysłaliśmy; a step for next time", async () => {
    await until("the context line under the last answer", () => has("context-line-open"), 30, 400);
    const line = await page.ev(`(()=>{const l=[...document.querySelectorAll('[data-testid="context-line"]')].pop();return (l&&l.innerText||'').replace(/\\s+/g,' ')})()`);
    await page.ev(`[...document.querySelectorAll('[data-testid="context-line-open"]')].pop().click()`);
    await until("the sheet with the receipt", () => page.ev(`${q("context-receipt")}?.getAttribute('data-state')`).then(v => (v === "receipt" || v === "missing" || v === "failed") && v), 30, 300)
      .then(async st => { if (st !== "receipt") throw new Error(`the sheet says ${st}: ${(await textTid("context-receipt")).replace(/\s+/g, " ").slice(0, 200)}`); });
    const header = await textTid("context-receipt-header").catch(() => "");
    const disclaimer = await textTid("context-receipt-disclaimer").catch(() => "");
    shot("context-receipt");
    let step = "no step button";
    if (await has("receipt-step-wider")) {
      await click("receipt-step-wider");
      const kind = await until("the step's result", () => page.ev(`${q("receipt-modify-result")}?.getAttribute('data-kind')`), 30, 300);
      step = `Szerzej -> ${kind}: "${(await textTid("receipt-modify-result")).replace(/\s+/g, " ").slice(0, 160)}"`;
    }
    await click("context-receipt-close"); await waitGone("context-receipt", 10);
    return `line "${line.slice(0, 120)}"; header "${header.replace(/\s+/g, " ").slice(0, 200)}"; "${disclaimer.trim()}"; ${step}`;
  }],
  // M1-W1b: + › Kontekst › Szerzej (next message only), the context strip says so; the model chip's ⓘ opens where the model comes from
  ["context: + › Kontekst › Szerzej, the strip, the model's provenance", async () => {
    await waitTid("shell-attach", 20); await click("shell-attach");
    await waitTid("composer-menu", 10); await click("composer-menu-context");
    await waitTid("composer-menu-wider", 10); await click("composer-menu-wider");
    const kind = await until("the kernel's answer to the step", () => page.ev(`${q("composer-menu-modify-result")}?.getAttribute('data-kind')`), 30, 300);
    const said = (await textTid("composer-menu-modify-result")).replace(/\s+/g, " ").slice(0, 200);
    shot("composer-context");
    await m1Reset();
    const strip = (await textTid("context-strip")).replace(/\s+/g, " ").slice(0, 160);
    const next = await has("context-strip-next");
    await waitTid("model-provenance-open", 10); await click("model-provenance-open");
    await until("the model's provenance", () => page.ev(`${q("model-provenance")}?.getAttribute('data-state')`).then(v => v === "ready"), 30, 300);
    const prov = (await textTid("model-provenance")).replace(/\s+/g, " ").slice(0, 260);
    shot("model-provenance");
    await click("model-provenance-close"); await waitGone("model-provenance", 10);
    if (kind !== "applied") throw new Error(`Szerzej -> ${kind}: "${said}"`);
    if (!next) throw new Error(`the strip does not say what the next message gets: "${strip}"`);
    return `Szerzej -> ${kind}: "${said}"; strip "${strip}"; model sheet "${prov}"`;
  }],
  // M1-K1c-3b: "Pokaż w grafie" in Co wysłaliśmy opens the navigator on the request with the perspective sent_to_model;
  // the sent rows carry a mark, a derived row's card says it is derived; Back closes the card, then the navigator
  ["context: Pokaż w grafie opens the record of the request in the navigator", async () => {
    await waitTid("context-line-open", 30);
    await page.ev(`[...document.querySelectorAll('[data-testid="context-line-open"]')].pop().click()`);
    await until("the sheet with the receipt", () => page.ev(`${q("context-receipt")}?.getAttribute('data-state')`).then(v => v === "receipt"), 30, 300);
    const graph = await until("the graph's answer for the button", () => page.ev(`${q("receipt-show-in-graph")}?.getAttribute('data-graph')`).then(v => v && v !== "checking" && v), 30, 300);
    if (graph !== "yes") throw new Error(`Pokaż w grafie is off (${graph}): "${(await textTid("receipt-show-in-graph-reason")).replace(/\s+/g, " ").slice(0, 200)}"`);
    await clickSettled("receipt-show-in-graph");
    const nav = await until("the navigator on the request", () => page.ev(`(()=>{const n=${q("graph-navigator")};return n&&/sent_to_model/.test(n.getAttribute('data-perspectives')||'')?[n.getAttribute('data-focus'),n.getAttribute('data-perspectives')].join(' / '):''})()`), 30, 300);
    await waitTid("nav-rows", 30);
    const sent = await countTid("nav-sent");
    const derivedRows = await page.ev(`document.querySelectorAll('[data-testid="nav-rows"] li[data-derived="true"]').length`);
    let card = "no derived row";
    if (derivedRows > 0) {
      await page.ev(`document.querySelector('[data-testid="nav-rows"] li[data-derived="true"] .gnav-more').click()`);
      await waitTid("card-derived-note", 20);
      card = (await textTid("card-derived-note")).replace(/\s+/g, " ").slice(0, 200);
    }
    shot("receipt-graph");
    await m1Reset();
    if (await has("graph-navigator")) throw new Error("Back left the navigator open");
    if (sent < 1) throw new Error(`the navigator on ${nav} marks no row as sent`);
    return `focus/perspectives ${nav}; ${sent} rows marked sent; ${derivedRows} derived; card "${card}"; Back closed card and navigator`;
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
    await openSettingsField("cfg-semantic-model");
    const shown = await until("the field shows the saved value", async () => (await page.ev(`${q("cfg-semantic-model")}.value`)) === val && val, 20, 300);
    await closePanel();
    await setConfigViaUi("cfg-semantic-model", "semantic_model", "");   // leave the account as it was
    return "saved " + shown;
  }],
  ["+ opens the menu; Import conversations opens the import panel", async () => {
    await openImportPanel(); await waitTid("import-file-input");
    await closePanel();
    return "+ menu -> import panel";
  }],
  ["import (UI): ChatGPT conversations.json from a picked file", async () => {
    await openImportPanel(); await waitTid("import-file-input");
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
    await openImportPanel(); await waitTid("import-file-input");
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
    await openImportPanel(); await waitTid("import-file-input");
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
  ["graph: the old panel opens from the Workshop and shows nodes", async () => {
    // ⋮ › Graf is the navigator since M1-W0 (gate0 block in docs/reports/M1_W0_TESTIDS.md); the force-directed panel sits in the Workshop
    await openWorkshopPanel("graph"); await waitTid("panel-graph", 10); await waitTid("graph-view", 20);
    const nodes = await until("graph nodes through the API", async () => { const n = listOf((await apiJson("GET", "/api/graph/nodes")).body); return n.length > 0 && n.length; }, 60, 1000);
    const counts = await until("graph counts text", () => page.ev(`(${q("graph-counts")}?.innerText||'').replace(/\\s+/g,' ')`).then(t => /[1-9]/.test(t) && t), 30, 500);
    const canvas = await has("graph-canvas");
    shot("graph");
    await closePanel();
    return `${nodes} nodes (API); panel says "${counts}"; canvas ${canvas ? "present" : "absent"} (via Warsztat)`;
  }],
  ["memory: the old panel opens from the Workshop, a note is added and stored", async () => {
    await openWorkshopPanel("memory"); await waitTid("panel-memory", 10); await waitTid("memory-new-content", 20);
    const text = "Smoke notatka " + Date.now();
    await setVal("memory-new-content", text);
    await click("memory-add");
    await until("the note in the panel", () => page.ev(`[...document.querySelectorAll('[data-testid="memory-node"]')].some(n=>n.innerText.includes(${JSON.stringify(text)}))`), 20, 300);
    const stored = JSON.stringify((await apiJson("GET", "/api/memory")).body);
    if (!stored.includes(text)) throw new Error("the note is not in /api/memory");
    await closePanel();
    return "stored (API, via Warsztat)";
  }],
  ["knowledge: the old sheet opens from the Workshop", async () => {
    // in the Workshop layout Wiedza opens beside the chat (panel-knowledge-sheet is the chat-layout sheet only)
    await openWorkshopPanel("knowledge"); await waitTid("knowledge-workbench", 20);
    shot("knowledge");
    await click("nav-knowledge"); await waitGone("knowledge-workbench", 10);
    await backToChat();   // the next steps (paging, connection) start from the chat shell
    return "via Warsztat; back in the chat shell";
  }],
  ["paging: a 520-message conversation loads in pages", async () => {
    const r = await apiImport("long-claude.json", "long-claude.json");
    if (!r.last || r.last.type !== "done") throw new Error("API import: " + JSON.stringify(r).slice(0, 300));
    const patch = await apiJson("PATCH", "/api/config", { loom_web_message_window: 40 });
    if (patch.status >= 400) throw new Error("cannot set loom_web_message_window: " + JSON.stringify(patch.body).slice(0, 200));
    try {
      const c = await until("the long conversation", () => convTitled("Smoke Long 520"), 20, 500);
      // the import went straight to the API (not through the panel, which refreshes the list): reload the page to list it
      await page.ev("location.reload()").catch(() => {});
      await waitTid("chat-input", 60); await backToChat();   // the layout is remembered: a reload may come back in the Workshop
      await waitTid("shell-drawer-toggle", 30);
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
  // R38: the fake provider (deploy/first-steps-provider.py) routes "agent" to agent.run and, offered tools, asks for one
  // irreversible tool (usage_compact): the policy local_reversible proposes it, the run stops, the chat shows the block;
  // Accept resumes the run with the transcript, and the provider then answers with text.
  // the owner's phone runs the UI in Polish: the same interview conversation opened with ui_language = pl; UI words are Polish,
  // Polish control words work. The kernel's replies keep the conversation's language (English here) until S1's K2b-5: not checked.
  ["interview in Polish (ui_language=pl): the same conversation, Polish UI words, 'pomiń' and 'podsumowanie' work", async () => {
    const was = (await apiJson("GET", "/api/config")).body?.ui_language;
    const set = async v => { const r = await apiJson("PATCH", "/api/config", { ui_language: v }); if (r.status >= 400) throw new Error("cannot set ui_language: " + JSON.stringify(r.body).slice(0, 200)); };
    await set("pl");
    try {
      await page.ev("location.reload()").catch(() => {});
      await waitTid("chat-input", 60); await backToChat();
      await click("shell-drawer-toggle"); await waitTid("shell-conv", 10);
      if (!(await clickText("shell-conv", "/Poznajmy|Getting to know|Get to know|Let.s get/i"))) throw new Error("the interview conversation is not in the list: " + (await textTid("shell-conv")).slice(0, 200));
      await waitTid("interview-info", 30);
      const info = (await textTid("interview-info")).replace(/\s+/g, " ").slice(0, 160);
      const chips = (await textTid("interview-chips")).replace(/\s+/g, " ").slice(0, 160);
      if (!/bez modelu|Rozmowa/i.test(info)) throw new Error(`the info line is not Polish: "${info}"`);
      if (!/Pomiń|Nigdy|Podsumowanie|Dalej|Ile zostało|Nie teraz/i.test(chips)) throw new Error(`the quick replies are not Polish: "${chips}"`);
      const count = () => page.ev(`document.querySelectorAll('.msg.assistant .body').length`);
      const replies = [];
      for (const word of ["pomiń", "podsumowanie"]) {
        const n = await count();
        await send(word);
        await until(`an answer to '${word}'`, async () => (await count()) > n, 60, 400);
        if (await has("chat-error")) throw new Error("chat-error: " + await page.ev(`${q("chat-error")}.innerText`));
        replies.push(`${word} -> "${(await page.ev(`[...document.querySelectorAll('.msg.assistant .body')].pop().innerText`)).replace(/\s+/g, " ").slice(0, 120)}"`);
      }
      shot("interview-pl");
      return `info "${info}"; chips "${chips}"; ${replies.join("; ")}`;
    } finally {
      await set(was ?? "");
      await page.ev("location.reload()").catch(() => {}); await waitTid("chat-input", 60).catch(() => {});
    }
  }],
  ["agent proposals: the run stops on a proposal, Accept resumes it to the end", async () => {
    await send("poproś agenta żeby uporządkował moje notatki");   // the example phrase of agent.run: a rule routes it locally
    const proposed = await until("the action proposal or the agent block",
      async () => (await has("chat-agent-proposal")) ? "agent" : (await has("chat-action-proposal")) ? "action" : false, 90, 300);
    if (proposed === "action") await send("tak");
    await waitTid("chat-agent-proposal", 120);
    const action = await page.ev(`${q("chat-agent-proposal-action")}?.innerText||''`);
    if (!/usage\.compact/.test(action)) throw new Error(`the block proposes "${action}", expected usage.compact`);
    shot("agent-proposal");
    const before = providerCalls().length;
    await click("chat-agent-proposal-accept");
    // the answer resumes the run (agent.run with resume): the provider gets the turn with the tool result, then the block gives way
    await until("the resumed agent turn at the provider", () => providerCalls().slice(before).some(c => c.tools > 0 && !c.tool_calls), 120, 500);
    await until("the proposals block giving way", async () => !(await has("chat-agent-proposal-accept")), 60, 400);
    if (await has("chat-agent-proposal") && await page.ev(`!!document.querySelector('[data-testid="chat-agent-proposal"] .error, [data-testid="chat-agent-proposal"] [role=alert]')`))
      throw new Error("the block shows a failure after Accept: " + await page.ev(`${q("chat-agent-proposal")}.innerText`));
    // R38: the kernel posts the resumed run's answer to the conversation once, in words (not the run's JSON)
    const done = "Agent testowy: gotowe.";
    const count = () => page.ev(`[...document.querySelectorAll('.msg.assistant .body')].filter(b=>b.innerText.includes(${JSON.stringify(done)})).length`);
    await until("the agent's answer in the conversation", async () => (await count()) >= 1, 60, 400);
    await sleep(3000);   // a second writer of the same answer would show up by now
    const n = await count();
    if (n !== 1) throw new Error(`the agent's answer is in the conversation ${n} times, expected once`);
    const last = await page.ev(`(()=>{const b=[...document.querySelectorAll('.msg.assistant .body')].pop();return (b&&b.innerText||'').replace(/\\s+/g,' ').slice(0,200)})()`);
    if (/^\s*[{\[]|"(agent_loop|status|steps)"\s*:/.test(last)) throw new Error(`the last assistant message is JSON: ${last}`);
    shot("agent-done");
    return `proposed ${action.trim()}, accepted, the run resumed at the provider, the block gave way; the answer once: "${last}"`;
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
