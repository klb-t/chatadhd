import { featureMessage, type PresentationFeature } from "../onboarding/presentation.mjs";
import type { JsonValue } from "../onboarding/types";
import type { ClientStepSpec } from "./client-device";

/** Presentation feature holding the texts (`presentation.action_proposal`). */
export const ACTION_PROPOSAL_FEATURE = "action_proposal";
/** Final stream chunk types of a chat input that was a command (R54). */
export const ACTION_PROPOSAL_CHUNK = "action_proposal";
export const ACTION_RESULT_CHUNK = "action_result";

/** Index signatures keep the module lexically scannable by the literal guard. */
interface Row { [key: string]: JsonValue }
interface Meta { [key: string]: unknown }

/** Money is a range where the model registry lists a price, and unknown (with the reason) where it does not: never zero. */
export interface ProposalMoney { known?: boolean; low?: number | null; high?: number | null; currency?: string; reason?: string }
export interface ProposalItem { action?: string; title?: string; cost?: number; implementation?: string }
export interface ProposalConfirmation { action?: string; title?: string; reason?: unknown; initiator?: string }
/** A parameter of the action as the router saw it: given by the words (from phrase), by the user (explicit) or a setting, or still open. */
export interface ProposalParam { name?: string; required?: boolean; given?: boolean; value?: unknown; from?: string; default?: unknown }
/** A proposal that grows the catalogue (R55): an action, a repeated chain or a group; written to the user's overlay only. */
export interface ProposalDefinition { kind?: string; id?: string; overlay?: string; node?: string }
/** What a "no" leaves of the user's sentence: it can still go to the model as an ordinary message (setting intent_routing_on_reject).
 *  status: open (waiting for the choice), sent (taken), declined (closed); mode: the preset that made the offer. */
export interface ProposalOffer { action?: string; status?: string; message_id?: string; mode?: string }
/** What an accepted definition wrote: the overlay file(s) and the entry itself (user overlay only), kept on the proposal.
 *  `undone` is set once the definition was taken back. */
export interface ProposalWritten { kind?: string; id?: string; overlays?: string[]; entry?: unknown; undone?: boolean }
export interface ActionProposal {
  kind?: string;
  offer?: ProposalOffer | null;
  written?: ProposalWritten | null;
  params?: ProposalParam[];
  definition?: ProposalDefinition | null;
  replaces?: boolean;
  status?: string;
  run_id?: string;
  receipt_id?: string;
  title?: string;
  lang?: string;
  items?: ProposalItem[];
  cost?: { declared?: number | null; model_calls?: number | null; input_bytes?: number | null; money?: ProposalMoney | null };
  needs_confirmation?: ProposalConfirmation[];
  runnable?: boolean;
}
export interface ActionRunPending {
  kind?: string;
  receipt_id?: string | null;
  operation_id?: string | null;
  /** A run paused on a device action of this client (kind client_step): what the client has to do. */
  client_step?: ClientStepSpec;
  usage_pending?: { status?: string; receipt_id?: string | null; operation_id?: string | null; baseline_key?: string | null; projected?: { [name: string]: { [field: string]: unknown } } };
}
/** What a completed run left in the graph: the form nodes of its result, the content node they express, the run node. */
export interface ProducedMeta { target?: string; forms?: string[]; content?: string | null; run_node?: string | null }
export interface ActionRunMeta { run_id?: string; status?: string; lang?: string; pending?: ActionRunPending | null; produced?: ProducedMeta | null; client_id?: string }

/** What the context guardian adds to a proposal of its own (metadata.guardian): the claim, its scope and weight, the evidence, the exceptions and,
 *  for a run of clarifications, the one instruction that would stand in for it. */
export interface GuardianMeta {
  traffic?: boolean; hypothesis?: string; signal?: string; claim?: string; scope?: string; weight?: number; count?: number; promotes?: string;
  mode?: string; instruction?: string; evidence?: unknown[]; exceptions?: { text?: string; explicit?: boolean }[];
}

