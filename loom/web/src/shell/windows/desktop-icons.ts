// R101 D2 (PULPIT_R101_2026-10-28 §3.2, §4, §6, §9, §13): the content of the desktop window as pure data + total functions - the view
// declaration chosen by the setting desktop_declaration (preset pulpit_graf), the icons of the pins (perspective pinned_on), their grid
// (coordinates icon_grid), the window an icon opens (open_targets: address -> window kind), and the layout of the faint graph behind them.
// Nothing here touches React, the DOM or the network. Every number and list is a preset of shell/data/desktop.json; an unknown value is an
// advisory or a skipped element with its reason, never an error (CLAUDE.md: the engine takes every configuration, also a useless one).
import { DESKTOP, type DesktopData, type KindEntry, type KindTarget } from "./desktop";

type Text = { pl?: string; en?: string };
export interface DeclElement { id?: string; kind?: string; [key: string]: unknown }
export interface Declaration { id?: string; label?: Text | string; elements?: DeclElement[] }
export interface Advisory { code: string; vars?: Record<string, string> }
export interface IconSource { id: string; perspective: string; limit: number | null }
/** The icon sources the page can fill today (perspectives of the desktop's own data); another perspective is `unsupported` with its reason. */
export const ICON_SOURCES = ["window_kinds", "minimized_windows", "layouts", "recent_conversations"];
export interface ResolvedDesktop {
  id: string;
  /** preset: nothing set; setting: an id of desktop.json declarations; inline: a declaration object (or its JSON text) in the setting. */
  origin: "preset" | "setting" | "inline";
  pins: { focus: string; seed: string[] } | null;
  /** R101 D3: the background graph; `channel` set = it follows that coupling channel (W6, e.g. focus of a navigator window), `focus` is
   *  then the fallback when no window gives a value (pulpit_graf_zywy). */
  around: { focus: string; depth: number; budget: number; channel?: string } | null;
  /** R101 D3: icon sources other than the pins (window_kinds, minimized_windows, layouts, recent_conversations - an open list), in order. */
  sources: IconSource[];
  /** R101 D3: a card of a node as the content of the desktop (pulpit_karta: party:self). */
  card: { focus: string; limit: number } | null;
  /** R101 D3: the affordance of the declaration (§12): the sections it asks for when the setting desktop_sections is unset; null = the preset. */
  sections: string[] | null;
  alpha: number;
  edgeAlpha: number;
  grid: { columns: number | "auto"; cellPx: number; flow: string };
  icon: { labelLines: number } | null;
  unsupported: { id: string; kind: string; reason: string }[];
  advisories: Advisory[];
}

const num = (v: unknown): number | undefined => (typeof v === "number" && Number.isFinite(v) ? v : typeof v === "string" && v.trim() !== "" && Number.isFinite(Number(v)) ? Number(v) : undefined);
/** A preset per device class ({phone: 4, desktop: "auto"}) or one value for every class. */
export function byClass(v: unknown, deviceClass: string): unknown {
  if (v && typeof v === "object" && !Array.isArray(v)) { const o = v as Record<string, unknown>; return deviceClass in o ? o[deviceClass] : o.desktop ?? Object.values(o)[0]; }
  return v;
}
const clamp01 = (v: number) => Math.min(1, Math.max(0, v));
const str = (v: unknown, d: string) => (typeof v === "string" && v.trim() ? v.trim() : d);

/** Reads desktop_declaration: unset = the preset; an id of desktop.json `declarations`; a declaration object or its JSON text. An unknown
 *  id falls back to the preset with the advisory desktop_declaration_unknown; an element of a kind this page does not draw is listed in
 *  `unsupported` with the reason. Never throws, whatever the value. */
