// XR input as data (P28 step 3): the kernel resolves the `xr` bindings (loom_view_interaction, device xr); this module only interprets them, with no
// WebXR or 3D dependency. Gaze + pinch (or a controller trigger) selects the looked-at / pointed element, controller buttons are held modifiers, the
// thumbstick is the arrows, and large gestures are for coarse scene moves only. Voice stays recorded: a binding with a phrase is never matched here.
// The result of a match is a binding, applied by the same reducer as the keyboard and touch (interaction.ts `applyAction`).
import type { InteractionActionInfo, InteractionBinding } from "../api/view-model";
import { MODS, groupLabel, noMods, text } from "./interaction";
import type { Mod, Mods } from "./interaction";

/** What an XR event is, as the bindings name it: a controller button (`right_trigger`), a gesture (`pinch`, `stick_left`, `large_swipe_up`), with the gaze ray when the event is a look. */
export interface XrEvent { button?: string; gesture?: string; gaze?: boolean }

const list = (v: unknown): string[] => (Array.isArray(v) ? v.filter((x): x is string => typeof x === "string").map(x => x.toLowerCase()) : []);
const xrActive = (bindings: InteractionBinding[]) => bindings.filter(b => b.device === "xr" && b.status === "active");

/** The binding for an event with the modifiers held now; the last of several wins (the kernel warns about conflicts, nothing is blocked here). A binding
 * that lists `modifiers` needs exactly those held; one that does not list them works with any. A binding that names a phrase is voice: recorded, never matched. */
export function matchXr(bindings: InteractionBinding[], event: XrEvent, mods: Mods): InteractionBinding | null {
  let found: InteractionBinding | null = null;
  for (const b of xrActive(bindings)) {
    const i = b.input;
    if (i.phrase !== undefined) continue;
    if (event.button === undefined && event.gesture === undefined) continue;
    if (i.button !== undefined && i.button !== event.button) continue;
    if (i.gesture !== undefined && i.gesture !== event.gesture) continue;
    if (i.button === undefined && i.gesture === undefined) continue;
    if (i.gaze !== undefined && !event.gaze) continue;
    if (i.modifiers !== undefined) {
      const need = list(i.modifiers);
      if (!MODS.every(m => mods[m] === need.includes(m))) continue;
    }
    found = b;
  }
  return found;
}

/** The modifiers held by the controller buttons that are down (bindings with the action `hold_modifier` and a `button`). */
export function heldFromButtons(bindings: InteractionBinding[], down: Iterable<string>): Mods {
  const pressed = new Set(down), mods = noMods();
  for (const b of xrActive(bindings))
    if (b.action.kind === "hold_modifier" && typeof b.input.button === "string" && pressed.has(b.input.button) && (MODS as string[]).includes(String(b.action.modifier))) mods[b.action.modifier as Mod] = true;
  return mods;
}
/** Whether a button is a held-modifier button (it only holds; pressing it must not also be matched as a selection). */
export function isModifierButton(bindings: InteractionBinding[], button: string): boolean {
  return xrActive(bindings).some(b => b.action.kind === "hold_modifier" && b.input.button === button);
}

/** A thumbstick as an arrow gesture: the dominant axis past the threshold (y up is negative on gamepads). */
export function stickGesture(x: number, y: number, threshold: number): string | null {
  const ax = Math.abs(x), ay = Math.abs(y);
  if (Math.max(ax, ay) < threshold) return null;
  return ax >= ay ? (x < 0 ? "stick_left" : "stick_right") : (y < 0 ? "stick_up" : "stick_down");
}