export type AnswerKind = "proposal" | "confirmation" | "usage" | "client";
/** What a message of the conversation asks the user to answer, if anything. */
export interface PendingAnswer {
  kind: AnswerKind;
  runId: string;
  receiptId: string;
  /** The proposal that is being answered (kind proposal only). */
  proposal: ActionProposal | null;
  /** The usage guard's receipt (kind usage only): shown by the usage block. */
  usage: ActionRunPending["usage_pending"] | null;
  /** The step this client carries out (kind client only). */
  clientStep: ClientStepSpec | null;
  /** The guardian's own details of the proposal (kind proposal only), or null. */
  guardian: GuardianMeta | null;
  /** W3: the client whose sentence started the run (kind client only; "" when the kernel did not name one). */
  clientId?: string;
}

/** Steps only the asking client carries out (W3 gap A): its own windows. A preset - a prefix of the action id, the list is open. */
export const ASKING_CLIENT_ONLY = ["window.", "layout.", "artifact."];

/** W3: whether this page carries out the step of the answer. A step on the asking client's own windows is done by that client alone: another
 *  page showing the same conversation (another device, another tab) leaves it. A step without a named client, or any other step, stays as before. */
export function carriesOutHere(answer: PendingAnswer, ownClientId: string, only: readonly string[] = ASKING_CLIENT_ONLY): boolean {
  if (answer.kind !== "client" || !answer.clientStep) return true;
  const action = typeof answer.clientStep.action === "string" ? answer.clientStep.action : "";
  if (!only.some((prefix) => action.startsWith(prefix))) return true;
  const asker = answer.clientId ?? "";
  return asker === "" || asker === ownClientId;
}

function text(value: unknown): string {
  return typeof value === "string" ? value : "";
}

/** The question a message carries: a proposal waiting for yes/no, or a paused run waiting for its receipt answer. */
export function pendingAnswer(metadata: Meta | undefined): PendingAnswer | null {
  if (!metadata) return null;
  const proposal = metadata.action_proposal as ActionProposal | undefined;
  if (proposal && proposal.status === "pending" && text(proposal.run_id) !== "" && text(proposal.receipt_id) !== "") {
    const guardian = metadata.guardian && typeof metadata.guardian === "object" ? metadata.guardian as GuardianMeta : null;
    return { kind: "proposal", runId: text(proposal.run_id), receiptId: text(proposal.receipt_id), proposal, usage: null, clientStep: null, guardian };
  }
  const run = metadata.action_run as ActionRunMeta | undefined;
  const pending = run && run.pending ? run.pending : null;
  if (run && pending && text(run.run_id) !== "" && text(pending.receipt_id) !== "") {
    if (pending.kind === "usage") {
      return { kind: "usage", runId: text(run.run_id), receiptId: text(pending.receipt_id), proposal: null, usage: pending.usage_pending ?? null, clientStep: null, guardian: null };
    }
    if (pending.kind === "client_step" && pending.client_step) {
      return { kind: "client", runId: text(run.run_id), receiptId: text(pending.receipt_id), proposal: null, usage: null, clientStep: pending.client_step, guardian: null,
        clientId: text(run.client_id) };
    }
    return { kind: "confirmation", runId: text(run.run_id), receiptId: text(pending.receipt_id), proposal: null, usage: null, clientStep: null, guardian: null };
  }
  return null;
}

/** The language a message of the router was written in (its proposal's, else its run's), or null: every line the page adds to that
 *  message (title, buttons, status) is said in it, so one answer is in one language. */
export function messageLanguage(metadata: Meta | undefined): string | null {
  const proposal = metadata ? (metadata.action_proposal as ActionProposal | undefined) : undefined;
  const run = metadata ? (metadata.action_run as ActionRunMeta | undefined) : undefined;
  const lang = text(proposal ? proposal.lang : undefined) || text(run ? run.lang : undefined);
  return lang === "" ? null : lang;
}

/** The terminal state a message reports, for a small badge (executed, rejected, denied ...). */
export function proposalState(metadata: Meta | undefined): string | null {
  const proposal = metadata ? (metadata.action_proposal as ActionProposal | undefined) : undefined;
  if (!proposal || !proposal.status || proposal.status === "pending") return null;
  return proposal.status;
}

/** An open offer to send a rejected command's sentence as a message: the proposal message that carries it, the sentence's message,
 *  and whether the setting asks the client to send it at once. */
export interface OfferView { proposalId: string; sentenceId: string; auto: boolean }

