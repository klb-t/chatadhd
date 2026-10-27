// W2 (OKNA, luka 1 of W1): a screenshot that does not depend on timing. Before the shot it waits until
//  (1) no "loading" line is visible (Wczytuję… / Loading…, words of the page, a pattern that is a preset of this helper),
//  (2) the page's own requests are quiet (none in flight for `quietMs`; the live event feed /api/events/stream is long-lived and ignored),
//  (3) finite animations ended,
// and takes it with Playwright's `animations: "disabled"` (infinite animations such as the status dot are reset to their start,
// transitions are finished) and a hidden caret. Test-side only: the product is not changed. Every wait is bounded (`timeoutMs`);
// a wait that runs out is reported on stderr and the shot is taken anyway (an advisory, not a stop).
// W3 (luka 1 of W2): (4) scroll positions and the document height must stay the same for `settle_frames` frames, (5) wall-clock text
// written by the kernel (there is no fixed server clock for tests) is painted over by flat boxes - patterns and selectors are data in
// stable-shot.json. `installClock(page)` freezes the page's own Date for scripts that want it (before navigation).
import path from "node:path";
import { readFileSync } from "node:fs";

export const SHOT_PRESETS = JSON.parse(readFileSync(new URL("./stable-shot.json", import.meta.url), "utf8"));

/** Freezes the page's Date at the preset time (Playwright clock); timers still run. Call before the first navigation. */
export async function installClock(page, time = SHOT_PRESETS.fixed_time) {
  await page.clock.setFixedTime(new Date(time));
}

/** Waits until every scrollable element and the document keep the same scroll position and height for `frames` frames. */
async function settleScroll(page, frames, timeoutMs) {
  return page.evaluate(({ frames, timeoutMs }) => new Promise(resolve => {
    const sample = () => {
      const parts = [document.documentElement.scrollHeight, window.scrollY];
      for (const el of document.querySelectorAll("*")) if (el.scrollHeight > el.clientHeight + 1) parts.push(el.scrollTop, el.scrollHeight);
      return parts.join(",");
    };
    const start = performance.now();
    let last = sample(), same = 0;
    const tick = () => {
      const now = sample();
      same = now === last ? same + 1 : 0;
      last = now;
      if (same >= frames) resolve(true);
      else if (performance.now() - start > timeoutMs) resolve(false);
      else requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }), { frames, timeoutMs });
}

/** Paints flat boxes over wall-clock text (whole element width on the matched line) and over the preset selectors; returns how many. */
async function paintMasks(page, presets) {
  return page.evaluate(({ patterns, selectors, color }) => {
    const res = patterns.map(p => new RegExp(p, "u"));
    const boxes = [];
    const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
      const value = node.nodeValue || "";
      const parent = node.parentElement;
      if (!parent || !value.trim() || parent.closest("[data-shot-mask-box]")) continue;
      for (const re of res) {
        const m = re.exec(value);
        if (!m) continue;
        const range = document.createRange();
        range.setStart(node, m.index);
        range.setEnd(node, m.index + m[0].length);
        const outer = parent.getBoundingClientRect();
        for (const r of range.getClientRects()) if (r.width > 0 && r.height > 0) boxes.push({ left: outer.left, width: outer.width, top: r.top, height: r.height });
      }
    }
    for (const sel of selectors) for (const el of document.querySelectorAll(sel)) { const r = el.getBoundingClientRect(); boxes.push({ left: r.left, width: r.width, top: r.top, height: r.height }); }
    for (const b of boxes) {
      const d = document.createElement("div");
      d.setAttribute("data-shot-mask-box", "");
      d.style.cssText = `position:fixed;left:${Math.floor(b.left)}px;top:${Math.floor(b.top)}px;width:${Math.ceil(b.width) + 1}px;height:${Math.ceil(b.height) + 1}px;background:${color};z-index:2147483647;pointer-events:none`;
      document.body.appendChild(d);
    }
    return boxes.length;
  }, { patterns: presets.mask_text_patterns || [], selectors: presets.mask_selectors || [], color: presets.mask_color || "#7f7f7f" });
}

const tracked = new WeakMap();

/** Counts the page's requests in flight (attached once per page; call it early, before navigation, for full coverage). */
export function trackRequests(page, { ignore = /\/api\/events\/stream/ } = {}) {
  if (tracked.has(page)) return tracked.get(page);
  const state = { inflight: new Set(), last: Date.now() };
  const skip = request => ignore.test(request.url()) || (request.headers().accept || "").includes("text/event-stream");
  page.on("request", r => { if (!skip(r)) { state.inflight.add(r); state.last = Date.now(); } });
  const done = r => { if (state.inflight.delete(r)) state.last = Date.now(); };
  page.on("requestfinished", done);
  page.on("requestfailed", done);
  tracked.set(page, state);
  return state;
}

export const LOADING_PATTERN_PRESET = "^\\s*(Wczytuję|Wczytuje|Ładuję|Loading)\\b";

/** Waits for a still page, then shoots. Returns the list of waits that ran out (empty = the page was still). */
export async function stableShot(page, file, { quietMs = 250, timeoutMs = 8000, loading = LOADING_PATTERN_PRESET } = {}) {
  const state = trackRequests(page);
  const advisories = [];
  const deadline = Date.now() + timeoutMs;
  try {
    await page.waitForFunction(source => {
      const re = new RegExp(source, "m");
      return !document.body || !re.test(document.body.innerText || "");
    }, loading, { timeout: Math.max(1, deadline - Date.now()) });
  } catch { advisories.push("loading text still visible"); }
  for (;;) {
    if (state.inflight.size === 0 && Date.now() - state.last >= quietMs) break;
    if (Date.now() > deadline) { advisories.push(`requests in flight: ${[...state.inflight].map(r => r.url()).join(", ")}`); break; }
    await page.waitForTimeout(50);
  }
  try {
    await page.evaluate(() => Promise.race([
      Promise.all(document.getAnimations().filter(a => { const t = a.effect && a.effect.getComputedTiming ? a.effect.getComputedTiming() : null; return !t || t.iterations !== Infinity; }).map(a => a.finished.catch(() => {}))),
      new Promise(resolve => setTimeout(resolve, 3000)),
    ]));
  } catch { advisories.push("animations not finished"); }
  // fonts and layout settle within a frame or two after the last request
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  if (!(await settleScroll(page, SHOT_PRESETS.settle_frames ?? 6, SHOT_PRESETS.settle_timeout_ms ?? 3000).catch(() => false))) advisories.push("scroll still moving");
  const masked = await paintMasks(page, SHOT_PRESETS).catch(() => 0);
  try {
    await page.screenshot({ path: file, animations: "disabled", caret: "hide" });
  } finally {
    if (masked) await page.evaluate(() => { for (const d of document.querySelectorAll("[data-shot-mask-box]")) d.remove(); }).catch(() => {});
  }
  if (advisories.length) process.stderr.write(`stable-shot ${path.basename(file)}: ${advisories.join("; ")}\n`);
  return advisories;
}
