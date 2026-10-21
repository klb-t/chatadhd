// X-Port (module :xport) on the emulator, driven with adb + UI Automator only (the app is native views, no WebView).
// Proves: the APK installs and opens (Chaquopy unpacks Python on the first start), sources are added through the
// system picker and analysed by the engine on the phone, every tab opens without an error, the process never crashes.
// Synthetic sources only: loom/tests/fixtures/extract (pushed by xport.sh). Report: report.txt, results.json.
import { execFileSync } from "node:child_process";
import { appendFileSync, mkdirSync, writeFileSync } from "node:fs";
import path from "node:path";

const PKG = process.env.XPORT_PKG || "com.chatadhd.xport";
const OUT = process.env.XPORT_OUT || "xport-out";
const SCALE = Number(process.env.SMOKE_TIMEOUT_S || 1);
const SHOTS = path.join(OUT, "shots");
mkdirSync(SHOTS, { recursive: true });
const t0 = Date.now();
const log = m => { const l = `[${((Date.now() - t0) / 1000).toFixed(1).padStart(6)}s] ${m}`; console.error(l); appendFileSync(path.join(OUT, "drive.log"), l + "\n"); };
const sleep = ms => new Promise(r => setTimeout(r, ms));
const adb = (...a) => execFileSync("adb", a, { encoding: "utf8", maxBuffer: 64 << 20, timeout: 60000 });
let shotN = 0;
function shot(name) {
  try { writeFileSync(path.join(SHOTS, String(++shotN).padStart(2, "0") + "-" + name + ".png"), execFileSync("adb", ["exec-out", "screencap", "-p"], { maxBuffer: 64 << 20, timeout: 30000 })); }
  catch (e) { log("screencap failed: " + e.message); }
}
async function until(what, fn, timeoutS, every = 500) {
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
function dump() {
  adb("shell", "uiautomator", "dump", "/sdcard/ui.xml");
  const xml = execFileSync("adb", ["exec-out", "cat", "/sdcard/ui.xml"], { encoding: "utf8", maxBuffer: 16 << 20 });
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
const find = (nodes, pred) => nodes.find(n => n.cx !== undefined && pred(n));
const tap = n => adb("shell", "input", "tap", String(n.cx), String(n.cy));
const texts = nodes => nodes.map(n => n.text || n["content-desc"]).filter(Boolean);
const inApp = nodes => nodes.some(n => n.package === PKG);
// What a person sees on the screen: the app's own texts, top to bottom, without repeats (the report quotes them).
const TABS = ["Przegląd", "Przeglądarka", "Wybór", "Edycje", "Operacje", "Eksport"];
const seen = (nodes, max = 14) => [...new Set(nodes.filter(n => n.package === PKG && (n.text || "").trim())
  .sort((a, b) => (a.t ?? 0) - (b.t ?? 0) || (a.l ?? 0) - (b.l ?? 0)).map(n => n.text.trim().replace(/\s+/g, " ").slice(0, 160)))]
  .slice(0, max).join(" | ");
const resumed = () => new RegExp(`(mResumedActivity|topResumedActivity)[^\\n]*${PKG.replace(/\./g, "\\.")}/`).test(adb("shell", "dumpsys", "activity", "activities"));
const crashes = () => { try { return adb("logcat", "-d", "-b", "crash").split("\n").filter(l => l.includes(PKG)); } catch { return []; } };

// "<app> isn't responding": a stuck launcher is restarted (Close app), anything else gets Wait; the text is logged.
function dismissSystemDialogs(nodes) {
  const title = nodes.find(n => /isn't responding|nie odpowiada/i.test(n.text || ""));
  if (!title) return false;
  const launcher = /launcher/i.test(title.text);
  const button = find(nodes, n => n["resource-id"] === (launcher ? "android:id/aerr_close" : "android:id/aerr_wait"));
  if (!button) return false;
  log(`system: "${title.text}", tapping ${button.text}`); tap(button);
  return true;
}

// The system picker (ACTION_OPEN_DOCUMENT): tap the named file; go to Downloads when it is not listed where it opened.
async function pick(target) {
  let seen = false, taps = 0, last = "", wentDl = false;
  for (let i = 0; i < 60; i++) {
    await sleep(1000);
    let nodes;
    try { nodes = dump(); } catch { continue; }
    if (dismissSystemDialogs(nodes)) continue;
    const inPicker = nodes.some(n => /documentsui/i.test(n.package || ""));
    last = texts(nodes).slice(0, 40).join(" | ");
    if (!inPicker) { if (seen) return; continue; }
    if (!seen) { seen = true; log("picker: " + last); }
    const allow = find(nodes, n => /Button/.test(n.class || "") && /^allow$/i.test(n.text || "")); if (allow) { tap(allow); continue; }
    const file = find(nodes, n => n.text === target);
    if (file && taps < 4) {
      log(`picker: tap ${target} ${taps % 2 ? "row start" : "name"} bounds=${file.bounds}`);
      if (taps % 2) adb("shell", "input", "tap", String(Math.max(file.l - 120, 10)), String(file.cy)); else tap(file);
      taps++; await sleep(800);
      const open = find(dump(), n => /Button/.test(n.class || "") && /^(open|select)$/i.test(n.text || "")); if (open) tap(open);
      continue;
    }
    const dl = find(nodes, n => /^downloads?$/i.test(n.text || "") && !/toolbar/i.test(n["resource-id"] || ""));
    // the picker may open at the device root (a "Download" folder in the list) or in Recent: go to Downloads at once
    if (dl && !wentDl) { log("picker: Downloads"); tap(dl); wentDl = true; continue; }
    if (dl && i % 3 === 0) { log("picker: Downloads again"); tap(dl); continue; }
    const roots = find(nodes, n => /show roots|open drawer|navigate up/i.test(n["content-desc"] || ""));
    if (roots && i % 3 === 1) { log("picker: roots"); tap(roots); continue; }
  }
  for (let k = 0; k < 3 && dump().some(n => /documentsui/i.test(n.package || "")); k++) { adb("shell", "input", "keyevent", "KEYCODE_BACK"); await sleep(800); }
  throw new Error(`the system picker did not return ${target}; it showed: ${last}`);
}

// ---------- report ----------
const results = [];
async function check(name, fn) {
  const t = Date.now();
  try {
    const note = await fn();
    results.push({ step: name, status: "works", why: note || "", s: (Date.now() - t) / 1000 });
    log("OK   " + name + (note ? "  -- " + note : ""));
  } catch (e) {
    results.push({ step: name, status: "doesnt", why: String(e && e.message || e).replace(/\s+/g, " ").slice(0, 400), s: (Date.now() - t) / 1000 });
    log("FAIL " + name + ": " + (e && e.message));
    shot("fail-" + name.replace(/\W+/g, "-").slice(0, 40));
    try { writeFileSync(path.join(OUT, "fail-texts.txt"), texts(dump()).join("\n")); } catch { /* no dump */ }
  }
}
function report() {
  const works = results.filter(r => r.status === "works").length;
  const lines = [`=== X-PORT ANDROID REPORT (${works} works, ${results.length - works} don't) ===`];
  for (const r of results) lines.push(`${r.status === "works" ? "WORKS  " : "DOESN'T"}  ${r.step.padEnd(64)}  ${r.why}`);
  lines.push("=== END OF REPORT ===");
  writeFileSync(path.join(OUT, "report.txt"), lines.join("\n") + "\n");
  writeFileSync(path.join(OUT, "results.json"), JSON.stringify(results, null, 1));
  console.log(lines.join("\n"));
}

// ---------- scenario ----------
const statusText = nodes => texts(nodes).find(t => /^(Nic nie wybrano\.|Analizuję|Błąd|Przywracam)/.test(t)) || "";
async function addSource(file) {
  const nodes = await until("the add-source button", () => { const n = dump(); dismissSystemDialogs(n); return find(n, x => /^Dodaj źródło/.test(x.text || "")) && n; }, 60);
  const before = texts(nodes).length;
  tap(find(nodes, x => /^Dodaj źródło/.test(x.text || "")));
  await pick(file);
  // the engine works in a thread; done = the status is no longer "Analizuję…" (an error shows as "Błąd analizy: …")
  const after = await until(`the analysis of ${file}`, () => { const n = dump(); return !/^Analizuję/.test(statusText(n)) && inApp(n) && n; }, 180, 1500);
  const status = statusText(after);
  if (/^Błąd/.test(status)) throw new Error(status);
  const err = texts(after).find(t => /^Błąd/.test(t));
  if (err) throw new Error(err);
  return `${status ? `status "${status}"; ` : ""}on screen: ${seen(after)}`;
}

let exitCode = 0;
try {
  await check("opens (first start unpacks Python)", async () => {
    const out = adb("shell", "am", "start", "-W", "-n", `${PKG}/.MainActivity`);
    const total = /TotalTime:\s*(\d+)/.exec(out)?.[1];
    const nodes = await until("the X-Port window with its tabs", () => { const n = dump(); dismissSystemDialogs(n); return inApp(n) && find(n, x => x.text === "Przegląd") && n; }, 180, 1500);
    shot("opened");
    if (!resumed()) throw new Error("X-Port is not the resumed activity");
    return `start ${total ?? "?"} ms; tabs: ${["Przegląd", "Przeglądarka", "Wybór", "Edycje", "Operacje", "Eksport"].filter(t => nodes.some(n => n.text === t)).join(", ")}`;
  });
  await check("adds a source: our conversation.json (picker -> engine on the phone)", async () => { const r = await addSource("xport-ours-conversation.json"); shot("ours"); return r; });
  await check("adds a second source: grok_com.json", async () => { const r = await addSource("xport-grok.json"); shot("grok"); return r; });
  await check("adds a third source: a Gemini takeout.zip", async () => { const r = await addSource("xport-gemini-takeout.zip"); shot("gemini"); return r; });
  // The tab bar as a person sees it: rows, and whether a tab is cut off by the screen edge or overlaps another one.
  await check("tab bar: every tab visible, none cut off or overlapping", async () => {
    const nodes = dump();
    const w = Number(adb("shell", "wm", "size").match(/(\d+)x\d+/)?.[1] || 1080);
    const tabs = nodes.filter(n => n.package === PKG && /Button/.test(n.class || "") && TABS.includes(n.text) && n.cx !== undefined);
    const rows = [...new Set(tabs.map(n => n.t))].sort((a, b) => a - b).map(top => tabs.filter(n => n.t === top).sort((a, b) => a.l - b.l).map(n => n.text));
    const problems = [];
    for (const t of TABS) if (!tabs.some(n => n.text === t)) problems.push(`"${t}" not on screen`);
    for (const n of tabs) if (n.r > w || n.l < 0) problems.push(`"${n.text}" cut by the screen edge (${n.bounds}, screen ${w} px wide)`);
    for (const a of tabs) for (const b of tabs) if (a !== b && a.text < b.text && a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b) problems.push(`"${a.text}" overlaps "${b.text}"`);
    shot("tab-bar");
    const layout = rows.map((r, i) => `row ${i + 1}: ${r.join(" | ")}`).join("; ");
    if (problems.length) throw new Error(problems.join("; ") + " — " + layout);
    return `${layout} (screen ${w} px)`;
  });
  for (const tab of ["Przeglądarka", "Wybór", "Edycje", "Operacje", "Eksport", "Przegląd"]) {
    const scrollAll = ["Edycje", "Eksport"];   // panels quoted to the end (recipes, export profiles)
    await check(`tab ${tab} opens without an error`, async () => {
      // the tabs sit in a horizontal scroll: a tab off the screen edge is brought in by swiping the row
      let nodes = dump(); dismissSystemDialogs(nodes);
      let t = find(nodes, n => n.text === tab && /Button/.test(n.class || ""));
      for (let k = 0; !t && k < 4; k++) {
        const row = find(nodes, n => /Button/.test(n.class || "") && TABS.includes(n.text));
        if (!row) break;
        const w = Number(adb("shell", "wm", "size").match(/(\d+)x\d+/)?.[1] || 1080);
        const [from, to] = tab === "Przegląd" ? [w * 0.2, w * 0.9] : [w * 0.9, w * 0.2];
        adb("shell", "input", "swipe", String(Math.round(from)), String(row.cy), String(Math.round(to)), String(row.cy), "300");
        await sleep(700); nodes = dump();
        t = find(nodes, n => n.text === tab && /Button/.test(n.class || ""));
      }
      if (!t) throw new Error(`no tab button "${tab}" on screen (also after swiping the tab row)`);
      tap(t);
      const after = await until(`the ${tab} panel`, () => { const n = dump(); return inApp(n) && !n.some(x => /^(Ładuję|Liczę|Pracuję)/.test(x.text || "")) && n; }, 60, 1000);
      shot("tab-" + tab);
      const err = texts(after).find(x => /^Błąd/.test(x));
      if (err) throw new Error(err);
      if (!scrollAll.includes(tab)) return `on screen: ${seen(after, 14)}`;
      // whole panel: swipe up until nothing new appears, then report every text in order (and swipe back)
      const all = [...seen(after, 200).split(" | ")];
      for (let k = 0; k < 12; k++) {
        adb("shell", "input", "swipe", "500", "1700", "500", "700", "250"); await sleep(600);
        const more = seen(dump(), 200).split(" | ").filter(t => t && !all.includes(t));
        if (!more.length) break;
        all.push(...more);
      }
      for (let k = 0; k < 12; k++) adb("shell", "input", "swipe", "500", "700", "500", "1700", "150");
      return `whole panel (${all.length} texts): ${all.join(" | ")}`;
    });
    if (tab === "Wybór") await check("Wybór: the pattern for encoded data (base64, hex…) opens its window", async () => {
      let nodes = dump();
      let p = find(nodes, n => /dane kodowane/i.test(n.text || ""));
      for (let k = 0; !p && k < 6; k++) { adb("shell", "input", "swipe", "500", "1500", "500", "700", "300"); await sleep(600); nodes = dump(); p = find(nodes, n => /dane kodowane/i.test(n.text || "")); }
      if (!p) throw new Error("no pattern mentioning encoded data on the Wybór tab: " + seen(nodes, 30));
      const label = p.text;
      tap(p);
      const after = await until("the window of the pattern", () => { const n = dump(); return !n.some(x => /^(Liczę|Ładuję)/.test(x.text || "")) && n; }, 60, 1000);
      shot("encoded-pattern");
      const err = texts(after).find(x => /^Błąd/.test(x));
      if (err) throw new Error(err);
      const dialog = after.some(n => /alertTitle|android:id\/message|parentPanel/.test(n["resource-id"] || ""));
      const report = `tapped "${label}"; ${dialog ? "a dialog" : "the screen"} shows: ${seen(after, 30) || texts(after).slice(0, 30).join(" | ")}`;
      if (dialog) { adb("shell", "input", "keyevent", "KEYCODE_BACK"); await sleep(800); }
      return report;
    });
  }
  // ---- S1's acceptance asks (2026-10-21): the second recipe, and the per-conversation folder export ----
  const openTab = async (tab) => {
    let nodes = dump(); let t = find(nodes, n => n.text === tab && /Button/.test(n.class || ""));
    if (!t) throw new Error(`no tab "${tab}"`);
    tap(t); await sleep(1200);
    for (let k = 0; k < 12; k++) adb("shell", "input", "swipe", "500", "700", "500", "1700", "120");   // back to the top
    await sleep(500);
  };
  const scrollFind = async (pred, what, max = 10) => {
    for (let k = 0; k <= max; k++) {
      const nodes = dump(); const n = find(nodes, pred);
      if (n) return n;
      adb("shell", "input", "swipe", "500", "1500", "500", "900", "250"); await sleep(600);
    }
    throw new Error(`not found after scrolling: ${what}; screen: ${seen(dump(), 40)}`);
  };
  const settle = () => until("the panel to settle", () => { const n = dump(); return !n.some(x => /^(Liczę|Ładuję|Pracuję|Stosuję|Eksportuję)/.test(x.text || "")) && n; }, 90, 1000);
  const tapText = async (re, what) => { const n = await scrollFind(x => re.test(x.text || ""), what); tap(n); await sleep(800); return n.text; };

  await check("Edycje: the recipe list (a drop-down) and the recipe 'Zostaw w metadanych tylko znaczniki czasu': Podgląd, Zastosuj, Cofnij", async () => {
    await openTab("Edycje");
    const spinner = await scrollFind(n => /Spinner/.test(n.class || "") && n.package === PKG, "the recipe drop-down");
    tap(spinner); await sleep(1000);
    const items = dump().filter(n => /CheckedTextView|TextView/.test(n.class || "") && (n.text || "").trim() && n.cx !== undefined && n.t >= 0);
    const listed = [...new Set(items.map(n => n.text.trim()))];
    const pickIt = find(dump(), n => /Zostaw w metadanych/.test(n.text || ""));
    if (!pickIt) throw new Error("the drop-down does not list 'Zostaw w metadanych…'; it shows: " + listed.join(" | "));
    tap(pickIt); await sleep(1000);
    shot("recipe-chosen");
    const chosen = seen(dump(), 60);
    const out = [`drop-down: ${listed.join(" | ")}`, `after choosing: ${chosen}`];
    for (const [btn, label] of [[/^Podgląd przepisu$/, "Podgląd"], [/^Zastosuj przepis$/, "Zastosuj"], [/^Cofnij przepis$/, "Cofnij"]]) {
      await tapText(btn, label);
      const after = await settle();
      shot("recipe-" + label);
      const err = texts(after).find(x => /^Błąd/.test(x));
      out.push(`${label}: ${seen(after, 60)}`);
      if (err) throw new Error(out.join(" || "));
    }
    return out.join(" || ");
  });

  await check("Export: Wybór 'Wszystko' -> Eksport -> profile 'Wyciągnij spod zagnieżdżenia' -> 'Eksportuj zaznaczone (zip)' -> system save -> result", async () => {
    await openTab("Wybór");
    await tapText(/^Wszystko$/, "the 'Wszystko' button");
    const sel = await settle(); shot("select-all");
    const selection = seen(sel, 30);
    await openTab("Eksport");
    const profile = await tapText(/^Wyciągnij spod zagnieżdżenia/, "the profile 'Wyciągnij spod zagnieżdżenia'");
    shot("export-profile");
    await tapText(/^Eksportuj zaznaczone/, "the 'Eksportuj zaznaczone (zip)' button");
    // the system save dialog (ACTION_CREATE_DOCUMENT): Save into Downloads
    let saved = false, dialogText = "";
    for (let i = 0; i < 40 && !saved; i++) {
      await sleep(1000);
      const nodes = dump(); if (dismissSystemDialogs(nodes)) continue;
      if (!nodes.some(n => /documentsui/i.test(n.package || ""))) { if (i > 3) break; continue; }
      dialogText = texts(nodes).slice(0, 30).join(" | ");
      const save = find(nodes, n => /Button/.test(n.class || "") && /^(save|zapisz)$/i.test(n.text || ""));
      if (save) { shot("save-dialog"); tap(save); saved = true; break; }
      const dl = find(nodes, n => /^downloads?$/i.test(n.text || "") && !/toolbar/i.test(n["resource-id"] || ""));
      if (dl && i % 3 === 0) tap(dl);
      const roots = find(nodes, n => /show roots/i.test(n["content-desc"] || ""));
      if (!dl && roots && i % 3 === 1) tap(roots);
    }
    if (!saved) throw new Error(`no Save in the system dialog; it showed: ${dialogText || "(no dialog)"}; app: ${seen(dump(), 30)}`);
    const after = await until("the export result", () => { const n = dump(); return n.some(x => x.package === PKG) && !n.some(x => /^Eksportuję/.test(x.text || "")) && n; }, 120, 1500);
    shot("export-done");
    const err = texts(after).find(x => /^Błąd/.test(x));
    const files = (() => { try { return adb("shell", "ls", "-la", "/sdcard/Download").split("\n").filter(l => /\.zip/.test(l)).join(" ; "); } catch { return ""; } })();
    let listing = "";
    try {
      const zip = adb("shell", "ls", "-t", "/sdcard/Download").split("\n").map(x => x.trim()).find(x => x.endsWith(".zip"));
      if (zip) { execFileSync("adb", ["pull", `/sdcard/Download/${zip}`, path.join(OUT, "export.zip")], { timeout: 60000 });
        listing = execFileSync("unzip", ["-l", path.join(OUT, "export.zip")], { encoding: "utf8" }).split("\n").slice(3, -3).map(l => l.trim().split(/\s+/).slice(3).join(" ")).filter(Boolean).slice(0, 60).join(" ; "); }
    } catch (e) { listing = "could not list the zip: " + e.message; }
    const report = `selection: ${selection} || profile: ${profile} || app after save: ${seen(after, 40)} || Download: ${files} || inside the zip: ${listing}`;
    if (err) throw new Error(report);
    return report;
  });
  await check("the process never crashed", async () => {
    const c = crashes();
    if (c.length) throw new Error(c.slice(0, 5).join(" / "));
    if (!resumed()) throw new Error("X-Port is no longer the resumed activity");
    return "crash buffer clean";
  });
} catch (e) {
  exitCode = 1; log("set-up failed: " + e.message);
} finally {
  report();
  if (!results.length || results[0].status !== "works") exitCode = 1;
  process.exitCode = exitCode;
}
