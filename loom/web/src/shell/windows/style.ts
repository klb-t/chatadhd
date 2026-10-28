// W9 (OKNA §7.2, D14): the style of a window = an application profile (loom.application_profile/1) applied to one window or to a whole
// layout, as data. `window.profile` → `layout.profile` → the person's profile → the fallback (preset `loom-default`, data/styles.json).
// A reference is `{id, revision}` (revision null = the newest revision of that id known here); a reference to a profile not known on this
// device is never dropped silently: resolution falls through to the next level and says so (advisory `profile_missing`).
// The layout fields of a profile (`sidebar.side/width`) are a SUGGESTION; where they apply is `profile_layout_fields` (preset `layout`):
// `layout` - the profile of the layout (or the person's) suggests the side of the conversation list; `window` - the profile assigned to
// the list window itself suggests it too (window → layout → person); `off` - nobody reads them. A suggestion that would move a dock
// raises the non-blocking advisory `profile_moves_dock` with the fix „Przestaw” (mirror of the split, one journal entry, undoable).
// UI17: nothing here reads or writes a model, permissions, privacy or the selection of context - only `profile` of a window/layout.
import stylesFile from "../data/styles.json";
import type { Tree, WLayout, WState, Win } from "./model";
import type { WinAdvisory } from "./arrange";

type Words = { pl?: string; en?: string };
export const STYLES = stylesFile as unknown as {
  fallback_profile?: string; layout_fields_default?: string; layout_fields_known?: string[]; words: Record<string, Words>;
};
export interface ProfileRef { id: string; revision: number | null }
/** The part of an application profile this module reads (the whole document is passed through untouched). */
export interface StyleDoc { id: string; profile_revision: number; label?: string; presentation?: { sidebar?: { side?: string; width?: number } } }
export type StyleSource = "window" | "layout" | "person" | "default" | "none";
export interface Resolved<D extends StyleDoc = StyleDoc> { profile: D | null; source: StyleSource; ref: ProfileRef | null; missing: ProfileRef[] }
export type LayoutFieldsMode = "layout" | "window" | "off";

/** A reference from any stored shape: "id", "id@3", {id, revision|profile_revision}; anything else is null (= inherit). */
export function refOf(v: unknown): ProfileRef | null {
  if (typeof v === "string") {
    const m = /^(.+?)(?:@(\d+))?$/.exec(v.trim());
    return m && m[1] ? { id: m[1], revision: m[2] !== undefined ? Number(m[2]) : null } : null;
  }
  if (v && typeof v === "object" && !Array.isArray(v)) {
    const o = v as Record<string, unknown>;
    if (typeof o.id !== "string" || !o.id) return null;
    const r = o.revision ?? o.profile_revision;
    return { id: o.id, revision: typeof r === "number" && Number.isInteger(r) ? r : null };
  }
  return null;
}
export const refOfDoc = (d: StyleDoc): ProfileRef => ({ id: d.id, revision: d.profile_revision });
export const sameRef = (a: ProfileRef | null, b: ProfileRef | null) => (a === null || b === null ? a === b : a.id === b.id && a.revision === b.revision);
export const refKey = (r: ProfileRef | null) => (r ? `${r.id}@${r.revision ?? "*"}` : "");

/** The document a reference names: that exact revision, or (revision null) the newest of the id; null when unknown here. */
export function findProfile<D extends StyleDoc>(docs: readonly D[], ref: ProfileRef | null): D | null {
  if (!ref) return null;
  const same = docs.filter((d) => d && d.id === ref.id && Number.isFinite(d.profile_revision));
  if (ref.revision !== null) return same.find((d) => d.profile_revision === ref.revision) ?? null;
  return same.reduce<D | null>((best, d) => (!best || d.profile_revision > best.profile_revision ? d : best), null);
}

/** The style of a window (or, windowId null, of the layout): the first level with a known profile wins; unknown references are reported. */
export function resolveStyle<D extends StyleDoc>(layout: WLayout, windowId: string | null, person: ProfileRef | null, docs: readonly D[], fallback: string | null = STYLES.fallback_profile ?? "loom-default"): Resolved<D> {
  const w = windowId ? layout.windows.find((x) => x.id === windowId) : undefined;
  const levels: [StyleSource, ProfileRef | null][] = [["window", w ? refOf(w.profile) : null], ["layout", refOf(layout.profile)], ["person", person], ["default", fallback ? { id: fallback, revision: null } : null]];
  const missing: ProfileRef[] = [];
  for (const [source, ref] of levels) {
    if (!ref) continue;
    const profile = findProfile(docs, ref);
    if (profile) return { profile, source, ref, missing };
    missing.push(ref);
  }
  return { profile: null, source: "none", ref: null, missing };
}

