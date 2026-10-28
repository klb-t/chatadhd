// W0 of the window system (docs/architecture/OKNA_SYSTEM_2026-10-23.md §2): the window model as pure data + total operations.
// Nothing here touches React or the DOM. Every operation is TOTAL: invalid input returns the unchanged state plus an advisory, never throws.
// Every number, preset and name below is an example/preset (CLAUDE.md "lists are examples"): modes, placements and kinds are open strings where
// the design allows it; `Mode` is closed only because the modes are the vocabulary of the design (§2.1), reversible by widening the type.
// R101 D1 (PULPIT_R101 §7) widened it by the sixth mode `background`: the desktop, exactly one per layout, under every other window. Decision
// D5 (card S1 / M1-R101-D1 in docs/reports/INDEX.md): the desktop is NOT an element of `windows[]` (162 reads of `.windows` in 18 files of
// the page treat every element as a closable, switchable window); it lives in the optional field `layout.desktop` and is seen through the
// adapter `desktopOf` / `allWindows`. Absent field = the preset desktop (nothing written, every saved layout stays byte-identical). Reversal:
// put it into windows[] (normalize inserts it) and filter `mode !== "background"` in those readers.

export type Mode = "maximized" | "docked" | "floating" | "minimized" | "tabbed" | "background";
export const MODES: readonly Mode[] = ["maximized", "docked", "floating", "minimized", "tabbed", "background"];
/** The kind of the background window and its preset (data: shell/data/window_kinds.json, kind `desktop`, `preset_params`). */
export const DESKTOP_KIND = "desktop";
export const DESKTOP_PRESET: Readonly<Pick<Win, "id" | "node">> = { id: "w_desktop", node: "desktop:main" };
export type Rect = { x: number; y: number; w: number; h: number; unit: "fraction" | "px" };
export type Leaf = { leaf: string; born?: "explicit" };
export type Split = { split: "h" | "v"; ratio: number; a: Tree; b: Tree };
export type Tree = Leaf | Split;
export interface Restore { mode: Mode; dock?: { leaf: string }; rect?: Rect }
export interface Win {
  id: string; kind: string; node: string; mode: Mode; dock?: { leaf: string }; rect?: Rect; z: number;
  params: Record<string, unknown>; profile: unknown; title_override: string | null; collapsed: boolean; pinned: boolean; restore?: Restore;
}
export interface LeafInfo { windows: string[]; active: string | null; born?: "explicit" }
export interface WLayout {
  schema?: string; id: string; label?: unknown; origin?: string; device_class?: string;
  tree: Tree; leaves: Record<string, LeafInfo>; windows: Win[]; focus: string | null;
  channels: Record<string, unknown>; shelf: { handles: string[] }; profile?: unknown;
  /** R101 D1: the one background window (the desktop); absent = the preset one (`desktopOf`). Never an element of `windows`. */
  desktop?: Win;
  [extra: string]: unknown;   // chat/topbar/menu/... travel with the layout untouched
}
export interface Advisory { id: string; window?: string; evidence: Record<string, unknown>; text: string }
export interface JournalEntry { op: string; layout: WLayout }
export interface WState { layout: WLayout; past: JournalEntry[]; future: JournalEntry[]; limit: number }
export interface Ctx {
  /** kind -> status (`present`/`planned`); when absent every kind is accepted (the registry is open). */
  kinds?: Record<string, { status: string; reason?: string }>;
  collapseEmptyLeaves?: "emptied" | "all" | "none";   // preset `emptied` (§2.2)
  newId?: () => string;
  minUsefulWidth?: number;
}
export type Placement = "maximize" | "beside" | "below" | "float" | "tab" | "shelf";
export type Target = { leaf: string; index?: number } | { side: "left" | "right" | "top" | "bottom"; ratio?: number; of?: string };
export type Op =
  | { op: "open"; kind: string; node?: string; placement?: Placement; params?: Record<string, unknown> }
  | { op: "close"; id?: string; all_except?: string[]; kind?: string }
  | { op: "focus"; id: string } | { op: "next" } | { op: "prev" }
  | { op: "maximize"; id?: string } | { op: "restore"; id?: string } | { op: "minimize"; id: string }
  | { op: "dock"; id: string; target: Target }
  | { op: "float"; id: string; rect?: Partial<Rect> }
  | { op: "split"; leaf: string; dir: "h" | "v"; ratio?: number; new_first?: boolean }
  | { op: "move"; id: string; rect?: Partial<Rect>; dx?: number; dy?: number; leaf?: string; index?: number }
  /** W4: the ratio of one split, addressed by its path from the root ("" = root, "a"/"b" = a side, "ab" = side b of side a). */
  | { op: "ratio"; path: string; ratio: number };
