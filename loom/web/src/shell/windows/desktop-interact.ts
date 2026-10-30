// R101 D6 (PULPIT_R101_2026-10-28 §13 "Interakcje", §14 row D6): the interactions of the desktop as data, pure (no React, no DOM, no
// kernel). What a drop does is a list of rules in desktop.json (interactions.drop_rules): a source (an icon by address prefix, or a
// dragged window) over a target (an icon, a window by kind or by the coupling parameter it reads, the empty desktop) → a step
// (set_param through the W6 coupling, trash, close, move). The page turns the stack of elements under the pointer into `Hit`s and this
// module picks the rule; nothing here is a closed list: an unknown `do` is carried to the page, which says it cannot do it. How a drag
// starts (gestures.json targets.icon.drag_start) and what the keys do (keymap.json desktop) are read here too, with their advisories.
// P3 (§13): every step has an inverse; `applyStep` on a small model of the state returns the inverse step, and a generative test checks
// that any sequence undone in reverse order comes back to the first state and redone reaches the last one.
import gesturesFile from "../data/gestures.json";
import keymapFile from "../data/keymap.json";
import { DESKTOP_X } from "./desktop-icons";

export type Text = { pl?: string; en?: string };
export type SourceMatch = string | string[] | { window?: boolean | string | string[]; icon?: string | string[] };
export interface TargetMatch { icon?: string | string[]; window_kind?: string | string[]; window_param?: string; desktop?: boolean; [k: string]: unknown }
export interface DropRule { id?: string; source?: SourceMatch; target?: TargetMatch; do?: Record<string, unknown>; enabled?: boolean | Record<string, boolean>; [k: string]: unknown }
export interface InteractionsData { drop_rules?: DropRule[]; receipt_ms?: number | null; icon_menu?: string[]; [k: string]: unknown }
export const INTERACTIONS: InteractionsData = ((DESKTOP_X as unknown as { interactions?: InteractionsData }).interactions) ?? {};

/** What a drag carries: a desktop icon (its address; `key` tells two icons of one address apart) or a floating window. */
export type DropSource = { kind: "icon"; address: string; key?: string; label?: string } | { kind: "window"; id: string; windowKind: string; label?: string };
/** One thing under the pointer, from the top down. `params`: the coupling parameters the window's kind reads (coupling.json kind_params). */
export type Hit =
  | { kind: "icon"; address: string; key?: string; label?: string }
  | { kind: "window"; id: string; windowKind: string; mode?: string; params?: readonly string[]; label?: string }
  | { kind: "desktop" }
  | { kind: "other"; what?: string };

export interface Advisory { code: string; vars?: Record<string, string> }
export type Resolved =
  | { kind: "step"; rule: DropRule; ruleId: string; hit: Hit; do: Record<string, unknown>; matchedPrefix: string }
  | { kind: "none"; advisory: Advisory | null; hit: Hit | null };

const list = (v: string | string[] | undefined): string[] | undefined => (v === undefined ? undefined : Array.isArray(v) ? v.filter((x) => typeof x === "string") : typeof v === "string" ? [v] : []);

/** The prefix of `source` that matches (the longest), or null. "" matches every icon. */
export function matchSource(source: SourceMatch | undefined, s: DropSource): string | null {
  if (s.kind === "icon") {
    const pres = typeof source === "string" || Array.isArray(source) ? list(source) : source && typeof source === "object" ? list(source.icon) : undefined;
    if (!pres) return null;
    let best: string | null = null;
    for (const p of pres) if (s.address.startsWith(p) && (best === null || p.length > best.length)) best = p;
    return best;
  }
  if (!source || typeof source !== "object" || Array.isArray(source)) return null;
  const w = (source as { window?: boolean | string | string[] }).window;
  if (w === true) return "";
  const kinds = list(typeof w === "string" || Array.isArray(w) ? w : undefined);
  return kinds && kinds.includes(s.windowKind) ? "" : null;
}

