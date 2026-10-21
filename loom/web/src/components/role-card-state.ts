// M1-W0 (4), package P6: the role card (RoleCard.tsx) as pure logic, so a unit test drives it without a browser. The screen draws what
// roles.resolved answers, as it answers: the roles in the order of the data, for each the model and its class, the chain of the policy
// word for word (the kernel's own sentence for every step), the class with its reason, the flow of every class (the class of the
// model decides which flow runs), and every flow that reads the role's setting. Nothing here lists a role, a class, a flow or a
// sentence of its own; the words around the data come from presentation.roles (a missing word shows its id, never English).
import type { RoleChainStep, RoleResolved, RoleTodayStep, RolesResolved } from "../api/graph-projection";
import type { ShellT } from "../shell/text";

/** The group of the settings screen where the roles are drawn (spec §3.5/§3.6: Modele › Role). A preset: when no group has this id
 *  (an overlay renamed or hid it) the roles are drawn under the group list, so the screen never disappears. */
export const ROLES_GROUP_PRESET = "modele";

/** The address of the settings field behind a key (graph_sources.pack prefix setting:). The card of that node is the one place that writes. */
export const settingAddress = (key: string): string => `setting:${key}`;

export interface RoleRowView {
  id: string;
  label: string;
  why: string;
  modelText: string;
  tierId: string;
  tierLabel: string;
  /** planned | partial | ... : the chip text, null when the role is fully implemented (no chip is drawn for a state nobody needs to read). */
  state: { id: string; text: string } | null;
}

/** The state of a role in words: the state the data says, the word from the catalogue (a missing word shows the id). */
export function stateChip(role: Pick<RoleResolved, "status">, t: ShellT): { id: string; text: string } | null {
  const id = role.status?.state ?? "";
  if (id === "" || id === "implemented") return null;
  return { id, text: t(`state.${id}`) };
}

/** The table of roles: exactly the roles the kernel sent, in the order it sent them. */
export function roleRows(doc: Pick<RolesResolved, "roles">, t: ShellT): RoleRowView[] {
  return doc.roles.map(role => ({
    id: role.id, label: role.label, why: role.why, modelText: role.model_human, tierId: role.tier.id, tierLabel: role.tier.label, state: stateChip(role, t),
  }));
}

/** A value of a step in words: a string as it is, nothing as the word for "empty". */
export function valueText(value: unknown, t: ShellT): string {
  if (value === null || value === undefined || value === "") return t("step.value.empty");
  return typeof value === "string" ? value : JSON.stringify(value);
}

export interface ChainStepView {
  n: number;
  type: string;
  /** "1. wprost: Model analizy w tle (semantic_model)" */
  title: string;
  /** The kernel's own sentence for the step, word for word. */
  meaning: string;
  value: string;
  decides: boolean;
  /** relative_to: the note that says the preset never inherits from the conversation. */
  note: string | null;
}

/** The label of a setting of the role (role.settings), else the key itself. */
function settingLabel(role: Pick<RoleResolved, "settings">, key: string): string {
  return role.settings?.find(s => s.key === key)?.label ?? key;
}

/** The name of a step: its kind in words and what it points at. Roles are named by their label from the same answer. */
export function stepName(step: Pick<RoleChainStep, "type" | "setting" | "role">, role: Pick<RoleResolved, "settings">, roles: Pick<RoleResolved, "id" | "label">[], t: ShellT): string {
  if (step.type === "explicit") {
    if (!step.setting) return t("step.explicit.plain");
    return t("step.explicit", { setting: t("step.setting", { label: settingLabel(role, step.setting), key: step.setting }) });
  }
  if (step.type === "role") return t("step.role", { role: roles.find(r => r.id === step.role)?.label ?? step.role ?? "" });
  return t(`step.${step.type}`);
}

/** The policy chain, step by step, as the data has it (the order, which step decides, the sentence of each). */
export function chainSteps(role: RoleResolved, roles: Pick<RoleResolved, "id" | "label">[], t: ShellT): ChainStepView[] {
  return (role.chain ?? []).map(step => ({
    n: step.step, type: step.type,
    title: t("chain.step", { n: String(step.step), step: stepName(step, role, roles, t) }),
    meaning: step.meaning, value: valueText(step.value, t), decides: step.decides === true,
    note: step.type === "relative_to" ? t("relative_to.note") : null,
  }));
}

export interface TodayStepView { n: number; title: string; meaning: string; value: string; decides: boolean }