export interface Result { state: WState; advisories: Advisory[]; applied: boolean }

const ADV_TEXT: Record<string, string> = {
  unknown_window: "Nie ma takiego okna.", unknown_leaf: "Nie ma takiego panelu.", bad_op: "Nieprawidłowe polecenie, nic nie zmieniono.",
  planned_kind: "Ten rodzaj okna jeszcze nie istnieje.", unknown_kind: "Rodzaj okna nie jest w rejestrze; otwarto mimo to.",
  empty_leaf_collapsed: "Pusty panel został usunięty. Czy tak ma być?", ratio_clamped: "Proporcja podziału poza zakresem 0..1, przycięto.",
  zero_size: "Okno ma zerowy rozmiar. Czy to celowe?", dock_onto_self: "Okno jest już jedyne w tym panelu.", focus_hidden: "Okno ma fokus, ale jest zasłonięte.",
  rect_invalid: "Prostokąt okna był nieprawidłowy, użyto domyślnego.", nothing_to_undo: "Nie ma czego cofnąć.", nothing_to_redo: "Nie ma czego ponowić.",
  dock_narrow: "Panel jest wąski. Czy tak ma być?",
  desktop_not_closable: "Pulpit się nie zamyka; można go zasłonić oknami albo pokazać skrótem „pokaż pulpit”.",
  desktop_fixed: "Pulpit leży pod wszystkimi oknami; nie da się go zadokować, odpiąć ani przesunąć.",
  desktop_one: "Pulpit już jest (jeden na układ); pokazuje go „pokaż pulpit”.",
  desktop_duplicate: "W układzie było drugie okno-pulpit; zwinięto je, pulpit jest jeden. Czy tak ma być?",
  desktop_lifted: "Okno w trybie tła stało się pulpitem tego układu.", unknown_split: "Nie ma takiego podziału.", not_movable: "To okno nie ma gdzie się przesunąć w tym stanie.", no_windows: "Brak okien.",
};
const adv = (id: string, evidence: Record<string, unknown> = {}, window?: string): Advisory => ({ id, evidence, text: ADV_TEXT[id] ?? id, ...(window ? { window } : {}) });

// ---------- helpers on trees ----------
const clone = <T,>(x: T): T => JSON.parse(JSON.stringify(x));
const same = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);
export const isLeaf = (t: Tree): t is Leaf => (t as Leaf).leaf !== undefined;
export function leafIds(t: Tree): string[] { return isLeaf(t) ? [t.leaf] : [...leafIds(t.a), ...leafIds(t.b)]; }
function mapTree(t: Tree, f: (l: Leaf) => Tree): Tree { return isLeaf(t) ? f(t) : { ...t, a: mapTree(t.a, f), b: mapTree(t.b, f) }; }
function removeLeaf(t: Tree, id: string): Tree | null {
  if (isLeaf(t)) return t.leaf === id ? null : t;
  const a = removeLeaf(t.a, id), b = removeLeaf(t.b, id);
  return a && b ? { ...t, a, b } : a ?? b;
}
const clamp01 = (n: unknown, dflt = 0.5): { v: number; changed: boolean } => {
  if (typeof n !== "number" || Number.isNaN(n)) return { v: dflt, changed: n !== undefined };
  const v = Math.min(1, Math.max(0, n)); return { v, changed: v !== n };
};
function freshLeafId(l: WLayout): string { let i = 0; const used = new Set([...Object.keys(l.leaves), ...leafIds(l.tree)]); while (used.has("L" + i)) i++; return "L" + i; }
let counter = 0;
const defaultId = () => "w_" + (Date.now().toString(16) + (counter++).toString(16) + Math.floor(Math.random() * 2 ** 32).toString(16)).replace(/[^0-9a-f]/g, "").padEnd(12, "0").slice(-12);

// ---------- R101 D1: the background window (the desktop), through the adapter ----------
const isWinObj = (x: unknown): x is Win => !!x && typeof x === "object" && typeof (x as Win).id === "string" && !!(x as Win).id;
/** The one background window of the layout: the stored `layout.desktop` (forced to mode `background`, kind `desktop` unless it names its
 *  own kind) or the preset one. Never undefined, so "exactly one background window" holds for every layout, also one written before D1. */
