// M1-W0 (2), package P2: the state of the graph navigator's list, as pure logic so a unit test drives it without a browser.
// The screen renders what graph.navigate answers (loom/src/graph/projection.h through the typed API): nothing here lists nodes,
// chips or words of its own. Words come from presentation.graph_screen (loom/data/profiles/conversation_view.pack); the
// translator returns the id of a missing word, so a missing word is visible, never silently English.
import type { GraphNavigateRequest, GraphNavigateResult, GraphNodeRow } from "../api/graph-projection";
import type { ShellT } from "../shell/text";
import { countPhrase } from "../shell/plural";

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
  /** Perspektywa ▾: the perspectives (ids of the kernel's list, loom_perspectives) whose relations the rows follow; [] = the node's
   *  own children. They stay on while you drill and go back: a perspective is a way of looking, not a property of one level. */
  perspectives: string[];
}

/** Presets (not rules): the smallest budget and depth the buttons go down to; the kernel takes any value a request carries. */
export const LIMIT_MIN_PRESET = 1;
export const HOPS_MIN_PRESET = 1;
/** How long the search field waits after a keystroke before asking the kernel (ms). */
export const SEARCH_DEBOUNCE_PRESET_MS = 250;

export function initialState(focus = "", label = ""): NavigatorState {
  return { path: focus ? [{ address: focus, label }] : [], query: "", limit: null, hops: null, perspectives: [] };
}
export const focusOf = (s: NavigatorState): string => s.path.length ? s.path[s.path.length - 1].address : "";

/** Perspektywa ▾: one tap ticks or unticks a perspective (several may be on at once); the kernel then follows their relations. */
export function togglePerspective(s: NavigatorState, id: string): NavigatorState {
  const on = s.perspectives.includes(id);
  return { ...s, perspectives: on ? s.perspectives.filter(p => p !== id) : [...s.perspectives, id] };
}
export const clearPerspectives = (s: NavigatorState): NavigatorState => s.perspectives.length ? { ...s, perspectives: [] } : s;

export interface PerspectiveOption { id: string; label: string }
/** The options of Perspektywa ▾: exactly the kernel's list (GET perspectives / loom_perspectives), in its order, each named in the
 *  language asked (its English name, then its id, when the data has no word in that language — never a name of this file). */
export function perspectiveOptions(list: { perspectives: { id: string; entry?: { label?: { en?: string; pl?: string } | string } }[] }, lang: string): PerspectiveOption[] {
  return list.perspectives.map(p => {
    const label = p.entry?.label;
    const text = typeof label === "string" ? label : label?.[lang as "en" | "pl"] ?? label?.en ?? "";
    return { id: p.id, label: text || p.id };
  });
}
/** Działania: the node whose card holds the actions — the focus; at the root there is none, and the bar says where actions live. */
export function actionsTarget(s: NavigatorState, focusLabel: string): { address: string; label: string } | null {
  const focus = focusOf(s);
  return focus ? { address: focus, label: s.path[s.path.length - 1].label || focusLabel } : null;
}

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
  if (s.perspectives.length) request.perspectives = s.perspectives;
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
/** The provenance chip: the kernel's own words when it sends a chip; a profile slot (a row under Ty) says what its evidence says —
 *  "pytanie z wywiadu — bez odpowiedzi" / "Twoja odpowiedź w wywiadzie" (the kernel's evidence.human, else the method's word) — never
 *  "Preset (ROM)" on your own profile (r2:b1); else the layer's word (chip.<layer>; a missing word shows its id). */
export function chipText(row: Pick<GraphNodeRow, "provenance"> & Partial<Pick<GraphNodeRow, "kind" | "evidence" | "state">>, t: ShellT): string {
  if (row.provenance.chip?.label) return row.provenance.chip.label;
  if (row.kind === "profile_slot" && row.evidence) return row.evidence.human ?? t(`evidence.method.${row.evidence.provenance.method}`);
  // A slot with no value has no evidence: its chip is the state in words (bez odpowiedzi), never the layer of the scenario ("Preset (ROM)").
  if (row.kind === "profile_slot" && row.evidence === null) return stateWord(row.state, t);
  return t(`chip.${row.provenance.layer}`);
}
/** The state of an entry that holds no value, in the pack's words (me.state.<state>); a state without a word says "no value" (never the id). */
export function stateWord(state: string | undefined, t: ShellT): string {
  const word = state ? t(`me.state.${state}`) : `me.state.`;
  return !state || word === `me.state.${state}` ? t("me.state.other") : word;
}
/** The evidence chip, only when it is not the plain "declared" of a preset, and not for a profile slot (its provenance chip already
 *  says the evidence: one chip, not the same words twice). */