/** The open offer a proposal message carries, or null (answered otherwise, closed, or none was made). */
export function openOffer(metadata: Meta | undefined, messageId: string): OfferView | null {
  const proposal = metadata ? (metadata.action_proposal as ActionProposal | undefined) : undefined;
  const offer = proposal && proposal.status === "rejected" && proposal.offer ? proposal.offer : null;
  if (!offer || offer.status !== "open" || text(offer.message_id) === "" || messageId === "") return null;
  return { proposalId: messageId, sentenceId: text(offer.message_id), auto: offer.mode === "send_as_message" };
}

/** How an offer ended, for a small line under the proposal: sent or declined; null while open or without an offer. */
export function offerState(metadata: Meta | undefined): string | null {
  const proposal = metadata ? (metadata.action_proposal as ActionProposal | undefined) : undefined;
  const status = proposal && proposal.offer ? text(proposal.offer.status) : "";
  return status === "sent" || status === "declined" ? status : null;
}

/** The chat request that takes an offer. The engine reads the sentence from the conversation; `message` is the same sentence,
 *  for the transports that require one. */
export function offerRequest(offer: OfferView, sentence: string, convId: string) {
  return { message: sentence, conv_id: convId, send_as_message: offer.proposalId };
}

export interface WrittenView { kind: string; id: string; overlays: string[]; entry: string; undone: boolean }

/** What an accepted definition wrote, as the block shows it (the overlay entry as formatted JSON); null for any other message. */
export function writtenView(metadata: Meta | undefined): WrittenView | null {
  const proposal = metadata ? (metadata.action_proposal as ActionProposal | undefined) : undefined;
  const written = proposal && proposal.written ? proposal.written : null;
  if (!written || text(written.id) === "") return null;
  const overlays: string[] = [];
  if (Array.isArray(written.overlays)) {
    for (const file of written.overlays) if (typeof file === "string" && file !== "") overlays.push(file);
  }
  return {
    kind: text(written.kind), id: text(written.id), overlays,
    entry: written.entry === undefined || written.entry === null ? "" : JSON.stringify(written.entry, null, 2),
    undone: written.undone === true || (proposal !== undefined && proposal.status === "undone"),
  };
}

/** Which call takes what was written back: a rule is switched off, a group has its own removal, an action and a repeated chain are both actions. */
export function undoCall(view: WrittenView): "groupsRemove" | "actionsRemove" | "rulesDisable" {
  if (view.kind === "rule") return "rulesDisable";
  return view.kind === "define_group" ? "groupsRemove" : "actionsRemove";
}

/** The first call of taking something back: the removal is proposed and nothing is removed yet; the kernel answers with the proposal and its receipt. */
export function undoRequest(view: WrittenView) {
  return { id: view.id, propose: true, ref: "web" };
}

/** The answer of the click: the user's explicit yes to exactly the removal that was proposed, with the receipt of that proposal (the rules of the
 *  scope still apply). null when the kernel's answer is no proposal that can be answered (a denial, a failure). */
export function undoConfirmation(proposed: { id?: string; status?: string; pending?: { receipt_id?: string | null } | null }) {
  const receipt = proposed.pending && typeof proposed.pending.receipt_id === "string" ? proposed.pending.receipt_id : "";
  if (proposed.status !== "proposed" || typeof proposed.id !== "string" || proposed.id === "" || receipt === "") return null;
  return { run_id: proposed.id, approved: true, receipt_id: receipt, ref: "web" };
}

export interface ProducedView { target: string; forms: string[]; content: string; runNode: string }

/** The graph nodes a result message points at: the router's report (action_run.produced) or a posted result message
 *  (action_result), which is itself the form node. */
export function producedView(metadata: Meta | undefined, messageId: string = ""): ProducedView | null {
  if (!metadata) return null;
  const run = metadata.action_run as ActionRunMeta | undefined;
  const made = run && run.produced ? run.produced : null;
  const forms: string[] = [];
  if (made && Array.isArray(made.forms)) {
    for (const id of made.forms) if (typeof id === "string" && id !== "") forms.push(id);
  }
  if (forms.length > 0 && made) return { target: text(made.target), forms, content: text(made.content), runNode: text(made.run_node) };
  const posted = metadata.action_result as { content?: string; run_node?: string } | undefined;
  if (posted && messageId !== "") return { target: "conversation_message", forms: [messageId], content: text(posted.content), runNode: text(posted.run_node) };
  return null;
}

