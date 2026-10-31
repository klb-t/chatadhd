// W10 (OKNA §8.3, §4.4): which windows stay mounted while hidden, and the state an unmounted window comes back with. Pure: no React, no
// DOM. The policy is the settings field `window_keep_mounted` - an open value: `visible` (only the windows drawn now are mounted; a hidden
// one - an inactive tab, a minimized window, a panel waiting in the switcher - is unmounted and comes back from its kept state), `all`
// (every window of the layout stays mounted, hidden), `recent:<n>` (the drawn ones plus the n most recently drawn hidden ones), or any
// value not known here: used as the preset of its device class with an advisory, never refused. Presets per device class are data
// (layouts.json → window_mount.keep_mounted); unset = the preset. There is no upper number of windows: `many_windows` is an advisory
// whose threshold is a preset per device class (window_mount.mounted_soft) and whose evidence is the cost measured on a named machine
// and date (window_mount.cost_estimate, a dated observation, never a constraint).
// The state of a window that is not in its params (scroll, a local switch) is kept in a store keyed by the window id, filtered by the
// `state_keys` its kind declares in the registry (window_kinds.json); each key names its keeper (window_kinds.json → state_keepers).
import layoutsFile from "../data/layouts.json";
import { mapLocalizedText, type LocalizedText } from "../language-pack";
import registry from "../data/window_kinds.json";
import type { WLayout, WState, Win } from "./model";
import { apply, type Ctx } from "./model";
import { WINDOW_CTX, baseWindow, visibleOverlay } from "./session";
import { dockView, type DockNode } from "./dock";
import type { WinAdvisory } from "./arrange";

type Words = LocalizedText;
export interface CostEstimate { observed?: string | null; machine?: string | null; source?: string; per_window_bytes?: Record<string, number>; default_bytes?: number; base_bytes?: number }
export interface MountData {
  keep_mounted?: Record<string, string>;
  mounted_soft?: Record<string, number>;
  cost_estimate?: CostEstimate;
  advisories?: Record<string, { text?: Words; fix?: { id: string; label: Words } }>;
  restore_frames?: number;
}
export const MOUNT: MountData = ((layoutsFile as { window_mount?: MountData }).window_mount) ?? {};
const KINDS_FILE = registry as { kinds: { id: string; state_keys?: string[] }[]; state_keepers?: Record<string, unknown> };

export type Policy = { mode: "visible" } | { mode: "all" } | { mode: "recent"; n: number };
const byClass = <T,>(m: Record<string, T> | undefined, cls: string): T | undefined => m?.[cls] ?? m?.any;

/** Reads a policy word; null when it is not a known shape (the caller falls back to the preset and says so). */
export function parsePolicy(v: unknown): Policy | null {
  if (typeof v !== "string") return null;
  const s = v.trim().toLowerCase();
  if (s === "visible") return { mode: "visible" };
  if (s === "all") return { mode: "all" };
  const m = /^recent\s*:\s*(\d+)$/.exec(s);
  return m ? { mode: "recent", n: Number(m[1]) } : null;
}
/** The policy in force: the field's value when it reads, else the preset of the device class (else `visible`); `unknown` = the value
 *  that did not read (set, not empty), for the advisory window_keep_mounted_unknown. */
export function keepPolicy(value: unknown, deviceClass: string, data: MountData = MOUNT): { policy: Policy; unknown: string | null } {
  const own = parsePolicy(value);
  if (own) return { policy: own, unknown: null };
  const preset = parsePolicy(byClass(data.keep_mounted, deviceClass)) ?? { mode: "visible" as const };
  const set = value !== undefined && value !== null && !(typeof value === "string" && value.trim() === "");
  return { policy: preset, unknown: set ? String(typeof value === "string" ? value : JSON.stringify(value)) : null };
}

/** The windows drawn now, by the same rules as WindowHost: the window shown full-screen (or the active tab of every drawn leaf of the
 *  dock tree), the overlay in front, every floating window. */
