// M1-W0 (2), package P3: the state of the node card (NodeCard.tsx) as pure logic, so a unit test drives it without a browser.
// The card renders what graph.card answers: value, why, effect, provenance + layers, the whole inheritance chain, relations by edge
// kind, the actions that fit the node. Nothing here lists levels, chips or words of its own: the levels come from the field
// (human.levels) and from the chain the kernel drew, the words from presentation.graph_screen (a missing word shows its id).
// What the card can WRITE today, measured on the real server (docs/reports/INDEX.md, P3):
//   person          → settings.set {key, value}                       (config.json; may ask a confirmation the rules demand)
//   conversation, project, team, organization, role_in_context, group_of_groups
//                   → rules.set {scope, scope_id, effect: set_value}  (the user's overlay; confirm:rules.set asks first)
//   turn            → planned: no action writes a turn value yet (context.modify is M1-K1)
//   installation, app_preset, engine → not from the card: a baseline that is data (a file), not a value of yours
// Taking a value off a layer: rules.disable on the set_value rule of that layer (confirm:rules.disable asks first); the person layer
// has no kernel action that removes a key from config.json (settings.set null would store null), so it says "planned" with that reason.
import type { GraphCardResult, InheritanceLevel } from "../api/graph-projection";
import type { RuleRequest } from "../api/loom-api";
import type { ActionRun } from "../api/types";
import type { ShellT } from "../shell/text";

/** The steps of the card as history layers: Back closes the last one first (level picker → editor → the card itself). */
export type CardStep = "card" | "edit" | "level";

/** One row of "Skąd jest wartość": the chain the kernel drew, with the words of the role and of a missing unit. */
export interface ChainRow {
  level: string;
  label: string;
  role: string;
  roleText: string;
  /** The value of the level in words ("" when the level gives none). */
  valueText: string;
  /** The level that supplies the value (highlighted). */
  winning: boolean;
  /** "bez projektu" and the like: the conversation has no unit of that kind (shown, never skipped). */
  noUnitText: string | null;
  active: boolean;
  scopeId: string | null;
}

export function valueText(value: unknown, t: ShellT): string {
  if (value === undefined) return "";
  if (value === null) return t("card.value.none");
  if (typeof value === "boolean") return t(value ? "card.value.on" : "card.value.off");
  if (typeof value === "string") return value === "" ? t("card.value.empty") : value;
  if (typeof value === "number") return String(value);
  return JSON.stringify(value);
}

export function chainRows(inheritance: InheritanceLevel[] | undefined, t: ShellT): ChainRow[] {
  return (inheritance ?? []).map(l => ({
    level: l.level, label: l.label, role: l.role, roleText: t(`inheritance.role.${l.role}`),
    valueText: l.value === undefined ? "" : valueText(l.value, t),
    winning: l.role === "supplied",
    noUnitText: l.no_unit ? t(`inheritance.no_unit.${l.level}`) : null,
    active: l.active, scopeId: l.scope_id ?? null,
  }));
}

/** The kind of editor the value gets: by the field's declared type (attrs.type of the projection), else by the value itself. */
export type EditorKind = "toggle" | "number" | "text" | "json";
export function editorKind(card: Pick<GraphCardResult, "value"> & { attrs?: Record<string, unknown> }): EditorKind {
  const declared = typeof card.attrs?.type === "string" ? (card.attrs.type as string) : undefined;
  const type = declared ?? (typeof card.value === "boolean" ? "boolean" : typeof card.value === "number" ? "number" : typeof card.value === "string" ? "string" : "object");
  if (type === "boolean") return "toggle";
  if (type === "number" || type === "integer") return "number";
  if (type === "string") return "text";
  return "json";
}