/** The line that says where the result went; null without a catalog. */
export function producedText(feature: PresentationFeature | null, view: ProducedView): string | null {
  if (!feature) return null;
  try { return featureMessage(feature, "produced", { target: view.target, nodes: view.forms.join(", ") }); }
  catch { return null; }
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
  /** Arguments the words gave (name=value) and optional ones that keep their defaults (name (default)). */
  args: string[];
  open: string[];
  /** The kind and id of a definition the proposal would write; null for an ordinary command. */
  definition: { kind: string; id: string; overlay: string } | null;
  replaces: boolean;
  plan: string;
  cost: { cost: string | null; calls: string | null; bytes: string | null } | null;
  /** Null when the proposal says nothing about money; known false = unknown, with the reason. */
  money: { known: boolean; low: string | null; high: string | null; currency: string; reason: string } | null;
  confirmations: string[];
  /** The guardian's details of its own proposal; null for any other. */
  guardian: { claim: string; scope: string; weight: string; count: string; evidence: string; exceptions: string[]; instruction: string } | null;
}

function amount(value: unknown): string | null {
  return typeof value === "number" && Number.isFinite(value) ? String(value) : null;
}

/** A sum of money for people: enough digits to tell a hundredth of a cent from nothing. */
function sum(value: unknown): string | null {
  if (typeof value !== "number" || !Number.isFinite(value)) return null;
  return value >= 0.01 ? value.toFixed(2) : value.toFixed(4);
}

function shown(value: unknown): string {
  return typeof value === "string" ? value : JSON.stringify(value);
}

export function proposalView(answer: PendingAnswer): ProposalView {
  const proposal = answer.proposal;
  const args: string[] = [];
  const open: string[] = [];
  const params = proposal && proposal.params ? proposal.params : [];
  for (const param of params) {
    const name = text(param.name);
    if (name === "") continue;
    if (param.given === true) args.push(name + "=" + shown(param.value));
    else if (param.required !== true && param.default !== undefined && param.default !== null) open.push(name + " (" + shown(param.default) + ")");
  }
  const made = proposal && proposal.definition ? proposal.definition : null;
  const titles: string[] = [];
  const items = proposal && proposal.items ? proposal.items : [];
  for (const item of items) titles.push(item.title ? item.title : text(item.action));
  const cost = proposal && proposal.cost ? proposal.cost : null;
  const confirmations: string[] = [];
  const asked = proposal && proposal.needs_confirmation ? proposal.needs_confirmation : [];
  for (const entry of asked) confirmations.push(entry.title ? entry.title : text(entry.action));
  const money = cost && cost.money ? cost.money : null;
  return {
    answerable: answer.runId !== "" && answer.receiptId !== "",
    args,
    open,
    definition: made ? { kind: text(made.kind), id: text(made.id), overlay: text(made.overlay) } : null,
    replaces: proposal !== null && proposal.replaces === true,
    plan: titles.join(" -> "),
    cost: cost ? { cost: amount(cost.declared), calls: amount(cost.model_calls), bytes: amount(cost.input_bytes) } : null,
    money: money === null ? null : {
      known: money.known === true && sum(money.low) !== null && sum(money.high) !== null,
      low: sum(money.low), high: sum(money.high), currency: text(money.currency), reason: text(money.reason),
    },
    confirmations,
    guardian: guardianOf(answer.guardian),
  };
}

function guardianOf(meta: GuardianMeta | null): ProposalView["guardian"] {
  if (!meta || text(meta.hypothesis) === "") return null;
  const exceptions: string[] = [];
  if (Array.isArray(meta.exceptions)) {
    for (const e of meta.exceptions) if (e && text(e.text) !== "") exceptions.push(text(e.text));
  }
  return {
    claim: text(meta.claim), scope: text(meta.scope), weight: typeof meta.weight === "number" ? meta.weight.toFixed(2) : "", count: amount(meta.count) ?? "",
    evidence: Array.isArray(meta.evidence) ? String(meta.evidence.length) : "", exceptions, instruction: text(meta.instruction),
  };
}

export interface ProposalTexts {
  title: string | null;
  args: string | null;
  argsOpen: string | null;
  definition: string | null;
  replaces: string | null;
  plan: string | null;
  cost: string | null;
  money: string | null;
  confirmNeeded: string | null;
  guardianClaim: string | null;
  guardianScope: string | null;
  guardianExceptions: string | null;
  guardianInstruction: string | null;
  guardianNote: string | null;
  confirm: string | null;
  reject: string | null;
  usageHint: string | null;
  rejected: string | null;
  failed: (reason: string) => string;
  status: (state: string) => string | null;
}

