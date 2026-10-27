// W2 (OKNA §4.1, §6.1): the window operations a client step carries out (window.open / close / focus / next / prev) and the switcher's
// list. Pure functions over the WState of model.ts; App.tsx calls them for a client step of the kernel and for the sheet "Okna".
// On a phone the windows are a stack of maximized windows: "beside" is the next window of the stack, reachable from the switcher; the
// placement a screen uses for an asked one is a preset in data (layouts.json → window_placements), not a rule of this code.
import layoutsFile from "../data/layouts.json";
import { apply, type Ctx, type Placement, type WLayout, type WState, type Win } from "./model";
import { WINDOW_CTX, overlays, panelKind, visibleOverlay } from "./session";

export interface WindowData {
  window_words?: Record<string, string>;
  window_placements?: Record<string, Record<string, string>>;
  window_back?: string;
  window_restore_last?: boolean;
  window_restore_front?: string;
  panel_kinds?: Record<string, string>;
  panel_kinds_by_layout?: Record<string, Record<string, string>>;
  panel_placement?: string;
}
export const WINDOW_DATA = layoutsFile as WindowData;
/** What a window is for the operations: the own window of a kind (`chat`), or a panel of the app opened over the layout. */
export type WindowTarget = { chat: true } | { panel: string };
export interface WindowReceipt {
  op: string;
  /** ok: done; none: the target names no window; noop: nothing to do. */
  state: "ok" | "none" | "noop";
  /** Ids of the windows the step touched (opened, closed, brought forward). */
  windows: string[];
  word?: string;
  placementAsked?: string;
  placementUsed?: string;
  /** Pinned windows a close left open (§6.2: the receipt names them). */
  kept?: string[];
}

const fold = (t: string) => t.normalize("NFD").replace(/[̀-ͯ]/g, "").replace(/ł/g, "l").replace(/Ł/g, "L").toLowerCase();

/** A word of a sentence ("graf", "czatem", "the settings") to a window target, by the longest stem of the data; an id of a panel or the
 *  word `chat` is accepted as is. Null = the word names no window. */
export function resolveWord(word: string, knownPanel: (id: string) => boolean, data: WindowData = WINDOW_DATA): WindowTarget | null {
  const words = data.window_words ?? {};
  const stems = Object.keys(words).sort((a, b) => b.length - a.length);
  const raw = fold(word).trim();
  const of = (value: string): WindowTarget | null => (value === "chat" ? { chat: true } : knownPanel(value) ? { panel: value } : null);
  const exact = of(raw);
  if (exact) return exact;
  for (const w of raw.split(/[^a-z0-9_-]+/).filter(Boolean)) {
    const stem = stems.find((s) => w.startsWith(fold(s)));
    if (stem) { const t = of(words[stem]); if (t) return t; }
  }
  return null;
}

/** The windows the switcher lists: the own windows of the layout that are not collapsed handles, then the windows opened over it. */
export function switcherEntries(l: WLayout): Win[] {
  return [...l.windows.filter((w) => typeof w.params?.panel !== "string" && !w.collapsed), ...overlays(l)];
}
/** The window shown in front (the maximized overlay, else the maximized own window), or null (a grid). */
export function visibleId(l: WLayout): string | null {
  return visibleOverlay(l)?.id ?? l.windows.find((w) => w.mode === "maximized")?.id ?? null;
}
const findTarget = (l: WLayout, t: WindowTarget): Win | undefined =>
  "chat" in t ? l.windows.find((w) => w.kind === "chat" && typeof w.params?.panel !== "string") : overlays(l).find((w) => w.params.panel === t.panel);

function step(before: WState, after: WState, op: string): WState {
  if (after.layout === before.layout) return before;
  const past = [...before.past, { op, layout: before.layout }];
  while (past.length > before.limit) past.shift();
  return { layout: after.layout, past, future: [], limit: before.limit };
}