/** The value typed in the editor, or the id of the word that says what is wrong with it. */
export function parseDraft(kind: EditorKind, draft: string): { value: unknown } | { error: string } {
  if (kind === "toggle") return { value: draft === "true" };
  if (kind === "text") return { value: draft };
  if (kind === "number") {
    const n = Number(draft.trim().replace(",", "."));
    return draft.trim() === "" || !Number.isFinite(n) ? { error: "card.invalid_number" } : { value: n };
  }
  try { return { value: JSON.parse(draft) }; } catch { return { error: "card.invalid_json" }; }
}
export function draftOf(kind: EditorKind, value: unknown): string {
  if (kind === "toggle") return value === true ? "true" : "false";
  if (kind === "json") return JSON.stringify(value ?? null, null, 2);
  return value === undefined || value === null ? "" : String(value);
}

/** Levels that rules.set writes as a scoped value (scope + scope_id); the person level goes through settings.set. */
export const RULE_LEVELS = new Set(["conversation", "project", "team", "organization", "role_in_context", "group_of_groups"]);
export const BASELINE_LEVELS = new Set(["installation", "app_preset", "engine"]);

/** One answer to "Dla kogo?": a level with the way the card writes it, or the reason it cannot today. */
export interface LevelChoice {
  level: string;
  label: string;
  /** From the field's own list (human.levels); the rest show under "więcej poziomów". */
  declared: boolean;
  write: { kind: "settings.set" } | { kind: "rules.set"; scope: string; scope_id?: string } | { kind: "unavailable"; reasonText: string };
}

export function levelChoices(card: Pick<GraphCardResult, "levels" | "inheritance">, convId: string | null, t: ShellT): LevelChoice[] {
  const declared = new Set(card.levels ?? []);
  const chain = card.inheritance ?? [];
  const known = new Set(chain.map(l => l.level));
  // The order of the chain (engine → turn), then declared levels the chain does not draw (never dropped silently).
  const levels = [...chain.map(l => l.level), ...[...declared].filter(l => !known.has(l))];
  return levels.map(level => {
    const row = chain.find(l => l.level === level);
    const label = row?.label ?? t(`chip.${level}`);
    const isDeclared = declared.has(level);
    let write: LevelChoice["write"];
    if (level === "person") write = { kind: "settings.set" };
    else if (level === "conversation") write = convId ? { kind: "rules.set", scope: "conversation", scope_id: convId } : { kind: "unavailable", reasonText: t("card.level.no_conversation") };
    else if (RULE_LEVELS.has(level)) {
      if (row?.no_unit) write = { kind: "unavailable", reasonText: t(`inheritance.no_unit.${level}`) };
      else write = row?.scope_id ? { kind: "rules.set", scope: level, scope_id: row.scope_id } : { kind: "rules.set", scope: level };
    } else if (level === "turn") write = { kind: "unavailable", reasonText: t("card.level.turn_planned") };
    else if (BASELINE_LEVELS.has(level)) write = { kind: "unavailable", reasonText: t("card.level.baseline") };
    else write = { kind: "unavailable", reasonText: t("card.level.unknown", { level }) };
    return { level, label, declared: isDeclared, write };
  });
}

/** The request of a write: what the kernel is asked (settings.set through actions.run, or rules.set). */
export type WriteRequest = { kind: "settings.set"; run: Record<string, unknown> } | { kind: "rules.set"; rule: RuleRequest };
export function writeRequest(choice: LevelChoice, key: string, value: unknown): WriteRequest | null {
  if (choice.write.kind === "settings.set") return { kind: "settings.set", run: { action: "settings.set", args: { key, value } } };
  if (choice.write.kind === "rules.set")
    return { kind: "rules.set", rule: { scope: choice.write.scope, scope_id: choice.write.scope_id, effect: "set_value", key, value } };
  return null;
}

/** A rule of the user's overlay as rules.list returns it (only what the card reads). */
export interface ScopedRule { id: string; scope?: string; scope_id?: string; effect?: string; target?: { kind?: string; key?: string }; value?: unknown; disabled?: boolean }

