// W7 (OKNA §6.1-6.3): the arrangement operations a client step of the kernel carries out (window.maximize / restore / minimize / dock /
// float / tab / move / resize), the keys of keymap.json and the gestures of gestures.json. Pure functions over the WState of model.ts;
// App.tsx calls runArrangeStep for a sentence („zmaksymalizuj ustawienia”, „zadokuj graf po lewej”), useArrange for a key. Each step is
// ONE journal entry (layout.undo takes it back), also when the window had to be opened first. A word of a side, a direction or a size
// is data (layouts.json window_sides, window_move_step_px, window_resize_factor); a word not listed is answered, nothing is guessed.
import layoutsFile from "../data/layouts.json";
import keymapFile from "../data/keymap.json";
import gesturesFile from "../data/gestures.json";
import { apply, type Ctx, type Rect, type Tree, type WLayout, type WState, type Win } from "./model";
import { arrangeWindow, moveFloating, ARRANGE } from "./arrange";
import { openWindow, resolveWord, visibleId, WINDOW_DATA, type WindowData, type WindowReceipt, type WindowTarget } from "./ops";
import { WINDOW_CTX, overlays } from "./session";

export interface StepData {
  window_sides?: Record<string, string>;
  window_move_step_px?: number;
  window_resize_factor?: Record<string, number | { w?: number; h?: number }>;
}
export const STEP_DATA = layoutsFile as StepData;
export interface KeymapData { keys?: Record<string, string>; text_fields_keep_keys?: boolean }
export const KEYMAP: KeymapData = keymapFile as KeymapData;
export interface GestureData {
  long_press_ms?: number; double_tap_ms?: number;
  /** a target's gestures: a word per gesture; R101 D6 adds non-string entries (icon.drag_start per class), read by desktop-interact.ts */
  targets?: Record<string, Record<string, unknown>>;
  chain_cycle?: string[]; focus_order?: string[]; touch_target_px?: number;
}
export const GESTURES: GestureData = gesturesFile as GestureData;

const fold = (t: string) => t.normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/ł/g, "l").replace(/Ł/g, "L").toLowerCase().trim();

/** A key event's combination in the form of keymap.json: ctrl, alt, shift in this order, then the key in lower case. */
export function comboOf(e: { ctrlKey?: boolean; metaKey?: boolean; altKey?: boolean; shiftKey?: boolean; key: string }): string {
  return [e.ctrlKey || e.metaKey ? "ctrl" : "", e.altKey ? "alt" : "", e.shiftKey ? "shift" : "", e.key.toLowerCase()].filter(Boolean).join("+");
}
/** The operation a key combination names (keymap.json, or the old window_arrange.keymap of a layouts file that still has one). */
export function keyOperation(combo: string, keymap: KeymapData = KEYMAP, legacy: Record<string, string> | undefined = ARRANGE.keymap): string | null {
  return keymap.keys?.[combo] ?? legacy?.[combo] ?? null;
}
/** A word of a side or direction to left/right/top/bottom (data), or null. */
export function sideOf(word: string, data: StepData = STEP_DATA): string | null {
  const w = fold(word);
  return data.window_sides?.[w] ?? (["left", "right", "top", "bottom"].includes(w) ? w : null);
}

/** Replaces the steps between `before` and `after` with ONE journal entry (a step that first opens a window and then arranges it). */
export function oneEntry(before: WState, after: WState, op: string): WState {
  if (after.layout === before.layout) return before;
  const past = [...before.past, { op, layout: before.layout }];
  while (past.length > before.limit) past.shift();
  return { layout: after.layout, past, future: [], limit: before.limit };
}

const findTarget = (l: WLayout, t: WindowTarget): Win | undefined =>
  "chat" in t ? l.windows.find((w) => w.kind === "chat" && typeof w.params?.panel !== "string") : (overlays(l).find((w) => w.params.panel === t.panel) ?? l.windows.find((w) => w.params?.panel === t.panel));

/** The window a step names: the word's window (opened first when it is a panel not open yet), or the focused / front one when the word is empty. */
export function targetWindow(s: WState, word: string, knownPanel: (id: string) => boolean, deviceClass: string, ctx: Ctx = WINDOW_CTX, data: WindowData = WINDOW_DATA): { state: WState; id: string | null; opened: boolean } {
  if (!word.trim()) {
    const id = s.layout.focus && s.layout.windows.some((w) => w.id === s.layout.focus) ? s.layout.focus : visibleId(s.layout);
    return { state: s, id, opened: false };
  }
  const t = resolveWord(word, knownPanel, data);
  if (!t) return { state: s, id: null, opened: false };
  const w = findTarget(s.layout, t);
  if (w) return { state: s, id: w.id, opened: false };
  if ("chat" in t) return { state: s, id: null, opened: false };
  const r = openWindow(s, t, "", deviceClass, ctx, data);
  return { state: r.state, id: r.receipt.state === "ok" ? r.receipt.windows[0] ?? null : null, opened: r.receipt.state === "ok" };
}

