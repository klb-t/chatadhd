// R103 B2: the proof-of-work search off the page's thread (see pow.ts).
import { findNonce } from "./pow";

const scope = self as unknown as { onmessage: ((e: MessageEvent<{ challenge: string; bits: number }>) => void) | null; postMessage: (m: unknown) => void };
scope.onmessage = (event) => {
  const { challenge, bits } = event.data;
  for (let from = 0; from < 2 ** 32; from += 100000) {
    const nonce = findNonce(challenge, bits, from, 100000);
    if (nonce !== null) { scope.postMessage({ nonce }); return; }
  }
  scope.postMessage({});
};