function moneyText(say: (id: string, parameters?: Row) => string | null, view: ProposalView): string | null {
  const money = view.money;
  if (money === null || view.cost === null || view.cost.calls === "0") return null;
  if (money.known) return say("cost_money", { low: money.low ?? "", high: money.high ?? "", currency: money.currency });
  return say("cost_money_unknown", { reason: money.reason === "" ? (say("unknown") ?? "-") : money.reason });
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
  const made = view.definition;
  const g = view.guardian;
  return {
    title: say(answer.kind === "confirmation" ? "title_confirmation" : g !== null ? "title_guardian" : made !== null ? "title_definition" : "title"),
    guardianClaim: g === null || g.claim === "" ? null : say("guardian_claim", { claim: g.claim }),
    guardianScope: g === null ? null : say("guardian_scope", { scope: say("guardian_scope_" + g.scope) ?? g.scope, weight: g.weight, count: g.count, evidence: g.evidence }),
    guardianExceptions: g === null || g.exceptions.length === 0 ? null : say("guardian_exceptions", { list: g.exceptions.join("; ") }),
    guardianInstruction: g === null || g.instruction === "" ? null : say("guardian_instruction", { instruction: g.instruction }),
    guardianNote: g === null ? null : say("guardian_note"),
    args: view.args.length === 0 ? null : say("args", { args: view.args.join(", ") }),
    argsOpen: view.open.length === 0 ? null : say("args_open", { params: view.open.join(", ") }),
    definition: made === null ? null : say("definition_" + made.kind, { id: made.id, overlay: made.overlay }),
    replaces: view.replaces ? say("rule_replaces") : null,
    plan: view.plan === "" ? null : say("plan", { plan: view.plan }) ?? view.plan,
    cost: cost === null ? null : say("cost", { cost: shown(cost.cost), calls: shown(cost.calls), bytes: shown(cost.bytes) }),
    money: moneyText(say, view),
    confirmNeeded: view.confirmations.length === 0 ? null : say("confirm_needed", { what: view.confirmations.join("; ") }) ?? view.confirmations.join("; "),
    confirm: say("confirm"),
    reject: say("reject"),
    usageHint: say("usage_hint"),
    rejected: say("rejected"),
    failed: (reason) => say("answer_failed", { reason }) ?? reason,
    status: (state) => say("status_" + state),
  };
}

export interface OfferTexts {
  /** The button: send the sentence as a message. */
  send: string | null;
  hint: string | null;
  failed: (reason: string) => string;
  /** The line for an offer that ended: sent or declined. */
  state: (state: string) => string | null;
}

/** The words of the offer, from the catalog; without one the machine names are shown. */
export function offerTexts(feature: PresentationFeature | null): OfferTexts {
  const say = (id: string, parameters: Row = {}): string | null => {
    if (!feature) return null;
    try { return featureMessage(feature, id, parameters); }
    catch { return null; }
  };
  return {
    send: say("offer"),
    hint: say("offer_hint"),
    failed: (reason) => say("offer_failed", { reason }) ?? reason,
    state: (state) => say("offer_" + state),
  };
}

export interface WrittenTexts {
  /** The line that says what was written and where. */
  written: string | null;
  /** The label over the entry itself. */
  entry: string | null;
  undo: string | null;
  undone: string | null;
  failed: (reason: string) => string;
}

/** The words of the written definition, from the catalog; without one the machine names are shown. */
export function writtenTexts(feature: PresentationFeature | null, view: WrittenView): WrittenTexts {
  const say = (id: string, parameters: Row = {}): string | null => {
    if (!feature) return null;
    try { return featureMessage(feature, id, parameters); }
    catch { return null; }
  };
  return {
    written: say(view.kind === "rule" ? "written_rule" : "written", { id: view.id, overlay: view.overlays.join(", ") }),
    entry: say("written_entry"),
    undo: say("undo"),
    undone: say(view.kind === "rule" ? "undone_rule" : "undone"),
    failed: (reason) => say("undo_failed", { reason }) ?? reason,
  };
}