/** Whether a hit is what the rule's target names. A hit that is the source itself never matches (an icon on itself, a window on itself). */
export function matchTarget(target: TargetMatch | undefined, hit: Hit, s: DropSource): boolean {
  if (!target || typeof target !== "object") return false;
  if (hit.kind === "icon") {
    if (s.kind === "icon" && (s.key ?? s.address) === (hit.key ?? hit.address)) return false;
    const pres = list(target.icon); if (!pres) return false;
    return pres.some((p) => hit.address.startsWith(p));
  }
  if (hit.kind === "window") {
    if (s.kind === "window" && s.id === hit.id) return false;
    const kinds = list(target.window_kind);
    if (kinds && (kinds.includes("*") || kinds.includes(hit.windowKind))) return true;
    if (typeof target.window_param === "string" && (hit.params ?? []).includes(target.window_param)) return true;
    return false;
  }
  if (hit.kind === "desktop") return target.desktop === true;
  return false;
}

/** Whether the rule is on for this device class (enabled: bool or {<class>: bool}; unset = on). */
export function ruleEnabled(rule: DropRule, cls: string): boolean {
  const e = rule.enabled;
  if (e === undefined || e === null) return true;
  if (typeof e === "boolean") return e;
  if (typeof e === "object") { const v = (e as Record<string, unknown>)[cls]; return v === undefined ? true : v === true; }
  return true;
}

/** The first rule (in data order) that matches the topmost hit it names. Hits from the top down; the first hit with any matching rule
 *  decides (so a window over the desktop takes the drop, the desktop under it does not). A rule that matches but is off for the class
 *  gives `none` with drop_rule_off; no rule at all gives drop_no_rule (null when nothing was under the pointer but the desktop itself and
 *  the source is a window - the page then does nothing, quietly: that is just the end of a window move). */
export function resolveDrop(source: DropSource, hits: readonly Hit[], cls: string, rules: readonly DropRule[] | undefined = INTERACTIONS.drop_rules): Resolved {
  const rs = Array.isArray(rules) ? rules.filter((r) => r && typeof r === "object") : [];
  let off: { rule: DropRule; hit: Hit } | null = null;
  for (const hit of hits) {
    if (hit.kind === "other") continue;
    for (const rule of rs) {
      const pre = matchSource(rule.source, source);
      if (pre === null || !matchTarget(rule.target, hit, source)) continue;
      if (!ruleEnabled(rule, cls)) { off = off ?? { rule, hit }; continue; }
      return { kind: "step", rule, ruleId: typeof rule.id === "string" ? rule.id : `rule_${rs.indexOf(rule)}`, hit, do: rule.do && typeof rule.do === "object" ? rule.do : {}, matchedPrefix: pre };
    }
  }
  if (off) return { kind: "none", advisory: { code: "drop_rule_off", vars: { rule: typeof off.rule.id === "string" ? off.rule.id : "?" } }, hit: off.hit };
  const first = hits.find((h) => h.kind !== "other") ?? null;
  if (!first || (first.kind === "desktop" && source.kind === "window")) return { kind: "none", advisory: null, hit: first };
  return { kind: "none", advisory: { code: "drop_no_rule" }, hit: first };
}

/** $address = the whole address of the icon (or the window id), $rest = what follows the matched source prefix. */
export function fillValue(v: unknown, source: DropSource, matchedPrefix: string): unknown {
  if (typeof v !== "string") return v;
  const address = source.kind === "icon" ? source.address : source.id;
  return v.replace("$address", address).replace("$rest", address.slice(matchedPrefix.length));
}

// ---- how a drag starts (gestures.json targets.icon.drag_start) -------------------------------------------------------------------------
export const DRAG_STARTS_KNOWN = ["move", "long_press", "handle"] as const;
export type DragStart = (typeof DRAG_STARTS_KNOWN)[number];
export interface IconGestures { tap?: string; long_press?: string; drag_start?: unknown; [k: string]: unknown }
export const GESTURES_X = gesturesFile as unknown as { long_press_ms?: number; double_tap_ms?: number; targets?: Record<string, IconGestures> };

