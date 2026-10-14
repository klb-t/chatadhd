import type { LoomEventEnvelope } from "../api/types";

/** Kernel event: a run of a conversation is paused on a step the client carries out, or its question was answered or closed. Ids only. */
export const CLIENT_STEP_EVENT = "action:client_step";

export interface ClientStepEvent {
  convId: string;
  messageId: string;
  runId: string;
  /** waiting: a new question for the device is in the message; closed: it was answered or cancelled (the message no longer asks). */
  state: "waiting" | "closed";
}

function text(value: unknown): string {
  return typeof value === "string" ? value : "";
}

/** The event of an envelope of the event stream; anything else (another event, a payload without ids) is null. */
export function clientStepEvent(envelope: unknown): ClientStepEvent | null {
  if (envelope === null || typeof envelope !== "object") return null;
  const wrapper = envelope as { [key: string]: unknown };
  if (wrapper.event !== CLIENT_STEP_EVENT) return null;
  const payload = wrapper.payload;
  if (payload === null || typeof payload !== "object") return null;
  const row = payload as { [key: string]: unknown };
  const convId = text(row.conv_id);
  const messageId = text(row.message_id);
  const runId = text(row.run_id);
  if (convId === "" || messageId === "" || runId === "") return null;
  return { convId, messageId, runId, state: row.state === "closed" ? "closed" : "waiting" };
}

/** What the page needs of a transport: the live event feed (SSE on the web, subscribe on the native shell). */
export interface ClientStepSource {
  subscribeEvents?: (event: string, handlers: {
    onChunk?: (chunk: LoomEventEnvelope) => void;
    onDone?: () => void;
    onError?: (message: string) => void;
  }) => () => void;
}

/** How a feed that ended is picked up again: the wait doubles from `firstMs` up to `maxMs`; a feed that delivered resets it. Settings of the page, not limits. */
export interface ClientStepReconnect {
  firstMs: number;
  maxMs: number;
  /** Replaceable in tests. */
  wait: (run: () => void, ms: number) => () => void;
}

export const DEFAULT_RECONNECT: ClientStepReconnect = {
  firstMs: 1000,
  maxMs: 30000,
  wait: (run, ms) => {
    const handle = setTimeout(run, ms);
    return () => clearTimeout(handle);
  },
};

/**
 * Listens for new client steps of the conversations shown, so a step started or answered elsewhere reaches the page without a refresh and
 * without polling. Returns the stop function, or null when the transport has no event feed (then the page sees a step after a refresh, as before).
 * A feed that ends is opened again after a wait; stopping cancels the wait.
 */
export function watchClientSteps(source: ClientStepSource, onEvent: (event: ClientStepEvent) => void, reconnect: ClientStepReconnect = DEFAULT_RECONNECT): (() => void) | null {
  const subscribe = source.subscribeEvents;
  if (typeof subscribe !== "function") return null;
  let stopped = false;
  let stopFeed: (() => void) | null = null;
  let cancelWait: (() => void) | null = null;
  let delay = reconnect.firstMs;
  const open = () => {
    if (stopped) return;
    let ended = false;
    const lost = () => {
      if (ended || stopped) return;
      ended = true;
      stopFeed = null;
      const wait = delay;
      delay = Math.min(reconnect.maxMs, delay * 2);
      cancelWait = reconnect.wait(() => { cancelWait = null; open(); }, wait);
    };
    stopFeed = subscribe.call(source, CLIENT_STEP_EVENT, {
      onChunk: (envelope) => {
        delay = reconnect.firstMs;
        const event = clientStepEvent(envelope);
        if (event !== null) onEvent(event);
      },
      onDone: lost,
      onError: lost,
    });
  };
  open();
  return () => {
    stopped = true;
    if (stopFeed !== null) stopFeed();
    if (cancelWait !== null) cancelWait();
    stopFeed = null;
    cancelWait = null;
  };
}