export function evidenceText(row: Pick<GraphNodeRow, "evidence"> & Partial<Pick<GraphNodeRow, "kind">>, t: ShellT): string | null {
  if (row.kind === "profile_slot" || !row.evidence) return null;
  if (row.evidence.class === "declared") return null;
  if (row.evidence.human) return row.evidence.human;
  const confidence = row.evidence.confidence;
  if (row.evidence.class === "inferred" && confidence === null) return t("evidence.inferred_unknown");
  return t(`evidence.${row.evidence.class}`, { confidence: confidence === null ? "" : String(confidence) });
}
export const statusText = (row: Pick<GraphNodeRow, "status">, t: ShellT): string | null => row.status ? t(`status.${row.status}`) : null;
export const noTranslationText = (row: Pick<GraphNodeRow, "no_translation">, t: ShellT): string | null =>
  row.no_translation ? t("no_translation", { lang: row.no_translation.lang, shown_lang: row.no_translation.shown_lang }) : null;

/** The bottom bar in the order of the spec (§3.2); every button does what its word says (Perspektywa ▾ = the kernel's perspectives
 *  as a tick list sent to graph.navigate, Działania = the actions on the focus's card, Widok ▾ = the layout). */
export const BAR_BUTTONS = [
  { id: "perspective", word: "bar.perspective" },
  { id: "wider", word: "bar.wider" },
  { id: "narrower", word: "bar.narrower" },
  { id: "deeper", word: "bar.deeper" },
  { id: "shallower", word: "bar.shallower" },
  { id: "actions", word: "bar.actions" },
  { id: "view", word: "bar.view" },
] as const;
export type BarButtonId = typeof BAR_BUTTONS[number]["id"];

/** The lines under the list that keep the list honest: collapsed by the budget, hidden by you, sources that could not answer. */
export function honestyLines(result: Pick<GraphNavigateResult, "counts" | "hidden_by_you" | "unknown">, t: ShellT, language: string): { id: string; text: string }[] {
  const lines: { id: string; text: string }[] = [];
  // r2:b1: "zwinięto 1 węzeł / 2 węzły / 5 węzłów" — the form from the language, the word from count.nodes.<form>.
  if (result.counts.collapsed > 0) lines.push({ id: "collapsed", text: t("collapsed", { nodes: countPhrase(t, "nodes", result.counts.collapsed, language) }) });
  if (result.hidden_by_you.count > 0) lines.push({ id: "hidden", text: t("hidden_by_you", { nodes: countPhrase(t, "nodes", result.hidden_by_you.count, language) }) });
  for (const u of result.unknown)
    lines.push({ id: `unknown-${u.source}`, text: t("unknown_source", { source: u.source_label || u.source, reason: u.reason_human || t(`reason.${u.reason}`) }) });
  return lines;
}

/** The budget line under the bar: "budżet na poziom: 2 węzły · głębia 1 · 22 wiersze, pokazano 2" (limit 0 = no budget). The plural
 *  forms come from the language (count.nodes / count.rows of the catalogue), so 1 is never "1 węzłów". */
export function budgetLine(limit: number, hops: number, counts: { shown: number; total: number }, t: ShellT, language: string): string {
  const budget = limit === 0 ? t("bar.budget.unlimited", { hops: String(hops) })
    : t("bar.budget", { nodes: countPhrase(t, "nodes", limit, language), hops: String(hops) });
  return `${budget} · ${t("children", { shown: String(counts.shown), rows: countPhrase(t, "rows", counts.total, language) })}`;
}

/** A step opened from outside (menu ⋮ › Ty, › Ta rozmowa w grafie: initialState(focus) with no label) takes the kernel's label of
 *  the focus when the answer arrives — the crumb says "M1 card", not "conv:e880…". A step that has a label keeps it. */
export function withFocusLabel(s: NavigatorState, focus: { address: string; label: string }): NavigatorState {
  const last = s.path[s.path.length - 1];
  if (!last || last.label || last.address !== focus.address || !focus.label) return s;
  return { ...s, path: [...s.path.slice(0, -1), { address: last.address, label: focus.label }] };
}