/** The drag start of a device class: a word, or {<class>: word}; a class without its own word takes `desktop`'s, then "move". */
export function dragStartOf(cls: string, data: { targets?: Record<string, IconGestures> } = GESTURES_X): { mode: DragStart; written: string; advisory: Advisory | null } {
  const raw = data.targets?.icon?.drag_start;
  const word = typeof raw === "string" ? raw : raw && typeof raw === "object" ? ((raw as Record<string, unknown>)[cls] ?? (raw as Record<string, unknown>).desktop ?? (raw as Record<string, unknown>)["*"]) : undefined;
  const written = typeof word === "string" ? word : "move";
  if ((DRAG_STARTS_KNOWN as readonly string[]).includes(written)) return { mode: written as DragStart, written, advisory: null };
  return { mode: "move", written, advisory: { code: "drag_start_unknown", vars: { mode: written, known: DRAG_STARTS_KNOWN.join(", ") } } };
}

/** The CSS touch-action of an icon for a drag start: the icon takes the touch only when a move itself drags (move); otherwise the browser scrolls. */
export const touchActionOf = (mode: DragStart): string => (mode === "move" ? "none" : "pan-x pan-y");

// ---- the keys of the desktop (keymap.json desktop) ---------------------------------------------------------------------------------------
export const DESKTOP_KEY_OPS = ["next", "prev", "up", "down", "first", "last", "open", "trash", "menu", "menu_close"] as const;
export type DesktopKeyOp = (typeof DESKTOP_KEY_OPS)[number];
export const KEYMAP_X = keymapFile as unknown as { desktop?: Record<string, string>; text_fields_keep_keys?: boolean };

/** The operation a key combination names on the desktop, or null; an unknown operation is returned as `unknown` for the advisory. */
export function desktopKeyOf(combo: string, keymap: { desktop?: Record<string, string> } = KEYMAP_X): { op: DesktopKeyOp } | { unknown: string } | null {
  const op = keymap.desktop?.[combo];
  if (typeof op !== "string") return null;
  return (DESKTOP_KEY_OPS as readonly string[]).includes(op) ? { op: op as DesktopKeyOp } : { unknown: op };
}

/** The index the focus moves to: next/prev by one, up/down by a row (`columns`), first/last; clamped, never off the list (count 0 → -1). */
export function moveIndex(i: number, op: DesktopKeyOp, columns: number, count: number): number {
  if (count <= 0) return -1;
  const c = Math.max(1, Math.floor(columns) || 1);
  const at = Math.min(Math.max(0, Math.floor(i) || 0), count - 1);
  const want = op === "next" ? at + 1 : op === "prev" ? at - 1 : op === "down" ? at + c : op === "up" ? at - c : op === "first" ? 0 : op === "last" ? count - 1 : at;
  if (op === "down" && want >= count) return at + c >= count && at < count - 1 && Math.floor(at / c) < Math.floor((count - 1) / c) ? count - 1 : at;
  if (op === "up" && want < 0) return at;
  return Math.min(Math.max(0, want), count - 1);
}

// ---- the icon menu (interactions.icon_menu) ---------------------------------------------------------------------------------------------
export const ICON_MENU_KNOWN = ["open", "trash", "close"] as const;
/** The menu items for an icon: the known ones in data order, trash only when the address can go to the trash; unknown ids reported. */
export function iconMenuItems(items: readonly string[] | undefined, canTrash: boolean, canOpen: boolean): { items: string[]; unknown: string[] } {
  const out: string[] = []; const unknown: string[] = [];
  for (const it of items ?? ICON_MENU_KNOWN) {
    if (!(ICON_MENU_KNOWN as readonly string[]).includes(it)) { unknown.push(it); continue; }
    if (it === "trash" && !canTrash) continue;
    if (it === "open" && !canOpen) continue;
    out.push(it);
  }
  return { items: out, unknown };
}