export function shownIds(l: WLayout, shown?: (w: Win) => boolean): Set<string> {
  const out = new Set<string>();
  const base = baseWindow(l);
  if (base) out.add(base.id);
  else {
    const walk = (n: DockNode | null) => { if (!n || n.hidden) return; if (n.type === "leaf") { if (n.active) out.add(n.active.id); } else { walk(n.a); walk(n.b); } };
    walk(dockView(l, shown));
  }
  const top = visibleOverlay(l);
  if (top) out.add(top.id);
  for (const w of l.windows) if (w.mode === "floating" && w.rect) out.add(w.id);
  return out;
}

/** The order of windows most recently drawn first, after `drawn` was drawn (ids no longer in the layout are dropped). */
export function touchRecent(recent: readonly string[], drawn: Iterable<string>, l: WLayout): string[] {
  const ids = new Set(l.windows.map((w) => w.id));
  const now = [...drawn].filter((id) => ids.has(id));
  return [...now, ...recent.filter((id) => ids.has(id) && !now.includes(id))];
}

/** The windows mounted under a policy: always the drawn ones; `all` adds every window of the layout; `recent:n` the n most recent
 *  hidden ones (any n ≥ 0; 0 = as `visible`). */
export function mountedIds(l: WLayout, policy: Policy, recent: readonly string[] = [], shown?: (w: Win) => boolean): Set<string> {
  const out = shownIds(l, shown);
  if (policy.mode === "all") for (const w of l.windows) out.add(w.id);
  else if (policy.mode === "recent") {
    const ids = new Set(l.windows.map((w) => w.id));
    let n = policy.n;
    for (const id of recent) { if (n <= 0) break; if (ids.has(id) && !out.has(id)) { out.add(id); n--; } }
  }
  return out;
}
/** The hidden windows kept mounted (mounted and not drawn): what the host draws hidden. */
export function keptHidden(l: WLayout, policy: Policy, recent: readonly string[] = [], shown?: (w: Win) => boolean): Set<string> {
  const drawn = shownIds(l, shown);
  return new Set([...mountedIds(l, policy, recent, shown)].filter((id) => !drawn.has(id)));
}

// ---------- the cost and the advisory many_windows ----------
/** The estimated JS heap of the mounted windows from the dated measurement in data (per kind, else the default per window). */
export function estimatedBytes(l: WLayout, mounted: ReadonlySet<string>, data: MountData = MOUNT): number {
  const c = data.cost_estimate ?? {};
  const per = c.per_window_bytes ?? {};
  let sum = 0;
  for (const w of l.windows) if (mounted.has(w.id)) sum += per[w.kind] ?? c.default_bytes ?? 0;
  return sum;
}
const fill = (t: Words | undefined, vars: Record<string, string>): Words => {
  return mapLocalizedText(t, s => s.replace(/\{(\w+)\}/g, (_, k) => vars[k] ?? ""));
};
const mb = (b: number) => (Math.round(b / 104857.6) / 10).toString();

/** many_windows when more windows are mounted than the preset of the device class (any number ≥ 0; a missing preset = never asked);
 *  window_keep_mounted_unknown when the field's value did not read. The fingerprint of many_windows is the device class and the count, so
 *  "don't ask again" holds for that count on that class of screen. Evidence = the count, the threshold, the policy, the estimate and the
 *  observation it comes from. Nothing is unmounted or refused here. */