export function desktopOf(l: WLayout): Win {
  const d = isWinObj(l.desktop) ? l.desktop : undefined;
  return {
    id: d?.id ?? DESKTOP_PRESET.id, kind: typeof d?.kind === "string" && d.kind ? d.kind : DESKTOP_KIND, node: typeof d?.node === "string" && d.node ? d.node : DESKTOP_PRESET.node,
    mode: "background", z: 0, params: d?.params && typeof d.params === "object" ? d.params : {}, profile: d?.profile ?? null,
    title_override: d?.title_override ?? null, collapsed: false, pinned: true,
  };
}
/** Every window of the layout including the background one (first: it is under everything). */
export const allWindows = (l: WLayout): Win[] => [desktopOf(l), ...l.windows];
export const isBackground = (w: Pick<Win, "mode">): boolean => w.mode === "background";

const byId = (l: WLayout, id: unknown): Win | undefined => typeof id === "string" ? l.windows.find((w) => w.id === id) : undefined;
const isDockedMode = (m: Mode) => m === "docked" || m === "tabbed";
export const firstLeaf = (t: Tree): string => (isLeaf(t) ? t.leaf : firstLeaf(t.a));

// ---------- normalization (run after every operation) ----------
/** Makes the layout internally consistent: dock<->leaf membership, tabbed vs docked, z-order, focus, handles. Idempotent. */
export function normalize(l: WLayout, ctx: Ctx = {}, out: Advisory[] = [], mayCollapse: boolean = true): WLayout {
  // R101 D1: a background window inside windows[] (an imported or hand-written layout) becomes the desktop when the layout has none; any
  // other one is minimized with the advisory desktop_duplicate (nothing is dropped). Afterwards windows[] holds no background window.
  if (l.windows.some((w) => w.mode === "background")) {
    const kept: Win[] = [];
    for (const w of l.windows) {
      if (w.mode !== "background") { kept.push(w); continue; }
      if (!isWinObj(l.desktop)) { delete w.dock; delete w.rect; l.desktop = { ...w, mode: "background", z: 0 }; out.push(adv("desktop_lifted", { id: w.id }, w.id)); continue; }
      delete w.dock; w.mode = "minimized"; kept.push(w); out.push(adv("desktop_duplicate", { id: w.id, desktop: l.desktop.id }, w.id));
    }
    l.windows = kept;
  }
  if (l.desktop !== undefined && !isWinObj(l.desktop)) delete l.desktop;
  else if (l.desktop) { l.desktop.mode = "background"; l.desktop.z = 0; delete l.desktop.dock; delete l.desktop.rect; }
  const ids = new Set(l.windows.map((w) => w.id));
  const leaves = new Set(leafIds(l.tree));
  for (const k of Object.keys(l.leaves)) if (!leaves.has(k)) delete l.leaves[k];
  for (const k of leaves) l.leaves[k] = { ...(l.leaves[k] ?? { windows: [], active: null }), windows: [], active: l.leaves[k]?.active ?? null };
  for (const w of l.windows) {
    if (w.dock && !leaves.has(w.dock.leaf)) delete w.dock;
    if ((isDockedMode(w.mode)) && !w.dock) w.dock = { leaf: firstLeaf(l.tree) };
    if (w.mode === "floating" || w.mode === "minimized") delete w.dock;
    if (w.dock) l.leaves[w.dock.leaf].windows.push(w.id);
  }
  // leaf order: keep the previous order where possible (windows array order is the stable fallback)
  for (const k of leaves) {
    const li = l.leaves[k];
    if (li.born === undefined && leafBorn(l.tree, k)) li.born = "explicit";
    if (!li.windows.includes(li.active as string)) li.active = li.windows[0] ?? null;
  }
  for (const w of l.windows) if (w.dock) w.mode = w.mode === "maximized" ? "maximized" : l.leaves[w.dock.leaf].windows.length > 1 ? "tabbed" : "docked";
  // collapse emptied leaves
  const policy = ctx.collapseEmptyLeaves ?? "emptied";
  if (mayCollapse && policy !== "none") {
    for (const k of leafIds(l.tree)) {
      if (l.leaves[k].windows.length || leafIds(l.tree).length <= 1) continue;
      if (policy === "emptied" && leafBorn(l.tree, k)) continue;
      const t = removeLeaf(l.tree, k); if (t) { l.tree = t; delete l.leaves[k]; out.push(adv("empty_leaf_collapsed", { leaf: k })); }
    }
  }
  // single maximized window
  const max = l.windows.filter((w) => w.mode === "maximized");
  for (const w of max.slice(0, -1)) { if (w.dock) w.mode = "docked"; else { w.mode = "floating"; w.rect ??= defaultRect(0); } }
  for (const w of l.windows) if (w.mode === "floating" && !w.rect) w.rect = defaultRect(0);
  // z-order: floating are a permutation 0..k-1, others 0
  const fl = l.windows.filter((w) => w.mode === "floating").sort((a, b) => a.z - b.z || (a.id < b.id ? -1 : 1));
  fl.forEach((w, i) => (w.z = i));
  for (const w of l.windows) if (w.mode !== "floating") w.z = 0;
  l.shelf = { handles: [...new Set((l.shelf?.handles ?? []).filter((h) => ids.has(h)))] };
  if (l.focus !== null && !ids.has(l.focus)) l.focus = null;
  if (l.focus === null && l.windows.length) l.focus = l.windows[l.windows.length - 1].id;
  if (!l.windows.length) l.focus = null;
  return l;
}
function leafBorn(t: Tree, id: string): boolean { return isLeaf(t) ? t.leaf === id && t.born === "explicit" : leafBorn(t.a, id) || leafBorn(t.b, id); }
const defaultRect = (n: number): Rect => ({ x: Math.min(0.8, 0.1 + 0.05 * (n % 8)), y: Math.min(0.8, 0.08 + 0.05 * (n % 8)), w: 0.46, h: 0.4, unit: "fraction" });
const finite = (n: unknown): n is number => typeof n === "number" && Number.isFinite(n);
function mkRect(base: Rect, p: Partial<Rect> | undefined, out: Advisory[], id: string): Rect {
  if (!p) return base;
  const r: Rect = { ...base };
  for (const k of ["x", "y", "w", "h"] as const) { if (p[k] === undefined) continue; if (finite(p[k])) r[k] = p[k] as number; else out.push(adv("rect_invalid", { field: k }, id)); }
  if (p.unit === "fraction" || p.unit === "px") r.unit = p.unit;
  if (r.w === 0 || r.h === 0) out.push(adv("zero_size", { rect: r }, id));
  return r;
}

