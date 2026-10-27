// W5 (OKNA §2.5, §6.1): named layouts, the active layout per device class and the persisted journal of layout changes. Pure: the values
// are the settings fields `window_layouts`, `window_layout_active`, `window_layout_journal` (settings_fields.pack); App.tsx reads them
// from the config and writes them back with `setConfigKey`. Every reader is lenient: a value of another shape (a JSON text, an object
// instead of a list, rubbish) gives what can be read, never an exception.
import layoutsFile from "../data/layouts.json";
import { check, createState, type JournalEntry, type WLayout, type WState } from "./model";

export const NAMED_SCHEMA = "loom.window_layout/2";
export interface NamedLayout { schema: string; id: string; label: string; layout: WLayout; saved: string; device_class?: string }
/** A device class: the first rule whose width range holds the screen wins (preset `device_classes` in layouts.json; an open list). */
export interface DeviceClassRule { id: string; max_width?: number; min_width?: number }
interface NamedData { device_classes?: DeviceClassRule[]; journal_limit?: number; layout_reset?: Record<string, string> }
const DATA = layoutsFile as NamedData;
const PRESET_CLASSES: DeviceClassRule[] = [{ id: "phone", max_width: 599 }, { id: "desktop" }];

export function deviceClassOf(width: number, rules: DeviceClassRule[] = DATA.device_classes ?? PRESET_CLASSES): string {
  for (const r of rules) {
    if (typeof r?.id !== "string") continue;
    if ((typeof r.max_width !== "number" || width <= r.max_width) && (typeof r.min_width !== "number" || width >= r.min_width)) return r.id;
  }
  return "any";
}

