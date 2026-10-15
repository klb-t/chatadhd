import { useEffect, useState } from "react";
import type { SendGate } from "./send-gate";

/** Why a send waits instead of being dropped. Only these two are transient states of the page itself; every other reason of the send
 *  gate keeps its behaviour: it is shown, not queued. */
export type QueueReason = "conversation_creating" | "conversation_view_loading";
/** The reason of the send gate that queues (the others are refusals the person is told about). */
export const VIEW_LOADING_REASON = "conversation_view_loading";

export interface QueueInput { convId: string | null; creating: boolean; gate: SendGate }
/** A queued send remembers the conversation it was typed in: null while that conversation was still being created. */
export interface QueuedSend { convId: string | null }
/** wait: not yet; send: dispatch it now, once; failed: the conversation did not load, the text stays in the field; drop: the person
 *  moved to another conversation, nothing is sent there. */
export type QueueStep = "wait" | "send" | "failed" | "drop";

/** Does a send made now have to wait, and for what? */
export function queueReason(now: QueueInput): QueueReason | null {
  if (now.creating && now.convId === null) return "conversation_creating";
  if (now.gate.blocked && now.gate.blocked.reason === VIEW_LOADING_REASON) return "conversation_view_loading";
  return null;
}

/** What a queued send does now. */
export function queueStep(queued: QueuedSend, now: QueueInput): QueueStep {
  if (queued.convId !== null && now.convId !== queued.convId) return "drop";
  if (queueReason(now) !== null) return "wait";
  // Nothing waits any more. A view that failed, or a conversation that was never made, is said and the text stays in the field.
  if (now.convId === null || now.gate.viewFailure !== null) return "failed";
  return "send";
}

// ── the conversation being created ──────────────────────────────────

let inFlight = 0;
const listeners = new Set<() => void>();
function changed(): void { for (const listener of Array.from(listeners)) listener(); }

/** A conversation is being created (the sidebar asked the kernel and has not yet told the page which one it is). Returns the end. */
export function beginCreation(): () => void {
  inFlight += 1;
  changed();
  let ended = false;
  return () => { if (ended) return; ended = true; inFlight -= 1; changed(); };
}
export function creationInFlight(): boolean { return inFlight > 0; }
/** True while a conversation is being created, for the composer: a send typed in that moment waits for it. A plain state, not an external
 *  store read: the end of the creation and the page's new conversation are two updates of one tick and must be rendered together. */
export function useCreationInFlight(): boolean {
  const [now, setNow] = useState(creationInFlight);
  useEffect(() => {
    const listener = () => setNow(creationInFlight());
    listeners.add(listener);
    listener();
    return () => { listeners.delete(listener); };
  }, []);
  return now;
}