// ── picking the looked-at / pointed element ──
export interface PickTarget { id: string; x: number; y: number; z: number; radius: number }
/** The element a ray points at: inside a node's sphere, or within the angular tolerance of its centre; the smallest angle wins, then the nearest. */
export function pickByRay(origin: [number, number, number], dir: [number, number, number], targets: PickTarget[], toleranceDeg: number): string | null {
  const len = Math.hypot(...dir) || 1;
  const d: [number, number, number] = [dir[0] / len, dir[1] / len, dir[2] / len];
  let best: { id: string; angle: number; t: number } | null = null;
  for (const tg of targets) {
    const v: [number, number, number] = [tg.x - origin[0], tg.y - origin[1], tg.z - origin[2]];
    const t = v[0] * d[0] + v[1] * d[1] + v[2] * d[2];
    if (t <= 0) continue;
    const dist = Math.hypot(...v) || 1e-9;
    const angle = Math.acos(Math.max(-1, Math.min(1, t / dist))) * (180 / Math.PI);
    const off = Math.sqrt(Math.max(0, dist * dist - t * t));
    if (!(off <= tg.radius || angle <= toleranceDeg)) continue;
    if (!best || angle < best.angle - 1e-9 || (Math.abs(angle - best.angle) <= 1e-9 && t < best.t)) best = { id: tg.id, angle, t };
  }
  return best ? best.id : null;
}

// ── large gestures (coarse scene moves only) ──
export interface SwipeSettings { minDistanceM: number; maxMs: number; dominance: number; cooldownMs: number }
export interface RotateSettings { minAngleDeg: number; cooldownMs: number; /** Each hand must move at least this far (m): one hand moving past a resting one is not a two-hand turn. */ minHandMoveM: number; /** The turn must happen within this window (ms). */ maxMs: number }
export const DEFAULT_SWIPE: SwipeSettings = { minDistanceM: 0.45, maxMs: 700, dominance: 1.6, cooldownMs: 800 };
export const DEFAULT_ROTATE: RotateSettings = { minAngleDeg: 25, cooldownMs: 800, minHandMoveM: 0.12, maxMs: 1200 };
export function readGestureSettings(xr: unknown): { swipe: SwipeSettings; rotate: RotateSettings } {
  const g = (xr as { gestures?: Record<string, Record<string, unknown>> } | null)?.gestures ?? {};
  const n = (o: Record<string, unknown> | undefined, k: string, f: number) => (typeof o?.[k] === "number" && Number.isFinite(o[k] as number) ? (o[k] as number) : f);
  return {
    swipe: { minDistanceM: n(g.large_swipe, "min_distance_m", DEFAULT_SWIPE.minDistanceM), maxMs: n(g.large_swipe, "max_ms", DEFAULT_SWIPE.maxMs), dominance: n(g.large_swipe, "dominance", DEFAULT_SWIPE.dominance), cooldownMs: n(g.large_swipe, "cooldown_ms", DEFAULT_SWIPE.cooldownMs) },
    rotate: { minAngleDeg: n(g.two_hand_rotate, "min_angle_deg", DEFAULT_ROTATE.minAngleDeg), cooldownMs: n(g.two_hand_rotate, "cooldown_ms", DEFAULT_ROTATE.cooldownMs), minHandMoveM: n(g.two_hand_rotate, "min_hand_move_m", DEFAULT_ROTATE.minHandMoveM), maxMs: n(g.two_hand_rotate, "max_ms", DEFAULT_ROTATE.maxMs) },
  };
}
/** One hand's path: a swipe is a large, quick, mostly straight move; the result is the gesture the bindings name (`large_swipe_left`, ..._up). */
export class SwipeDetector {
  private samples: Array<{ t: number; p: [number, number, number] }> = [];
  private quietUntil = -Infinity;
  constructor(private s: SwipeSettings = DEFAULT_SWIPE) {}
  reset(): void { this.samples = []; }
  push(t: number, p: [number, number, number]): string | null {
    this.samples.push({ t, p });
    while (this.samples.length > 1 && t - this.samples[0].t > this.s.maxMs) this.samples.shift();
    if (t < this.quietUntil) return null;
    const a = this.samples[0].p, dx = p[0] - a[0], dy = p[1] - a[1], dz = p[2] - a[2];
    const ax = Math.abs(dx), ay = Math.abs(dy), az = Math.abs(dz);
    const major = Math.max(ax, ay), cross = Math.max(Math.min(ax, ay), az, 1e-9);
    if (major < this.s.minDistanceM || major / cross < this.s.dominance) return null;
    this.samples = [];
    this.quietUntil = t + this.s.cooldownMs;
    return ax >= ay ? (dx < 0 ? "large_swipe_left" : "large_swipe_right") : (dy < 0 ? "large_swipe_down" : "large_swipe_up");
  }
}
/** Two hands turning around each other in the horizontal plane, BOTH moving: `two_hand_rotate` with the direction (+1 clockwise seen from above). */
export class TwoHandRotateDetector {
  private base: { t: number; angle: number; left: [number, number, number]; right: [number, number, number] } | null = null;
  private quietUntil = -Infinity;
  constructor(private s: RotateSettings = DEFAULT_ROTATE) {}
  reset(): void { this.base = null; }
  push(t: number, left: [number, number, number], right: [number, number, number]): { gesture: string; direction: number } | null {
    const angle = (Math.atan2(right[2] - left[2], right[0] - left[0]) * 180) / Math.PI;
    if (t < this.quietUntil) return null;
    if (this.base === null || t - this.base.t > this.s.maxMs) { this.base = { t, angle, left, right }; return null; }
    let d = angle - this.base.angle;
    while (d > 180) d -= 360;
    while (d <= -180) d += 360;
    if (Math.abs(d) < this.s.minAngleDeg) return null;
    const moved = (a: [number, number, number], b: [number, number, number]) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
    if (moved(left, this.base.left) < this.s.minHandMoveM || moved(right, this.base.right) < this.s.minHandMoveM) return null;
    this.base = null;
    this.quietUntil = t + this.s.cooldownMs;
    return { gesture: "two_hand_rotate", direction: d > 0 ? 1 : -1 };
  }
}

