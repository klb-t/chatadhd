// W4 (OKNA §2.2, §4.4, §6.3): docking and floating as actions of a person - one journal entry each - and the advisories of an
// arrangement (dock_narrow, float_offscreen, float_under_keyboard). Pure: no React, no DOM, no kernel. The page draws the chips and
// answers them through the catalogue action advisory.answer (the store of G3b, `loom.advisory/1` code + fingerprint), not here.
// Every threshold, text and menu entry is a preset in data (layouts.json → window_arrange); nothing is refused: a tiny leaf, a window
// off the screen or under the keyboard is drawn as asked and only advised about.
import layoutsFile from "../data/layouts.json";
import { apply, fitToArea, type Ctx, type Op, type Rect, type WLayout, type WState, type Win } from "./model";
import { WINDOW_CTX } from "./session";
import { dockView, leafBoxes, type Box, type Zone } from "./dock";

type Words = { pl?: string; en?: string };
export interface ArrangeData {
  min_useful_width?: number; offscreen_fraction?: number; keyboard_min_px?: number; drop_edge?: number; drag_threshold_px?: number; split_grip_px?: number;
  float_rect?: Partial<Rect>; advisory_button_order?: string[];
  advisories?: Record<string, { text?: Words; fix?: { id: string; label: Words } }>;
  question?: Words; choices?: { id: string; label: Words }[];
  menu?: { id: string; label: Words; icon?: string }[]; keymap?: Record<string, string>;
}
export const ARRANGE: ArrangeData = ((layoutsFile as { window_arrange?: ArrangeData }).window_arrange) ?? {};
export const num = (v: unknown, d: number) => (typeof v === "number" && Number.isFinite(v) ? v : d);

/** An arrangement asked by a person: an open string; the known ones are docking to a side, a tab, floating, maximizing. */
export type Arrange = "left" | "right" | "top" | "bottom" | "tab" | "float" | "maximize" | string;
export interface ArrangeOpts { of?: string; rect?: Partial<Rect>; index?: number }

function oneStep(before: WState, after: WState, op: string): WState {
  if (after.layout === before.layout) return before;
  const past = [...before.past, { op, layout: before.layout }];
  while (past.length > before.limit) past.shift();
  return { layout: after.layout, past, future: [], limit: before.limit };
}
const isPanel = (w: Win) => typeof w.params?.panel === "string";
/** The leaf a window docks next to when none is named: the leaf of the window it was opened over, else the focused docked one, else the first drawn. */
export function homeLeaf(l: WLayout, w?: Win): string | undefined {
  const over = w && typeof w.params?.over === "string" ? l.windows.find((x) => x.id === w.params.over) : undefined;
  if (over?.dock && over.id !== w?.id) return over.dock.leaf;
  const own = l.windows.find((x) => !isPanel(x) && x.dock && !x.collapsed && x.id !== w?.id);
  return own?.dock?.leaf ?? l.windows.find((x) => x.dock && x.id !== w?.id)?.dock?.leaf;
}

/** Docks, floats, tabs or maximizes a window as one action (one journal entry, one undo). Docking shows the dock tree: a maximized window
 *  with a leaf goes back to it, a maximized one without a leaf waits in the switcher. Floating over a window shown full-screen keeps that
 *  window in front under it. Total: an unknown window or arrangement returns the same state. */