export function resolveDeclaration(setting: unknown, deviceClass: string, data: DesktopData & DesktopExtra = DESKTOP as DesktopData & DesktopExtra): ResolvedDesktop {
  const presetId = data.declaration_preset ?? "pulpit_graf";
  const decls = data.declarations ?? {};
  const advisories: Advisory[] = [];
  let decl: Declaration | undefined; let origin: ResolvedDesktop["origin"] = "preset"; let id = presetId;
  let raw: unknown = setting;
  if (typeof raw === "string" && raw.trim().startsWith("{")) { try { raw = JSON.parse(raw); } catch { advisories.push({ code: "desktop_declaration_unknown", vars: { id: String(setting).slice(0, 40), preset: presetId } }); raw = undefined; } }
  if (raw && typeof raw === "object" && !Array.isArray(raw)) { decl = raw as Declaration; origin = "inline"; id = typeof decl.id === "string" ? decl.id : "inline"; }
  else if (typeof raw === "string" && raw.trim()) {
    if (Object.prototype.hasOwnProperty.call(decls, raw.trim())) { decl = decls[raw.trim()]; origin = "setting"; id = raw.trim(); }
    else advisories.push({ code: "desktop_declaration_unknown", vars: { id: raw.trim(), preset: presetId } });
  }
  if (!decl) { decl = decls[presetId] ?? { elements: [] }; }
  const known = new Set(data.element_kinds_known ?? ["source", "encoding", "coordinates", "icon", "rules"]);
  const elements = Array.isArray(decl.elements) ? decl.elements.filter((e): e is DeclElement => !!e && typeof e === "object" && !Array.isArray(e)) : [];
  const out: ResolvedDesktop = { id, origin, pins: null, around: null, sources: [], card: null, sections: null, alpha: 0, edgeAlpha: 0, grid: { columns: "auto", cellPx: 96, flow: "row_major" }, icon: null, unsupported: [], advisories };
  let alphaSet = false, edgeSet = false;
  elements.forEach((e, i) => {
    const eid = typeof e.id === "string" ? e.id : `#${i}`; const kind = typeof e.kind === "string" ? e.kind : "";
    if (!known.has(kind)) { out.unsupported.push({ id: eid, kind: kind || "?", reason: "kind_unknown" }); return; }
    if (kind === "source") {
      const persp = str(e.perspective, "");
      if (persp === "pinned_on") out.pins = { focus: str(e.focus, "desktop:main"), seed: Array.isArray(e.seed) ? e.seed.filter((x): x is string => typeof x === "string" && !!x.trim()) : [] };
      else if (persp === "navigate" || persp === "scope_chain" || persp === "around") {
        const depth = num(e.depth); const budget = num(byClass(e.budget, deviceClass));
        // a focus is an address, or {channel, fallback}: the value of a coupling channel (W6), the fallback when no window gives one
        const f = e.focus; const live = f && typeof f === "object" && !Array.isArray(f) ? f as Record<string, unknown> : null;
        out.around = { focus: live ? str(live.fallback, "party:self") : str(f, "party:self"), depth: depth === undefined ? 2 : Math.max(0, Math.floor(depth)), budget: budget === undefined ? 400 : Math.max(0, Math.floor(budget)),
          ...(live ? { channel: str(live.channel, "focus") } : {}) };
      } else if (ICON_SOURCES.includes(persp)) {
        const lim = num(byClass(e.limit, deviceClass));
        out.sources.push({ id: eid, perspective: persp, limit: lim === undefined ? null : Math.max(0, Math.floor(lim)) });
      } else out.unsupported.push({ id: eid, kind, reason: `perspective_unknown:${persp || "?"}` });
    } else if (kind === "encoding") {
      const v = num(byClass(e.value, deviceClass));
      if (e.channel === "alpha") { out.alpha = v === undefined ? 0.12 : clamp01(v); alphaSet = true; }
      else if (e.channel === "edge_alpha") { out.edgeAlpha = v === undefined ? 0.07 : clamp01(v); edgeSet = true; }
      else out.unsupported.push({ id: eid, kind, reason: `channel_unknown:${String(e.channel ?? "?")}` });
    } else if (kind === "coordinates") {
      if (e.system !== undefined && e.system !== "icon_grid") { out.unsupported.push({ id: eid, kind, reason: `system_unknown:${String(e.system)}` }); return; }
      const c = byClass(e.columns, deviceClass); const cn = num(c); const px = num(byClass(e.cell_px, deviceClass));
      out.grid = { columns: cn === undefined ? "auto" : Math.max(1, Math.floor(cn)), cellPx: px === undefined || px <= 0 ? 96 : px, flow: str(e.flow, "row_major") };
    } else if (kind === "icon") {
      const n = num(e.label_lines); out.icon = { labelLines: n === undefined ? 2 : Math.max(0, Math.floor(n)) };
    } else if (kind === "card") {
      const lim = num(byClass(e.limit, deviceClass));
      out.card = { focus: str(e.focus, "party:self"), limit: lim === undefined ? 8 : Math.max(0, Math.floor(lim)) };
    } else if (kind === "affordance") {
      if (Array.isArray(e.sections)) out.sections = e.sections.filter((x): x is string => typeof x === "string" && !!x.trim());
      else out.unsupported.push({ id: eid, kind, reason: "sections_missing" });
    }
  });
  if (out.around && !alphaSet) out.alpha = 0.12;
  if (out.around && !edgeSet) out.edgeAlpha = 0.07;
  if ((out.pins || out.sources.length) && !out.icon) out.icon = { labelLines: 2 };
  if (out.around && out.alpha === 0 && out.edgeAlpha === 0) advisories.push({ code: "nothing_visible" });
  return out;
}

