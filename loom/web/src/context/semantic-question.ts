import { featureMessage, type PresentationFeature } from "../onboarding/presentation.mjs";
import type { JsonValue } from "../onboarding/types";
import type { UsagePendingReceipt } from "./usage-pending";

/** Presentation feature holding the texts (`presentation.semantic_question`). */
export const SEMANTIC_QUESTION_FEATURE = "semantic_question";
/** The run id the kernel's client-step event carries for the question of the background analysis ("semantic:<receipt>"): the page keeps one feed. */
export const SEMANTIC_QUESTION_RUN_PREFIX = "semantic:";
/** Who answers for the web page in the usage confirmation of the worker. */
export const SEMANTIC_ANSWER_REF = "web";
/** Statuses of a question once it is not pending any more. */
export const SEMANTIC_ANSWERED = ["approved", "declined", "stale"];

/** Index signature keeps the module lexically scannable by the literal guard. */
interface Row { [key: string]: JsonValue }
interface Meta { [key: string]: unknown }

export interface SemanticQuestion {
  /** pending: asks; approved | declined | stale: it was answered or is out of date. */
  status: string;
  /** The language the question was asked in: the lines the page adds are said in it. */
  lang: string | null;
  kind: string;
  receiptId: string;
  /** The guard's receipt, in the shape the usage block shows. */
  receipt: UsagePendingReceipt;
}

function text(value: unknown): string {
  return typeof value === "string" ? value : "";
}

/** The question a message of the conversation asks (metadata.semantic_question), or null: no receipt, no question. */
export function semanticQuestion(metadata: Meta | undefined): SemanticQuestion | null {
  const row = metadata ? metadata.semantic_question : undefined;
  if (row === null || typeof row !== "object") return null;
  const question = row as Meta;
  const receiptId = text(question.receipt_id);
  if (receiptId === "") return null;
  const receipt = (question.receipt !== null && typeof question.receipt === "object" ? question.receipt : {}) as UsagePendingReceipt;
  const lang = text(question.lang);
  return { status: text(question.status) || "pending", lang: lang === "" ? null : lang, kind: text(question.kind) || "batch", receiptId,
    receipt: { ...receipt, receipt_id: receiptId } };
}

/** The question is open: it can be answered with the buttons. */
export function semanticQuestionOpen(question: SemanticQuestion): boolean {
  return question.status === "pending";
}

/** The words for a question that was answered (or is out of date); null while it asks or when the catalog has none. */
export function semanticAnsweredText(feature: PresentationFeature | null, question: SemanticQuestion): string | null {
  if (!feature || !SEMANTIC_ANSWERED.includes(question.status)) return null;
  const parameters: Row = {};
  try { return featureMessage(feature, "status_" + question.status, parameters); } catch { return null; }
}

/** The words for an answer that could not be sent. */
export function semanticFailedText(feature: PresentationFeature | null, reason: string): string {
  if (!feature) return reason;
  try { return featureMessage(feature, "confirm_failed", { reason }); } catch { return reason; }
}