// ---------- state ----------
export function createState(layout: WLayout, limit = 50, ctx: Ctx = {}): WState {
  return { layout: normalize(clone(layout), ctx), past: [], future: [], limit: Number.isFinite(limit) && limit >= 0 ? Math.floor(limit) : 50 };
}

/** Detaches a window from its leaf (membership is recomputed by normalize from `dock`). */
function detach(w: Win) { delete w.dock; }
function setActive(l: WLayout, w: Win) { if (w.dock && l.leaves[w.dock.leaf]) l.leaves[w.dock.leaf].active = w.id; }
function remember(w: Win): Restore { return { mode: w.mode, ...(w.dock ? { dock: { ...w.dock } } : {}), ...(w.rect ? { rect: { ...w.rect } } : {}) }; }
function demoteMaximized(l: WLayout, except?: string) {
  for (const w of l.windows) if (w.mode === "maximized" && w.id !== except) {
    // W4: a maximized window without a dock goes back to floating only when it floated before; otherwise it waits in the switcher
    // (minimized) - the stack of maximized windows of §4.1, where a covered panel is not drawn over the one in front.
    if (w.dock) w.mode = "docked"; else if (w.restore?.mode === "floating") { w.mode = "floating"; w.rect ??= w.restore?.rect ?? defaultRect(l.windows.length); } else w.mode = "minimized";
  }
}
function topZ(l: WLayout, w: Win) { w.z = Math.max(-1, ...l.windows.filter((x) => x.mode === "floating" && x !== w).map((x) => x.z)) + 1; }
function sideSplit(l: WLayout, ofLeaf: string, side: "left" | "right" | "top" | "bottom", ratio: number, born?: "explicit"): string {
  const nl = freshLeafId(l); const dir = side === "left" || side === "right" ? "h" : "v"; const first = side === "left" || side === "top";
  const nLeaf: Leaf = born ? { leaf: nl, born } : { leaf: nl };
  l.leaves[nl] = { windows: [], active: null, ...(born ? { born } : {}) };
  l.tree = mapTree(l.tree, (lf) => lf.leaf !== ofLeaf ? lf : { split: dir, ratio: first ? ratio : 1 - ratio, a: first ? nLeaf : lf, b: first ? lf : nLeaf });
  return nl;
}
function visibleHost(l: WLayout): string | undefined { return (byId(l, l.focus)?.dock?.leaf) ?? l.windows.find((w) => w.dock)?.dock?.leaf ?? firstLeaf(l.tree); }

