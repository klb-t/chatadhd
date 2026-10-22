// M1-W1b: what this window knows about the values that wait in the kernel's turn carrier (context.modify). The kernel offers no read of the
// carrier, so the only honest source is its own answer to the last context.modify of THIS window: `result.data.waiting` is the whole content of
// the carrier at that moment. It is cleared when a request goes out (the chat start chunk: the kernel stored the message and took the values)
// or when the person takes the change back (context.modify reset). A window that did not make the change does not know about it - the strip
// then shows the profile only and the words say that the next request may carry changes made elsewhere.
type Waiting = Record<string, unknown>;
const store = new Map<string, Waiting>();
const listeners = new Set<() => void>();
let version = 0;
const emit = () => { version++; listeners.forEach(fn => fn()); };
/** The kernel keys the carrier by conversation; "" = a conversation not opened yet. */
export const turnKey = (convId: string | null | undefined): string => convId ?? "";

export function pendingTurn(convId: string | null | undefined): Waiting {
  return store.get(turnKey(convId)) ?? EMPTY;
}
const EMPTY: Waiting = Object.freeze({}) as Waiting;
export function setPendingTurn(convId: string | null | undefined, waiting: Waiting): void {
  if (Object.keys(waiting).length === 0) store.delete(turnKey(convId)); else store.set(turnKey(convId), waiting);
  emit();
}
export function clearPendingTurn(...convIds: (string | null | undefined)[]): void {
  let changed = false;
  for (const id of convIds) changed = store.delete(turnKey(id)) || changed;
  if (changed) emit();
}
/** A new conversation made from the blank page takes the carrier of "" (the kernel moved it with the first message). */
export function subscribePendingTurn(fn: () => void): () => void { listeners.add(fn); return () => { listeners.delete(fn); }; }
export function pendingTurnVersion(): number { return version; }