/** Same layout object when nothing changes (no journal entry); otherwise one entry `op`. */
function entry(s: WState, layout: WLayout, op: string): WState {
  if (layout === s.layout) return s;
  const past = [...s.past, { op, layout: s.layout }];
  while (past.length > s.limit) past.shift();
  return { layout, past, future: [], limit: s.limit };
}
/** Assign a style to one window (null = inherit from the layout). One journal entry; an unknown window or the same value changes nothing. */
export function setWindowProfile(s: WState, id: string, ref: ProfileRef | null): WState {
  const w = s.layout.windows.find((x) => x.id === id);
  if (!w || sameRef(refOf(w.profile), ref)) return s;
  const windows = s.layout.windows.map((x) => (x.id === id ? { ...x, profile: ref ? { ...ref } : null } : x));
  return entry(s, { ...s.layout, windows }, "window.profile");
}
/** Assign a style to the whole layout (null = inherit from the person). One journal entry. */
export function setLayoutProfile(s: WState, ref: ProfileRef | null): WState {
  if (sameRef(refOf(s.layout.profile), ref)) return s;
  return entry(s, { ...s.layout, profile: ref ? { ...ref } : null }, "layout.profile");
}

/** profile_layout_fields read as an open value: a known mode, or the preset with `unknown` set (the caller says it, nothing refuses). */
export function layoutFieldsMode(v: unknown): { mode: LayoutFieldsMode; unknown: string | null } {
  const preset = (STYLES.layout_fields_default ?? "layout") as LayoutFieldsMode;
  if (v === undefined || v === null || v === "") return { mode: preset, unknown: null };
  const s = String(v).trim().toLowerCase();
  if (s === "layout" || s === "window" || s === "off") return { mode: s, unknown: null };
  return { mode: preset, unknown: String(v) };
}

/** Where a leaf sits along the outermost horizontal split that separates it: side, share of the width, path of that split ("" = root). */
export function leafSide(t: Tree, leaf: string, path = ""): { side: "left" | "right"; share: number; path: string } | null {
  if ("leaf" in t) return null;
  const inA = hasLeaf(t.a, leaf), inB = !inA && hasLeaf(t.b, leaf);
  if (!inA && !inB) return null;
  if (t.split === "h") return { side: inA ? "left" : "right", share: inA ? t.ratio : 1 - t.ratio, path };
  return leafSide(inA ? t.a : t.b, leaf, path + (inA ? "a" : "b"));
}
function hasLeaf(t: Tree, leaf: string): boolean { return "leaf" in t ? t.leaf === leaf : hasLeaf(t.a, leaf) || hasLeaf(t.b, leaf); }
function mirrorAt(t: Tree, path: string): Tree {
  if ("leaf" in t) return t;
  if (!path) return { ...t, a: t.b, b: t.a, ratio: 1 - t.ratio };
  return path[0] === "a" ? { ...t, a: mirrorAt(t.a, path.slice(1)) } : { ...t, b: mirrorAt(t.b, path.slice(1)) };
}

const say = (w: Words | undefined, lang: string) => (lang === "pl" ? w?.pl ?? w?.en : w?.en ?? w?.pl) ?? "";
const fill = (w: Words | undefined, vars: Record<string, string>): Words => {
  const f = (s?: string) => (s === undefined ? undefined : s.replace(/\{(\w+)\}/g, (_, k) => vars[k] ?? `{${k}}`));
  return { pl: f(w?.pl), en: f(w?.en) };
};
const sideWords = (side: string): { pl: string; en: string } => ({ pl: say(STYLES.words[`side_${side}`], "pl") || side, en: say(STYLES.words[`side_${side}`], "en") || side });

/** The windows whose side the profile suggests (preset: the conversation list), docked in a leaf and drawn (a list collapsed to a handle,
 * the drawer of the layout `czat`, has no side on the screen to move - offering „Przestaw” there would be a control that does nothing). */
const sidebarWindows = (l: WLayout): Win[] => l.windows.filter((w) => w.kind === "conversation_list" && w.dock?.leaf && !w.collapsed && w.mode !== "minimized");

/**
 * The advisories of styles: `profile_moves_dock` (the suggested side of the list differs from where it is) and `profile_missing` (a
 * reference to a profile not known here). Nothing is changed; the fix `profile_side` mirrors the split (applyStyleFix).
 */