// ---------- operations (pure: mutate a clone) ----------
function run(l: WLayout, op: Op, ctx: Ctx, out: Advisory[]): boolean {
  const need = (id: unknown): Win | undefined => { const w = byId(l, id); if (!w) out.push(adv("unknown_window", { id })); return w; };
  // R101 D1: the background window is not closed, docked, floated, moved, maximized or minimized - a noop with a receipt (R101 point 3);
  // a second one is not opened (several desktops are D9 of PULPIT §11). Focus on it is a gap for D2 (keyboard on the icons).
  const desk = desktopOf(l);
  const onDesk = (op as { id?: unknown }).id === desk.id || (op.op === "close" && (op as { kind?: unknown }).kind === DESKTOP_KIND);
  if (onDesk && op.op !== "focus") { out.push(adv(op.op === "close" ? "desktop_not_closable" : "desktop_fixed", { op: op.op }, desk.id)); return false; }
  if (op.op === "open" && op.kind === DESKTOP_KIND) { out.push(adv("desktop_one", { kind: op.kind }, desk.id)); return false; }
  switch (op.op) {
    case "open": {
      if (typeof op.kind !== "string" || !op.kind) { out.push(adv("bad_op", { field: "kind" })); return false; }
      const info = ctx.kinds?.[op.kind];
      if (ctx.kinds && !info) out.push(adv("unknown_kind", { kind: op.kind }));
      else if (info?.status === "planned") { out.push(adv("planned_kind", { kind: op.kind, reason: info.reason })); return false; }
      const id = (ctx.newId ?? defaultId)();
      if (byId(l, id)) { out.push(adv("bad_op", { reason: "id_taken", id })); return false; }
      const w: Win = { id, kind: op.kind, node: typeof op.node === "string" ? op.node : "", mode: "docked", z: 0, params: op.params && typeof op.params === "object" ? clone(op.params) : {}, profile: null, title_override: null, collapsed: false, pinned: false };
      const place = op.placement ?? "maximize";
      if (place === "maximize") { demoteMaximized(l); w.mode = "maximized"; }
      else if (place === "float") { w.mode = "floating"; w.rect = defaultRect(l.windows.filter((x) => x.mode === "floating").length); }
      else if (place === "shelf") { w.mode = "minimized"; l.shelf.handles.push(id); }
      else if (place === "beside" || place === "below") { w.dock = { leaf: sideSplit(l, visibleHost(l)!, place === "beside" ? "right" : "bottom", 0.5) }; }
      else w.dock = { leaf: visibleHost(l)! };   // tab (and any unknown placement: the focused leaf)
      l.windows.push(w); l.focus = id; if (w.mode === "floating") topZ(l, w);
      if (w.dock) { l.leaves[w.dock.leaf] ??= { windows: [], active: null }; setActive(l, w); }
      return true;
    }
    case "close": {
      const victims: Win[] = [];
      if (op.id !== undefined) { const w = need(op.id); if (w) victims.push(w); }
      else if (Array.isArray(op.all_except)) victims.push(...l.windows.filter((w) => !op.all_except!.includes(w.id) && !w.pinned));
      else if (typeof op.kind === "string") victims.push(...l.windows.filter((w) => w.kind === op.kind));
      else { out.push(adv("bad_op", { reason: "close needs id, all_except or kind" })); return false; }
      if (!victims.length) return false;
      const vs = new Set(victims.map((w) => w.id));
      l.windows = l.windows.filter((w) => !vs.has(w.id));
      if (l.focus && vs.has(l.focus)) l.focus = null;
      return true;
    }
    case "focus": case "next": case "prev": {
      let w: Win | undefined;
      if (op.op === "focus") w = need(op.id);
      else { if (!l.windows.length) { out.push(adv("no_windows")); return false; } const i = l.windows.findIndex((x) => x.id === l.focus); w = l.windows[(i + (op.op === "next" ? 1 : -1) + l.windows.length * 2) % l.windows.length]; }
      if (!w) return false;
      if (w.mode === "minimized") restoreWin(l, w);
      l.focus = w.id; setActive(l, w); if (w.mode === "floating") topZ(l, w);
      const max = l.windows.find((x) => x.mode === "maximized");
      if (max && max.id !== w.id && w.mode !== "floating") out.push(adv("focus_hidden", { covered_by: max.id }, w.id));
      return true;
    }
    case "maximize": {
      const w = need(op.id ?? l.focus); if (!w) return false;
      if (w.mode === "maximized") return false;
      demoteMaximized(l, w.id); w.restore = remember(w); w.mode = "maximized"; l.focus = w.id; return true;
    }
    case "restore": {
      const w = need(op.id ?? l.focus); if (!w) return false;
      if (w.mode !== "maximized" && w.mode !== "minimized") return false;
      restoreWin(l, w); return true;
    }
    case "minimize": {
      const w = need(op.id); if (!w || w.mode === "minimized") return false;
      w.restore = remember(w); w.mode = "minimized"; detach(w); return true;
    }
    case "dock": {
      const w = need(op.id); if (!w) return false;
      const t = op.target as Target | undefined;
      if (!t || typeof t !== "object") { out.push(adv("bad_op", { field: "target" })); return false; }
      const leaves = leafIds(l.tree);
      let dest: string;
      if ("leaf" in t) {
        if (!leaves.includes(t.leaf)) { out.push(adv("unknown_leaf", { leaf: t.leaf })); return false; }
        dest = t.leaf;
        if (w.dock?.leaf === dest && l.leaves[dest].windows.length === 1 && w.mode !== "maximized") { out.push(adv("dock_onto_self", { leaf: dest }, w.id)); return false; }
      } else {
        const of = t.of ?? visibleHost(l)!;
        if (!leaves.includes(of)) { out.push(adv("unknown_leaf", { leaf: of })); return false; }
        if (!["left", "right", "top", "bottom"].includes(t.side)) { out.push(adv("bad_op", { field: "side" })); return false; }
        const r = clamp01(t.ratio, 0.5); if (r.changed) out.push(adv("ratio_clamped", { ratio: t.ratio }, w.id));
        if (w.dock?.leaf === of && l.leaves[of].windows.length === 1) { out.push(adv("dock_onto_self", { leaf: of }, w.id)); return false; }
        dest = sideSplit(l, of, t.side, r.v);
        if (r.v === 0 || r.v === 1) out.push(adv("zero_size", { ratio: r.v }, w.id));
      }
      if (w.mode === "maximized" || w.mode === "floating" || w.mode === "minimized") w.restore = remember(w);
      w.dock = { leaf: dest }; w.mode = "docked"; delete w.rect;
      if ("leaf" in t && finite(t.index)) { /* order inside a leaf = order in windows[]; move to index */
        const mates = l.windows.filter((x) => x.id !== w.id && x.dock?.leaf === dest);
        const at = mates[Math.max(0, Math.min(mates.length - 1, Math.floor(t.index)))];
        if (at) { l.windows = l.windows.filter((x) => x !== w); l.windows.splice(l.windows.indexOf(at) + (t.index! >= mates.length ? 1 : 0), 0, w); }
      }
      l.leaves[dest].active = w.id; l.focus = w.id; return true;
    }
    case "float": {
      const w = need(op.id); if (!w) return false;
      if (w.mode === "floating" && !op.rect) return false;
      if (w.mode !== "floating") w.restore = remember(w);
      w.rect = mkRect(w.rect ?? defaultRect(l.windows.filter((x) => x.mode === "floating").length), op.rect, out, w.id);
      w.mode = "floating"; detach(w); l.focus = w.id; topZ(l, w); return true;
    }
    case "split": {
      if (!leafIds(l.tree).includes(op.leaf)) { out.push(adv("unknown_leaf", { leaf: op.leaf })); return false; }
      if (op.dir !== "h" && op.dir !== "v") { out.push(adv("bad_op", { field: "dir" })); return false; }
      const r = clamp01(op.ratio, 0.5); if (r.changed) out.push(adv("ratio_clamped", { ratio: op.ratio }));
      const side = op.dir === "h" ? (op.new_first ? "left" : "right") : (op.new_first ? "top" : "bottom");
      sideSplit(l, op.leaf, side, r.v, "explicit"); return true;
    }
    case "move": {
      const w = need(op.id); if (!w) return false;
      if (op.leaf !== undefined) return run(l, { op: "dock", id: w.id, target: { leaf: op.leaf, index: op.index } }, ctx, out);
      if (w.mode !== "floating") { out.push(adv("not_movable", { mode: w.mode }, w.id)); return false; }
      const r = { ...(w.rect ?? defaultRect(0)) };
      if (op.rect) w.rect = mkRect(r, op.rect, out, w.id);
      else if (finite(op.dx) || finite(op.dy)) w.rect = { ...r, x: r.x + (finite(op.dx) ? op.dx : 0), y: r.y + (finite(op.dy) ? op.dy : 0) };
      else { out.push(adv("bad_op", { reason: "move needs rect, dx/dy or leaf" })); return false; }
      return true;
    }
    case "ratio": {
      if (typeof op.path !== "string" || /[^ab]/.test(op.path)) { out.push(adv("bad_op", { field: "path" })); return false; }
      const r = clamp01(op.ratio, NaN); if (Number.isNaN(r.v)) { out.push(adv("bad_op", { field: "ratio" })); return false; }
      if (r.changed) out.push(adv("ratio_clamped", { ratio: op.ratio }));
      const set = (t: Tree, rest: string): Tree | null => {
        if (isLeaf(t)) return null;
        if (!rest) return { ...t, ratio: r.v };
        const sub = set(rest[0] === "a" ? t.a : t.b, rest.slice(1));
        return sub && (rest[0] === "a" ? { ...t, a: sub } : { ...t, b: sub });
      };
      const t = set(l.tree, op.path);
      if (!t) { out.push(adv("unknown_split", { path: op.path })); return false; }
      if (r.v === 0 || r.v === 1) out.push(adv("zero_size", { ratio: r.v, path: op.path }));
      l.tree = t; return true;
    }
    default: out.push(adv("bad_op", { op: (op as { op?: unknown }).op })); return false;
  }
}
function restoreWin(l: WLayout, w: Win) {
  const r = w.restore; delete w.restore;
  if (w.mode === "maximized" && w.dock) { w.mode = "docked"; return; }
  if (r?.mode === "floating") { w.mode = "floating"; w.rect = r.rect ?? w.rect ?? defaultRect(0); delete w.dock; topZ(l, w); return; }
  if (r?.dock && leafIds(l.tree).includes(r.dock.leaf)) { w.dock = { leaf: r.dock.leaf }; w.mode = "docked"; return; }
  w.dock = { leaf: firstLeaf(l.tree) }; w.mode = "docked";
}