/** Brings a window to the front: on the stack of maximized windows this maximizes it (the others wait in the switcher). */
export function focusWindow(s: WState, id: string, ctx: Ctx = WINDOW_CTX): WState {
  const w = s.layout.windows.find((x) => x.id === id);
  if (!w) return s;
  if (w.mode === "maximized" && visibleId(s.layout) === id) return s;
  return step(s, apply(s, { op: "maximize", id }, ctx).state, "focus");
}

/** The placement used for an asked one on this screen (data: window_placements[deviceClass] then [any]; an unknown one is kept). */
export function placementFor(asked: string, deviceClass: string, data: WindowData = WINDOW_DATA): string {
  const p = data.window_placements ?? {};
  return p[deviceClass]?.[asked] ?? p.any?.[asked] ?? asked;
}

/** window.open: an open window of the target is brought forward; otherwise the panel opens as a new window of the stack, over the window
 *  in front (which waits in the switcher). */
export function openWindow(s: WState, target: WindowTarget, asked: string, deviceClass: string, ctx: Ctx = WINDOW_CTX, data: WindowData = WINDOW_DATA): { state: WState; receipt: WindowReceipt } {
  const placementUsed = placementFor(asked || data.panel_placement || "maximize", deviceClass, data);
  const base = { op: "open", placementAsked: asked || undefined, placementUsed };
  const existing = findTarget(s.layout, target);
  if (existing) return { state: focusWindow(s, existing.id, ctx), receipt: { ...base, state: "ok", windows: [existing.id] } };
  if ("chat" in target) return { state: s, receipt: { ...base, state: "none", windows: [] } };
  const over = visibleId(s.layout);
  const r = apply(s, { op: "open", kind: panelKind(target.panel, s.layout.id, data), placement: placementUsed as Placement, params: { panel: target.panel, over } }, ctx);
  if (!r.applied) return { state: s, receipt: { ...base, state: "none", windows: [] } };
  return { state: step(s, r.state, "open"), receipt: { ...base, state: "ok", windows: [r.state.layout.focus ?? ""] } };
}

/** window.close: the target window, or every window opened over the layout except the targets. Pinned windows stay (named in the receipt).
 *  When the window in front is closed, the window it covered comes forward, else the first own window of the layout. */
export function closeWindows(s: WState, spec: { target?: WindowTarget | null; except?: WindowTarget[] }, ctx: Ctx = WINDOW_CTX): { state: WState; receipt: WindowReceipt } {
  const l = s.layout;
  const keep = new Set((spec.except ?? []).map((t) => findTarget(l, t)?.id).filter((x): x is string => !!x));
  let victims: Win[];
  if (spec.except) victims = overlays(l).filter((w) => !keep.has(w.id));
  else if (spec.target) { const w = findTarget(l, spec.target); victims = w && typeof w.params?.panel === "string" ? [w] : []; }
  else { const v = visibleOverlay(l); victims = v ? [v] : []; }
  const kept = victims.filter((w) => w.pinned).map((w) => w.id);
  victims = victims.filter((w) => !w.pinned);
  if (!victims.length) return { state: s, receipt: { op: "close", state: spec.target && !findTarget(l, spec.target) ? "none" : "noop", windows: [], kept } };
  const front = visibleId(l);
  let state = s;
  for (const w of victims) state = apply(state, { op: "close", id: w.id }, ctx).state;
  if (front && victims.some((w) => w.id === front)) {
    const gone = new Set(victims.map((w) => w.id));
    const chain = [l.windows.find((w) => w.id === front)?.params?.over, ...[...keep]].filter((x): x is string => typeof x === "string" && !gone.has(x));
    const next = chain.map((id) => state.layout.windows.find((w) => w.id === id)).find(Boolean) ?? switcherEntries(state.layout)[0];
    if (next && next.mode !== "maximized") state = apply(state, { op: "maximize", id: next.id }, ctx).state;
  } else if (keep.size && spec.except) {
    const first = [...keep][0];
    if (visibleId(state.layout) !== first) state = apply(state, { op: "maximize", id: first }, ctx).state;
  }
  return { state: step(s, state, "close"), receipt: { op: "close", state: "ok", windows: victims.map((w) => w.id), kept } };
}

