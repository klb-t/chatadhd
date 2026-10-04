import type { LoomApi, StreamHandlers, Unsubscribe } from "../api/loom-api";
import type { ChatChunk, ChatRequest } from "../api/types";
import { createProfileRegistry, type OperationAdapter } from "./runtime";

/** Trusted UI bindings, installed by the host rather than an imported profile. */
export interface LoomProfileUi {
  selectConversation?: (id: string) => unknown;
  conversationCreated?: (id: string) => unknown;
  openKnowledge?: () => unknown;
  openPanel?: (panel: "memory" | "import" | "context" | "graph" | "settings" | "logs") => unknown;
}
export interface ProfileChatSendPayload {
  request: ChatRequest;
  handlers?: StreamHandlers<ChatChunk>;
  onSubscription?: (unsubscribe: Unsubscribe) => void;
}
export interface ProfileChatCancelPayload { requestId?: string; unsubscribe?: Unsubscribe }
type ChatDone = Extract<ChatChunk, { type: "done" }>;
interface ActiveChat { requestId?: string; unsubscribe: Unsubscribe }

function payloadObject(payload: unknown, operation: string): Record<string, unknown> {
  if (!payload || typeof payload !== "object" || Array.isArray(payload)) {
    throw new TypeError(`${operation} requires an object payload.`);
  }
  return payload as Record<string, unknown>;
}
function stringField(payload: Record<string, unknown>, field: string, operation: string, allowEmpty = false): string {
  const value = payload[field];
  if (typeof value !== "string" || (!allowEmpty && !value.trim())) {
    throw new TypeError(`${operation}.${field} must be ${allowEmpty ? "a string" : "a nonempty string"}.`);
  }
  return value;
}
function errorValue(error: unknown): Error { return error instanceof Error ? error : new Error(String(error)); }
function cancellation(): Error { const error = new Error("Chat cancelled."); error.name = "AbortError"; return error; }

/**
 * The profile is a caller of the existing Loom operations, not another model or
 * context policy. In particular, the original ChatRequest object is forwarded
 * unchanged. UI callbacks advertise opening a view, never executing its tools.
 */
