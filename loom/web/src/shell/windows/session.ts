// W1 (OKNA §3.2–3.3): the app's window state. A layout from layouts.json becomes a WState of model.ts; a panel of the app is a window of
// its kind opened `maximized` over the layout (journaled operations of the model), and closing it gives the window it covered back.
// Pure: React state holds the WState, App.tsx calls these functions. Nothing here knows a component.
import layoutsFile from "../data/layouts.json";
import { apply, createState, type Ctx, type Placement, type WLayout, type WState, type Win } from "./model";
import { kindsCtx } from "./registry";

interface PanelData { panel_kinds?: Record<string, string>; panel_kinds_by_layout?: Record<string, Record<string, string>>; panel_placement?: string }
const DATA = layoutsFile as PanelData;
export const WINDOW_CTX: Ctx = { kinds: kindsCtx() };

/** The window kind of a panel id in a layout (data; an unknown panel opens as the kind of its own id — the registry is open). */
export function panelKind(panel: string, layoutId: string, data: PanelData = DATA): string {
  return data.panel_kinds_by_layout?.[layoutId]?.[panel] ?? data.panel_kinds?.[panel] ?? panel;
}

/** A fresh state of a layout. */
export function layoutState(layout: WLayout, limit = 50): WState { return createState(layout, limit, WINDOW_CTX); }

/** Windows opened over the layout for a panel (they carry `params.panel`). */
export const overlays = (l: WLayout): Win[] => l.windows.filter((w) => typeof w.params?.panel === "string");
/** The panel shown on top (the last overlay), or null. */
export function activePanelOf(s: WState | null): string | null {
  const o = s ? overlays(s.layout) : [];
  return o.length ? String(o[o.length - 1].params.panel) : null;
}
/** The window of the layout itself shown full-screen: the maximized one, or the one an overlay covers. Null = the layout is a grid (W3). */
export function baseWindow(l: WLayout): Win | null {
  const own = l.windows.filter((w) => typeof w.params?.panel !== "string");
  const max = own.find((w) => w.mode === "maximized");
  if (max) return max;
  const covered = overlays(l).map((w) => w.params.over).find((id) => typeof id === "string");
  return own.find((w) => w.id === covered) ?? null;
}

/** Several model operations done for one action of the person are one entry of the journal (one undo brings the whole action back). */
function oneStep(before: WState, after: WState, op: string): WState {
  if (after === before || after.layout === before.layout) return before;
  const past = [...before.past, { op, layout: before.layout }];
  while (past.length > before.limit) past.shift();
  return { layout: after.layout, past, future: [], limit: before.limit };
}

/** Closes the overlays; a window an overlay covered is maximized again (the stack of §4.1). One journal entry. */
export function closePanels(s: WState, ctx: Ctx = WINDOW_CTX): WState {
  let state = s;
  for (const w of overlays(s.layout)) {
    state = apply(state, { op: "close", id: w.id }, ctx).state;
    const over = w.params.over;
    const back = typeof over === "string" ? state.layout.windows.find((x) => x.id === over) : undefined;
    if (back && back.mode !== "maximized") state = apply(state, { op: "maximize", id: back.id }, ctx).state;
  }
  return oneStep(s, state, "close");
}

/** Opens a panel as a window over the layout (preset placement: maximize). One panel at a time, as before W1: an open one is closed first. */
export function openPanel(s: WState, panel: string, ctx: Ctx = WINDOW_CTX, data: PanelData = DATA): WState {
  const before = overlays(s.layout);
  const over = before.map((w) => w.params.over).find((id) => typeof id === "string") ?? s.layout.windows.find((w) => w.mode === "maximized")?.id ?? null;
  let state = before.length ? apply(s, { op: "close", id: before[0].id }, ctx).state : s;
  for (const w of before.slice(1)) state = apply(state, { op: "close", id: w.id }, ctx).state;
  const placement = (data.panel_placement ?? "maximize") as Placement;
  const r = apply(state, { op: "open", kind: panelKind(panel, s.layout.id, data), placement, params: { panel, over } }, ctx);
  return r.applied ? oneStep(s, r.state, "open") : s;
}

/** The React-style setter of the old `activePanel`: a value, null, or a function of the current panel. */
export function setPanel(s: WState, next: string | null | ((cur: string | null) => string | null), ctx: Ctx = WINDOW_CTX): WState {
  const cur = activePanelOf(s);
  const want = typeof next === "function" ? next(cur) : next;
  if (want === cur) return s;
  return want === null ? closePanels(s, ctx) : openPanel(s, want, ctx);
}

/** Another layout; the open panel stays open over it (as before W1, the panel state did not depend on the layout). */
export function switchLayout(s: WState, layout: WLayout, limit = 50, ctx: Ctx = WINDOW_CTX): WState {
  const panel = activePanelOf(s);
  const fresh = layoutState(layout, limit);
  return panel === null ? fresh : openPanel(fresh, panel, ctx);
}
