// R101 P0: a sentence written on the desktop goes the route of the chat composer: the desktop opens (or restores) a chat window and
// hands the sentence over here; the chat composer that is (or gets) mounted takes it once, puts it in its field and - preset `send` of
// desktop.json - sends it as if typed there. One pending sentence at a time (a newer one replaces an untaken one).
export const DESKTOP_SENTENCE_EVENT = "loom:desktop-sentence";
let pending: { text: string; send: boolean } | null = null;
export function handSentence(text: string, send: boolean): void {
  pending = { text, send };
  if (typeof window !== "undefined") window.dispatchEvent(new Event(DESKTOP_SENTENCE_EVENT));
}
export function takeSentence(): { text: string; send: boolean } | null { const p = pending; pending = null; return p; }
