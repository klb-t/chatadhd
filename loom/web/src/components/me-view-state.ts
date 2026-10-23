// M1-W2b-1: the pure parts of the screen "Ty" (ME VIEW). Everything here is a function of what the kernel answered (graph.navigate of party:self,
// of one part, graph.card of a row) and of the words of presentation.graph_screen (keys `me.*`): which parts there are, their order, their
// counts, the evidence of a row, the actions of a row - all of it comes from the kernel's answer, nothing is a list of this file.
// The only things written here are PRESETS, each labelled as one, and the argument names that a typed action takes, which are DATA in the pack
// words (me.input.<action>) because the kernel does not yet list the parameters of an action on a node (gap, see the report).
import type { GraphNavigateResult, GraphNodeRow } from "../api/graph-projection";
import type { ActionRun } from "../api/types";
import type { ShellT } from "../shell/text";
import { pendingGate, runOutcome, type PendingGate } from "./node-card-state";

/** PRESET (not a rule): an inferred row is drawn with this opacity (UI11: an inference visibly marked, one of many ways). Configurable through
 *  the setting me_inferred_opacity (0 to 1); 1 draws it like any other row. */
export const INFERRED_OPACITY_PRESET = 0.72;
export const INFERRED_OPACITY_SETTING = "me_inferred_opacity";
/** PRESET: which parts are open the first time (none): the person opens what he wants to see. The state is remembered in the setting below. */
export const OPEN_SECTIONS_SETTING = "me_open_sections";
/** The address of the person's node: the focus on which the navigator shows the ME VIEW. */
export const ME_FOCUS = "party:self";
/** The one action that is not run like the others: it opens the conversation of the interview (the kernel answers data.conv_id). */
export const INTERVIEW_ACTION = "profile.interview";

export function inferredOpacityOf(setting: unknown): number {
  const n = typeof setting === "number" ? setting : typeof setting === "string" && setting.trim() !== "" ? Number(setting) : NaN;
  return Number.isFinite(n) && n >= 0 && n <= 1 ? n : INFERRED_OPACITY_PRESET;
}

/** The open sections as saved: a string of part addresses separated by spaces (an address has none). Anything else = none open. */
export function openSectionsOf(setting: unknown): string[] {
  return typeof setting === "string" ? setting.split(/\s+/).filter(Boolean) : [];
}
export function withSection(open: string[], address: string, isOpen: boolean): string[] {
  const rest = open.filter(a => a !== address);
  return isOpen ? [...rest, address] : rest;
}
export const serializeSections = (open: string[]): string => open.join(" ");

/** The person's page is shown for the person's node, when the answer is the kernel's answer for it. */
export const isMeFocus = (focus: string): boolean => focus === ME_FOCUS;

export interface Word { text: string; known: boolean }
/** A word of the pack, or null when the catalogue has none (the translator answers the id for a missing word). */
export function wordOrNull(t: ShellT, id: string, params?: Record<string, string>): string | null {
  const text = t(id, params);
  return text === id ? null : text;
}

const CLASS_IDS = ["declared", "observed", "derived", "inferred", "extrapolated"];
/** The evidence class in words (the chip of a row). An inferred row with a known confidence says it. */
export function classWord(evidence: { class: string; confidence?: number | null }, t: ShellT): string {
  const cls = evidence.class;
  if (cls === "inferred" && typeof evidence.confidence === "number") return t("me.class.inferred_conf", { confidence: String(evidence.confidence) });
  return CLASS_IDS.includes(cls) ? t(`me.class.${cls}`) : t("me.class.other");
}
/** A conclusion of the app, not something the person said or did: drawn differently (preset opacity, a note). */
export const isInferred = (row: Pick<GraphNodeRow, "evidence">): boolean => row.evidence?.class === "inferred" || row.evidence?.class === "extrapolated";

