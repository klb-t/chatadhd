// R101 D5 (PULPIT_R101_2026-10-28 §6, §13 P3/P4, §14 row D5): the arrangement of the desktop icons as pure data + total functions.
// The setting desktop_arrangement has the shape of window_layout_active (W5b): {<device class>: <arrangement>} at the level `person` and
// installations.<installation id>.<device class> at the level `installation`, which wins. An arrangement is an object such as
// {mode: grid|free|computed [examples, an unknown mode is read as grid with an advisory], columns, snap, flow, positions: {<address>: {col,
// row} | {x, y, unit}}, zones}. Only the icons touched by hand have a position; the others fill the free cells in the order of the flow.
// A change of the width (rotation, on-screen keyboard) never rewrites the stored value: the position is snapped to the grid of the moment
// (P4: placing is idempotent and never loses or stacks an icon, whatever the sequence of sizes). Every operation on the icons is one entry
// of a journal (P3: undo restores the value before, redo the value after). Every number and list is a preset of shell/data/desktop.json
// (`arrangement`); nothing here touches React, the DOM or the network.
import { DESKTOP_X } from "./desktop-icons";

export type Cell = number | "last";
export interface Pos { col?: Cell; row?: Cell; x?: number; y?: number; unit?: string; moved?: string; [k: string]: unknown }
export interface Arrangement { mode?: string; columns?: number | "auto"; snap?: boolean; flow?: string; positions?: Record<string, Pos>; [k: string]: unknown }
export interface Advisory { code: string; vars?: Record<string, string> }
export interface ResolvedArrangement {
  mode: "grid" | "free" | "computed";
  /** The mode as written (an unknown one stays visible here; it is drawn as grid with the advisory arrangement_mode_unknown). */
  modeWritten: string;
  columns: number | "auto" | null;
  snap: boolean;
  flow: string | null;
  positions: Record<string, Pos>;
  /** Which level gave the value: installation (this browser), person, or preset (nothing written). */
  from: "installation" | "person" | "preset";
  value: Arrangement | null;
  advisories: Advisory[];
}
export interface ArrangeData {
  preset?: Arrangement | Record<string, Arrangement>;
  modes_known?: string[];
  levels?: string[];
  journal_limit?: number | null;
  arrange_by?: string[];
  free_rows?: number;
  drag_threshold_px?: number;
}
export const ARRANGE: ArrangeData = ((DESKTOP_X as unknown as { arrangement?: ArrangeData }).arrangement) ?? {};

const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);
const parse = (v: unknown): unknown => { if (typeof v === "string" && v.trim().startsWith("{")) { try { return JSON.parse(v); } catch { return undefined; } } return v; };
const fin = (v: unknown): number | undefined => (typeof v === "number" && Number.isFinite(v) ? v : typeof v === "string" && v.trim() !== "" && Number.isFinite(Number(v)) ? Number(v) : undefined);
const byClass = (v: unknown, cls: string): unknown => (isObj(v) && !("mode" in v) && !("positions" in v) ? (cls in v ? v[cls] : v.any ?? v.desktop) : v);

/** Reads desktop_arrangement for a device class: installations.<id>.<class> (or .any) wins over <class> (or .any); nothing = the preset.
 *  Any value resolves (a JSON text is parsed, an unreadable one is the preset with the advisory arrangement_unreadable). Never throws. */
