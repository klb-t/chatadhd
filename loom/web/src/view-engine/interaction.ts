// Interaction as data (P28 step 2), the web side: the kernel resolves the effective bindings (loom_view_interaction); this module only interprets them.
// Pure functions: matching an input against bindings (held modifiers, lock states), the state of the locks with the documented fallback, what the
// hint overlay says, and the reducer that applies an action to the view state. No binding is built in: with no bindings nothing happens.
import type { InteractionActionInfo, InteractionBinding, ViewText } from "../api/view-model";

export type Mod = "shift" | "ctrl" | "alt" | "meta";
export type Lock = "caps" | "num" | "scroll";
export const MODS: Mod[] = ["shift", "ctrl", "alt", "meta"];
export const LOCKS: Lock[] = ["caps", "num", "scroll"];
export type Mods = Record<Mod, boolean>;
export type Locks = Record<Lock, boolean>;
export const noMods = (): Mods => ({ shift: false, ctrl: false, alt: false, meta: false });
export const noLocks = (): Locks => ({ caps: false, num: false, scroll: false });
export const LOCK_NAMES: Record<Lock, string> = { caps: "CapsLock", num: "NumLock", scroll: "ScrollLock" };

const list = (v: unknown): string[] => (Array.isArray(v) ? v.filter((x): x is string => typeof x === "string").map(x => x.toLowerCase()) : []);
export const text = (t: ViewText | string | undefined, language: string, fallback = ""): string =>
  typeof t === "string" ? t : (language === "pl" ? t?.pl ?? t?.en : t?.en ?? t?.pl) ?? fallback;

// ── matching ──
/** The binding for a key with the held modifiers and lock states; the last of several wins (a conflict is warned by the kernel, never blocked here). */
export function matchKey(bindings: InteractionBinding[], key: string, mods: Mods, locks: Locks, devices: string[] = ["keyboard"]): InteractionBinding | null {
  let found: InteractionBinding | null = null;
  const single = key.length === 1;
  for (const b of bindings) {
    if (b.status !== "active" || !devices.includes(b.device)) continue;
    const want = b.input.key;
    if (typeof want !== "string") continue;
    if (single ? want.toLowerCase() !== key.toLowerCase() : want !== key) continue;
    const needed = list(b.input.modifiers);
    // a printable key already carries shift in its character ("+"): shift is ignored there unless the binding names it
    const ok = MODS.every(m => (single && m === "shift" && !needed.includes("shift")) || mods[m] === needed.includes(m));
    if (!ok) continue;
    if (!list(b.input.locks).every(l => locks[l as Lock] === true)) continue;
    found = b;
  }
  return found;
}
export function matchGesture(bindings: InteractionBinding[], gesture: string, mods: Mods, devices: string[] = ["touch"]): InteractionBinding | null {
  let found: InteractionBinding | null = null;
  for (const b of bindings) {
    if (b.status !== "active" || !devices.includes(b.device) || b.input.gesture !== gesture) continue;
    const needed = list(b.input.modifiers);
    if (MODS.every(m => mods[m] === needed.includes(m))) found = b;
  }
  return found;
}

/** What the locks that are ON do now: lock-state bindings hold while the lock is on (a view, a zoom), the last of several wins for the view. */
export interface LockEffects { view: string | null; zoom: number; active: InteractionBinding[] }
export function lockEffects(bindings: InteractionBinding[], locks: Locks, devices: string[] = ["keyboard", "touch"]): LockEffects {
  const out: LockEffects = { view: null, zoom: 1, active: [] };
  for (const b of bindings) {
    const lock = b.input.lock;
    if (b.status !== "active" || b.shadowed === true || !devices.includes(b.device) || typeof lock !== "string" || (b.input.state ?? "on") !== "on") continue;
    if (!locks[lock as Lock]) continue;
    out.active.push(b);
    if (b.action.kind === "switch_view" && typeof b.action.declaration === "string") out.view = b.action.declaration;
    if (b.action.kind === "zoom") out.zoom *= typeof b.action.factor === "number" && b.action.factor > 0 ? b.action.factor : 1;
  }
  return out;
}