export function mountAdvisories(l: WLayout, mounted: ReadonlySet<string>, deviceClass: string, policy: Policy, unknown: string | null, data: MountData = MOUNT): WinAdvisory[] {
  const out: WinAdvisory[] = [];
  const q = { pl: "Czy to celowe?", en: "Is this intended?" };
  const A = data.advisories ?? {};
  const soft = byClass(data.mounted_soft, deviceClass);
  const count = l.windows.filter((w) => mounted.has(w.id)).length;
  if (typeof soft === "number" && Number.isFinite(soft) && count > soft) {
    const bytes = estimatedBytes(l, mounted, data);
    const c = data.cost_estimate ?? {};
    out.push({ code: "many_windows", fingerprint: `${deviceClass}|${count}`, question: q,
      evidence: { mounted: count, open: l.windows.length, threshold: soft, device_class: deviceClass, policy: policy.mode === "recent" ? `recent:${policy.n}` : policy.mode, estimated_bytes: bytes, observed: c.observed ?? null, machine: c.machine ?? null },
      text: fill(A.many_windows?.text ?? { pl: "many_windows", en: "many_windows" }, { count: String(count), soft: String(soft), mb: mb(bytes), observed: c.observed ?? "?" }),
      ...(A.many_windows?.fix ? { fix: A.many_windows.fix } : {}) });
  }
  if (unknown !== null) {
    out.push({ code: "window_keep_mounted_unknown", fingerprint: unknown, question: q, evidence: { value: unknown, used: policy.mode === "recent" ? `recent:${policy.n}` : policy.mode },
      text: fill(A.window_keep_mounted_unknown?.text ?? { pl: "window_keep_mounted: {value}", en: "window_keep_mounted: {value}" }, { value: unknown, used: policy.mode }) });
  }
  return out;
}
/** The fix of many_windows („Odstaw niewidoczne”): every floating window except the focused and the pinned ones is minimized - one
 *  journal entry, undo brings them back. Under `visible` a minimized window is unmounted and comes back with its kept state. */
export function parkWindows(s: WState, ctx: Ctx = WINDOW_CTX): WState {
  let state = s;
  for (const w of s.layout.windows) {
    if (w.mode !== "floating" || w.pinned || w.id === s.layout.focus) continue;
    state = apply(state, { op: "minimize", id: w.id }, ctx).state;
  }
  if (state.layout === s.layout) return s;
  const past = [...s.past, { op: "park", layout: s.layout }];
  while (past.length > s.limit) past.shift();
  return { layout: state.layout, past, future: [], limit: s.limit };
}

// ---------- state_keys: the state an unmounted window comes back with ----------
/** The state keys a kind declares (registry); an unknown kind keeps nothing but its params (the registry is open). */
export function stateKeysOf(kind: string, kinds: readonly { id: string; state_keys?: string[] }[] = KINDS_FILE.kinds): string[] {
  return kinds.find((k) => k.id === kind)?.state_keys ?? [];
}
export const STATE_KEEPERS: Record<string, unknown> = KINDS_FILE.state_keepers ?? {};
export type StateStore = ReadonlyMap<string, Readonly<Record<string, unknown>>>;
/** Keeps a snapshot of a window's state: only the keys its kind declares; other keys are dropped (a key not declared is not kept). */
export function keepState(store: StateStore, w: Pick<Win, "id" | "kind">, snapshot: Record<string, unknown>, kinds?: readonly { id: string; state_keys?: string[] }[]): Map<string, Record<string, unknown>> {
  const keys = new Set(stateKeysOf(w.kind, kinds));
  const next = new Map(store as Map<string, Record<string, unknown>>);
  const own = { ...(next.get(w.id) ?? {}) };
  for (const [k, v] of Object.entries(snapshot)) if (keys.has(k)) own[k] = v;
  next.set(w.id, own);
  return next;
}
/** The kept state of a window (empty when none). */
export function restoreState(store: StateStore, id: string): Record<string, unknown> { return { ...(store.get(id) ?? {}) }; }
/** Drops the kept state of windows no longer in the layout (a closed window does not come back with a stale scroll). */
export function pruneState(store: StateStore, l: WLayout): Map<string, Record<string, unknown>> {
  const ids = new Set(l.windows.map((w) => w.id));
  return new Map([...store].filter(([id]) => ids.has(id)) as [string, Record<string, unknown>][]);
}

/** One store of the page (a session of the page; a reload restores what the layout and the coupling values carry). */
let PAGE_STORE: Map<string, Record<string, unknown>> = new Map();
export const pageStore = {
  get: (id: string) => restoreState(PAGE_STORE, id),
  keep: (w: Pick<Win, "id" | "kind">, snapshot: Record<string, unknown>) => { PAGE_STORE = keepState(PAGE_STORE, w, snapshot); },
  prune: (l: WLayout) => { PAGE_STORE = pruneState(PAGE_STORE, l); },
  size: () => PAGE_STORE.size,
};
