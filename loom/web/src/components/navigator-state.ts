// M1-W0 (2), package P2: the state of the graph navigator's list, as pure logic so a unit test drives it without a browser.
// The screen renders what graph.navigate answers (loom/src/graph/projection.h through the typed API): nothing here lists nodes,
// chips or words of its own. Words come from presentation.graph_screen (loom/data/profiles/conversation_view.pack); the
// translator returns the id of a missing word, so a missing word is visible, never silently English.
import type { GraphNavigateRequest, GraphNavigateResult, GraphNodeRow } from "../api/graph-projection";
import type { ShellT } from "../shell/text";

export interface PathStep { address: string; label: string }
export interface NavigatorState {
  /** The steps taken from the root; the last one is the focus. Each step is one history layer (shell/history.ts). */
  path: PathStep[];
  /** What the magnifier searches on this level ("" = nothing). */
  query: string;
  /** Children per level; null = the kernel's preset (the setting navigator_children_limit), 0 = no limit. */
  limit: number | null;
  /** Levels below the focus; null = the kernel's preset. */
  hops: number | null;
}

/** Presets (not rules): the smallest budget and depth the buttons go down to; the kernel takes any value a request carries. */
export const LIMIT_MIN_PRESET = 1;
export const HOPS_MIN_PRESET = 1;
/** How long the search field waits after a keystroke before asking the kernel (ms). */
export const SEARCH_DEBOUNCE_PRESET_MS = 250;

export function initialState(focus = "", label = ""): NavigatorState {
  return { path: focus ? [{ address: focus, label }] : [], query: "", limit: null, hops: null };
}
export const focusOf = (s: NavigatorState): string => s.path.length ? s.path[s.path.length - 1].address : "";

/** A tap on a row with children: one step deeper; the search of the old level does not carry over. */
export function drillInto(s: NavigatorState, row: Pick<GraphNodeRow, "address" | "label">): NavigatorState {
  if (focusOf(s) === row.address) return s;
  return { ...s, path: [...s.path, { address: row.address, label: row.label }], query: "" };
}
/** Back to the step at `length` (0 = the root): a tap on a crumb, the ‹ button, Back / popstate closing the last step. */
export function goBack(s: NavigatorState, length: number): NavigatorState {
  const keep = Math.max(0, Math.min(length, s.path.length));
  if (keep === s.path.length) return s;
  return { ...s, path: s.path.slice(0, keep), query: "" };
}
export const setQuery = (s: NavigatorState, query: string): NavigatorState => s.query === query ? s : { ...s, query };

/** Szerzej: twice the budget of the level (the effective one the kernel reported). No budget (0) stays no budget. */
export function wider(s: NavigatorState, effectiveLimit: number): NavigatorState {
  const current = s.limit ?? effectiveLimit;
  return current === 0 ? s : { ...s, limit: current * 2 };
}
/** Węziej: half the budget; from "no budget" it starts at half of what is shown. */
export function narrower(s: NavigatorState, effectiveLimit: number, shown: number): NavigatorState {
  const current = s.limit ?? effectiveLimit;
  const base = current > 0 ? current : shown;
  return { ...s, limit: Math.max(LIMIT_MIN_PRESET, Math.floor(base / 2)) };
}
export const deeper = (s: NavigatorState, effectiveHops: number): NavigatorState => ({ ...s, hops: (s.hops ?? effectiveHops) + 1 });
export const shallower = (s: NavigatorState, effectiveHops: number): NavigatorState =>
  ({ ...s, hops: Math.max(HOPS_MIN_PRESET, (s.hops ?? effectiveHops) - 1) });

/** The request graph.navigate gets: only what the state sets; the kernel's presets fill the rest. */
export function requestFor(s: NavigatorState, lang: string, convId?: string | null): GraphNavigateRequest {
  const request: GraphNavigateRequest = { lang };
  const focus = focusOf(s);
  if (focus) request.focus = focus;
  if (s.query) request.query = s.query;
  if (s.limit !== null) request.limit = s.limit;
  if (s.hops !== null) request.hops = s.hops;
  if (convId) request.conv_id = convId;
  return request;
}

/** The count of a row in words: a number, "unknown (reason)" with the kernel's reason, "unknown" for a parent that gave none,
 *  nothing for a leaf (a leaf has no count to claim; never a made-up 0). */
export function countText(row: GraphNodeRow, t: ShellT): string | null {
  if (typeof row.count === "number") return t("count.value", { count: String(row.count) });
  if (row.count_unknown_reason) return t("count.unknown_reason", { reason: row.count_unknown_human ?? t(`reason.${row.count_unknown_reason}`) });
  if (row.has_children) return t("count.unknown");
  return null;
}
/** The provenance chip: the kernel's own words when it sends a chip, else the layer's word (chip.<layer>; a missing word shows its id). */
export function chipText(row: Pick<GraphNodeRow, "provenance">, t: ShellT): string {
  return row.provenance.chip?.label ?? t(`chip.${row.provenance.layer}`);
}
/** The evidence chip, only when it is not the plain "declared" of a preset. */
export function evidenceText(row: Pick<GraphNodeRow, "evidence">, t: ShellT): string | null {
  if (row.evidence.class === "declared") return null;
  if (row.evidence.human) return row.evidence.human;
  const confidence = row.evidence.confidence;
  if (row.evidence.class === "inferred" && confidence === null) return t("evidence.inferred_unknown");
  return t(`evidence.${row.evidence.class}`, { confidence: confidence === null ? "" : String(confidence) });
}
export const statusText = (row: Pick<GraphNodeRow, "status">, t: ShellT): string | null => row.status ? t(`status.${row.status}`) : null;
export const noTranslationText = (row: Pick<GraphNodeRow, "no_translation">, t: ShellT): string | null =>
  row.no_translation ? t("no_translation", { lang: row.no_translation.lang, shown_lang: row.no_translation.shown_lang }) : null;

/** The bottom bar in the order of the spec (§3.2). `works` = this package wires it; the others say "planned" with the reason. */
export const BAR_BUTTONS = [
  { id: "perspective", word: "bar.perspective", works: false },
  { id: "wider", word: "bar.wider", works: true },
  { id: "narrower", word: "bar.narrower", works: true },
  { id: "deeper", word: "bar.deeper", works: true },
  { id: "shallower", word: "bar.shallower", works: true },
  { id: "actions", word: "bar.actions", works: false },
  { id: "view", word: "bar.view", works: false },
] as const;
export type BarButtonId = typeof BAR_BUTTONS[number]["id"];

/** The lines under the list that keep the list honest: collapsed by the budget, hidden by you, sources that could not answer. */
export function honestyLines(result: Pick<GraphNavigateResult, "counts" | "hidden_by_you" | "unknown">, t: ShellT): { id: string; text: string }[] {
  const lines: { id: string; text: string }[] = [];
  if (result.counts.collapsed > 0) lines.push({ id: "collapsed", text: t("collapsed", { count: String(result.counts.collapsed) }) });
  if (result.hidden_by_you.count > 0) lines.push({ id: "hidden", text: t("hidden_by_you", { count: String(result.hidden_by_you.count) }) });
  for (const u of result.unknown)
    lines.push({ id: `unknown-${u.source}`, text: t("unknown_source", { source: u.source_label || u.source, reason: u.reason_human || t(`reason.${u.reason}`) }) });
  return lines;
}