export function arrangeWindow(s: WState, id: string, how: Arrange, opts: ArrangeOpts = {}, ctx: Ctx = WINDOW_CTX, data: ArrangeData = ARRANGE): WState {
  const w = s.layout.windows.find((x) => x.id === id);
  if (!w) return s;
  let state = s;
  const run = (op: Op) => { state = apply(state, op, ctx).state; };
  if (how === "maximize") run({ op: "maximize", id });
  else if (how === "float") {
    const base = state.layout.windows.find((x) => x.mode === "maximized" && x.id !== id) ?? (typeof w.params?.over === "string" ? state.layout.windows.find((x) => x.id === w.params.over) : undefined);
    run({ op: "float", id, rect: opts.rect ?? (w.mode === "floating" ? undefined : { ...(data.float_rect ?? {}) }) });
    // the window it floated over stays in front under it (the chat on a phone), unless the layout is a grid
    if (base && base.dock && !state.layout.windows.some((x) => x.mode === "maximized") && !state.layout.windows.some((x) => isPanel(x) && x.dock)) run({ op: "maximize", id: base.id });
  } else if (["left", "right", "top", "bottom", "tab"].includes(how)) {
    const of = opts.of ?? homeLeaf(state.layout, w);
    if (!of) return s;
    run(how === "tab" ? { op: "dock", id, target: { leaf: of, ...(opts.index !== undefined ? { index: opts.index } : {}) } } : { op: "dock", id, target: { side: how as Zone & ("left" | "right" | "top" | "bottom"), of } });
    for (const x of [...state.layout.windows]) if (x.mode === "maximized" && x.id !== id) run(x.dock ? { op: "restore", id: x.id } : { op: "minimize", id: x.id });
    const back = state.layout.windows.find((x) => x.id === id);
    if (back?.mode === "maximized") run({ op: "restore", id });
  } else return s;
  return oneStep(s, state, how);
}
/** The edge of a split dragged: the new ratio, one journal entry (the live preview applies it to the state the drag started from). */
export function setRatio(s: WState, path: string, ratio: number, ctx: Ctx = WINDOW_CTX): WState { return apply(s, { op: "ratio", path, ratio }, ctx).state; }
/** A floating window moved or resized to a rect (one journal entry), brought to the top of the floating ones. */
export function moveFloating(s: WState, id: string, rect: Partial<Rect>, ctx: Ctx = WINDOW_CTX): WState {
  const r = apply(s, { op: "move", id, rect }, ctx).state;
  return oneStep(s, apply(r, { op: "focus", id }, ctx).state, "move");
}

// ---------- advisories ----------
export interface WinAdvisory {
  code: string; fingerprint: string; window?: string; evidence: Record<string, unknown>;
  text: Words; question: Words; fix?: { id: string; label: Words };
}
export interface Env {
  area: Box; deviceClass: string;
  /** The box the on-screen keyboard covers (from visualViewport), or null. */
  keyboard?: Box | null;
  shown?: (w: Win) => boolean;
  title?: (w: Win) => string;
}
const fill = (t: Words | undefined, vars: Record<string, string>): Words => {
  const f = (s?: string) => s?.replace(/\{(\w+)\}/g, (_, k) => vars[k] ?? "");
  return { pl: f(t?.pl), en: f(t?.en) };
};
/** The rect of a floating window in pixels of an area (a fraction rect is relative to it). */
export function rectBox(r: Rect, area: Box): Box {
  return r.unit === "px" ? { x: r.x, y: r.y, w: r.w, h: r.h } : { x: area.x + r.x * area.w, y: area.y + r.y * area.h, w: r.w * area.w, h: r.h * area.h };
}
const overlap = (a: Box, b: Box) => Math.max(0, Math.min(a.x + a.w, b.x + b.w) - Math.max(a.x, b.x)) * Math.max(0, Math.min(a.y + a.h, b.y + b.h) - Math.max(a.y, b.y));

/** The advisories of an arrangement on a screen. The fingerprint is the configuration (device class + kinds), so "don't ask again" holds for
 *  the same arrangement on the same class of screen and asks again for another one. */