/** Applies one operation to the state. Total: never throws; an invalid operation returns the same state and advisories. */
export function apply(state: WState, op: Op, ctx: Ctx = {}): Result {
  const out: Advisory[] = [];
  try {
    const next = clone(state.layout);
    const changedFlag = run(next, op, ctx, out);
    if (!changedFlag) return { state, advisories: out, applied: false };
    normalize(next, ctx, out);
    if (same(next, state.layout)) return { state, advisories: out, applied: false };
    const past = [...state.past, { op: (op as Op).op, layout: state.layout }];
    while (past.length > state.limit) past.shift();
    return { state: { layout: next, past, future: [], limit: state.limit }, advisories: out, applied: true };
  } catch (e) {
    return { state, advisories: [adv("bad_op", { error: String(e) })], applied: false };
  }
}
export function undo(state: WState): Result {
  const e = state.past[state.past.length - 1];
  if (!e) return { state, advisories: [adv("nothing_to_undo")], applied: false };
  return { state: { ...state, layout: e.layout, past: state.past.slice(0, -1), future: [...state.future, { op: e.op, layout: state.layout }] }, advisories: [], applied: true };
}
export function redo(state: WState): Result {
  const e = state.future[state.future.length - 1];
  if (!e) return { state, advisories: [adv("nothing_to_redo")], applied: false };
  return { state: { ...state, layout: e.layout, future: state.future.slice(0, -1), past: [...state.past, { op: e.op, layout: state.layout }] }, advisories: [], applied: true };
}