export function readArrangement(value: unknown, cls: string, installation?: string, data: ArrangeData = ARRANGE): ResolvedArrangement {
  const advisories: Advisory[] = [];
  const v = parse(value);
  if (value !== undefined && value !== null && value !== "" && !isObj(v)) advisories.push({ code: "arrangement_unreadable" });
  let from: ResolvedArrangement["from"] = "preset"; let a: unknown;
  const mine = installation && isObj(v) && isObj(v.installations) ? (v.installations as Record<string, unknown>)[installation] : undefined;
  if (isObj(mine)) { const x = parse(mine[cls] ?? mine.any); if (isObj(x)) { a = x; from = "installation"; } }
  if (a === undefined && isObj(v)) { const x = parse(v[cls] ?? v.any); if (isObj(x)) { a = x; from = "person"; } }
  const preset = byClass(data.preset, cls);
  const base: Arrangement = isObj(preset) ? preset as Arrangement : {};
  const val = isObj(a) ? a as Arrangement : null;
  const m = { ...base, ...(val ?? {}) } as Arrangement;
  const known = data.modes_known ?? ["grid", "free", "computed"];
  const written = typeof m.mode === "string" && m.mode.trim() ? m.mode.trim() : "grid";
  let mode: ResolvedArrangement["mode"] = written === "free" ? "free" : written === "computed" ? "computed" : "grid";
  if (!known.includes(written) || !["grid", "free", "computed"].includes(written)) { advisories.push({ code: "arrangement_mode_unknown", vars: { mode: written } }); mode = "grid"; }
  const cn = fin(m.columns);
  const positions: Record<string, Pos> = {};
  // a null member means "absent" (RFC 7386: the merge on the host cannot keep a null), so it is read as absent here too
  if (isObj(m.positions)) for (const [k, p] of Object.entries(m.positions)) if (isObj(p)) positions[k] = Object.fromEntries(Object.entries(p).filter(([, x]) => x !== null)) as Pos;
  return { mode, modeWritten: written, columns: m.columns === "auto" ? "auto" : cn === undefined ? null : Math.max(1, Math.floor(cn)), snap: m.snap !== false,
    flow: typeof m.flow === "string" && m.flow.trim() ? m.flow.trim() : null, positions, from, value: val, advisories };
}

/** The RFC 7386 merge patch that turns `from` into `to`: members of `from` missing in `to` become null (so a removed position does not come
 *  back from the merge on the host), objects are diffed member by member, anything else is replaced. */
export function diffPatch(from: unknown, to: unknown): unknown {
  if (!isObj(to)) return to === undefined ? null : to;
  if (!isObj(from)) return to;
  const out: Record<string, unknown> = {};
  for (const k of Object.keys(from)) if (!(k in to)) out[k] = null;
  for (const [k, v] of Object.entries(to)) {
    if (isObj(v) && isObj(from[k])) { const d = diffPatch(from[k], v); if (isObj(d) && Object.keys(d).length === 0) continue; out[k] = d; }
    else if (JSON.stringify(from[k]) !== JSON.stringify(v)) out[k] = v;
  }
  return out;
}

/** The merge patch (merged on the host, as window_layout_active: two devices never overwrite each other) that writes (arrangement) or
 *  clears (null) the arrangement of a class at the levels `levels` (preset installation and person, data.levels): installation =
 *  installations.<id>.<class>, person = <class>. */
export function arrangementPatch(value: unknown, cls: string, arrangement: Arrangement | null, installation?: string, levels: string[] = ARRANGE.levels ?? ["installation", "person"]): Record<string, unknown> {
  const v = parse(value);
  const patch: Record<string, unknown> = {};
  const oldInst = installation && isObj(v) && isObj(v.installations) && isObj((v.installations as Record<string, unknown>)[installation])
    ? parse(((v.installations as Record<string, unknown>)[installation] as Record<string, unknown>)[cls]) : undefined;
  const oldPerson = isObj(v) ? parse(v[cls]) : undefined;
  const to = arrangement === null ? null : JSON.parse(JSON.stringify(arrangement));
  if (installation && levels.includes("installation")) patch.installations = { [installation]: { [cls]: to === null ? null : diffPatch(oldInst, to) } };
  if (levels.includes("person") || !installation || !levels.includes("installation")) patch[cls] = to === null ? null : diffPatch(oldPerson, to);
  return patch;
}

export interface Placed { address: string; col: number; row: number; x: number; y: number; pinned: boolean }
export interface Placement { mode: ResolvedArrangement["mode"]; columns: number; rows: number; cellW: number; cellH: number; width: number; height: number; placed: Placed[]; advisories: Advisory[] }

/** The free cells in the order of the flow: row_major (preset) fills rows, column_major fills the columns of `baseRows` rows first and
 *  then goes on row by row below; any other flow is read as row_major. The sequence is endless (the grid grows downwards). */
function* cellsInFlow(cols: number, baseRows: number, flow: string): Generator<[number, number]> {
  if (flow === "column_major") {
    const r = Math.max(1, baseRows);
    for (let c = 0; c < cols; c++) for (let y = 0; y < r; y++) yield [c, y];
    for (let y = r; ; y++) for (let c = 0; c < cols; c++) yield [c, y];
  }
  for (let y = 0; ; y++) for (let c = 0; c < cols; c++) yield [c, y];
}