export interface DesktopExtra {
  declaration_preset?: string;
  declarations?: Record<string, Declaration>;
  element_kinds_known?: string[];
  sections_more?: string[];
  long_press_ms?: number;
  open_targets?: { prefixes?: Record<string, OpenRule>; fallback?: OpenRule };
  address_labels?: Record<string, Text | string>;
  glyphs?: Record<string, string>;
  minimized?: { preset?: unknown; known?: string[] };
  declaration_words?: Record<string, { pl?: string[]; en?: string[] }>;
  declaration_clear_words?: string[];
}
export interface OpenRule { kind?: string; focus?: string; conversation?: string }
export const DESKTOP_X = DESKTOP as DesktopData & DesktopExtra;

/** The longest key of `table` that `address` starts with ("" matches everything when present). */
export function longestPrefix<T>(table: Record<string, T> | undefined, address: string): string | undefined {
  let best: string | undefined;
  for (const k of Object.keys(table ?? {})) if (k !== "note" && address.startsWith(k) && (best === undefined || k.length > best.length)) best = k;
  return best;
}

export interface OpenTarget { kind: string; focus?: string; conversation?: string }
/** Which window an address opens (open_targets): $address = the whole address, $rest = what follows the matched prefix. */
export function openTargetOf(address: string, data: DesktopExtra = DESKTOP_X): OpenTarget | null {
  const pre = longestPrefix(data.open_targets?.prefixes, address);
  const rule = pre !== undefined ? data.open_targets!.prefixes![pre] : data.open_targets?.fallback;
  if (!rule || typeof rule.kind !== "string") return null;
  const fill = (v: string) => v.replace("$address", address).replace("$rest", pre !== undefined ? address.slice(pre.length) : address);
  const t: OpenTarget = { kind: fill(rule.kind) };
  if (rule.focus) t.focus = fill(rule.focus);
  if (rule.conversation) t.conversation = fill(rule.conversation);
  return t;
}

export interface PinRow { node: string; address: string; nodeLabel: string; nodeKind: string; order: number; labelOverride?: string | null; glyph?: string | null }
export interface DesktopIcon {
  address: string; node: string; label: string; glyph: string; order: number;
  /** The window kind it opens (null: no rule); status/reason as the kind's entry on the desktop (planned and unopenable greyed, never hidden). */
  kind: string | null; status: "available" | "planned" | "cannot_open"; reason?: string;
  open?: { target: KindTarget; focus?: string; conversation?: string };
  /** R101 D3: which source of the declaration gave the icon (pins, or the id of a source element). */
  source?: string;
  /** R101 D3: an icon that does not open a kind - a minimized window to restore, a layout to load. */
  act?: { restore: string } | { layout: string; own: boolean };
}

const pickText = (t: Text | string | undefined, lang: string): string | undefined => (typeof t === "string" ? t : t ? (lang === "pl" ? t.pl ?? t.en : t.en ?? t.pl) : undefined);

/** The rows of the perspective pinned_on (nodes) joined with the metadata of the links (order, label_override, glyph, address). */
export function pinsFrom(desktopNode: string, nodes: { id: string; kind?: string; label?: string; metadata?: Record<string, unknown> }[], links: { src: string; dst: string; metadata?: Record<string, unknown> }[]): PinRow[] {
  const meta = new Map<string, Record<string, unknown>>();
  for (const l of links) if (l.dst === desktopNode) meta.set(l.src, l.metadata ?? {});
  const rows: PinRow[] = [];
  for (const n of nodes) {
    if (n.id === desktopNode) continue;
    const m = meta.get(n.id) ?? {};
    if (m.trash) continue;   // R101 D4: a pin in the trash (link metadata.trash) is not drawn; it waits in the Trash window
    const address = typeof m.address === "string" && m.address ? m.address : typeof n.metadata?.address === "string" ? (n.metadata.address as string) : (n.label ?? n.id);
    const order = num(m.order);
    rows.push({ node: n.id, address, nodeLabel: n.label ?? address, nodeKind: n.kind ?? "", order: order === undefined ? Number.MAX_SAFE_INTEGER : order,
      labelOverride: typeof m.label_override === "string" ? m.label_override : null, glyph: typeof m.glyph === "string" ? m.glyph : null });
  }
  return rows.sort((a, b) => a.order - b.order || a.address.localeCompare(b.address));
}

