// W2 (OKNA, luka 1 of W1): a screenshot that does not depend on timing. Before the shot it waits until
//  (1) no "loading" line is visible (Wczytuję… / Loading…, words of the page, a pattern that is a preset of this helper),
//  (2) the page's own requests are quiet (none in flight for `quietMs`; the live event feed /api/events/stream is long-lived and ignored),
//  (3) finite animations ended,
// and takes it with Playwright's `animations: "disabled"` (infinite animations such as the status dot are reset to their start,
// transitions are finished) and a hidden caret. Test-side only: the product is not changed. Every wait is bounded (`timeoutMs`);
// a wait that runs out is reported on stderr and the shot is taken anyway (an advisory, not a stop).
import path from "node:path";

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
  await page.screenshot({ path: file, animations: "disabled", caret: "hide" });
  if (advisories.length) process.stderr.write(`stable-shot ${path.basename(file)}: ${advisories.join("; ")}\n`);
  return advisories;
}