/** Places the icons (their addresses, in the order the declaration gives) on a grid `width` px wide with cells of `cellPx`. grid: a hand
 *  position {col,row} is snapped to the grid of this width (col clamped to 0…columns-1, "last" = the last column/row, fractions rounded);
 *  two icons on one cell: the later one goes to the next free cell of the flow; the others fill the free cells in flow order. free: x/y as
 *  a fraction of the area (unit fraction, the preset) or px (unit px), clamped into the area; snap=true rounds them to the cells. computed:
 *  the positions are not used (the declaration's order and flow place them). Every icon is placed exactly once whatever the input. */
export function placeIcons(addresses: readonly string[], arr: ResolvedArrangement, width: number, grid: { columns: number | "auto"; cellPx: number; flow: string }, data: ArrangeData = ARRANGE): Placement {
  const advisories: Advisory[] = [];
  const cellPx = Number.isFinite(grid.cellPx) && grid.cellPx > 0 ? grid.cellPx : 96;
  const w = Number.isFinite(width) && width > 0 ? width : cellPx;
  const wantCols = arr.columns ?? grid.columns;
  const cols = Math.max(1, wantCols === "auto" ? Math.floor(w / cellPx) : Math.floor(Number(wantCols)) || 1);
  const cellW = w / cols;
  const flow = arr.flow ?? grid.flow ?? "row_major";
  const uniq: string[] = []; const seen = new Set<string>();
  for (const a of addresses) if (!seen.has(a)) { seen.add(a); uniq.push(a); }
  const n = uniq.length;
  const baseRows = Math.max(n ? 1 : 0, Math.ceil(n / cols));
  const placed = new Map<string, Placed>();
  const taken = new Set<string>();
  const key = (c: number, r: number) => `${c},${r}`;
  const usePos = arr.mode !== "computed";
  const freeRows = Math.max(baseRows, Math.floor(fin(data.free_rows) ?? 4));
  const areaH = freeRows * cellPx;
  if (arr.mode === "free") {
    const it = cellsInFlow(cols, baseRows, flow);
    for (const a of uniq) {
      const p = usePos ? arr.positions[a] : undefined;
      const px = fin(p?.x), py = fin(p?.y);
      if (p && px !== undefined && py !== undefined) {
        const unitPx = p.unit === "px";
        let x = unitPx ? px : px * w, y = unitPx ? py : py * areaH;
        x = Math.min(Math.max(0, x), Math.max(0, w - cellW)); y = Math.min(Math.max(0, y), Math.max(0, areaH - cellPx));
        let col = Math.min(cols - 1, Math.max(0, Math.round(x / cellW))), row = Math.max(0, Math.round(y / cellPx));
        if (arr.snap) { x = col * cellW; y = row * cellPx; }
        else { col = Math.min(cols - 1, Math.floor(x / cellW + 1e-9)); row = Math.floor(y / cellPx + 1e-9); }
        placed.set(a, { address: a, col, row, x, y, pinned: true }); continue;
      }
      let c: [number, number];
      do c = it.next().value as [number, number]; while (taken.has(key(c[0], c[1])));
      taken.add(key(c[0], c[1]));
      placed.set(a, { address: a, col: c[0], row: c[1], x: c[0] * cellW, y: c[1] * cellPx, pinned: false });
    }
  } else {
    // the rows the "last" row means: as many as the icons need, or the furthest hand row
    let maxRow = baseRows - 1;
    for (const a of uniq) { const r = fin(arr.positions[a]?.row); if (usePos && r !== undefined) maxRow = Math.max(maxRow, Math.round(r)); }
    const lastRow = Math.max(0, maxRow);
    const snapCell = (v: Cell | undefined, last: number): number | undefined => (v === "last" ? last : fin(v) === undefined ? undefined : Math.round(fin(v)!));
    for (const a of uniq) {
      const p = usePos ? arr.positions[a] : undefined;
      const c0 = snapCell(p?.col, cols - 1), r0 = snapCell(p?.row, lastRow);
      if (c0 === undefined || r0 === undefined) continue;
      let c = Math.min(cols - 1, Math.max(0, c0)), r = Math.max(0, r0);
      if (taken.has(key(c, r))) {
        // the next free cell after this one, in row-major order (the grid grows downwards, so one is always found)
        let i = r * cols + c; do { i++; c = i % cols; r = Math.floor(i / cols); } while (taken.has(key(c, r)));
        advisories.push({ code: "arrangement_cell_taken", vars: { address: a } });
      }
      taken.add(key(c, r));
      placed.set(a, { address: a, col: c, row: r, x: c * cellW, y: r * cellPx, pinned: true });
    }
    const it = cellsInFlow(cols, baseRows, flow);
    for (const a of uniq) {
      if (placed.has(a)) continue;
      let c: [number, number];
      do c = it.next().value as [number, number]; while (taken.has(key(c[0], c[1])));
      taken.add(key(c[0], c[1]));
      placed.set(a, { address: a, col: c[0], row: c[1], x: c[0] * cellW, y: c[1] * cellPx, pinned: false });
    }
  }
  const list = uniq.map((a) => placed.get(a)!);
  const rows = list.reduce((m, p) => Math.max(m, p.row + 1), 0);
  const height = arr.mode === "free" ? Math.max(areaH, list.reduce((m, p) => Math.max(m, p.y + cellPx), 0)) : rows * cellPx;
  return { mode: arr.mode, columns: cols, rows, cellW, cellH: cellPx, width: w, height, placed: list, advisories };
}