// ── the hint, in the scene ──
export interface XrHint {
  /** What a select (pinch with the gaze, or a trigger) does, by binding label. */
  select: string[];
  /** Controller buttons that hold modifiers: "left trigger = Shift". */
  held: Array<{ button: string; mod: Mod }>;
  /** What the thumbstick does with the modifiers held now. */
  stick: Array<{ gesture: string; label: string }>;
  /** What each held modifier would change for the thumbstick. */
  modifiers: Array<{ mod: Mod; what: string }>;
  coarse: string[];
  recorded: number;
}
export function buildXrHint(bindings: InteractionBinding[], actions: InteractionActionInfo[], mods: Mods, language: string): XrHint {
  const xr = xrActive(bindings);
  const label = (b: InteractionBinding) => text(b.label, language, b.id);
  const select = xr.filter(b => (b.action.kind === "change_focus" || b.action.kind === "toggle_element") && b.input.phrase === undefined).map(label);
  const held = xr.filter(b => b.action.kind === "hold_modifier" && typeof b.input.button === "string").map(b => ({ button: String(b.input.button), mod: b.action.modifier as Mod }));
  const stick = ["stick_left", "stick_right", "stick_up", "stick_down"].flatMap(gesture => {
    const b = matchXr(bindings, { gesture }, mods);
    return b ? [{ gesture, label: label(b) }] : [];
  });
  const modifiers: XrHint["modifiers"] = [];
  for (const m of MODS) {
    const b = matchXr(bindings, { gesture: "stick_left" }, { ...noMods(), [m]: true } as Mods);
    if (b && held.some(h => h.mod === m)) modifiers.push({ mod: m, what: groupLabel(b, actions, language) });
  }
  const coarse = xr.filter(b => typeof b.input.gesture === "string" && (b.input.gesture.startsWith("large_") || b.input.gesture === "two_hand_rotate")).map(label);
  return { select, held, stick, modifiers, coarse, recorded: bindings.filter(b => b.device === "xr" && b.status === "recorded").length };
}