/** window.next / window.prev: the next window of the switcher's order comes forward. */
export function cycleWindow(s: WState, dir: 1 | -1, ctx: Ctx = WINDOW_CTX): { state: WState; receipt: WindowReceipt } {
  const list = switcherEntries(s.layout);
  const op = dir > 0 ? "next" : "prev";
  if (list.length < 2) return { state: s, receipt: { op, state: "noop", windows: [] } };
  const i = list.findIndex((w) => w.id === visibleId(s.layout));
  const w = list[(i + dir + list.length) % list.length];
  return { state: focusWindow(s, w.id, ctx), receipt: { op, state: "ok", windows: [w.id] } };
}

/** One client step of the kernel (action window.<op>, its arguments `with`) on the window state. */
export function runWindowStep(s: WState, action: string, args: Record<string, unknown>, knownPanel: (id: string) => boolean, deviceClass: string, ctx: Ctx = WINDOW_CTX, data: WindowData = WINDOW_DATA): { state: WState; receipt: WindowReceipt } {
  const text = (k: string) => (typeof args[k] === "string" ? (args[k] as string) : "");
  const op = action.replace(/^window\./, "");
  const word = text("target") || text("kind");
  const target = word ? resolveWord(word, knownPanel, data) : null;
  if (op === "open") {
    if (!target) return { state: s, receipt: { op, state: "none", windows: [], word } };
    return openWindow(s, target, text("placement"), deviceClass, ctx, data);
  }
  if (op === "close") {
    const except = text("except");
    if (except) {
      const targets = except.split(/\s*(?:,|\bi\b|\band\b|\boraz\b)\s*/).map((w) => resolveWord(w, knownPanel, data)).filter((t): t is WindowTarget => t !== null);
      return closeWindows(s, { except: targets }, ctx);
    }
    if (word && !target) return { state: s, receipt: { op, state: "none", windows: [], word } };
    return closeWindows(s, { target }, ctx);
  }
  if (op === "focus") {
    const w = target ? findTarget(s.layout, target) : undefined;
    if (!w) return { state: s, receipt: { op, state: "none", windows: [], word } };
    return { state: focusWindow(s, w.id, ctx), receipt: { op, state: "ok", windows: [w.id] } };
  }
  if (op === "next" || op === "prev") return cycleWindow(s, op === "next" ? 1 : -1, ctx);
  return { state: s, receipt: { op, state: "none", windows: [], word: action } };
}

/** W5: the window presets as the person set them. A settings field of the same name (window_back, window_restore_last,
 *  window_restore_front, window_placements; settings_fields.pack) overrides the preset of layouts.json; unset, empty or unreadable = the preset. */
export const WINDOW_SETTING_KEYS = ["window_back", "window_restore_last", "window_restore_front", "window_placements"] as const;
export function windowDataOf(config: Record<string, unknown>, data: WindowData = WINDOW_DATA): WindowData {
  const out: WindowData = { ...data };
  const set = out as Record<string, unknown>;
  for (const key of WINDOW_SETTING_KEYS) {
    let v = config[key];
    if (v === undefined || v === null || v === "") continue;
    if (key === "window_placements" && typeof v === "string") { try { v = JSON.parse(v); } catch { continue; } }
    if (key === "window_placements" && (!v || typeof v !== "object" || Array.isArray(v))) continue;
    if (key === "window_restore_last" && typeof v !== "boolean") v = !(v === "false" || v === 0 || v === "0");
    if ((key === "window_back" || key === "window_restore_front") && typeof v !== "string") continue;
    set[key] = v;
  }
  return out;
}