const parse = (v: unknown): unknown => {
  if (typeof v !== "string") return v;
  try { return JSON.parse(v); } catch { return null; }
};
const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);
/** Folded form of a name: no case, no diacritics, no quotes around it („praca”, «praca», "praca" and Praca are one name). */
export const foldName = (t: string): string => t.normalize("NFD").replace(/[̀-ͯ]/g, "").replace(/ł/g, "l").replace(/Ł/g, "L")
  .toLowerCase().replace(/[„”“"«»‚‘’'`]/g, "").replace(/\s+/g, " ").trim();
const slug = (label: string): string => "u_" + (foldName(label).replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "") || "uklad");

function validLayout(v: unknown): v is WLayout {
  if (!isObj(v) || typeof v.id !== "string" || !Array.isArray(v.windows) || !v.tree || !isObj(v.leaves)) return false;
  try { return check(v as unknown as WLayout).length === 0; } catch { return false; }
}

/** `window_layouts`: a list of `loom.window_layout/2` documents (or `{layouts: [...]}`, or its JSON text). Unreadable entries are skipped. */
export function readNamed(value: unknown): NamedLayout[] {
  const v = parse(value);
  const list = Array.isArray(v) ? v : isObj(v) && Array.isArray(v.layouts) ? v.layouts : [];
  const out: NamedLayout[] = [];
  for (const e of list) {
    if (!isObj(e) || typeof e.label !== "string" || !e.label.trim() || !validLayout(e.layout)) continue;
    const id = typeof e.id === "string" && e.id ? e.id : slug(e.label);
    if (out.some((x) => x.id === id)) continue;
    out.push({ schema: NAMED_SCHEMA, id, label: e.label, layout: e.layout, saved: typeof e.saved === "string" ? e.saved : "",
      ...(typeof e.device_class === "string" ? { device_class: e.device_class } : {}) });
  }
  return out;
}

/** A name of a sentence or a menu (id, label in any case, with or without quotes) to a saved layout. */
export function findNamed(list: NamedLayout[], name: unknown): NamedLayout | undefined {
  if (typeof name !== "string" || !name.trim()) return undefined;
  const f = foldName(name);
  return list.find((x) => x.id === name) ?? list.find((x) => foldName(x.label) === f) ?? list.find((x) => x.id === slug(name));
}

/** Saves the layout under a label; the same label (folded) replaces the earlier one in place. */
export function saveNamed(list: NamedLayout[], label: string, layout: WLayout, now: string, deviceClass?: string): { list: NamedLayout[]; entry: NamedLayout; replaced: boolean } {
  const clean = label.trim().replace(/^[„”“"«»]+|[„”“"«»]+$/g, "").trim() || label.trim();
  const before = findNamed(list, clean);
  const entry: NamedLayout = { schema: NAMED_SCHEMA, id: before?.id ?? unique(list, slug(clean)), label: clean, layout: JSON.parse(JSON.stringify(layout)), saved: now,
    ...(deviceClass ? { device_class: deviceClass } : {}) };
  return { list: before ? list.map((x) => (x.id === before.id ? entry : x)) : [...list, entry], entry, replaced: !!before };
}
function unique(list: NamedLayout[], id: string): string {
  let n = 2, out = id;
  while (list.some((x) => x.id === out)) out = `${id}-${n++}`;
  return out;
}

/** Renames; a label another layout already has is refused (`conflict`), the list stays. */
export function renameNamed(list: NamedLayout[], id: string, label: string): { list: NamedLayout[]; conflict?: string } {
  const clean = label.trim();
  if (!clean) return { list };
  const other = findNamed(list, clean);
  if (other && other.id !== id) return { list, conflict: other.label };
  return { list: list.map((x) => (x.id === id ? { ...x, label: clean } : x)) };
}
export const deleteNamed = (list: NamedLayout[], id: string): NamedLayout[] => list.filter((x) => x.id !== id);

/** `window_layout_active`: `{<device class>: <layout id>}` (or its JSON text, or a bare id = every class). */
export function readActive(value: unknown, cls: string): string | null {
  const v = parse(value);
  if (typeof v === "string" && v) return v;
  if (!isObj(v)) return null;
  const id = v[cls] ?? v.any;
  return typeof id === "string" && id ? id : null;
}
export function writeActive(value: unknown, cls: string, id: string | null): Record<string, string> {
  const v = parse(value);
  const out: Record<string, string> = {};
  if (isObj(v)) for (const [k, x] of Object.entries(v)) if (typeof x === "string") out[k] = x;
  else if (typeof v === "string" && v) out.any = v;
  if (id) out[cls] = id; else delete out[cls];
  return out;
}

/** The last own layout (layout_mode=windows): the active one of this class if it is own, else the most recently saved, else null. */
export function lastOwn(list: NamedLayout[], activeId: string | null): NamedLayout | null {
  const active = activeId ? list.find((x) => x.id === activeId) : undefined;
  if (active) return active;
  return [...list].sort((a, b) => (a.saved < b.saved ? 1 : a.saved > b.saved ? -1 : 0))[0] ?? null;
}

/** The bound of the journal: the value's own `limit` (any number ≥ 0), else the preset `journal_limit` of layouts.json. */
export function journalLimit(value: unknown, preset: number = DATA.journal_limit ?? 50): number {
  const v = parse(value);
  const n = isObj(v) ? Number(v.limit) : NaN;
  return Number.isFinite(n) && n >= 0 ? Math.floor(n) : preset;
}
const entries = (v: unknown, limit: number): JournalEntry[] => {
  const list = (Array.isArray(v) ? v : []).filter((e): e is JournalEntry => isObj(e) && typeof e.op === "string" && validLayout(e.layout));
  return limit > 0 ? list.slice(-limit) : [];
};
/** `window_layout_journal`: `{limit?, <device class>: {past, future}}` - the undo/redo journal of each device class. */
export function readJournal(value: unknown, cls: string): { past: JournalEntry[]; future: JournalEntry[]; limit: number } {
  const v = parse(value);
  const limit = journalLimit(v);
  const j = isObj(v) && isObj(v[cls]) ? v[cls] as Record<string, unknown> : {};
  return { past: limit === 0 ? [] : entries(j.past, limit), future: limit === 0 ? [] : entries(j.future, limit), limit };
}
export function writeJournal(value: unknown, cls: string, s: Pick<WState, "past" | "future">): Record<string, unknown> {
  const v = parse(value);
  const out: Record<string, unknown> = isObj(v) ? { ...v } : {};
  const limit = journalLimit(out);
  out[cls] = { past: limit === 0 ? [] : s.past.slice(-limit), future: limit === 0 ? [] : s.future.slice(-limit) };
  return out;
}

/** Replaces the whole layout as ONE journal entry (layout.apply / layout.reset): undo brings the previous layout back whole. */
export function replaceLayout(s: WState, layout: WLayout, op: string): WState {
  const next = createState(layout, s.limit).layout;
  if (JSON.stringify(next) === JSON.stringify(s.layout)) return s;
  const past = [...s.past, { op, layout: s.layout }];
  while (past.length > s.limit) past.shift();
  return { layout: next, past, future: [], limit: s.limit };
}

/** layout.reset: which shipped layout a class goes back to (preset `layout_reset`: `same` = the shipped form of the layout drawn now). */
export function resetTarget(currentId: string, cls: string, shipped: Readonly<Record<string, WLayout>>, data: NamedData = DATA): string {
  const want = data.layout_reset?.[cls] ?? data.layout_reset?.any ?? "same";
  if (want !== "same" && shipped[want]) return want;
  return shipped[currentId] ? currentId : Object.keys(shipped)[0];
}

/** A name of a sentence or a menu to a shipped layout (its id, an alias of layouts.json, or its label in any language), or null. */
export function shippedId(name: unknown, shipped: Readonly<Record<string, WLayout>>, aliases: Record<string, string> = (layoutsFile as { aliases?: Record<string, string> }).aliases ?? {}): string | null {
  if (typeof name !== "string" || !name.trim()) return null;
  const f = foldName(name);
  if (shipped[name]) return name;
  if (shipped[f]) return f;
  const alias = aliases[name] ?? aliases[f];
  if (alias && shipped[alias]) return alias;
  for (const [id, l] of Object.entries(shipped)) {
    const labels = typeof l.label === "string" ? [l.label] : l.label && typeof l.label === "object" ? Object.values(l.label as Record<string, unknown>) : [];
    if (labels.some((x) => typeof x === "string" && foldName(x) === f)) return id;
  }
  return null;
}
export function labelOf(l: WLayout | undefined, lang: string): string {
  if (!l) return "";
  if (typeof l.label === "string") return l.label;
  const m = l.label as Record<string, unknown> | undefined;
  const v = m?.[lang] ?? m?.en ?? m?.pl;
  return typeof v === "string" ? v : l.id;
}
