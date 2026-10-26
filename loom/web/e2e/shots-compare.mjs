// Compares two directories of screenshots (e.g. M1_SHOTS_DIR of an m1-*-native script before and after a change), pixel by pixel in
// Chromium (no image library needed). Thresholds are data: e2e/shots-compare.json (or --preset <file>). Usage:
//   node e2e/shots-compare.mjs <before-dir> <after-dir> [--preset file] [--json out.json]
// Exit 0 when every pair is within the preset, 1 otherwise; a picture present on one side only is reported and counts as a difference.
import { readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import path from "node:path";

const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i >= 0 ? args.splice(i, 2)[1] : undefined; };
const presetFile = opt("--preset") ?? new URL("./shots-compare.json", import.meta.url);
const jsonOut = opt("--json");
const [before, after] = args;
if (!before || !after) { console.error("usage: shots-compare.mjs <before-dir> <after-dir> [--preset file] [--json out]"); process.exit(2); }
const preset = JSON.parse(readFileSync(presetFile, "utf8"));
const list = (dir) => { const out = []; for (const f of readdirSync(dir)) { const p = path.join(dir, f); if (statSync(p).isDirectory()) out.push(...list(p).map((x) => path.join(f, x))); else if (f.endsWith(".png")) out.push(f); } return out; };
const names = [...new Set([...list(before), ...list(after)])].sort();
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const chromium = await import("playwright").then((m) => m.chromium, () => import("/opt/node22/lib/node_modules/playwright/index.mjs").then((m) => m.chromium));
const browser = await chromium.launch({ executablePath: CHROMIUM });
const page = await browser.newPage();
const rows = [];
for (const name of names) {
  let a, b;
  try { a = readFileSync(path.join(before, name)); } catch { rows.push({ name, status: "only_after" }); continue; }
  try { b = readFileSync(path.join(after, name)); } catch { rows.push({ name, status: "only_before" }); continue; }
  if (a.equals(b)) { rows.push({ name, status: "identical", diff: 0 }); continue; }
  const r = await page.evaluate(async ({ a, b, tol }) => {
    const load = (src) => new Promise((ok, no) => { const i = new Image(); i.onload = () => ok(i); i.onerror = no; i.src = src; });
    const [x, y] = await Promise.all([load(a), load(b)]);
    if (x.width !== y.width || x.height !== y.height) return { size: [x.width, x.height, y.width, y.height] };
    const px = (img) => { const c = document.createElement("canvas"); c.width = img.width; c.height = img.height; const g = c.getContext("2d"); g.drawImage(img, 0, 0); return g.getImageData(0, 0, img.width, img.height).data; };
    const p = px(x), q = px(y); let diff = 0;
    for (let i = 0; i < p.length; i += 4) if (Math.abs(p[i] - q[i]) > tol || Math.abs(p[i + 1] - q[i + 1]) > tol || Math.abs(p[i + 2] - q[i + 2]) > tol || Math.abs(p[i + 3] - q[i + 3]) > tol) diff++;
    return { diff, total: p.length / 4 };
  }, { a: "data:image/png;base64," + a.toString("base64"), b: "data:image/png;base64," + b.toString("base64"), tol: preset.channel_tolerance ?? 0 });
  if (r.size) rows.push({ name, status: "size_differs", size: r.size });
  else rows.push({ name, status: r.diff <= (preset.max_diff_pixels ?? 0) && r.diff / r.total <= (preset.max_diff_ratio ?? 0) ? "within_preset" : "differs", diff: r.diff, ratio: r.diff / r.total });
}
await browser.close();
const bad = rows.filter((r) => r.status !== "identical" && r.status !== "within_preset");
for (const r of rows) if (r.status !== "identical") console.log(`${r.status.padEnd(14)} ${r.name}${r.diff !== undefined ? ` ${r.diff} px (${(r.ratio * 100).toFixed(4)} %)` : ""}${r.size ? ` ${r.size.join("x")}` : ""}`);
console.log(`[shots-compare] ${rows.length} pictures: ${rows.filter((r) => r.status === "identical").length} identical, ${rows.filter((r) => r.status === "within_preset").length} within the preset, ${bad.length} different`);
if (jsonOut) writeFileSync(jsonOut, JSON.stringify({ preset, rows }, null, 2));
process.exit(bad.length ? 1 : 0);
