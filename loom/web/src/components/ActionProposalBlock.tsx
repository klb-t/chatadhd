import { useMemo } from "react";
import type { PresentationFeature } from "../onboarding/presentation.mjs";
import { offerTexts, proposalTexts, proposalView, writtenTexts, type OfferView, type PendingAnswer, type WrittenView } from "../context/action-proposal";

/** A command from the conversation window waiting for the user's answer (R54): what will run, the estimate,
 *  what the rules ask, and the two ways to answer. Never an error state; nothing has been executed. */
export default function ActionProposalBlock({ feature, answer, busy, failure, onConfirm, onReject, testId, plain }: {
  feature: PresentationFeature | null; answer: PendingAnswer; busy?: boolean; failure?: string | null;
  onConfirm: () => void; onReject: () => void; testId: string;
  /** M1-W2a: the proposal of the interview ("Tak zrozumiałem"): the sentence above it already says what was understood in words, so the block
   *  shows only the two answers, in the given words - not the kernel's arguments (kind=remark, user_id=local ...), the plan or the model estimate. */
  plain?: { confirm: string; reject: string };
}) {
  const view = useMemo(() => proposalView(answer), [answer]);
  const texts = useMemo(() => proposalTexts(feature, answer, view), [feature, answer, view]);
  if (plain) return (
    <div className="pill warn" role="group" aria-label={texts.title ?? undefined} data-testid={testId} aria-live="polite" data-plain="true"
      data-run-id={answer.runId} data-receipt-id={answer.receiptId} data-kind={answer.kind}>
      <button type="button" className="primary" disabled={busy || !view.answerable} onClick={onConfirm}
        data-testid={testId + "-confirm"}>{plain.confirm}</button>
      <button type="button" disabled={busy || !view.answerable} onClick={onReject}
        data-testid={testId + "-reject"}>{plain.reject}</button>
      {failure && <p role="alert" data-testid={testId + "-failure"}>{texts.failed(failure)}</p>}
    </div>
  );
  return (
    <div className="pill warn" role="group" data-testid={testId} aria-live="polite"
      data-run-id={answer.runId} data-receipt-id={answer.receiptId} data-kind={answer.kind}>
      <strong>{texts.title ?? answer.kind}</strong>
      {texts.guardianClaim && <p data-testid={testId + "-guardian-claim"}>{texts.guardianClaim}</p>}
      {texts.guardianScope && <p data-testid={testId + "-guardian-scope"}>{texts.guardianScope}</p>}
      {texts.guardianExceptions && <p data-testid={testId + "-guardian-exceptions"}>{texts.guardianExceptions}</p>}
      {texts.guardianInstruction && <p data-testid={testId + "-guardian-instruction"}>{texts.guardianInstruction}</p>}
      {texts.guardianNote && <p data-testid={testId + "-guardian-note"}>{texts.guardianNote}</p>}
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

/** What a "no" leaves of the user's sentence: nothing went to the model, and one choice sends it as an ordinary message.
 *  Shown under the dropped proposal, in the message of the proposal itself. */
export function ProposalOffer({ feature, offer, busy, failure, onSend, testId }: {
  feature: PresentationFeature | null; offer: OfferView; busy?: boolean; failure?: string | null;
  onSend: () => void; testId: string;
}) {
  const texts = useMemo(() => offerTexts(feature), [feature]);
  return (
    <div className="pill warn" role="group" data-testid={testId} aria-live="polite"
      data-proposal-id={offer.proposalId} data-sentence-id={offer.sentenceId}>
      {texts.hint && <p data-testid={testId + "-hint"}>{texts.hint}</p>}
      <button type="button" className="primary" disabled={busy} onClick={onSend}
        data-testid={testId + "-send"}>{texts.send ?? offer.proposalId}</button>
      {failure && <p role="alert" data-testid={testId + "-failure"}>{texts.failed(failure)}</p>}
    </div>
  );
}

/** What an accepted definition wrote: the line, the overlay entry itself, and the one click that takes it back. */
export function ProposalWritten({ feature, written, busy, failure, onUndo, testId }: {
  feature: PresentationFeature | null; written: WrittenView; busy?: boolean; failure?: string | null;
  onUndo: () => void; testId: string;
}) {
  const texts = useMemo(() => writtenTexts(feature, written), [feature, written]);
  return (
    <div className="pill" role="group" data-testid={testId} aria-live="polite"
      data-id={written.id} data-kind={written.kind} data-undone={written.undone ? "true" : "false"}>
      <p data-testid={testId + "-line"}>{texts.written ?? written.id}</p>
      {written.entry !== "" && <details data-testid={testId + "-entry"}>
        <summary>{texts.entry ?? written.id}</summary>
        <pre>{written.entry}</pre>
      </details>}
      {written.undone
        ? <p data-testid={testId + "-undone"}>{texts.undone ?? written.id}</p>
        : <button type="button" disabled={busy} onClick={onUndo} data-testid={testId + "-undo"}>{texts.undo ?? written.id}</button>}
      {failure && <p role="alert" data-testid={testId + "-failure"}>{texts.failed(failure)}</p>}
    </div>
  );
}