// ---- P3: the model of the state and the inverse of every step ---------------------------------------------------------------------------
/** The state an interaction touches: the arrangement value, which addresses are in the trash, each window's parameters, which windows are
 *  open. A small model of what the page and the kernel hold; the page executes the same steps against the real ones. */
export interface IState { arrangement: unknown; trashed: string[]; params: Record<string, Record<string, unknown>>; open: string[] }
export type IStep =
  | { op: "arrangement"; value: unknown }
  | { op: "trash"; address: string }
  | { op: "untrash"; address: string }
  | { op: "set_param"; window: string; param: string; value: unknown }
  | { op: "close"; window: string }
  | { op: "reopen"; window: string };

const clone = <T,>(v: T): T => (v === undefined ? v : JSON.parse(JSON.stringify(v)) as T);

/** Applies one step and returns the inverse step (what brings the state back). A step that changes nothing (trashing what is in the
 *  trash, closing a closed window) has the identity as inverse: undoing it changes nothing either. */
export function applyStep(s: IState, step: IStep): { state: IState; inverse: IStep } {
  const st: IState = { arrangement: clone(s.arrangement), trashed: [...s.trashed], params: clone(s.params), open: [...s.open] };
  switch (step.op) {
    case "arrangement": { const before = st.arrangement; st.arrangement = clone(step.value); return { state: st, inverse: { op: "arrangement", value: before } }; }
    case "trash": if (st.trashed.includes(step.address)) return { state: st, inverse: { op: "trash", address: step.address } }; st.trashed.push(step.address); return { state: st, inverse: { op: "untrash", address: step.address } };
    case "untrash": if (!st.trashed.includes(step.address)) return { state: st, inverse: { op: "untrash", address: step.address } }; st.trashed = st.trashed.filter((a) => a !== step.address); return { state: st, inverse: { op: "trash", address: step.address } };
    case "set_param": {
      // an undefined value removes the parameter (and an emptied window entry), so that "set back to nothing" is a true inverse
      const w = { ...(st.params[step.window] ?? {}) }; const before = w[step.param];
      if (step.value === undefined) delete w[step.param]; else w[step.param] = clone(step.value);
      if (Object.keys(w).length) st.params[step.window] = w; else delete st.params[step.window];
      return { state: st, inverse: { op: "set_param", window: step.window, param: step.param, value: before } };
    }
    case "close": if (!st.open.includes(step.window)) return { state: st, inverse: { op: "close", window: step.window } }; st.open = st.open.filter((w) => w !== step.window); return { state: st, inverse: { op: "reopen", window: step.window } };
    case "reopen": if (st.open.includes(step.window)) return { state: st, inverse: { op: "reopen", window: step.window } }; st.open.push(step.window); return { state: st, inverse: { op: "close", window: step.window } };
  }
  return { state: st, inverse: step };
}

/** The step a resolved drop asks for, in the model's words (null: a `do` this page does not know, or a move - the D5 arrangement does it). */
export function stepOf(r: Extract<Resolved, { kind: "step" }>, source: DropSource): IStep | { unknown: string } | null {
  const d = r.do;
  if (d.set_param && typeof d.set_param === "object" && r.hit.kind === "window") {
    const sp = d.set_param as { param?: unknown; value?: unknown };
    if (typeof sp.param !== "string") return { unknown: "set_param" };
    return { op: "set_param", window: r.hit.id, param: sp.param, value: fillValue(sp.value ?? "$address", source, r.matchedPrefix) };
  }
  if (d.trash === true && source.kind === "icon") return { op: "trash", address: source.address };
  if (d.close === true && source.kind === "window") return { op: "close", window: source.id };
  if (d.move === true) return null;
  const keys = Object.keys(d);
  return { unknown: keys.length ? keys.join(",") : "(empty)" };
}

export const WINDOW_DRAGGED = "loom:window-dragged";
export interface WindowDragged { id: string; x: number; y: number }
