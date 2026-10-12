import { useMemo } from "react";
import type { PresentationFeature } from "../onboarding/presentation.mjs";
import { usagePendingTexts, usagePendingView, type UsagePendingReceipt } from "../context/usage-pending";

/** Inline "requires confirmation" state of a paused send or batch (never an error). */
export default function UsagePendingBlock({ feature, receipt, busy, onConfirm, onReject, testId }: {
  feature: PresentationFeature | null; receipt: UsagePendingReceipt; busy?: boolean;
  onConfirm: () => void; onReject: () => void; testId: string;
}) {
  const view = useMemo(() => usagePendingView(receipt), [receipt]);
  const texts = useMemo(() => usagePendingTexts(feature, view), [feature, view]);
  return (
    <div className="pill warn" role="group" data-testid={testId} aria-live="polite"
      data-receipt-id={receipt.receipt_id ?? undefined} data-operation-id={receipt.operation_id ?? undefined}>
      <strong>{texts.title ?? receipt.status}</strong>
      {texts.explanation && <p>{texts.explanation}</p>}
      {texts.baselineKey && <p><code>{texts.baselineKey}</code></p>}
      <ul>{view.lines.map(line => <li key={line.name} data-testid={`${testId}-line`}>{texts.line(line)}</li>)}</ul>
      <button type="button" className="primary" disabled={busy || !view.answerable} onClick={onConfirm}
        data-testid={`${testId}-confirm`}>{texts.confirm ?? receipt.receipt_id}</button>
      <button type="button" disabled={busy || !view.answerable} onClick={onReject}
        data-testid={`${testId}-reject`}>{texts.reject ?? receipt.operation_id}</button>
    </div>
  );
}
