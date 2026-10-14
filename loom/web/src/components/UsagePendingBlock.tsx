import { useMemo } from "react";
import type { PresentationFeature } from "../onboarding/presentation.mjs";
import { usagePendingTexts, usagePendingView, type UsagePendingReceipt } from "../context/usage-pending";

/** Inline "requires confirmation" state of a paused send or batch (never an error). A spending limit (P18) has its own title, reasons and,
 * when it refuses, a single button that only dismisses: there is nothing to confirm. */
export default function UsagePendingBlock({ feature, receipt, busy, onConfirm, onReject, testId }: {
  feature: PresentationFeature | null; receipt: UsagePendingReceipt; busy?: boolean;
  onConfirm: () => void; onReject?: () => void; testId: string;
}) {
  const view = useMemo(() => usagePendingView(receipt, feature?.locale), [receipt, feature]);
  const texts = useMemo(() => usagePendingTexts(feature, view), [feature, view]);
  const limit = view.limit;
  return (
    <div className="pill warn" role="group" data-testid={testId} aria-live="polite"
      data-receipt-id={receipt.receipt_id ?? undefined} data-operation-id={receipt.operation_id ?? undefined}
      data-reason={limit ? "spend_limit" : undefined} data-refused={limit?.refused ? "true" : undefined}>
      <strong>{limit ? (texts.limitTitle ?? receipt.status) : (texts.title ?? receipt.status)}</strong>
      {limit ? texts.limitExplanation && <p>{texts.limitExplanation}</p> : texts.explanation && <p>{texts.explanation}</p>}
      {!limit && texts.baselineKey && <p><code>{texts.baselineKey}</code></p>}
      {limit
        ? <ul>{limit.reasons.map((reason, i) => <li key={i} data-testid={`${testId}-reason`}>{reason}</li>)}</ul>
        : <ul>{view.lines.map(line => <li key={line.name} data-testid={`${testId}-line`}>{texts.line(line)}</li>)}</ul>}
      {!(limit && limit.refused) && <button type="button" className="primary" disabled={busy || !view.answerable} onClick={onConfirm}
        data-testid={`${testId}-confirm`}>{(limit ? texts.limitConfirm : texts.confirm) ?? receipt.receipt_id}</button>}
      {(onReject || !(limit && limit.refused)) && <button type="button" disabled={busy || (!view.answerable && !(limit && limit.refused))} onClick={onReject}
        data-testid={`${testId}-reject`}>{(limit && limit.refused ? texts.limitDismiss : texts.reject) ?? receipt.operation_id}</button>}
    </div>
  );
}
