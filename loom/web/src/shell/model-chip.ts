// M1-W1b: the model chip. The chip shows a short name; the sheet says where the model came from (which level of the scope chain set it, or that
// this window picked it), its class and its price. Everything comes from the kernel's answers (settings scope chain, roles.resolved, the model
// list); what none of them gives is said as unknown with the reason, never guessed.
import type { ScopeResolution } from "../api/graph-views";
import type { RoleResolved } from "../api/graph-projection";
import type { ModelInfo } from "../api/types";
import { labelOf } from "../components/context-receipt-state";
import type { ShellT } from "./text";

/** The role whose model is the model of an ordinary answer (guardian.pack). */
export const ANSWER_ROLE = "chat.default";

/** "anthropic/claude-sonnet-4" -> "claude-sonnet-4": the part after the last slash (the vendor is in the sheet). Empty stays empty. */
export function shortModelName(id: string): string {
  const at = id.lastIndexOf("/");
  const tail = at >= 0 ? id.slice(at + 1) : id;
  return tail || id;
}

/** Where the shown model comes from: the conversation's own (chain), this window's pick (sent with each request, not in the chain), or the chain's answer. */
export type ModelSource = "conversation" | "window" | "chain";

export interface ProvenanceInput {
  /** The model shown in the chip ("" = the default). */
  model: string;
  source: ModelSource;
  resolution: ScopeResolution | null;
  /** Why the chain could not be read, if it could not. */
  resolutionError: string | null;
  role: RoleResolved | null;
  info: ModelInfo | undefined;
}
export interface ProvenanceView {
  effective: string;
  lines: { id: string; text: string }[];
  /** The levels that supplied the value or were overridden by a more specific one, general first. */
  chain: { level: string; text: string }[];
  /** One line naming the other levels (no value / not active), or null when there are none. */
  chainRest: string | null;
  /** The kernel's own sentence about the class, word for word (it is a developer's English sentence): shown folded under "Szczegóły techniczne", never instead of the plain words. */
  technical: string | null;
}

const money = (n: number): string => `$${n >= 1 ? n.toFixed(2).replace(/\.?0+$/, "") : n.toFixed(4).replace(/0+$/, "").replace(/\.$/, "")}`;

/** The price per million tokens: the model list's own pricing (per token, as strings) first, else the role's cost when it is the same model. */
export function priceLine(input: Pick<ProvenanceInput, "info" | "role">, effective: string, t: ShellT): string {
  const pricing = input.info?.pricing as Record<string, unknown> | undefined;
  const prompt = pricing ? Number(pricing.prompt) : NaN, completion = pricing ? Number(pricing.completion) : NaN;
  if (Number.isFinite(prompt) && Number.isFinite(completion) && prompt > 0 && completion >= 0)
    return t("mp_price", { input: money(prompt * 1e6), output: money(completion * 1e6) });
  const per = input.role && (input.role.model === effective) ? input.role.cost?.per_million : undefined;
  if (per && Number.isFinite(per.input) && per.input > 0 && typeof per.output === "number")
    return t("mp_price", { input: money(per.input), output: money(per.output) });
  return t("mp_price_unknown");
}

export function provenanceView(input: ProvenanceInput, t: ShellT, language: string): ProvenanceView {
  const chainValue = typeof input.resolution?.value === "string" ? input.resolution.value : "";
  const effective = input.model || chainValue || input.role?.model || "";
  const lines: { id: string; text: string }[] = [];
  lines.push({ id: "model", text: effective ? t("mp_model", { model: effective }) : t("mp_model_default") });
  if (input.source === "window") lines.push({ id: "set-by", text: t("mp_set_by_window") });
  else if (input.resolutionError !== null) lines.push({ id: "set-by", text: t("mp_unread", { reason: input.resolutionError }) });
  else if (input.resolution?.defined) {
    const level = (input.resolution.chain ?? []).find(l => l.level === input.resolution?.scope);
    lines.push({ id: "set-by", text: t("mp_set_by", { level: labelOf(level?.label, language, input.resolution.scope ?? "") }) });
  } else lines.push({ id: "set-by", text: t("mp_set_by_none") });
  const role = input.role;
  const sameModel = Boolean(role) && (role?.model === effective || effective === "");
  let technical: string | null = null;   // the kernel's sentence (tier.reason), kept verbatim for the folded block
  if (role && role.tier && role.tier.id !== "none" && role.tier.id !== "unknown" && sameModel)
    lines.push({ id: "class", text: t("mp_class", { class: role.tier.label || role.tier.id }) });
  else if (role && role.tier?.id === "unknown" && role.tier.reason && sameModel) {
    lines.push({ id: "class", text: t("mp_class_plain") });   // plain words here; the kernel's sentence says why, folded below
    technical = role.tier.reason;
  } else if (role && effective && role.model !== effective) lines.push({ id: "class", text: t("mp_class_unknown", { reason: t("mp_reason_other_model", { model: role.model ?? "" }) }) });
  else if (role?.tier?.reason) {
    lines.push({ id: "class", text: t("mp_class_plain") });
    technical = role.tier.reason;
  } else lines.push({ id: "class", text: t("mp_class_unknown", { reason: t("mp_reason_no_role") }) });
  lines.push({ id: "price", text: priceLine(input, effective, t) });
  const levels = input.resolution?.chain ?? [];
  const said = (l: { role: string }) => l.role === "supplied" || l.role === "overridden";
  const chain = levels.filter(said).map(l => ({ level: l.level, text: `${labelOf(l.label, language, l.level)} — ${t(`mp_role_${l.role}`)}` }));
  const rest = levels.filter(l => !said(l)).map(l => labelOf(l.label, language, l.level));
  return { effective, lines, chain, chainRest: rest.length ? t("mp_chain_rest", { levels: rest.join(", ") }) : null, technical };
}
