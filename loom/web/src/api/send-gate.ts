// R103 B3 (§4.3): a gate the chat send passes before the request leaves the browser. The guest layer installs one when
// the current source's data terms need an explicit tap (retention other than none, data_terms_prompt first_use); with
// no gate installed every send goes straight through. The gate resolves true = send, false = not sent.
export type SendGate = () => Promise<boolean>;

let gate: SendGate | null = null;

export function setSendGate(next: SendGate | null): void { gate = next; }
export function sendGate(): SendGate | null { return gate; }

/** Whether these data terms ask for a tap before the first send (§4.3): not "retention none…" with "training no". */
export function termsNeedConsent(terms: unknown): boolean {
  if (!terms || typeof terms !== "object") return false;
  const t = terms as { retention?: unknown; training?: unknown };
  const quiet = typeof t.retention === "string" && t.retention.startsWith("none") && t.training === "no";
  return !quiet;
}
