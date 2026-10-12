import { featureMessage, type PresentationFeature } from "../onboarding/presentation.mjs";
import type { JsonValue } from "../onboarding/types";

/** Presentation feature holding the texts (`presentation.chat_usage`). */
export const USAGE_PENDING_FEATURE = "chat_usage";
/** Final stream chunk type of a send paused by the shared usage guard. */
export const USAGE_PENDING_CHUNK = "usage_pending";

/** Index signature keeps the module lexically scannable by the literal guard. */
interface Row { [key: string]: JsonValue }
interface ReceiptRow { [key: string]: unknown }
export interface UsagePendingReceipt {
  status?: string;
  operation_id?: string | null;
  receipt_id?: string | null;
  baseline_key?: string | null;
  projected?: { [name: string]: ReceiptRow };
}
export interface UsageLine { name: string; projected: string | null; baseline: string | null; ratio: string | null }
export interface UsagePendingView {
  baselineKey: string | null;
  lines: UsageLine[];
  /** Cannot be answered without both identifiers of the ledger receipt. */
  answerable: boolean;
}

/** null/absent is unknown, never zero. */
function amount(value: unknown): string | null {
  return typeof value === "number" && Number.isFinite(value) ? String(value) : null;
}

export function usagePendingView(receipt: UsagePendingReceipt): UsagePendingView {
  const projected = receipt.projected ?? {};
  const lines = Object.keys(projected).sort().map((name): UsageLine => {
    const row = projected[name] ?? {};
    return { name, projected: amount(row.projected) ?? amount(row.projected_lower_bound),
      baseline: amount(row.baseline), ratio: amount(row.ratio) };
  });
  return { baselineKey: receipt.baseline_key ?? null, lines,
    answerable: Boolean(receipt.receipt_id) && Boolean(receipt.operation_id) };
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
    line: (line) => text("resource", { name: line.name, projected: shown(line.projected),
      baseline: shown(line.baseline), ratio: shown(line.ratio) }) ?? line.name,
    failed: (reason) => text("confirm_failed", { reason }) ?? reason,
  };
}
