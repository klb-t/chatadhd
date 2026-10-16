import type { Conversation, Message } from "../api/types";

/** R35-2: the "host exposes stored messages only" notice is true only when something of THIS conversation is linked to a source history
 * the host cannot read: a message that is a reference, a message carrying a source_ref, or a conversation that records source history. */
const CONVERSATION_KEYS = ["source_history", "source_resources", "source_ref", "linked_source"] as const;
const filled = (v: unknown): boolean => v !== undefined && v !== null && v !== false && v !== "" &&
  !(Array.isArray(v) && v.length === 0) && !(typeof v === "object" && !Array.isArray(v) && Object.keys(v as object).length === 0);

export function sourceHistoryUnavailable(
  conversation: Pick<Conversation, "metadata"> | null | undefined,
  messages: readonly Pick<Message, "storage" | "source_ref">[],
): boolean {
  if (messages.some(m => m.storage === "reference" || filled(m.source_ref))) return true;
  const meta = conversation?.metadata;
  return !!meta && CONVERSATION_KEYS.some(key => filled(meta[key]));
}
