import { featureMessage, type PresentationFeature } from "../onboarding/presentation.mjs";
import type { JsonValue } from "../onboarding/types";

/** Presentation feature holding the texts (`presentation.action_proposal`). */
export const ACTION_PROPOSAL_FEATURE = "action_proposal";
/** Final stream chunk types of a chat input that was a command (R54). */
export const ACTION_PROPOSAL_CHUNK = "action_proposal";
export const ACTION_RESULT_CHUNK = "action_result";

/** Index signatures keep the module lexically scannable by the literal guard. */
interface Row { [key: string]: JsonValue }
interface Meta { [key: string]: unknown }

export interface ProposalItem { action?: string; title?: string; cost?: number; implementation?: string }
export interface ProposalConfirmation { action?: string; title?: string; reason?: unknown; initiator?: string }
export interface ActionProposal {
  status?: string;
  run_id?: string;
  receipt_id?: string;
  title?: string;
  lang?: string;
  items?: ProposalItem[];
  cost?: { declared?: number | null; model_calls?: number | null; input_bytes?: number | null };
  needs_confirmation?: ProposalConfirmation[];
  runnable?: boolean;
}
export interface ActionRunPending {
  kind?: string;
  receipt_id?: string | null;
  operation_id?: string | null;
  usage_pending?: { status?: string; receipt_id?: string | null; operation_id?: string | null; baseline_key?: string | null; projected?: { [name: string]: { [field: string]: unknown } } };
}
export interface ActionRunMeta { run_id?: string; status?: string; pending?: ActionRunPending | null }

export type AnswerKind = "proposal" | "confirmation" | "usage";
/** What a message of the conversation asks the user to answer, if anything. */
export interface PendingAnswer {
  kind: AnswerKind;
  runId: string;
  receiptId: string;
  /** The proposal that is being answered (kind proposal only). */
  proposal: ActionProposal | null;
  /** The usage guard's receipt (kind usage only): shown by the usage block. */
  usage: ActionRunPending["usage_pending"] | null;
}

function text(value: unknown): string {
  return typeof value === "string" ? value : "";
}

/** The question a message carries: a proposal waiting for yes/no, or a paused run waiting for its receipt answer. */
export function pendingAnswer(metadata: Meta | undefined): PendingAnswer | null {
  if (!metadata) return null;
  const proposal = metadata.action_proposal as ActionProposal | undefined;
  if (proposal && proposal.status === "pending" && text(proposal.run_id) !== "" && text(proposal.receipt_id) !== "") {
    return { kind: "proposal", runId: text(proposal.run_id), receiptId: text(proposal.receipt_id), proposal, usage: null };
  }
  const run = metadata.action_run as ActionRunMeta | undefined;
  const pending = run && run.pending ? run.pending : null;
  if (run && pending && text(run.run_id) !== "" && text(pending.receipt_id) !== "") {
    if (pending.kind === "usage") {
      return { kind: "usage", runId: text(run.run_id), receiptId: text(pending.receipt_id), proposal: null, usage: pending.usage_pending ?? null };
    }
    return { kind: "confirmation", runId: text(run.run_id), receiptId: text(pending.receipt_id), proposal: null, usage: null };
  }
  return null;
}

/** The terminal state a message reports, for a small badge (executed, rejected, denied ...). */
export function proposalState(metadata: Meta | undefined): string | null {
  const proposal = metadata ? (metadata.action_proposal as ActionProposal | undefined) : undefined;
  if (!proposal || !proposal.status || proposal.status === "pending") return null;
  return proposal.status;
}

/** The body of `actions.confirm`: the answer plus the receipt it answers. */
export function answerRequest(answer: PendingAnswer, approved: boolean, ref: string) {
  return { run_id: answer.runId, approved, receipt_id: answer.receiptId, ref };
}

/** A chat request re-sent after the model router's own call was held by the usage guard (stage route_intent). */
export function routeConfirmedRequest(request: { message: string }, receipt: { receipt_id?: string | null; operation_id?: string | null }, approved: boolean, ref: string) {
  return { ...request, route_usage_confirmation: { approved, receipt_id: receipt.receipt_id ?? "", operation_id: receipt.operation_id ?? "", ref } };
}

export interface ProposalView {
  answerable: boolean;
  plan: string;
  cost: { cost: string | null; calls: string | null; bytes: string | null } | null;
  confirmations: string[];
}

function amount(value: unknown): string | null {
  return typeof value === "number" && Number.isFinite(value) ? String(value) : null;
}

export function proposalView(answer: PendingAnswer): ProposalView {
  const proposal = answer.proposal;
  const titles: string[] = [];
  const items = proposal && proposal.items ? proposal.items : [];
  for (const item of items) titles.push(item.title ? item.title : text(item.action));
  const cost = proposal && proposal.cost ? proposal.cost : null;
  const confirmations: string[] = [];
  const asked = proposal && proposal.needs_confirmation ? proposal.needs_confirmation : [];
  for (const entry of asked) confirmations.push(entry.title ? entry.title : text(entry.action));
  return {
    answerable: answer.runId !== "" && answer.receiptId !== "",
    plan: titles.join(" -> "),
    cost: cost ? { cost: amount(cost.declared), calls: amount(cost.model_calls), bytes: amount(cost.input_bytes) } : null,
    confirmations,
  };
}

export interface ProposalTexts {
  title: string | null;
  plan: string | null;
  cost: string | null;
  confirmNeeded: string | null;
  confirm: string | null;
  reject: string | null;
  usageHint: string | null;
  rejected: string | null;
  failed: (reason: string) => string;
  status: (state: string) => string | null;
}

/** All user-visible words come from the catalog; without one the machine names are shown. */
export function proposalTexts(feature: PresentationFeature | null, answer: PendingAnswer, view: ProposalView): ProposalTexts {
  const say = (id: string, parameters: Row = {}): string | null => {
    if (!feature) return null;
    try { return featureMessage(feature, id, parameters); }
    catch { return null; }
  };
  const unknown = say("unknown") ?? "-";
  const shown = (value: string | null) => (value === null ? unknown : value);
  const cost = view.cost;
  return {
    title: say(answer.kind === "confirmation" ? "title_confirmation" : "title"),
    plan: view.plan === "" ? null : say("plan", { plan: view.plan }) ?? view.plan,
    cost: cost === null ? null : say("cost", { cost: shown(cost.cost), calls: shown(cost.calls), bytes: shown(cost.bytes) }),
    confirmNeeded: view.confirmations.length === 0 ? null : say("confirm_needed", { what: view.confirmations.join("; ") }) ?? view.confirmations.join("; "),
    confirm: say("confirm"),
    reject: say("reject"),
    usageHint: say("usage_hint"),
    rejected: say("rejected"),
    failed: (reason) => say("answer_failed", { reason }) ?? reason,
    status: (state) => say("status_" + state),
  };
}