const pxRect = (r: Rect | undefined, area: { w: number; h: number }): { x: number; y: number; w: number; h: number } => {
  const f = r?.unit !== "px";
  const x = r?.x ?? 0.1, y = r?.y ?? 0.1, w = r?.w ?? 0.5, h = r?.h ?? 0.5;
  return f ? { x: x * area.w, y: y * area.h, w: w * area.w, h: h * area.h } : { x, y, w, h };
};
const backTo = (r: Rect | undefined, px: { x: number; y: number; w: number; h: number }, area: { w: number; h: number }): Partial<Rect> =>
  r?.unit === "px" ? { ...px, unit: "px" } : { x: px.x / area.w, y: px.y / area.h, w: px.w / area.w, h: px.h / area.h, unit: "fraction" };

export interface ArrangeReceipt extends WindowReceipt { how?: string }

/** One client step window.<op> of §6.1 that arranges a window. `area` = the screen in CSS px (move/resize in px on a fraction rect). */
export function runArrangeStep(s: WState, action: string, args: Record<string, unknown>, knownPanel: (id: string) => boolean, deviceClass: string,
  area: { w: number; h: number } = { w: 1280, h: 800 }, ctx: Ctx = WINDOW_CTX, data: WindowData & StepData = { ...WINDOW_DATA, ...STEP_DATA }): { state: WState; receipt: ArrangeReceipt } {
  const text = (k: string) => (typeof args[k] === "string" ? (args[k] as string) : typeof args[k] === "number" ? String(args[k]) : "");
  const op = action.replace(/^window\./, "");
  const word = text("target") || text("kind");
  const none = (w = word): { state: WState; receipt: ArrangeReceipt } => ({ state: s, receipt: { op, state: "none", windows: [], word: w } });
  const t = targetWindow(s, word, knownPanel, deviceClass, ctx, data);
  if (!t.id) return none();
  const id = t.id;
  let state = t.state;
  let how = op;
  const w0 = () => state.layout.windows.find((x) => x.id === id);
  if (op === "maximize" || op === "float" || op === "tab") state = arrangeWindow(state, id, op);
  else if (op === "restore") state = apply(state, { op: "restore", id }, ctx).state;
  else if (op === "minimize") state = apply(state, { op: "minimize", id }, ctx).state;
  else if (op === "dock") {
    const side = sideOf(text("side") || "right", data);
    if (!side) return none(text("side"));
    how = side;
    state = arrangeWindow(state, id, side);
  } else if (op === "move" || op === "resize") {
    if (w0()?.mode !== "floating") state = arrangeWindow(state, id, "float");
    const w = w0();
    if (!w || w.mode !== "floating") return none();
    const px = pxRect(w.rect, area);
    if (op === "move") {
      const dir = sideOf(text("direction") || "right", data);
      if (!dir) return none(text("direction"));
      const n = Number(text("amount")) || (data.window_move_step_px ?? 80);
      how = dir;
      if (dir === "left") px.x -= n; else if (dir === "right") px.x += n; else if (dir === "top") px.y -= n; else px.y += n;
    } else {
      const wN = Number(text("w")), hN = Number(text("h"));
      if (wN > 0 || hN > 0) { if (wN > 0) px.w = wN; if (hN > 0) px.h = hN; how = `${Math.round(px.w)}×${Math.round(px.h)}`; }
      else {
        const word2 = fold(text("how") || "powieksz");
        const f = data.window_resize_factor?.[word2];
        if (f === undefined) return none(text("how"));
        const fw = typeof f === "number" ? f : f.w ?? 1, fh = typeof f === "number" ? f : f.h ?? 1;
        px.w *= fw; px.h *= fh; how = text("how");
      }
    }
    state = moveFloating(state, id, backTo(w.rect, px, area), ctx);
  } else return none(action);
  const done = oneEntry(s, state, op);
  return { state: done, receipt: { op, state: done === s ? "noop" : "ok", windows: [id], how } };
}

/** §6.4: the order of windows for the switcher keys and a reader of the screen (gestures.json focus_order): the dock tree in reading
 *  order, the floating ones by z rising, then the shelf; any order of these words, a word not known is skipped. */
export function focusOrder(l: WLayout, order: string[] = GESTURES.focus_order ?? ["dock", "floating", "shelf"]): string[] {
  const reading: string[] = [];
  const walk = (t: Tree | undefined): void => { if (!t) return; if ("leaf" in t) reading.push(...(l.leaves[t.leaf]?.windows ?? [])); else { walk(t.a); walk(t.b); } };
  walk(l.tree);
  const groups: Record<string, string[]> = {
    dock: reading.filter((id) => l.windows.some((w) => w.id === id)),
    floating: l.windows.filter((w) => w.mode === "floating").sort((a, b) => a.z - b.z).map((w) => w.id),
    shelf: [...(l.shelf?.handles ?? [])],
  };
  const out: string[] = [];
  for (const g of order) for (const id of groups[g] ?? []) if (!out.includes(id)) out.push(id);
  for (const w of l.windows) if (!out.includes(w.id) && w.mode !== "minimized") out.push(w.id);
  return out;
}

/** The next mode of the chain tap (gestures.json chain_cycle): the mode after the current one, skipping `linked` when no source exists. */
export function nextChainMode(current: string, hasSource: boolean, cycle: string[] = GESTURES.chain_cycle ?? ["shared", "independent", "linked"]): string {
  if (!cycle.length) return current;
  let i = cycle.indexOf(current);
  for (let k = 0; k < cycle.length; k++) {
    i = (i + 1) % cycle.length;
    if (cycle[i] !== "linked" || hasSource) return cycle[i];
  }
  return current;
}