/** The cell under a point of the icon area (px from its top-left corner), snapped into the grid. */
export function cellAt(x: number, y: number, pl: Pick<Placement, "columns" | "cellW" | "cellH">): { col: number; row: number } {
  const col = Math.min(pl.columns - 1, Math.max(0, Math.floor((Number.isFinite(x) ? x : 0) / Math.max(1e-9, pl.cellW))));
  const row = Math.max(0, Math.floor((Number.isFinite(y) ? y : 0) / Math.max(1e-9, pl.cellH)));
  return { col, row };
}

/** The value of the arrangement as the person last wrote it (or {} for the preset), without the level bookkeeping. */
const own = (arr: ResolvedArrangement): Arrangement => ({ ...(arr.value ?? {}), positions: { ...arr.positions } });

/** Moving one icon by hand. grid/computed: {col,row} of the cell under the finger; an icon pinned on that cell takes the moved icon's
 *  old cell (they swap). free: {x,y} as a fraction of the area. `computed` keeps no hand positions: the move switches the mode to grid
 *  (the mode written in the value; undo brings computed back) - the move is never dropped silently. */
export function moveIcon(arr: ResolvedArrangement, pl: Placement, address: string, to: { col: number; row: number } | { x: number; y: number }, now = new Date().toISOString()): Arrangement {
  const next = own(arr);
  const positions = { ...(next.positions ?? {}) };
  if (arr.mode === "computed") next.mode = "grid";
  const me = pl.placed.find((p) => p.address === address);
  if ("col" in to) {
    const other = pl.placed.find((p) => p.address !== address && p.col === to.col && p.row === to.row);
    if (other && me) positions[other.address] = { col: me.col, row: me.row, moved: now };
    positions[address] = { col: to.col, row: to.row, moved: now };
  } else {
    const fx = Math.min(1, Math.max(0, to.x)), fy = Math.min(1, Math.max(0, to.y));
    positions[address] = { x: Number(fx.toFixed(4)), y: Number(fy.toFixed(4)), unit: "fraction", moved: now };
  }
  next.positions = positions;
  return next;
}

export interface ArrangeIcon { address: string; label: string; kind?: string | null; count?: number }
/** desktop.arrange {by}: label (the preset), kind (then label), recent (the latest hand move first, then the others in order), count
 *  (the highest badge first), declaration (no hand position: the declaration's order) [examples; data.arrange_by]. An unknown `by` changes
 *  nothing and says so (advisory arrange_by_unknown). The result is the whole new value: one journal entry. */
