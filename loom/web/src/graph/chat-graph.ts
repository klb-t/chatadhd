import { packetResult, record, type GraphReplyAction, type PacketCommandApi } from "./reply-inspection";

/** Read native graph results; labels and text never manufacture a compilation. */
export function recordedGraphReply(metadata: unknown): Record<string, unknown> | undefined {
  const source = record(metadata);
  const value = source.graph_reply ?? record(source.context_trace).graph_reply;
  const reply = record(value);
  return reply.schema === "loom.chat_graph_reply/1" ? reply : undefined;
}
export function nativeGraphDisplayText(reply: Record<string, unknown> | undefined, fallback: string): string {
  return reply && typeof reply.text === "string" ? reply.text : fallback;
}
/** The native fragment lookup recompiles captured bytes against this base packet. */
export async function addressNativeGraphFragment(graphReply: PacketCommandApi, reply: Record<string, unknown>, localId: string, messageId?: string): Promise<Record<string, unknown>> {
  const compilation = record(reply.compilation);
  if (!localId || !compilation.compilation_sha256 || !compilation.base_packet_sha256 || !Object.keys(record(reply.base_packet)).length) throw new Error("Native graph compilation and base packet are unavailable.");
  const command = messageId
    ? { action: "fragment", message_id: messageId, expected_compilation_sha256: compilation.compilation_sha256, address: { local_id: localId } }
    : { action: "fragment", reply_result: reply, address: { local_id: localId } };
  const result = packetResult(await graphReply(command));
  if (result.schema !== "loom.graph_reply_fragment/1" || result.local_id !== localId || result.compilation_sha256 !== compilation.compilation_sha256 || typeof result.text !== "string" || !result.source_observation_id || !Object.keys(record(result.source_locator)).length) throw new Error("Native fragment did not match the recorded compilation and provenance.");
  return result;
}
export function nativeFragmentPrompt(action: GraphReplyAction, fragment: Record<string, unknown>): string {
  return `${action === "expand" ? "Expand" : "Correct"} the addressed model fragment. Treat its content as unverified.\n${JSON.stringify(fragment, null, 2)}`;
}