/** One pin as an icon: label (label_override, else address_labels, else the node's label), glyph, and what a tap opens. */
export function iconOf(pin: PinRow, lang: string, entries: readonly KindEntry[], data: DesktopExtra = DESKTOP_X, cannotOpen = "—"): DesktopIcon {
  const lp = longestPrefix(data.address_labels as Record<string, Text | string> | undefined, pin.address);
  const label = pin.labelOverride || (lp !== undefined ? pickText((data.address_labels as Record<string, Text | string>)[lp], lang) : undefined) || pin.nodeLabel || pin.address;
  const gp = longestPrefix(data.glyphs, pin.address);
  const glyph = pin.glyph || (gp !== undefined ? data.glyphs![gp] : "•");
  const base = { address: pin.address, node: pin.node, label, glyph, order: pin.order };
  const t = openTargetOf(pin.address, data);
  if (!t) return { ...base, kind: null, status: "cannot_open", reason: cannotOpen };
  const e = entries.find((x) => x.kind === t.kind);
  if (!e) return { ...base, kind: t.kind, status: "cannot_open", reason: cannotOpen };
  if (e.status !== "available" || !e.target) return { ...base, kind: t.kind, status: e.status, reason: e.reason };
  return { ...base, kind: t.kind, status: "available", open: { target: e.target, ...(t.focus ? { focus: t.focus } : {}), ...(t.conversation ? { conversation: t.conversation } : {}) } };
}

export interface GridCell { index: number; col: number; row: number; x: number; y: number }
/** icon_grid: n icons in a grid `width` px wide. columns "auto" = as many cells of cellPx as fit (at least 1); a number is taken as it is
 *  (at least 1), also when the cells end up narrower than a finger (that is the advisory icons_tiny, not a refusal). flow row_major
 *  (preset) fills rows, column_major fills columns; any other flow is read as row_major. */
export function iconGrid(n: number, width: number, grid: { columns: number | "auto"; cellPx: number; flow: string }, touchPx = 44): { columns: number; rows: number; cellW: number; cells: GridCell[]; advisories: Advisory[] } {
  const count = Math.max(0, Math.floor(Number.isFinite(n) ? n : 0));
  const w = Number.isFinite(width) && width > 0 ? width : grid.cellPx;
  const cols = Math.max(1, grid.columns === "auto" ? Math.floor(w / Math.max(1, grid.cellPx)) : Math.floor(grid.columns));
  const rows = Math.max(count ? 1 : 0, Math.ceil(count / cols));
  const cellW = w / cols;
  const cells: GridCell[] = [];
  for (let i = 0; i < count; i++) {
    const col = grid.flow === "column_major" ? Math.floor(i / Math.max(1, rows)) : i % cols;
    const row = grid.flow === "column_major" ? i % Math.max(1, rows) : Math.floor(i / cols);
    cells.push({ index: i, col, row, x: col * cellW, y: row * grid.cellPx });
  }
  const advisories: Advisory[] = cellW < touchPx && count ? [{ code: "icons_tiny", vars: { px: String(Math.round(cellW)) } }] : [];
  return { columns: cols, rows, cellW, cells, advisories };
}

export interface BackNode { address: string; kind?: string }
export interface BackEdge { src: string; dst: string }
/** The faint graph: positions in [0,1]² for the nodes, and the edges whose BOTH ends are among them (real edges only - an edge to a node
 *  outside the budget is not drawn, never invented). Deterministic (seeded by the address), a few rounds of a spring layout. */
