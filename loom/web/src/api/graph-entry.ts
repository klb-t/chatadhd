/** Entry view of the graph (R45/R52/R53): areas with counts, read-only. */
export interface GraphEntryCount {
  known: boolean;
  value?: number;
  by?: Record<string, number>;
  reason?: string;
  run?: string;
}
export interface GraphEntryExpand { view: string; params: Record<string, unknown> }
export interface GraphEntryArea {
  id: string;
  label: string;
  origin: string;
  axes: Record<string, string>;
  order: number;
  counts: Record<string, GraphEntryCount>;
  sources: { binding: string; provider: string }[];
  expand: GraphEntryExpand | null;
  hidden?: boolean;
  [key: string]: unknown;
}
export interface GraphEntrySeedElement {
  id: string;
  kind: string;
  label: string;
  counts: Record<string, GraphEntryCount>;
  node?: { class: string; slots: { id: string; label: string; state: string; value: unknown }[] };
  [key: string]: unknown;
}
export interface GraphEntryView {
  schema: "loom.graph_entry/1";
  lang: string;
  profile_hash: string;
  areas: GraphEntryArea[];
  seed: GraphEntrySeedElement[];
  issues: Record<string, unknown>[];
  axes: Record<string, unknown>;
  [key: string]: unknown;
}
export interface GraphEntryOptions { lang?: string; include_hidden?: boolean }

function record(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}
function readCounts(value: unknown): Record<string, GraphEntryCount> {
  if (!record(value)) throw new Error("graph_entry_counts_invalid");
  for (const count of Object.values(value)) {
    if (!record(count) || typeof count.known !== "boolean" ||
        (count.known && (typeof count.value !== "number" || !Number.isFinite(count.value))) ||
        (!count.known && count.value !== undefined)) throw new Error("graph_entry_count_invalid");
  }
  return value as unknown as Record<string, GraphEntryCount>;
}
/** Validates the native envelope. An unknown count never carries a value, so it cannot be shown as zero. */
export function readGraphEntry(value: unknown): GraphEntryView {
  if (!record(value) || value.schema !== "loom.graph_entry/1" || !Array.isArray(value.areas) || !Array.isArray(value.seed))
    throw new Error("graph_entry_envelope_invalid");
  for (const row of [...value.areas, ...value.seed]) {
    if (!record(row) || typeof row.id !== "string" || !row.id || typeof row.label !== "string") throw new Error("graph_entry_node_invalid");
    readCounts(row.counts);
  }
  return value as unknown as GraphEntryView;
}

/** The slice of a presentation feature this view needs (see presentation.mjs). */
export type EntryTexts = (id: string, parameters?: Record<string, string | number>) => string;

/** A count as text: an unknown count states that it is unknown and why; it is never a number. */
export function countText(count: GraphEntryCount, texts: EntryTexts): string {
  if (!count.known) return count.reason ? texts("count.unknown_reason", { reason: count.reason }) : texts("count.unknown");
  return texts("count.value", { value: count.value as number });
}
/** Per-group breakdown lines (for example messages by status) of a known count. */
export function breakdown(count: GraphEntryCount, texts: EntryTexts): string[] {
  if (!count.known || !count.by) return [];
  return Object.entries(count.by).map(([name, value]) => texts("count.by", { name: name || "-", value }));
}
/** Whether the host can open an area's `expand` descriptor with an existing view. */
export function canExpand(area: { expand: GraphEntryExpand | null }, openers: Record<string, unknown>): boolean {
  return area.expand !== null && typeof openers[area.expand.view] === "function";
}