/** The provenance line of a row: where it comes from in the kernel's words, with the time when the kernel has one; never an internal id. */
export function provenanceLine(row: Pick<GraphNodeRow, "evidence">, t: ShellT, language: string): { text: string; time: string | null; method: string | null } {
  const ev = row.evidence;
  const human = ev?.human?.trim() ?? "";
  const time = ev?.provenance?.time ?? null;
  const method = ev?.provenance?.method ?? null;
  if (human === "") return { text: t("me.provenance.none"), time, method };
  const when = time ? formatTime(time, language) : null;
  return { text: when ? t("me.provenance.line_time", { human, time: when }) : t("me.provenance.line", { human }), time, method };
}
export function formatTime(iso: string, language: string): string {
  const date = new Date(iso);
  if (Number.isNaN(date.getTime())) return iso;
  try { return new Intl.DateTimeFormat(language, { dateStyle: "medium", timeStyle: "short" }).format(date); } catch { return iso; }
}

/** The value of a row in the kernel's words. A slot the person closed ("never"), skipped or left unknown has no value in the answer: only the
 *  kernel's word for its state is shown, never a value of this file's. */
export function valueText(row: Pick<GraphNodeRow, "value" | "value_human">): string | null {
  if (typeof row.value_human === "string" && row.value_human !== "") return row.value_human;
  const v = row.value;
  return typeof v === "string" || typeof v === "number" || typeof v === "boolean" ? String(v) : null;
}

export interface ActionButton {
  id: string;
  /** What the button says: the short word of the pack for this action when there is one (me.action.<id>), else the kernel's title. */
  label: string;
  /** The kernel's title of the action (the full name; the tooltip and the sentence of the question). */
  title: string;
  planned: boolean;
  args: Record<string, unknown>;
}
type RowAction = { id: string; label: string; decision?: string | null; implementation?: string | null; args?: Record<string, unknown> };
/** The buttons of a node: its actions[] as the kernel offered them (nothing is added here). */
export function actionButtons(actions: RowAction[] | undefined, t: ShellT): ActionButton[] {
  return (actions ?? []).map(a => ({
    id: a.id, title: a.label, label: wordOrNull(t, `me.action.${a.id}`) ?? a.label,
    planned: a.implementation !== undefined && a.implementation !== null && a.implementation !== "implemented",
    args: a.args ?? {},
  }));
}

export interface InputField { arg: string; label: string }
/** The arguments an action takes from the person's own typing: declared by the words (me.input.<action> = the names, me.input.<action>.<name> =
 *  the label of each), so a new action needs a word, not code. No word = nothing to type (the action runs with the arguments the kernel gave). */
export function inputFields(actionId: string, t: ShellT): InputField[] {
  const names = wordOrNull(t, `me.input.${actionId}`);
  if (!names) return [];
  return names.split(/\s+/).filter(Boolean).map(arg => ({ arg, label: wordOrNull(t, `me.input.${actionId}.${arg}`) ?? arg }));
}
/** The arguments of a run: the kernel's, then what was typed (an empty field is left out - the kernel says what is missing). */
export function runArgs(button: Pick<ActionButton, "args">, typed: Record<string, string>): Record<string, unknown> {
  const out: Record<string, unknown> = { ...button.args };
  for (const [k, v] of Object.entries(typed)) if (v.trim() !== "") out[k] = v.trim();
  return out;
}
/** A node action is run on the application (the kernel offers these through the class application). */
export const runRequest = (button: Pick<ActionButton, "id">, args: Record<string, unknown>) =>
  ({ action: button.id, args, element: { class: "application" } });

export type RunOutcome =
  | { kind: "done"; text: string; data: Record<string, unknown> | null }
  | { kind: "gate"; gate: PendingGate }
  | { kind: "error"; text: string };