export function arrangeIcons(arr: ResolvedArrangement, pl: Placement, icons: readonly ArrangeIcon[], by: string, lang = "pl", data: ArrangeData = ARRANGE): { value: Arrangement | null; advisory?: Advisory } {
  const known = data.arrange_by ?? ["label", "kind", "recent", "count", "declaration"];
  const b = (by || "label").trim();
  if (!known.includes(b) || !["label", "kind", "recent", "count", "declaration"].includes(b)) return { value: null, advisory: { code: "arrange_by_unknown", vars: { by: b } } };
  const next = own(arr);
  if (arr.mode === "computed") next.mode = "grid";
  if (b === "declaration") { next.positions = {}; return { value: next }; }
  const idx = new Map(icons.map((ic, i) => [ic.address, i]));
  const cmpText = (x: string, y: string) => x.localeCompare(y, lang === "pl" ? "pl" : "en", { sensitivity: "base" });
  const moved = (a: string) => String(arr.positions[a]?.moved ?? "");
  const sorted = [...icons].sort((x, y) => {
    const ord = (idx.get(x.address) ?? 0) - (idx.get(y.address) ?? 0);
    if (b === "label") return cmpText(x.label, y.label) || ord;
    if (b === "kind") return cmpText(String(x.kind ?? "~"), String(y.kind ?? "~")) || cmpText(x.label, y.label) || ord;
    if (b === "count") return (y.count ?? 0) - (x.count ?? 0) || ord;
    return moved(y.address).localeCompare(moved(x.address)) || ord;   // recent
  });
  const positions: Record<string, Pos> = {};
  const it = cellsInFlow(pl.columns, Math.max(1, Math.ceil(sorted.length / pl.columns)), arr.flow ?? "row_major");
  for (const ic of sorted) {
    const [c, r] = it.next().value as [number, number];
    positions[ic.address] = arr.mode === "free" ? { x: Number(((c * pl.cellW) / Math.max(1, pl.width)).toFixed(4)), y: Number(((r * pl.cellH) / Math.max(1, pl.height)).toFixed(4)), unit: "fraction" } : { col: c, row: r };
  }
  next.positions = positions;
  return { value: next };
}

/** The journal of the arrangement (P3): each entry keeps the value before and after (null = nothing written: the preset). */
export interface JournalEntry { op: string; before: Arrangement | null; after: Arrangement | null; at?: string }
export interface Journal { past: JournalEntry[]; future: JournalEntry[] }
export const emptyJournal = (): Journal => ({ past: [], future: [] });
/** Adds an entry (a new operation clears what could be redone); `limit` = data.journal_limit (null or absent = no limit). */
export function record(j: Journal, e: JournalEntry, limit: number | null | undefined = ARRANGE.journal_limit): Journal {
  const past = [...j.past, e];
  const lim = fin(limit);
  return { past: lim !== undefined && lim >= 0 ? past.slice(Math.max(0, past.length - Math.floor(lim))) : past, future: [] };
}
export function undo(j: Journal): { journal: Journal; value: Arrangement | null; entry: JournalEntry } | null {
  const e = j.past[j.past.length - 1]; if (!e) return null;
  return { journal: { past: j.past.slice(0, -1), future: [e, ...j.future] }, value: e.before, entry: e };
}
export function redo(j: Journal): { journal: Journal; value: Arrangement | null; entry: JournalEntry } | null {
  const e = j.future[0]; if (!e) return null;
  return { journal: { past: [...j.past, e], future: j.future.slice(1) }, value: e.after, entry: e };
}
/** A journal read back from storage (a JSON text or an object); anything unreadable is an empty journal. */
export function readJournal(v: unknown): Journal {
  const x = parse(v);
  if (!isObj(x)) return emptyJournal();
  const ok = (a: unknown): JournalEntry[] => (Array.isArray(a) ? a.filter((e): e is JournalEntry => isObj(e) && typeof e.op === "string") : []);
  return { past: ok(x.past), future: ok(x.future) };
}

/** The order a word of a sentence names („według nazwy”, „by kind”): data.arrange_words (an open list per order and language); an
 *  empty word = the first order of data.arrange_by (the preset label); a word no list has = undefined (the step says what it knows). */
export function arrangeByOfWord(word: string, data: ArrangeData & { arrange_words?: Record<string, { pl?: string[]; en?: string[] }> } = ARRANGE as ArrangeData & { arrange_words?: Record<string, { pl?: string[]; en?: string[] }> }): string | undefined {
  const w = (word ?? "").trim().toLowerCase();
  const orders = data.arrange_by ?? ["label", "kind", "recent", "count", "declaration"];
  if (!w) return orders[0];
  if (orders.includes(w)) return w;
  for (const by of orders) {
    const words = [...(data.arrange_words?.[by]?.pl ?? []), ...(data.arrange_words?.[by]?.en ?? [])].map((x) => x.toLowerCase());
    if (words.some((x) => w === x || w.split(/\s+/).includes(x) || (x.includes(" ") && w.includes(x)))) return by;
  }
  return undefined;
}
/** The event a client step (desktop.arrange by a sentence) sends to the mounted desktop; the desktop fills `result` synchronously. */
export const DESKTOP_ARRANGE = "loom:desktop-arrange";
export interface ArrangeRequest { by: string; result?: { ok: boolean; code?: string; by?: string } }