export function backdropLayout(nodes: readonly BackNode[], edges: readonly BackEdge[], rounds = 80): { pos: Map<string, { x: number; y: number }>; edges: BackEdge[] } {
  const ids = [...new Set(nodes.map((n) => n.address))];
  const at = new Map(ids.map((a, i) => [a, i]));
  const kept = edges.filter((e) => e.src !== e.dst && at.has(e.src) && at.has(e.dst));
  const hash = (s: string, k: number) => { let h = 2166136261 ^ k; for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 16777619); return ((h >>> 0) % 10007) / 10007; };
  const x = ids.map((a) => 0.1 + 0.8 * hash(a, 1)); const y = ids.map((a) => 0.1 + 0.8 * hash(a, 2));
  const n = ids.length;
  if (n > 1) {
    const k = Math.sqrt(1 / n);
    for (let r = 0; r < rounds; r++) {
      const t = 0.1 * (1 - r / rounds) + 0.005;
      const dx = new Float64Array(n), dy = new Float64Array(n);
      for (let i = 0; i < n; i++) for (let j = i + 1; j < n; j++) {
        let ex = x[i] - x[j], ey = y[i] - y[j]; let d2 = ex * ex + ey * ey;
        if (d2 < 1e-9) { ex = (hash(ids[i], r) - 0.5) * 1e-3; ey = (hash(ids[j], r) - 0.5) * 1e-3; d2 = ex * ex + ey * ey + 1e-9; }
        const f = (k * k) / d2; dx[i] += ex * f; dy[i] += ey * f; dx[j] -= ex * f; dy[j] -= ey * f;
      }
      for (const e of kept) {
        const i = at.get(e.src)!, j = at.get(e.dst)!; const ex = x[i] - x[j], ey = y[i] - y[j]; const d = Math.sqrt(ex * ex + ey * ey) + 1e-9;
        const f = d / k; dx[i] -= ex * f; dy[i] -= ey * f; dx[j] += ex * f; dy[j] += ey * f;
      }
      for (let i = 0; i < n; i++) {
        const l = Math.sqrt(dx[i] * dx[i] + dy[i] * dy[i]) + 1e-9; const s = Math.min(l, t) / l;
        x[i] = Math.min(0.97, Math.max(0.03, x[i] + dx[i] * s)); y[i] = Math.min(0.97, Math.max(0.03, y[i] + dy[i] * s));
      }
    }
  }
  return { pos: new Map(ids.map((a, i) => [a, { x: x[i], y: y[i] }])), edges: kept };
}

// ---------- R101 D3: the other concepts as presets (PULPIT §3.1, §3.2, §8) ----------

const glyphOf = (address: string, data: DesktopExtra) => { const gp = longestPrefix(data.glyphs, address); return gp !== undefined ? data.glyphs![gp] : "•"; };

export interface SourceContext {
  entries: readonly KindEntry[];
  minimized: readonly { id: string; title: string }[];
  layouts: { shipped: readonly { id: string; label: string }[]; own: readonly { id: string; label: string }[] };
  conversations: readonly { id: string; title: string }[];
}
/** The icons of one source of the declaration. window_kinds: every kind of window_kinds.json as a tile (planned and unopenable greyed
 *  with their reason, never hidden); minimized_windows: the minimized windows (tap restores); layouts: the shipped and own layouts (tap
 *  loads); recent_conversations: the latest conversations (tap opens it in the chat). `limit` null = all. Total: an unknown perspective
 *  gives no icon (resolveDeclaration already listed it as unsupported). */
export function sourceIcons(src: IconSource, ctx: SourceContext, _lang: string, data: DesktopExtra = DESKTOP_X): DesktopIcon[] {
  const cut = <T,>(xs: readonly T[]) => (src.limit === null ? [...xs] : xs.slice(0, src.limit));
  const mk = (address: string, label: string, i: number) => ({ address, node: `${src.id}:${address}`, label, glyph: glyphOf(address, data), order: i, source: src.id });
  if (src.perspective === "window_kinds") return cut(ctx.entries).map((e, i) => {
    const b = mk(`kind:${e.kind}`, e.label, i);
    return e.status === "available" && e.target ? { ...b, kind: e.kind, status: "available" as const, open: { target: e.target } } : { ...b, kind: e.kind, status: e.status, reason: e.reason };
  });
  if (src.perspective === "minimized_windows") return cut(ctx.minimized).map((w, i) => ({ ...mk(`window:${w.id}`, w.title, i), kind: null, status: "available" as const, act: { restore: w.id } }));
  if (src.perspective === "layouts") return cut([...ctx.layouts.shipped.map((l) => ({ ...l, own: false })), ...ctx.layouts.own.map((l) => ({ ...l, own: true }))])
    .map((l, i) => ({ ...mk(`layout:${l.id}`, l.label, i), kind: null, status: "available" as const, act: { layout: l.id, own: l.own } }));
  if (src.perspective === "recent_conversations") return cut(ctx.conversations).map((c, i) => {
    const t = openTargetOf(`conv:${c.id}`, data); const e = t ? ctx.entries.find((x) => x.kind === t.kind) : undefined;
    const b = mk(`conv:${c.id}`, c.title || c.id, i);
    return e?.status === "available" && e.target ? { ...b, kind: e.kind, status: "available" as const, open: { target: e.target, conversation: c.id } } : { ...b, kind: t?.kind ?? null, status: "cannot_open" as const, reason: e?.reason };
  });
  return [];
}

