import type { Message } from "./types";

export interface ConversationReadRequest {
  sourceAccess: "metadata" | "local_read";
  readOptions?: Record<string, unknown>;
}
export interface ConversationView {
  schema: "loom.conversation_view/1";
  conversation_id: string;
  view_id: string;
  status: "complete" | "partial" | "unavailable";
  messages: Message[];
  resources: Record<string, unknown>[];
  omissions: unknown[];
  capabilities: { source_history_send: { available: boolean; reason?: string } };
  read_configuration?: Record<string, unknown>;
  configuration_error?: unknown;
  [key: string]: unknown;
}
function record(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}
// Exact native JSON is inspection evidence, never a serializable view field or
// a client-side replacement for native source parsing / numeric representation.
const nativeWires = new WeakMap<ConversationView, string>();
export function conversationViewWireJson(view: ConversationView): string | undefined { return nativeWires.get(view); }
/** This validates the native view envelope, never parses an external source. */
export function readConversationView(value: unknown, conversationId: string, nativeWire?: string): ConversationView {
  if (!record(value) || value.schema !== "loom.conversation_view/1" || value.conversation_id !== conversationId ||
      typeof value.view_id !== "string" || !value.view_id || typeof value.status !== "string" || !["complete", "partial", "unavailable"].includes(value.status) ||
      !Array.isArray(value.messages) || !Array.isArray(value.resources) || !Array.isArray(value.omissions) ||
      !value.resources.every(record) || !record(value.capabilities) || !record(value.capabilities.source_history_send) ||
      typeof value.capabilities.source_history_send.available !== "boolean") throw new Error("conversation_view_envelope_invalid");
  const ids = new Set<string>();
  for (const row of value.messages) {
    if (!record(row) || typeof row.id !== "string" || !row.id || ids.has(row.id) || row.conv_id !== conversationId ||
        typeof row.text !== "string" || typeof row.role !== "string" || !row.role ||
        typeof row.status !== "string" || !["active", "excluded", "version", "deleted"].includes(row.status) ||
        typeof row.storage !== "string" || !["native", "reference"].includes(row.storage) || !record(row.capabilities) ||
        !["parent_id", "version_group_id", "model"].every(key => row[key] === undefined || row[key] === null || typeof row[key] === "string") ||
        (row.version_num !== undefined && (typeof row.version_num !== "number" || !Number.isSafeInteger(row.version_num) || row.version_num < 1)) ||
        (row.weight !== undefined && (typeof row.weight !== "number" || !Number.isFinite(row.weight))) ||
        (row.created !== null && typeof row.created !== "string") ||
        (row.metadata !== undefined && !record(row.metadata)) ||
        ["edit", "set_status", "restore", "native_lookup"].some(key => typeof (row.capabilities as Record<string, unknown>)[key] !== "boolean"))
      throw new Error("conversation_view_message_invalid");
    if (row.storage === "reference" && (!record(row.source_ref) || typeof row.source_ref.version !== "string" ||
        !row.source_ref.version || Object.values(row.capabilities).some(value => value !== false)))
      throw new Error("conversation_view_reference_capability_invalid");
    if (row.storage === "reference" && record(row.source_ref) && row.source_ref.message_index !== undefined &&
        (typeof row.source_ref.message_index !== "number" || !Number.isSafeInteger(row.source_ref.message_index) || row.source_ref.message_index < 0))
      throw new Error("conversation_view_reference_index_invalid");
    ids.add(row.id);
  }
  // Decoded fields are a display projection. Unknown numeric token precision
  // belongs to the opaque native wire, not JavaScript number round-trips.
  const view = value as unknown as ConversationView;
  if (nativeWire !== undefined) nativeWires.set(view, nativeWire);
  return view;
}
export function messageCan(message: Message | undefined, operation: "edit" | "set_status" | "restore" | "native_lookup"): boolean {
  return Boolean(message && message.storage !== "reference" && (message.capabilities === undefined || message.capabilities[operation] === true));
}
export function sourceHistorySendUnavailable(view: ConversationView | null): boolean {
  return Boolean(view && (view.resources.length || view.messages.some(row => row.storage === "reference")) &&
    !view.capabilities.source_history_send.available);
}
/** Select all/current history from the same version. No rendering or I/O required. */
export function conversationViewMessages(view: ConversationView, all = false): Message[] {
  return all ? view.messages : view.messages.filter(row => row.status === "active");
}
export interface ResourceReadChoice { key: string; value: string; choices: string[] }
/** Options and defaults come from the native RuntimeProfile inspection. */
export function resourceReadChoices(view: ConversationView): ResourceReadChoice[] {
  const config = view.read_configuration;
  if (!record(config) || !record(config.values) || !record(config.value_schema) || !record(config.value_schema.properties))
    throw new Error("conversation_view_read_configuration_unavailable");
  const values = config.values;
  return Object.entries(config.value_schema.properties).map(([key, schema]) => {
    const value = values[key];
    if (!record(schema) || schema.type !== "string" || !Array.isArray(schema.enum) ||
        schema.enum.some(item => typeof item !== "string") || typeof value !== "string" || !schema.enum.includes(value))
      throw new Error("conversation_view_read_option_unsupported");
    return { key, value, choices: schema.enum as string[] };
  });
}
