import { featureMessage, type PresentationFeature } from "../onboarding/presentation.mjs";
import type { JsonValue } from "../onboarding/types";

/** Presentation feature holding the texts (`presentation.chat_usage`). */
export const USAGE_PENDING_FEATURE = "chat_usage";
/** Final stream chunk type of a send paused by the shared usage guard. */
export const USAGE_PENDING_CHUNK = "usage_pending";

/** Index signature keeps the module lexically scannable by the literal guard. */
interface Row { [key: string]: JsonValue }
interface ReceiptRow { [key: string]: unknown }
/** A spending limit that holds or refuses (P18): {period, on_exceed, code, reason: {language: text}}; the texts come from the kernel's data. */
export interface UsageLimit { period?: string; on_exceed?: string; code?: string; reason?: { [language: string]: unknown } }
export interface UsagePendingReceipt {
  status?: string;
  operation_id?: string | null;
  receipt_id?: string | null;
  baseline_key?: string | null;
  projected?: { [name: string]: ReceiptRow };
  /** Present when a spending limit is the reason (reason_codes has "spend_limit"). */
  limits?: UsageLimit[];
  reason_codes?: string[];
}
export interface UsageLine { name: string; projected: string | null; baseline: string | null; ratio: string | null }
export interface UsageLimitView {
  /** on_exceed deny: nothing can be confirmed, the block only tells. */
  refused: boolean;
  /** One reason per limit, in the language asked for (else English, else whatever language the kernel gave). */
  reasons: string[];
}
export interface UsagePendingView {
  baselineKey: string | null;
  lines: UsageLine[];
  /** Set when a spending limit holds or refuses; the x10 lines are then not what the block is about. */
  limit: UsageLimitView | null;
  /** Cannot be answered without both identifiers of the ledger receipt (and never when the limit refuses). */
  answerable: boolean;
}

/** null/absent is unknown, never zero. */
function amount(value: unknown): string | null {
  return typeof value === "number" && Number.isFinite(value) ? String(value) : null;
}

/** The reason of a limit in the language of the page, falling back to English and then to any text there is. */
function reasonText(limit: UsageLimit, language: string): string | null {
  const reason = limit.reason;
  if (reason === undefined || reason === null || typeof reason !== "object") return null;
  for (const key of [language, "en", ...Object.keys(reason)]) {
    const value = reason[key];
    if (typeof value === "string" && value !== "") return value;
  }
  return null;
}

export function usagePendingView(receipt: UsagePendingReceipt, language = "en"): UsagePendingView {
  const projected = receipt.projected ?? {};
  const limits = Array.isArray(receipt.limits) ? receipt.limits : [];
  const limit: UsageLimitView | null = limits.length === 0 ? null : {
    refused: receipt.status === "denied" || limits.some((row) => row.on_exceed === "deny"),
    reasons: limits.map((row) => reasonText(row, language)).filter((row): row is string => row !== null),
  };
  const lines = Object.keys(projected).sort().map((name): UsageLine => {
    const row = projected[name] ?? {};
    return { name, projected: amount(row.projected) ?? amount(row.projected_lower_bound),
      baseline: amount(row.baseline), ratio: amount(row.ratio) };
  });
  return { baselineKey: receipt.baseline_key ?? null, lines, limit,
    answerable: Boolean(receipt.receipt_id) && Boolean(receipt.operation_id) && !(limit && limit.refused) };
}

/** Confirmation object the native side expects for the identical re-send. */
export function usageConfirmation(receipt: UsagePendingReceipt, approved: boolean, ref: string) {
  return { approved, receipt_id: receipt.receipt_id ?? "", operation_id: receipt.operation_id ?? "", ref };
}

/** The identical request plus the confirmation; nothing else changes. */
export function confirmedRequest<T extends { message: string }>(request: T, receipt: UsagePendingReceipt, approved: boolean, ref: string) {
  return { ...request, usage_confirmation: usageConfirmation(receipt, approved, ref) };
}

export interface UsagePendingTexts {
  title: string | null; explanation: string | null; baselineKey: string | null; unknown: string;
  confirm: string | null; reject: string | null; restored: string | null; rejected: string | null;
  /** The words of a spending limit's block (P18); null without a catalog. */
  limitTitle: string | null; limitExplanation: string | null; limitConfirm: string | null; limitDismiss: string | null; limitDismissed: string | null;
  line: (line: UsageLine) => string;
  failed: (reason: string) => string;
}

/** All user-visible words come from the catalog; without one the machine names are shown. */
export function usagePendingTexts(feature: PresentationFeature | null, view: UsagePendingView): UsagePendingTexts {
  const text = (id: string, parameters: Row = {}): string | null => {
    if (!feature) return null;
    try { return featureMessage(feature, id, parameters); }
    catch { return null; }
  };
  const unknown = text("unknown") ?? "-";
  const shown = (value: string | null) => value ?? unknown;
  return {
    title: text("title"), explanation: text("explanation"),
    baselineKey: view.baselineKey === null ? null : text("baseline_key", { key: view.baselineKey }) ?? view.baselineKey,
    unknown, confirm: text("confirm"), reject: text("reject"), restored: text("restored"), rejected: text("rejected"),
    limitTitle: text("limit_title"),
    limitExplanation: view.limit === null ? null : text(view.limit.refused ? "limit_refused_explanation" : "limit_explanation"),
    limitConfirm: text("limit_confirm"), limitDismiss: text("limit_dismiss"), limitDismissed: text("limit_dismissed"),
    line: (line) => text("resource", { name: line.name, projected: shown(line.projected),
      baseline: shown(line.baseline), ratio: shown(line.ratio) }) ?? line.name,
    failed: (reason) => text("confirm_failed", { reason }) ?? reason,
  };
}

/** Stream chunk type (P18): the usage ledger only told while admitting the send - a spending limit with on_exceed notify. The send went on. */
export const USAGE_NOTICE_CHUNK = "usage_notice";

/** One quiet line per notice, in the language of the page (else English, else any language the kernel gave). */
export function usageNoticeLines(notices: unknown, language: string): string[] {
  if (!Array.isArray(notices)) return [];
  const lines: string[] = [];
  for (const item of notices) {
    if (item === null || typeof item !== "object") continue;
    const text = (item as { text?: { [language: string]: unknown } }).text;
    if (text === undefined || text === null || typeof text !== "object") continue;
    for (const key of [language, "en", ...Object.keys(text)]) {
      const value = text[key];
      if (typeof value === "string" && value !== "") { lines.push(value); break; }
    }
  }
  return lines;
}