export function windowAdvisories(l: WLayout, env: Env, data: ArrangeData = ARRANGE): WinAdvisory[] {
  const out: WinAdvisory[] = [];
  const q = data.question ?? { pl: "Czy to celowe?", en: "Is this intended?" };
  const A = data.advisories ?? {};
  const title = env.title ?? ((w: Win) => w.kind);
  const make = (code: string, fingerprint: string, evidence: Record<string, unknown>, vars: Record<string, string>, window?: string): WinAdvisory =>
    ({ code, fingerprint, evidence, text: fill(A[code]?.text ?? { pl: code, en: code }, vars), question: q, ...(A[code]?.fix ? { fix: A[code].fix } : {}), ...(window ? { window } : {}) });
  // dock_narrow: only while the dock tree is drawn (no window maximized over it), for drawn leaves sized by ratio
  if (!l.windows.some((w) => w.mode === "maximized")) {
    const view = dockView(l, env.shown);
    const boxes = leafBoxes(view, env.area);
    const min = num(data.min_useful_width, 240);
    const drawn: { leaf: string; kinds: string[]; w: number; content: boolean }[] = [];
    const walk = (n: typeof view) => { if (!n || n.hidden) return; if (n.type === "leaf") drawn.push({ leaf: n.leaf, kinds: n.windows.map((w) => w.kind), w: boxes[n.leaf]?.w ?? 0, content: n.sizing === "content" }); else { walk(n.a); walk(n.b); } };
    walk(view);
    const narrow = drawn.filter((d) => !d.content && d.w < min);
    if (drawn.length >= 2 && narrow.length) {
      const kinds = drawn.flatMap((d) => d.kinds).sort();
      out.push(make("dock_narrow", `${env.deviceClass}|${kinds.join("+")}`, { leaves: narrow.map((d) => ({ leaf: d.leaf, width: Math.round(d.w) })), min, area: env.area.w },
        { min: String(min), widths: narrow.map((d) => `${Math.round(d.w)} px`).join(", ") }));
    }
  }
  const frac = Math.min(1, Math.max(0, num(data.offscreen_fraction, 0.5)));
  for (const w of l.windows) {
    if (w.mode !== "floating" || !w.rect) continue;
    const b = rectBox(w.rect, env.area); const size = Math.max(0, b.w) * Math.max(0, b.h);
    const outside = size > 0 ? 1 - overlap(b, env.area) / size : 0;
    if (size > 0 && outside > frac) out.push(make("float_offscreen", `${env.deviceClass}|${w.kind}`, { outside: Math.round(outside * 100) / 100, threshold: frac, rect: w.rect }, { title: title(w) }, w.id));
    const kb = env.keyboard;
    if (kb && kb.h >= num(data.keyboard_min_px, 80) && overlap(b, kb) > 0)
      out.push(make("float_under_keyboard", `${env.deviceClass}|${w.kind}`, { keyboard: kb, rect: w.rect }, { title: title(w) }, w.id));
  }
  return out;
}

/** The fix an advisory offers, as an action on the state (one journal entry): maximize the narrow window, snap a window back, move it above the keyboard. */
export function applyFix(s: WState, a: WinAdvisory, env: Env, ctx: Ctx = WINDOW_CTX): WState {
  const fix = a.fix?.id;
  if (fix === "maximize") {
    const id = a.window ?? s.layout.focus ?? undefined;
    return id ? arrangeWindow(s, id, "maximize", {}, ctx) : s;
  }
  const w = a.window ? s.layout.windows.find((x) => x.id === a.window) : undefined;
  if (!w?.rect) return s;
  if (fix === "snap") {
    const fitted = fitToArea(s.layout, { w: w.rect.unit === "px" ? env.area.w : 1, h: w.rect.unit === "px" ? env.area.h : 1 }, ctx).layout.windows.find((x) => x.id === w.id);
    return fitted?.rect ? moveFloating(s, w.id, fitted.rect, ctx) : s;
  }
  if (fix === "above_keyboard" && env.keyboard) {
    const b = rectBox(w.rect, env.area); const top = Math.max(env.area.y, env.keyboard.y - b.h);
    const y = w.rect.unit === "px" ? top : env.area.h > 0 ? (top - env.area.y) / env.area.h : w.rect.y;
    return moveFloating(s, w.id, { y }, ctx);
  }
  return s;
}

/** A chip is shown while its condition holds, unless the person answered dont_ask (remembered by the kernel: advisory.list) or keep in this
 *  page session (cleared when the condition goes away, so the same arrangement made again asks again), or asking is off (policy ask=false). */
export function visibleChips(all: WinAdvisory[], remembered: ReadonlySet<string>, keptNow: ReadonlySet<string>, ask = true): WinAdvisory[] {
  if (!ask) return [];
  return all.filter((a) => !remembered.has(`${a.code}|${a.fingerprint}`) && !remembered.has(`${a.code}|`) && !keptNow.has(`${a.code}|${a.fingerprint}`));
}
/** The keys remembered as "don't ask" in the answer of advisory.list (transient answers such as keep do not hide a chip). */
export function rememberedKeys(list: { acks?: { code: string; fingerprint?: string; choice?: string }[] } | null, transient: string[] = ["keep"]): Set<string> {
  return new Set((list?.acks ?? []).filter((a) => !transient.includes(a.choice ?? "")).map((a) => `${a.code}|${a.fingerprint ?? ""}`));
}