export function createLoomProfileRegistry(api: LoomApi, ui: LoomProfileUi = {}) {
  const active = new Set<ActiveChat>();
  const add = (operation: string, execute: OperationAdapter["execute"]): OperationAdapter =>
    ({ operation, capability: operation, execute });

  const send = (payload: unknown): Promise<ChatDone> => {
    const value = payloadObject(payload, "chat.send");
    const request = payloadObject(value.request, "chat.send.request");
    stringField(request, "message", "chat.send.request");
    if (request.request_id !== undefined) stringField(request, "request_id", "chat.send.request");
    if (value.handlers !== undefined) {
      const handlers = payloadObject(value.handlers, "chat.send.handlers");
      for (const field of ["onChunk", "onDone", "onError"]) {
        if (handlers[field] !== undefined && typeof handlers[field] !== "function") {
          throw new TypeError(`chat.send.handlers.${field} must be a function.`);
        }
      }
    }
    if (value.onSubscription !== undefined && typeof value.onSubscription !== "function") {
      throw new TypeError("chat.send.onSubscription must be a function.");
    }
    const { handlers = {}, onSubscription } = value as unknown as ProfileChatSendPayload;
    return new Promise<ChatDone>((resolve, reject) => {
      let settled = false;
      let stopRequested = false;
      let stopped = false;
      let rawUnsubscribe: Unsubscribe | undefined;
      const stop = () => {
        stopRequested = true;
        if (rawUnsubscribe && !stopped) { stopped = true; rawUnsubscribe(); }
      };
      const finish = (result: ChatDone | Error) => {
        if (settled) return;
        settled = true;
        active.delete(chat);
        if (result instanceof Error) reject(result); else resolve(result);
      };
      const chat: ActiveChat = {
        requestId: request.request_id as string | undefined,
        unsubscribe: () => {
          finish(cancellation());
          stop();
        },
      };
      const fail = (error: unknown) => { finish(errorValue(error)); stop(); };
      active.add(chat);
      try {
        rawUnsubscribe = api.chat(value.request as ChatRequest, {
          onChunk: (chunk) => {
            if (settled) return;
            if (chunk.type === "start") chat.requestId = chunk.request_id;
            try {
              handlers.onChunk?.(chunk);
              if (chunk.type === "done") finish(chunk);
              else if (chunk.type === "error") fail(new Error(chunk.message));
            } catch (error) { fail(error); }
          },
          onError: (message) => {
            if (settled) return;
            try { handlers.onError?.(message); } catch (error) { fail(error); return; }
            fail(new Error(message));
          },
          onDone: () => {
            if (!settled) {
              const message = "Chat stream ended before a done result.";
              try { handlers.onDone?.(); } catch (error) { fail(error); return; }
              fail(new Error(message));
              return;
            }
            // Transport EOF is distinct from the native done result. Existing
            // observers still receive it, but it cannot advance the workflow.
            handlers.onDone?.();
          },
        });
        if (typeof rawUnsubscribe !== "function") throw new TypeError("chat.send transport did not return a subscription.");
        if (stopRequested) stop();
        onSubscription?.(chat.unsubscribe);
      } catch (error) { fail(error); }
    });
  };

  const cancel = async (payload: unknown) => {
    const value = payloadObject(payload, "chat.cancel");
    const requestId = value.requestId === undefined ? undefined : stringField(value, "requestId", "chat.cancel");
    const unsubscribe = value.unsubscribe;
    if (unsubscribe !== undefined && typeof unsubscribe !== "function") throw new TypeError("chat.cancel.unsubscribe must be a function.");
    if (!requestId && !unsubscribe) throw new TypeError("chat.cancel needs a requestId or subscription; a conversation ID is not a request ID.");
    if (requestId) {
      try {
        await api.cancelChat(requestId);
        for (const chat of [...active]) if (chat.requestId === requestId) chat.unsubscribe();
      } finally {
        // An explicit unsubscribe aborts locally even if server cancellation
        // fails. Without it, a rejected cancel leaves the live request intact.
        if (typeof unsubscribe === "function") unsubscribe();
      }
    } else if (typeof unsubscribe === "function") unsubscribe();
  };

  const adapters: OperationAdapter[] = [
    add("chat.create", async (payload) => {
      const value = payload === undefined ? {} : payloadObject(payload, "chat.create");
      const title = value.title === undefined ? undefined : stringField(value, "title", "chat.create", true);
      const conversation = await api.createConversation(title);
      await ui.conversationCreated?.(conversation.id);
      return conversation;
    }),
    add("chat.rename", (payload) => {
      const value = payloadObject(payload, "chat.rename");
      return api.updateConversation(stringField(value, "id", "chat.rename"), { title: stringField(value, "title", "chat.rename", true) });
    }),
    add("chat.send", send),
    add("chat.cancel", cancel),
    add("message.edit", (payload) => {
      const value = payloadObject(payload, "message.edit");
      return api.editMessage(stringField(value, "id", "message.edit"), stringField(value, "text", "message.edit", true));
    }),
    add("message.restore", (payload) => {
      const value = payloadObject(payload, "message.restore");
      return api.restoreVersion(stringField(value, "id", "message.restore"));
    }),
    add("message.exclude", (payload) => {
      const value = payloadObject(payload, "message.exclude");
      const id = stringField(value, "id", "message.exclude");
      if (value.status !== "active" && value.status !== "excluded") throw new TypeError("message.exclude.status must be active or excluded.");
      return api.setMessageStatus(id, value.status);
    }),
  ];
  if (ui.selectConversation) adapters.push(add("chat.select", (payload) => {
    const value = payloadObject(payload, "chat.select");
    return ui.selectConversation!(stringField(value, "id", "chat.select"));
  }));
  if (api.knowledge && ui.openKnowledge) adapters.push(add("knowledge.open", () => ui.openKnowledge!()));
  const panelOperations = ["memory", "import", "context", "graph", "settings", "logs"] as const;
  if (ui.openPanel) for (const panel of panelOperations) adapters.push(add(`${panel}.open`, () => ui.openPanel!(panel)));
  return createProfileRegistry({ adapters, operations: panelOperations.map(panel => `${panel}.open`) });
}