/** What a run came to: finished, waiting for the person's yes (the kernel's reasons in the language asked), or refused/failed with the kernel's words. */
export function interpretRun(run: ActionRun, t: ShellT, language: string): RunOutcome {
  const gate = pendingGate(run, language);
  if (gate) return { kind: "gate", gate };
  const outcome = runOutcome(run, t);
  if (!outcome.ok) return { kind: "error", text: t("me.confirm.failed", { reason: String(run.error?.message ?? run.error?.code ?? run.status) }) };
  const part = run.result?.parts?.find(p => typeof (p as { text?: unknown })?.text === "string") as { text?: string } | undefined;
  const data = run.result?.data && typeof run.result.data === "object" ? run.result.data as Record<string, unknown> : null;
  return { kind: "done", text: part?.text ?? "", data };
}

export interface SectionHead {
  address: string;
  label: string;
  count: number | null;
  head: string;
  why: string;
  mix: string | null;
  actions: ActionButton[];
}
/** The sections of the page: the parts the kernel answered for the person, in its order, each with its count, its mix of evidence and its actions. */
export function sections(result: Pick<GraphNavigateResult, "children">, t: ShellT): SectionHead[] {
  return result.children.filter(c => c.kind === "person_subgraph").map(part => {
    const count = typeof part.count === "number" ? part.count : null;
    return {
      address: part.address, label: part.label, count, why: part.why ?? "",
      head: count === null ? t("me.section.head_unknown", { label: part.label }) : t("me.section.head", { label: part.label, count: String(count) }),
      mix: classMix(part.evidence_classes, t), actions: actionButtons(part.actions, t),
    };
  });
}
/** The children of the person that are not parts (a kernel without the table of parts, a user layer that left some out): shown as they are. */
export const looseRows = (result: Pick<GraphNavigateResult, "children">): GraphNodeRow[] => result.children.filter(c => c.kind !== "person_subgraph");

export function classMix(classes: Record<string, number> | undefined, t: ShellT): string | null {
  const entries = Object.entries(classes ?? {}).filter(([, n]) => n > 0);
  if (entries.length === 0) return null;
  const order = (c: string) => { const i = CLASS_IDS.indexOf(c); return i < 0 ? CLASS_IDS.length : i; };
  entries.sort((a, b) => order(a[0]) - order(b[0]));
  return t("me.section.mix", { list: entries.map(([c, n]) => t("me.section.mix_item", { class: classWord({ class: c }, t), n: String(n) })).join(" · ") });
}

export interface RowView {
  address: string;
  label: string;
  value: string | null;
  chip: string;
  classId: string;
  inferred: boolean;
  setBy: string | null;
  provenance: { text: string; time: string | null; method: string | null };
  /** The kernel's sentence for the row (for a slot above the person's level it is the reason). */
  why: string;
  inferredNote: string | null;
  actions: ActionButton[];
  expandable: boolean;
}
export function rowView(row: GraphNodeRow, t: ShellT, language: string): RowView {
  const inferred = isInferred(row);
  const who = row.provenance?.chip?.label ?? null;
  return {
    address: row.address, label: row.label, value: valueText(row), chip: classWord(row.evidence, t), classId: row.evidence?.class ?? "",
    inferred, setBy: row.kind === "preference" && who ? t("me.chip.set_by", { who }) : null,
    provenance: provenanceLine(row, t, language), why: row.why ?? "",
    inferredNote: inferred ? t("me.inferred.note") : null, actions: actionButtons(row.actions, t),
    expandable: true,
  };
}

/** What the details of a row say, from graph.card: why the kernel does (not) ask a slot, the words of a note's origin; the chain of a preference
 *  is drawn with chainRows (node-card-state) by the component. */
export function detailLines(attrs: Record<string, unknown> | undefined, t: ShellT): string[] {
  const out: string[] = [];
  if (!attrs) return out;
  const reason = typeof attrs.reason_text === "string" ? attrs.reason_text : "";
  if (reason) out.push(t("me.details.reason", { reason }));
  else if (attrs.asked_at_level === true) out.push(t("me.details.in_level"));
  const prov = attrs.provenance as { words?: unknown } | undefined;
  if (prov && typeof prov.words === "string" && prov.words) out.push(t("me.details.provenance", { words: prov.words }));
  return out;
}
