import { useShellLanguage } from "../shell/text";
import { localizedText } from "../shell/language-pack";
import type { SecretPending } from "../api/types";

/** R67: the answer to the kernel's secret check. The words come from the kernel's own document (runtime/chat_secrets.pack, pl and en); the
 * block names what matched and where, never the value. "needs_confirmation" asks (send anyway / do not send); "refused" only says so. */
const pick = (words: Record<string, string> | undefined, language: string): string => localizedText(words, language) ?? "";

export default function SecretPendingBlock({ pending, onConfirm, onDismiss, testId }: {
  pending: SecretPending; onConfirm: () => void; onDismiss: () => void; testId: string;
}) {
  const language = useShellLanguage();
  const asks = pending.status === "needs_confirmation";
  return (
    <div className="pill warn" role="group" data-testid={testId} data-state={pending.status} aria-live="polite">
      <strong data-testid={`${testId}-title`}>{pick(pending.text, language)}</strong>
      <ul>{pending.hits.map((h, i) => (
        <li key={i} data-testid={`${testId}-hit`} data-rule={h.rule}>
          {pick(h.label, language)}{h.place ? " · " + pick(h.place, language) : ""}{h.file ? " (" + h.file + ")" : ""}
        </li>))}</ul>
      {asks && <button type="button" className="primary" onClick={onConfirm} data-testid={`${testId}-confirm`}>{pick(pending.actions?.send_anyway, language)}</button>}
      <button type="button" onClick={onDismiss} data-testid={`${testId}-dismiss`}>{asks ? pick(pending.actions?.dont_send, language) : "OK"}</button>
    </div>
  );
}