/** Every icon of the desktop in the order of the declaration: the pins first (when declared), then each source in its order. */
export function allIcons(decl: ResolvedDesktop, pinIcons: readonly DesktopIcon[], ctx: SourceContext, lang: string, data: DesktopExtra = DESKTOP_X): DesktopIcon[] {
  const out: DesktopIcon[] = decl.pins ? pinIcons.map((p) => ({ ...p, source: p.source ?? "pins" })) : [];
  for (const s of decl.sources) out.push(...sourceIcons(s, ctx, lang, data));
  return out;
}

/** The focus of a live background graph (pulpit_graf_zywy): the value of the coupling channel; else the same parameter of the focused
 *  window, else of the last window that has one (a navigator writes its focus there); else the declaration's fallback. */
export function liveFocus(around: NonNullable<ResolvedDesktop["around"]>, values: { windows: Record<string, Record<string, unknown>>; channels: Record<string, unknown> } | null | undefined,
  windows: readonly { id: string }[] = [], focused: string | null = null): { focus: string; from: "channel" | "window" | "fallback"; window?: string } {
  if (!around.channel || !values) return { focus: around.focus, from: "fallback" };
  const ch = values.channels?.[around.channel];
  if (typeof ch === "string" && ch) return { focus: ch, from: "channel" };
  const ids = windows.map((w) => w.id);
  const order = focused && ids.includes(focused) ? [focused, ...ids.filter((x) => x !== focused).reverse()] : [...ids].reverse();
  for (const id of order) { const v = values.windows?.[id]?.[around.channel]; if (typeof v === "string" && v) return { focus: v, from: "window", window: id }; }
  return { focus: around.focus, from: "fallback" };
}

/** desktop_minimized (PULPIT §8): where a minimized window shows on the desktop - shelf (the list section), icons (a strip at the bottom of
 *  the desktop), both, none [examples]. Unset = the preset per device class; an unknown value = the preset with an advisory. */
export function minimizedMode(setting: unknown, deviceClass: string, data: DesktopExtra = DESKTOP_X): { mode: string; advisory?: Advisory } {
  const preset = String(byClass(data.minimized?.preset, deviceClass) ?? "shelf");
  const known = data.minimized?.known ?? ["shelf", "icons", "both", "none"];
  if (setting === undefined || setting === null || setting === "") return { mode: preset };
  const v = String(setting).trim();
  return known.includes(v) ? { mode: v } : { mode: preset, advisory: { code: "desktop_minimized_unknown", vars: { value: v.slice(0, 40), preset } } };
}

const fold = (s: string) => s.normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/ł/g, "l").replace(/Ł/g, "L").toLowerCase().replace(/[^a-z0-9_:]+/g, " ").trim();
/** The declaration a word names (the sentence „pulpit jako ikony”): an id of `declarations` (with or without pulpit_), its label, or a
 *  word of declaration_words; a clear word (default, domyślny) = null (the preset). Unknown: undefined (the page says which it knows). */
export function declarationOfWord(word: string, data: DesktopExtra = DESKTOP_X): string | null | undefined {
  const w = fold(word); if (!w) return undefined;
  if ((data.declaration_clear_words ?? []).some((c) => fold(c) === w)) return null;
  const ids = Object.keys(data.declarations ?? {});
  for (const id of ids) if (fold(id) === w || fold(id.replace(/^pulpit_/, "")) === w || fold(id.replace(/^pulpit_/, "").replace(/_/g, " ")) === w) return id;
  for (const id of ids) {
    const words = data.declaration_words?.[id]; const label = data.declarations![id].label;
    const all = [...(words?.pl ?? []), ...(words?.en ?? []), ...(typeof label === "string" ? [label] : [label?.pl ?? "", label?.en ?? ""])];
    if (all.some((x) => x && fold(x) === w)) return id;
  }
  return undefined;
}