// ── lock state: getModifierState with a documented fallback ──
export interface LockModel {
  state: Locks;
  /** The browser has reported this lock as on at least once, so its reading is trusted; otherwise the state is counted from key presses. */
  read: Locks;
  /** Whether the browser has getModifierState at all. */
  api: boolean;
}
export const initialLockModel = (): LockModel => ({ state: noLocks(), read: noLocks(), api: true });
export interface KeyInfo { key: string; type: string; repeat: boolean; read: ((name: string) => boolean) | null }
/**
 * Browsers read CapsLock reliably, NumLock and ScrollLock often not (on Linux ScrollLock is usually unreadable), and a synthetic event never carries
 * them. So: a reading of "on" is trusted (and marks that lock readable); pressing a lock key flips the state when the browser did not say "on";
 * a reading of "off" corrects the state only for a lock that has been seen readable. Without getModifierState everything is counted from presses.
 */
export function updateLocks(prev: LockModel, e: KeyInfo): LockModel {
  const state = { ...prev.state }, read = { ...prev.read };
  for (const l of LOCKS) {
    const name = LOCK_NAMES[l];
    const pressed = e.type === "keydown" && !e.repeat && e.key === name;
    let v: boolean | null = null;
    if (e.read) {
      try { v = e.read(name); } catch { v = null; }
    }
    if (v === true) { state[l] = true; read[l] = true; continue; }
    if (pressed) { state[l] = !prev.state[l]; continue; }
    if (v === false && prev.read[l]) state[l] = false;
  }
  return { state, read, api: prev.api && e.read !== null };
}
export const unreadLocks = (m: LockModel): Lock[] => LOCKS.filter(l => !m.read[l]);

// ── the hint overlay ──
export function groupLabel(b: InteractionBinding, actions: InteractionActionInfo[], language: string): string {
  const info = actions.find(a => a.id === b.action.kind);
  const name = b.action.kind === "move" && typeof b.action.pair === "string" ? text(info?.pairs?.[b.action.pair]?.label, language, b.action.pair) : text(info?.label, language, b.action.kind);
  return name.toLocaleLowerCase(language);
}
export interface Hint {
  /** What the arrows do with the modifiers held now. */
  now: string | null;
  arrows: Array<{ key: string; label: string; binding: string }>;
  /** What each modifier would change: "Shift" -> "głębia i czas". */
  held: Array<{ mod: Mod; what: string }>;
  locks: Array<{ lock: Lock; on: boolean; view?: string; zoom?: number; bindings: string[] }>;
}
const ARROWS = ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"];
export function buildHint(bindings: InteractionBinding[], actions: InteractionActionInfo[], mods: Mods, locks: Locks, language: string): Hint {
  const arrows: Hint["arrows"] = [];
  for (const key of ARROWS) {
    const b = matchKey(bindings, key, mods, locks);
    if (b) arrows.push({ key, label: text(b.label, language, b.id), binding: b.id });
  }
  const first = arrows.length ? bindings.find(b => b.id === arrows[0].binding) : undefined;
  const held: Hint["held"] = [];
  for (const m of MODS) {
    const only = { ...noMods(), [m]: true } as Mods;
    const b = matchKey(bindings, "ArrowLeft", only, locks);
    if (b) held.push({ mod: m, what: groupLabel(b, actions, language) });
  }
  const lockRows: Hint["locks"] = LOCKS.map(l => {
    const own = bindings.filter(b => b.status === "active" && typeof b.input.lock === "string" && b.input.lock === l && (b.input.state ?? "on") === "on" && b.device === "keyboard");
    const last = own[own.length - 1];
    const row: Hint["locks"][number] = { lock: l, on: locks[l], bindings: own.map(b => b.id) };
    if (last?.action.kind === "switch_view" && typeof last.action.declaration === "string") row.view = last.action.declaration;
    if (last?.action.kind === "zoom" && typeof last.action.factor === "number") row.zoom = last.action.factor;
    return row;
  });
  return { now: first ? groupLabel(first, actions, language) : null, arrows, held, locks: lockRows };
}