// ---------- reading, checking, predicates ----------
/** Schema check of a layout: the list of violated invariants (empty = valid). Used by the generative tests (P1, P5, P6, P7). */
export function check(l: WLayout, opts: { allowEmptyLeaves?: boolean } = {}): string[] {
  const p: string[] = [];
  const ids = l.windows.map((w) => w.id);
  if (new Set(ids).size !== ids.length) p.push("duplicate window id");
  const leaves = leafIds(l.tree);
  if (new Set(leaves).size !== leaves.length) p.push("duplicate leaf id");
  (function walk(t: Tree) { if (!isLeaf(t)) { if (!(t.ratio >= 0 && t.ratio <= 1)) p.push("ratio out of range"); if (t.split !== "h" && t.split !== "v") p.push("bad split"); walk(t.a); walk(t.b); } })(l.tree);
  for (const k of Object.keys(l.leaves)) if (!leaves.includes(k)) p.push("leaf info without tree leaf " + k);
  for (const k of leaves) if (!l.leaves[k]) p.push("tree leaf without info " + k);
  for (const w of l.windows) {
    if (!MODES.includes(w.mode)) p.push("bad mode " + w.id);
    if (w.dock) { if (!leaves.includes(w.dock.leaf)) p.push("dock.leaf missing " + w.id); else if (!l.leaves[w.dock.leaf].windows.includes(w.id)) p.push("dock/leaf mismatch " + w.id); }
    else if (isDockedMode(w.mode)) p.push("docked without dock " + w.id);
    if ((w.mode === "floating" || w.mode === "minimized") && w.dock) p.push("floating with dock " + w.id);
    if (w.mode === "floating" && !w.rect) p.push("floating without rect " + w.id);
  }
  for (const k of leaves) {
    const li = l.leaves[k]; if (!li) continue;
    for (const id of li.windows) { const w = l.windows.find((x) => x.id === id); if (!w || w.dock?.leaf !== k) p.push("leaf lists foreign window " + id); }
    if (li.active !== null && !li.windows.includes(li.active)) p.push("active not in leaf " + k);
    if (li.windows.length === 0 && li.born !== "explicit" && leaves.length > 1 && !opts.allowEmptyLeaves) p.push("empty leaf not explicit " + k);
  }
  const fl = l.windows.filter((w) => w.mode === "floating").map((w) => w.z).sort((a, b) => a - b);
  if (fl.some((z, i) => z !== i)) p.push("z not a permutation");
  if (l.windows.filter((w) => w.mode === "maximized").length > 1) p.push("two maximized");
  if (l.windows.length === 0 ? l.focus !== null : !ids.includes(l.focus as string)) p.push("focus invalid");
  for (const h of l.shelf.handles) if (!ids.includes(h)) p.push("handle without window " + h);
  // R101 D1 (P2 of PULPIT §14): exactly one background window, and it is not in windows[].
  if (allWindows(l).filter((w) => w.mode === "background").length !== 1) p.push("background windows != 1");
  if (ids.includes(desktopOf(l).id)) p.push("desktop id reused by a window");
  return p;
}
export type Reach = "visible" | "handle" | "switcher";
/** How a person gets to each window (P2): on screen, by a handle on the shelf, or through the switcher (every window is listed there). */
export function reachability(l: WLayout): Record<string, Reach> {
  const max = l.windows.find((w) => w.mode === "maximized");
  const r: Record<string, Reach> = {};
  for (const w of l.windows) {
    const onScreen = w.mode === "floating" || w.mode === "maximized" || (!max && w.dock && l.leaves[w.dock.leaf]?.active === w.id);
    r[w.id] = onScreen ? "visible" : l.shelf.handles.includes(w.id) ? "handle" : "switcher";
  }
  return r;
}
/** P8: the "ordinary chat app": exactly one maximized window and it is a chat; any conversation list is collapsed. */
export function isPlainChatApp(l: WLayout): boolean {
  const max = l.windows.filter((w) => w.mode === "maximized");
  return max.length === 1 && max[0].kind === "chat" && l.windows.filter((w) => w.kind === "conversation_list").every((w) => w.collapsed);
}
export interface Area { w: number; h: number }
/** Size in pixels of each leaf for an area (a ratio of 0 or 1 gives a leaf of 0 px, which is shown, not refused). */
/** W4: the split at a path ("" = root), or null. */
export function splitAt(t: Tree, path: string): Split | null {
  let n: Tree = t;
  for (const c of path) { if (isLeaf(n)) return null; n = c === "a" ? n.a : c === "b" ? n.b : (null as unknown as Tree); if (!n) return null; }
  return isLeaf(n) ? null : n;
}
export function leafSizes(t: Tree, area: Area): Record<string, Area> {
  if (isLeaf(t)) return { [t.leaf]: { ...area } };
  const h = t.split === "h";
  return { ...leafSizes(t.a, h ? { w: area.w * t.ratio, h: area.h } : { w: area.w, h: area.h * t.ratio }), ...leafSizes(t.b, h ? { w: area.w * (1 - t.ratio), h: area.h } : { w: area.w, h: area.h * (1 - t.ratio) }) };
}
/** P11: moves a layout to another area: floating rects are clamped into it (never dropped), narrow leaves raise `dock_narrow`. Pure. */
export function fitToArea(l: WLayout, area: Area, ctx: Ctx = {}): { layout: WLayout; advisories: Advisory[] } {
  const out: Advisory[] = []; const n = clone(l); const min = ctx.minUsefulWidth ?? 160;
  for (const w of n.windows) if (w.rect) {
    const px = w.rect.unit === "px"; const W = px ? area.w : 1, H = px ? area.h : 1;
    const wd = Math.min(Math.max(w.rect.w, 0), W), ht = Math.min(Math.max(w.rect.h, 0), H);
    w.rect = { ...w.rect, w: wd, h: ht, x: Math.min(Math.max(w.rect.x, 0), W - wd), y: Math.min(Math.max(w.rect.y, 0), H - ht) };
  }
  const sizes = leafSizes(n.tree, area);
  for (const [k, s] of Object.entries(sizes)) if (n.leaves[k]?.windows.length && s.w < min) out.push(adv("dock_narrow", { leaf: k, width: s.w, min }));
  return { layout: n, advisories: out };
}