export function styleAdvisories<D extends StyleDoc>(l: WLayout, person: ProfileRef | null, docs: readonly D[], modeValue: unknown): WinAdvisory[] {
  const out: WinAdvisory[] = [];
  const { mode, unknown } = layoutFieldsMode(modeValue);
  if (unknown !== null) out.push({ code: "profile_layout_fields_unknown", fingerprint: unknown, evidence: { value: unknown, used: mode }, text: fill(STYLES.words.unknown_mode, { value: unknown }), question: {} });
  const seen = new Set<string>();
  const missingOf = (r: Resolved<D>, window?: string) => {
    for (const m of r.missing) {
      const k = refKey(m);   // a reference unknown here is said once, whichever window or level names it
      if (seen.has(k)) continue; seen.add(k);
      out.push({ code: "profile_missing", fingerprint: refKey(m), window, evidence: { ref: m, used: r.profile ? refOfDoc(r.profile) : null },
        text: fill(STYLES.words.missing, { id: m.revision === null ? m.id : `${m.id}@${m.revision}`, used: r.profile?.label ?? r.profile?.id ?? "—" }), question: {} });
    }
  };
  for (const w of l.windows) if (w.profile !== null && w.profile !== undefined) missingOf(resolveStyle(l, w.id, person, docs), w.id);
  if (l.profile !== null && l.profile !== undefined) missingOf(resolveStyle(l, null, person, docs));
  if (mode === "off") return out;
  for (const w of sidebarWindows(l)) {
    const r = resolveStyle(l, mode === "window" ? w.id : null, person, docs);
    // only an explicit choice suggests a move: the person's or the fallback profile never moves a dock the layout preset placed
    if (!r.profile || (r.source !== "layout" && r.source !== "window")) continue;
    const want = r.profile.presentation?.sidebar?.side;
    const at = leafSide(l.tree, w.dock!.leaf);
    if ((want !== "left" && want !== "right") || !at || at.side === want) continue;
    const label = r.profile.label ?? r.profile.id;
    const sw = sideWords(want), sn = sideWords(at.side);
    out.push({
      code: "profile_moves_dock", fingerprint: `${refKey(refOfDoc(r.profile))}|${want}|${at.path}`, window: w.id,
      evidence: { profile: refOfDoc(r.profile), source: r.source, mode, want, now: at.side, split: at.path, share: at.share, width_px: r.profile.presentation?.sidebar?.width ?? null },
      text: { pl: fill(STYLES.words.moves_dock, { label, want: sw.pl, now: sn.pl }).pl, en: fill(STYLES.words.moves_dock, { label, want: sw.en, now: sn.en }).en },
      question: STYLES.words.moves_dock_question ?? {}, fix: { id: "profile_side", label: STYLES.words.moves_dock_fix ?? { pl: "Przestaw", en: "Move it" } },
    });
  }
  return out;
}

/** The fix of `profile_moves_dock`: mirror the split that separates the list (side swapped, share kept), one journal entry. */
export function applyStyleFix(s: WState, a: WinAdvisory): WState {
  if (a.fix?.id !== "profile_side" || typeof a.evidence.split !== "string") return s;
  const w = a.window ? s.layout.windows.find((x) => x.id === a.window) : undefined;
  const at = w?.dock?.leaf ? leafSide(s.layout.tree, w.dock.leaf) : null;
  if (!at || at.path !== a.evidence.split || at.side === a.evidence.want) return s;
  return entry(s, { ...s.layout, tree: mirrorAt(s.layout.tree, at.path) }, "profile.move_dock");
}

/** Words of the inherit entry for a window or the layout ("dziedzicz (z układu: …)"). */
export function inheritLabel<D extends StyleDoc>(l: WLayout, windowId: string | null, person: ProfileRef | null, docs: readonly D[], lang: string): string {
  const base = windowId ? resolveStyle({ ...l, windows: l.windows.map((w) => (w.id === windowId ? { ...w, profile: null } : w)) }, windowId, person, docs)
    : resolveStyle({ ...l, profile: null }, null, person, docs);
  const label = base.profile?.label ?? base.profile?.id ?? "—";
  const from = say(STYLES.words[base.source === "layout" ? "from_layout" : base.source === "person" ? "from_person" : "from_default"], lang).replace("{label}", label);
  return say(STYLES.words.inherit, lang).replace("{from}", from);
}
export { say as sayStyle };