// ── applying an action to the view state ──
export interface ViewState { panX: number; panY: number; zoom: number; angle: number; depth: number; labels: boolean }
export const initialViewState = (): ViewState => ({ panX: 0, panY: 0, zoom: 1, angle: 0, depth: 0, labels: false });
export const PAN_STEP = 40;
/** The effect of an action on the view state, and what it asks of the panel beyond that (a new focus, another declaration, a re-evaluation). */
export type Request = { type: "focus"; target: "origin" | "time" | "branch" | "along_branch" | "gazed" | "selected"; direction: number }
  | { type: "perspective"; direction: number } | { type: "switch_view"; declaration: string } | { type: "unsupported"; kind: string };
export function applyAction(state: ViewState, action: InteractionBinding["action"]): { state: ViewState; request?: Request } {
  const dir = typeof action.direction === "number" ? action.direction : 1;
  const step = typeof action.step === "number" ? action.step : 1;
  switch (action.kind) {
    case "move": {
      const axis = action.axis === "vertical" ? "vertical" : "horizontal";
      if (action.pair === "plane") return { state: axis === "horizontal" ? { ...state, panX: state.panX + dir * step * PAN_STEP } : { ...state, panY: state.panY + dir * step * PAN_STEP } };
      if (action.pair === "depth_time") {
        if (axis === "vertical") return { state: { ...state, depth: Math.max(-6, state.depth + dir * step) } };
        return { state, request: { type: "focus", target: "time", direction: dir } };
      }
      if (action.pair === "branches") return { state, request: { type: "focus", target: axis === "horizontal" ? "branch" : "along_branch", direction: dir } };
      return { state, request: { type: "unsupported", kind: `move:${String(action.pair)}` } };
    }
    case "rotate":
      if (action.target === "perspective") return { state, request: { type: "perspective", direction: dir } };
      return { state: { ...state, angle: state.angle + dir * step } };
    case "zoom": return { state: { ...state, zoom: Math.max(0.05, state.zoom * (typeof action.factor === "number" && action.factor > 0 ? action.factor : 1)) } };
    case "change_depth": return { state: { ...state, depth: Math.max(-6, state.depth + (typeof action.delta === "number" ? action.delta : 1)) } };
    case "toggle_element": return action.element === "labels" ? { state: { ...state, labels: !state.labels } } : { state, request: { type: "unsupported", kind: `toggle_element:${String(action.element)}` } };
    case "switch_view": return typeof action.declaration === "string" ? { state, request: { type: "switch_view", declaration: action.declaration } } : { state };
    case "change_focus": {
      const target = typeof action.target === "string" ? action.target : "selected";
      if (target === "origin" || target === "gazed" || target === "selected") return { state, request: { type: "focus", target, direction: 0 } };
      return { state, request: { type: "unsupported", kind: `change_focus:${target}` } };
    }
    default: return { state, request: { type: "unsupported", kind: action.kind } };
  }
}

/** The element to move the focus to: the neighbour in order of a measure or of a coordinate (null when there is none or the order is not defined). */
export function neighbour(items: Array<{ id: string; value: number | null }>, current: string, direction: number): string | null {
  const ordered = items.filter((i): i is { id: string; value: number } => i.value !== null).sort((a, b) => a.value - b.value || a.id.localeCompare(b.id));
  const at = ordered.findIndex(i => i.id === current);
  if (at < 0) return null;
  const next = ordered[at + (direction < 0 ? -1 : 1)];
  return next ? next.id : null;
}