/** What today's reader of a policy no reader reads yet reads, in order (shown under a chain that only describes). */
export function todaySteps(role: RoleResolved, t: ShellT): TodayStepView[] {
  return (role.today_chain ?? []).map((step: RoleTodayStep) => ({
    n: step.step,
    title: t("chain.step", { n: String(step.step), step: step.setting ? t("step.explicit", { setting: t("step.setting", { label: settingLabel(role, step.setting), key: step.setting }) }) : t("step.explicit.plain") }),
    meaning: step.meaning, value: valueText(step.value, t), decides: step.decides === true,
  }));
}

/** true: a reader reads the chain (it decides); false: the chain describes what no reader reads yet (today's reader is today_chain). */
export const chainIsRead = (role: Pick<RoleResolved, "chain_status">): boolean => role.chain_status !== "described_not_read";

export interface FlowView {
  id: string;
  title: string;
  calls: string;
  /** The status the data says (calibration ...) in words; null when the data gives none. */
  status: { id: string; text: string } | null;
  does: string[];
  planned: string[];
}

type FlowData = NonNullable<RoleResolved["flow"]> & { title?: string; does_human?: string[]; planned_human?: string[] };

/** A flow in words: the kernel gives its title and its lines in the language asked; a flow without them shows its id (and the raw lines
 *  are never shown instead, they are English). The status "calibration" is the chip "kalibracja". */
export function flowView(flow: FlowData, t: ShellT): FlowView {
  const status = flow.status ? { id: flow.status, text: flow.status === "calibration" ? t("flow.calibration") : t(`flow.status.${flow.status}`) } : null;
  return {
    id: flow.id, title: flow.title && flow.title !== "" ? flow.title : t("flow.no_words", { id: flow.id }),
    calls: flow.calls ? t(`calls.${flow.calls}`) : "", status,
    does: flow.does_human ?? [], planned: flow.planned_human ?? [],
  };
}

export interface ClassFlowView { tierId: string; tierLabel: string; current: boolean; flow: FlowView }

/** The flow of every class of model, the class the role has now marked. Empty when the role's flow does not depend on the class. */
export function flowsByClass(role: RoleResolved, t: ShellT): ClassFlowView[] {
  return (role.flow_by_class ?? []).map(entry => ({
    tierId: entry.tier.id, tierLabel: entry.tier.label, current: entry.tier.id === role.tier.id, flow: flowView(entry.flow, t),
  }));
}

/** The flows of the role (they declare it) and the other flows that read one of its settings: exactly the lists the kernel built from the
 *  action catalogue (id + title in the language asked, and for the second list the key read). Together they are EVERY flow that reads the role's key. */
export function readers(role: Pick<RoleResolved, "flows_reading" | "flows_reading_setting">): { own: { id: string; title: string }[]; also: { id: string; title: string; key: string }[]; total: number } {
  const own = role.flows_reading ?? [];
  const also = role.flows_reading_setting ?? [];
  return { own, also, total: own.length + also.length };
}

export interface ModeView { modes: string[]; threshold: string | null; batchNoKey: string | null }

/** The background mode (analysis only): the modes the worker has, the threshold of the batch mode, and the missing key when the batch needs one. */
export function modeView(role: Pick<RoleResolved, "mode">, t: ShellT): ModeView | null {
  const mode = role.mode;
  if (!mode) return null;
  const list = Array.isArray(mode.modes) ? (mode.modes as unknown[]).filter((m): m is string => typeof m === "string") : [];
  const names: Record<string, string> = { regex: "mode.local", llm: "mode.model", batch: "mode.batch" };
  const batch = list.includes("batch");
  return {
    modes: list.map(id => t(names[id] ?? `mode.${id}`)),
    threshold: batch && typeof mode.batch_threshold === "number" ? t("mode.threshold", { n: String(mode.batch_threshold) }) : null,
    batchNoKey: batch && mode.batch_key_set === false ? t("mode.batch_no_key") : null,
  };
}

/** The price of a million tokens, or the kernel's reason for not knowing (never 0). */
export function priceText(role: Pick<RoleResolved, "cost">, t: ShellT): string {
  const price = role.cost?.per_million;
  if (price) {
    const out = price.output === null || price.output === undefined ? t("cost.output_unknown") : String(price.output);
    return t("cost.price", { price: t("cost.price_io", { input: String(price.input), output: out, currency: price.currency }) });
  }
  return role.cost?.per_million_unknown_human ?? t("cost.unknown");
}

/** The last 7 days: an amount or the kernel's reason (the ledger records actions, not roles). */
export function weekText(role: Pick<RoleResolved, "cost">, t: ShellT): string {
  const week = role.cost?.last_7d;
  if (week !== undefined && week !== null) return t("cost.week", { amount: typeof week === "string" ? week : JSON.stringify(week) });
  return role.cost?.last_7d_unknown_human ?? t("cost.unknown");
}
