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
interface NamedData { device_classes?: DeviceClassRule[]; journal_limit?: number; layout_reset?: Record<string, string>; journal_writers?: number; journal_inherit?: string[]; active_levels?: string[] }
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
  try { return JSON.parse(v); } catch { return v; }   // a bare word (a layout id) stays a word
};
const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);
/** Folded form of a name: no case, no diacritics, no quotes around it („praca”, «praca», "praca" and Praca are one name). */
export const foldName = (t: string): string => t.normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/ł/g, "l").replace(/Ł/g, "L")
  .toLowerCase().replace(/[„”“"«»‚‘’'`]/g, "").replace(/\s+/g, " ").trim();
const slug = (label: string): string => "u_" + (foldName(label).replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "") || "uklad");

function validLayout(v: unknown): v is WLayout {
  if (!isObj(v) || typeof v.id !== "string" || !Array.isArray(v.windows) || !v.tree || !isObj(v.leaves)) return false;
  try { return check(v as unknown as WLayout).length === 0; } catch { return false; }
}

/** `window_layouts`: a list of `loom.window_layout/2` documents (or `{layouts: [...]}`, or its JSON text). Unreadable entries are skipped. */
export function readNamed(value: unknown): NamedLayout[] {
  const v = parse(value);
  // W5b: the stored form is an object id → layout (written by merge patches, so two tabs never drop each other's layouts); the W5
  // list (and `{layouts: [...]}`) is still read
  const pairs: [string | null, unknown][] = Array.isArray(v) ? v.map(e => [null, e]) : isObj(v) && Array.isArray(v.layouts) ? (v.layouts as unknown[]).map(e => [null, e])
    : isObj(v) ? Object.entries(v) : [];
  const out: NamedLayout[] = [];
  for (const [key, e] of pairs) {
    if (!isObj(e) || typeof e.label !== "string" || !e.label.trim() || !validLayout(e.layout)) continue;
    const id = typeof e.id === "string" && e.id ? e.id : key ?? slug(e.label);
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
/** True when the stored value already has the W5b form (an object id → layout), not the W5 list, its JSON text or `{layouts}`. */
export function isNamedMap(value: unknown): boolean {
  const v = parse(value);
  return isObj(v) && !Array.isArray(v.layouts);
}
/** W5b: the merge patch of `window_layouts` from what this tab read (`before`) to what it wants (`after`): changed or new layouts as
 *  members, deleted ones as null; the layouts this tab never saw (saved meanwhile by another tab) are not in the patch, so they stay.
 *  A stored W5 list is migrated by the first write: the patch then carries every layout (it replaces the list as a whole, nothing is
 *  dropped - a layout removed in this very write is the only one missing). */
export function namedPatch(stored: unknown, before: NamedLayout[], after: NamedLayout[]): Record<string, unknown> {
  const patch: Record<string, unknown> = {};
  const migrate = !isNamedMap(stored) && parse(stored) != null && parse(stored) !== "";
  for (const e of after) {
    const was = before.find((x) => x.id === e.id);
    if (migrate || !was || JSON.stringify(was) !== JSON.stringify(e)) patch[e.id] = e;
  }
  if (!migrate) for (const e of before) if (!after.some((x) => x.id === e.id)) patch[e.id] = null;
  return patch;
}

/** `window_layout_active`: `{<device class>: <layout id>}` (or its JSON text, or a bare id = every class). */
export function readActive(value: unknown, cls: string, installation?: string): string | null {
  const v = parse(value);
  // W5b (D19): this installation's own value wins; an empty id there = no own layout on this installation
  const mine = installation && isObj(v) && isObj(v.installations) ? (v.installations as Record<string, unknown>)[installation] : undefined;
  if (isObj(mine)) { const id = mine[cls] ?? mine.any; if (typeof id === "string") return id || null; }
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

const levelsOf = (v: unknown, preset: string[]): string[] => {
  const own = isObj(v) ? v.levels : undefined;
  return Array.isArray(own) ? own.filter((x): x is string => typeof x === "string") : preset;
};
/** W5b: the RFC 7386 merge patch that sets (or clears, id null) the active own layout of a class at the levels of `active_levels`
 *  (`installation` = installations.<id>.<class>, an empty id = cleared here; `person` = <class>). Merged on the host, so two devices
 *  writing at once never lose each other's value. A bare stored id (every class) is kept as `any`. */
export function activePatch(value: unknown, cls: string, id: string | null, installation?: string, preset: string[] = DATA.active_levels ?? ["installation", "person"]): Record<string, unknown> {
  const v = parse(value);
  const levels = levelsOf(v, preset);
  const patch: Record<string, unknown> = typeof v === "string" && v ? { any: v } : {};
  if (installation && levels.includes("installation")) patch.installations = { [installation]: { [cls]: id ?? "" } };
  if (levels.includes("person") || !installation || !levels.includes("installation")) patch[cls] = id ?? null;
  return patch;
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

/** W5b: who writes a journal - the tab (`writer`, its client id: kept across reloads of the tab, another for every tab) and the browser
 *  installation it runs in (`installation`, kept across tabs). */
export interface JournalWho { writer: string; installation?: string }
type WriterRecord = Record<string, unknown> & { installation?: unknown; updated?: unknown };
const writersOf = (v: unknown): Record<string, WriterRecord> => (isObj(v) && isObj(v.writers) ? v.writers as Record<string, WriterRecord> : {});
const updatedOf = (r: WriterRecord): string => (typeof r.updated === "string" ? r.updated : "");
/** W5b: the journal of this writer and class, with where it came from: `own` (its own record), else - a new tab - a copy from the
 *  levels of `journal_inherit` (`installation`: the newest writer of the same installation and class, `person`: the newest writer of the
 *  class), else `class` (the W5 per-class journal, read once - it is never written again and never removed), else `none`. */
export function readWriterJournal(value: unknown, cls: string, who: JournalWho, inheritPreset: string[] = DATA.journal_inherit ?? ["installation", "person"]): { past: JournalEntry[]; future: JournalEntry[]; limit: number; source: string } {
  const v = parse(value);
  const limit = journalLimit(v);
  const take = (j: unknown) => (isObj(j) ? { past: limit === 0 ? [] : entries(j.past, limit), future: limit === 0 ? [] : entries(j.future, limit) } : null);
  const writers = writersOf(v);
  const own = isObj(writers[who.writer]) ? take(writers[who.writer][cls]) : null;
  if (own) return { ...own, limit, source: "own" };
  const inherit = isObj(v) && Array.isArray(v.inherit) ? (v.inherit as unknown[]).filter((x): x is string => typeof x === "string") : inheritPreset;
  const newest = (pred: (r: WriterRecord) => boolean) => Object.entries(writers)
    .filter(([id, r]) => id !== who.writer && isObj(r) && isObj(r[cls]) && pred(r))
    .sort(([, a], [, b]) => (updatedOf(a) < updatedOf(b) ? 1 : updatedOf(a) > updatedOf(b) ? -1 : 0))[0];
  for (const level of inherit) {
    const hit = level === "installation" ? (who.installation ? newest(r => r.installation === who.installation) : undefined) : level === "person" ? newest(() => true) : undefined;
    const j = hit ? take(hit[1][cls]) : null;
    if (j) return { ...j, limit, source: level };
  }
  const legacy = isObj(v) ? take(v[cls]) : null;
  return legacy ? { ...legacy, limit, source: "class" } : { past: [], future: [], limit, source: "none" };
}
/** W5b: the merge patch that stores this writer's journal of a class (`writers.<writer>.<class>`), merged on the host: two tabs never
 *  overwrite each other's records. Past `writers_limit` (else the preset journal_writers; 0 = no bound) the records updated longest ago
 *  are dropped (null in the patch) - never this writer's own. */
export function journalPatch(value: unknown, cls: string, s: Pick<WState, "past" | "future">, who: JournalWho, now: string, writersPreset: number = DATA.journal_writers ?? 20): Record<string, unknown> {
  const v = parse(value);
  const limit = journalLimit(v);
  const own = Number(isObj(v) ? v.writers_limit : NaN);
  const bound = Number.isFinite(own) && own >= 0 ? Math.floor(own) : writersPreset;
  const record: Record<string, unknown> = { updated: now, [cls]: { past: limit === 0 ? [] : s.past.slice(-limit), future: limit === 0 ? [] : s.future.slice(-limit) } };
  if (who.installation) record.installation = who.installation;
  const patch: Record<string, unknown> = { [who.writer]: record };
  const others = Object.entries(writersOf(v)).filter(([id]) => id !== who.writer)
    .sort(([, a], [, b]) => (updatedOf(a) < updatedOf(b) ? -1 : updatedOf(a) > updatedOf(b) ? 1 : 0));
  if (bound > 0) for (const [id] of others.slice(0, Math.max(0, others.length + 1 - bound))) patch[id] = null;
  return { writers: patch };
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
