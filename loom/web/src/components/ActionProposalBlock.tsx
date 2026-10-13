import { useMemo } from "react";
import type { PresentationFeature } from "../onboarding/presentation.mjs";
import { proposalTexts, proposalView, type PendingAnswer } from "../context/action-proposal";

/** A command from the conversation window waiting for the user's answer (R54): what will run, the estimate,
 *  what the rules ask, and the two ways to answer. Never an error state; nothing has been executed. */
export default function ActionProposalBlock({ feature, answer, busy, failure, onConfirm, onReject, testId }: {
  feature: PresentationFeature | null; answer: PendingAnswer; busy?: boolean; failure?: string | null;
  onConfirm: () => void; onReject: () => void; testId: string;
}) {
  const view = useMemo(() => proposalView(answer), [answer]);
  const texts = useMemo(() => proposalTexts(feature, answer, view), [feature, answer, view]);
  return (
    <div className="pill warn" role="group" data-testid={testId} aria-live="polite"
      data-run-id={answer.runId} data-receipt-id={answer.receiptId} data-kind={answer.kind}>
      <strong>{texts.title ?? answer.kind}</strong>
      {texts.definition && <p data-testid={testId + "-definition"}>{texts.definition}</p>}
      {texts.args && <p data-testid={testId + "-args"}>{texts.args}</p>}
      {texts.argsOpen && <p data-testid={testId + "-args-open"}>{texts.argsOpen}</p>}
      {texts.replaces && <p data-testid={testId + "-replaces"}>{texts.replaces}</p>}
      {texts.plan && <p data-testid={testId + "-plan"}>{texts.plan}</p>}
      {texts.cost && <p data-testid={testId + "-cost"}>{texts.cost}</p>}
      {texts.money && <p data-testid={testId + "-money"}>{texts.money}</p>}
      {texts.confirmNeeded && <p data-testid={testId + "-confirm-needed"}>{texts.confirmNeeded}</p>}
      <button type="button" className="primary" disabled={busy || !view.answerable} onClick={onConfirm}
        data-testid={testId + "-confirm"}>{texts.confirm ?? answer.receiptId}</button>
      <button type="button" disabled={busy || !view.answerable} onClick={onReject}
        data-testid={testId + "-reject"}>{texts.reject ?? answer.runId}</button>
      {failure && <p role="alert" data-testid={testId + "-failure"}>{texts.failed(failure)}</p>}
    </div>
  );
}
