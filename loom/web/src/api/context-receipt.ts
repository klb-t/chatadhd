// M1-W1a: the calls and the shapes of "Co wysłaliśmy" (kernel M1-K1b). Two actions of the catalogue, no new C ABI function:
//   context.receipt  POST /api/context/receipt  {msg_id, conversation_id?} -> a run; result.data = {msg_id, request_msg_id, receipt} or
//                    {msg_id, request_msg_id, missing_reason: "before_m1", also_when: [...]}  (the receipt lives with the message the person sent;
//                    asked about an answer, the kernel gives the receipt of the request that made it)
//   context.modify   POST /api/context/modify   {step: wider|narrower|deeper|shallower} | {profile: <selector_profiles id>} | {reset: true},
//                    conversation_id -> a run: completed (result.parts[0].text, result.data), awaiting_usage (the kernel's x10 question: answer it
//                    with actions.confirm {run_id, approved, receipt_id, ref}), rejected (the person said no) or failed.
// The receipt itself is the kernel contract loom.context_receipt/1; only the fields the sheet reads are typed, the rest stays as it came.
import type { ActionRun } from "./types";

export interface ReceiptItem {
  ref: string;
  label?: string;
  /** instruction | rule | memory_node | graph_node | kb_claim | profile_slot | message:L0..L3 | graph_packet | task | question (and any new string). */
  source_kind?: string;
  why?: string;
  tokens_est?: number;
}
export interface ReceiptSection { class: string; items: ReceiptItem[]; items_omitted?: number; tokens_est: number }
export interface ReceiptDropped { ref?: string; reason: string; count?: number; refs?: string[] }
export interface ReceiptProfile {
  id: string | null;
  label?: { pl?: string; en?: string } | string | null;
  /** turn | request | settings | conversation | person ... : where the choice came from. */
  source?: string;
  /** preset | calibration | legacy (from the selector_profiles row). */
  status?: string;
  applied?: Record<string, unknown>;
  unapplied?: unknown[];
  ignored?: unknown;
}
export interface ReceiptGuard {
  resource?: string; bytes?: number; baseline?: number | null; ratio?: number | null; growth_factor?: number | null;
  asked?: boolean; confirmed?: boolean; status?: string; unknown_reason?: string;
}
export interface ContextReceipt {
  schema?: string;
  order?: { name?: string; classes?: string[] };
  profile?: ReceiptProfile;
  profile_prefix?: { level?: string; slots?: number; unavailable?: string; state?: string } | null;
  modifiers?: { profile?: string | null; breadth_delta?: string[] | null; depth_delta?: string[] | null; scope?: string | null };
  guard?: ReceiptGuard | null;
  flow?: { flow?: string; renderer?: string; output?: string; mode?: string; model_class?: string; why?: string } | null;
  renderer?: string;
  sections: ReceiptSection[];
  dropped?: ReceiptDropped[];
  tokens_est?: number;
  sha256?: string;
  truncated?: boolean;
  /** Not in loom.context_receipt/1 today: if a later kernel adds a price, the sheet shows it; without it the sheet says "unknown". */
  cost_usd?: number | null;
}

export type ContextStep = "wider" | "narrower" | "deeper" | "shallower";
export interface ContextReceiptRequest { msg_id?: string; conversation_id?: string }
export interface ContextModifyRequest { step?: ContextStep | string; profile?: string; reset?: boolean; conversation_id?: string }

/** What a receipt run answers: the receipt, "no receipt" with the kernel's reason, or a failure with the kernel's message. */
export type ReceiptAnswer =
  | { kind: "receipt"; msgId: string; requestMsgId: string; receipt: ContextReceipt }
  | { kind: "missing"; msgId: string; requestMsgId: string; reason: string; alsoWhen: string[] }
  | { kind: "failed"; message: string };

export function readReceiptRun(run: ActionRun | null | undefined): ReceiptAnswer {
  if (!run || typeof run !== "object") return { kind: "failed", message: "no_answer" };
  if (run.status !== "completed") return { kind: "failed", message: run.error?.message ?? run.status ?? "no_answer" };
  const data = (run.result?.data ?? null) as Record<string, unknown> | null;
  if (!data || typeof data !== "object") return { kind: "failed", message: "no_data" };
  const msgId = typeof data.msg_id === "string" ? data.msg_id : "";
  const requestMsgId = typeof data.request_msg_id === "string" ? data.request_msg_id : msgId;
  const receipt = data.receipt as ContextReceipt | undefined;
  if (receipt && typeof receipt === "object" && Array.isArray(receipt.sections)) return { kind: "receipt", msgId, requestMsgId, receipt };
  if (typeof data.missing_reason === "string")
    return { kind: "missing", msgId, requestMsgId, reason: data.missing_reason, alsoWhen: Array.isArray(data.also_when) ? data.also_when.filter((x): x is string => typeof x === "string") : [] };
  return { kind: "failed", message: "receipt_shape" };
}

/** What a context.modify run answers: applied (the kernel's sentence and what waits for the next message), the x10 question, declined or failed. */
export type ModifyAnswer =
  | { kind: "applied"; text: string; waiting: Record<string, unknown>; ratio: number | null }
  | { kind: "question"; runId: string; receiptId: string; operationId: string | null; usage: Record<string, unknown> }
  | { kind: "declined"; text: string }
  | { kind: "failed"; message: string };

export function readModifyRun(run: ActionRun | null | undefined): ModifyAnswer {
  if (!run || typeof run !== "object") return { kind: "failed", message: "no_answer" };
  const text = typeof run.result?.parts?.[0]?.text === "string" ? (run.result.parts[0].text as string) : "";
  if (run.status === "awaiting_usage" && run.pending?.receipt_id) {
    const pending = run.pending as Record<string, unknown>;
    const usage = (pending.usage_pending ?? {}) as Record<string, unknown>;
    return { kind: "question", runId: run.id, receiptId: String(run.pending.receipt_id), operationId: run.pending.operation_id ?? null, usage };
  }
  if (run.status === "completed") {
    const data = (run.result?.data ?? {}) as Record<string, unknown>;
    const guard = (data.guard && typeof data.guard === "object" ? data.guard : {}) as Record<string, unknown>;
    return { kind: "applied", text, waiting: (data.waiting && typeof data.waiting === "object" ? data.waiting : {}) as Record<string, unknown>,
      ratio: typeof guard.ratio === "number" && Number.isFinite(guard.ratio) ? guard.ratio : null };
  }
  if (run.status === "rejected") return { kind: "declined", text: run.error?.message ?? text };
  return { kind: "failed", message: run.error?.message ?? text ?? run.status };
}