/** "Zdejmij wartość: <warstwa> (<wartość>) — wróci: <warstwa> (<wartość>)": one per layer that holds a value, each taken off alone. */
export interface UnsetChoice {
  level: string;
  label: string;
  text: string;
  returnsText: string;
  /** The rule to switch off (rules.disable), or null with the reason the layer cannot be taken off today. */
  ruleId: string | null;
  reasonText: string | null;
}

export function unsetChoices(card: Pick<GraphCardResult, "inheritance">, key: string, rules: ScopedRule[], t: ShellT): UnsetChoice[] {
  const chain = card.inheritance ?? [];
  const holding = chain.filter(l => l.active && l.value !== undefined && (l.role === "supplied" || l.role === "overridden" || l.role === "conflict"));
  return holding.filter(l => !BASELINE_LEVELS.has(l.level)).map(l => {
    const rest = holding.filter(o => o.level !== l.level);
    const returns = rest.length ? rest[rest.length - 1] : null;   // the chain runs engine → turn: the last remaining level wins
    const returnsText = returns ? t("card.unset.returns", { layer: returns.label, value: valueText(returns.value, t) }) : t("card.unset.returns_nothing");
    const text = t("card.unset", { layer: l.label, value: valueText(l.value, t) });
    if (l.level === "person") return { level: l.level, label: l.label, text, returnsText, ruleId: null, reasonText: t("card.unset.person_planned") };
    const rule = rules.find(r => !r.disabled && r.effect === "set_value" && r.target?.key === key && r.scope === l.level && (l.scope_id ? r.scope_id === l.scope_id : true));
    if (!rule) return { level: l.level, label: l.label, text, returnsText, ruleId: null, reasonText: t("card.unset.no_rule", { layer: l.label }) };
    return { level: l.level, label: l.label, text, returnsText, ruleId: rule.id, reasonText: null };
  });
}

/** The confirmation a run waits for (confirm:rules.set, require_confirmation ...), in the language of the screen, with its receipt. */
export interface PendingGate { runId: string; receiptId: string; reasons: string[] }
export function pendingGate(run: ActionRun, lang: string): PendingGate | null {
  if (run.status !== "awaiting_confirmation" || !run.pending?.receipt_id) return null;
  const gates = Array.isArray((run.pending as { gates?: unknown }).gates) ? (run.pending as { gates: Record<string, unknown>[] }).gates : [];
  const reasons = gates.map(g => {
    const reason = g.reason as Record<string, string> | string | undefined;
    if (typeof reason === "string") return reason;
    return reason?.[lang] ?? reason?.en ?? String(g.rule ?? g.id ?? "");
  }).filter(Boolean);
  return { runId: run.id, receiptId: run.pending.receipt_id, reasons };
}

/** What a finished run says: completed, or the kernel's error in words (never swallowed). */
export function runOutcome(run: ActionRun, t: ShellT): { ok: true } | { ok: false; text: string } {
  if (run.status === "completed") return { ok: true };
  const message = run.error?.message ?? run.error?.code ?? run.status;
  return { ok: false, text: t("card.write_error", { reason: String(message) }) };
}

/** The relations grouped by edge kind, out and in, as the kernel grouped them; a group with nothing is not drawn. */
export function relationGroups(card: Pick<GraphCardResult, "relations">) {
  return Object.entries(card.relations ?? {}).filter(([, g]) => g.out.length + g.in.length > 0)
    .map(([kind, g]) => ({ kind, label: g.label, meaning: g.meaning, out: g.out, in: g.in }));
}

/** The actions of the card in words: the kernel's label, "planowane" when its implementation says so, the rules' decision. */
export function actionRows(card: Pick<GraphCardResult, "actions">, t: ShellT) {
  return (card.actions ?? []).map(a => ({
    id: a.id, label: a.label,
    planned: a.implementation !== null && a.implementation !== "implemented",
    decisionText: a.decision && a.decision !== "applicable" ? t(`card.decision.${a.decision}`) : null,
  }));
}
