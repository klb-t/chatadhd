// Shared JSON shapes returned by the Loom C ABI (loom/include/loom/loom.h),
// as relayed by both transports (loom-http.ts over the server, loom-jni.ts
// over the Android WebView bridge). Kept intentionally loose (most fields
// optional) since Loom's JSON is hand-written across many modules and this
// is a UI-facing convenience layer, not a schema validator.

export interface LoomError {
  error: { code: string; message: string };
}

export function isLoomError(v: unknown): v is LoomError {
  if (!v || typeof v !== "object" || !("error" in v)) return false;
  const error = (v as { error: unknown }).error;
  // Pipeline results also carry an `error` string (empty on success).
  // Only the C ABI error envelope means the request itself failed.
  return !!error && typeof error === "object" && "message" in error && typeof error.message === "string";
}

export interface Conversation {
  id: string;
  title: string;
  created: string;
  updated: string;
  source?: string;
  metadata?: Record<string, unknown>;
}

export type MessageStatus = "active" | "excluded" | "version" | "deleted";

export interface Message {
  id: string;
  conv_id: string;
  parent_id?: string | null;
  role: "user" | "assistant" | "system";
  text: string;
  model?: string | null;
  status: MessageStatus;
  version_group_id?: string | null;
  version_num?: number;
  weight?: number;
  attachments?: string[];
  metadata?: Record<string, unknown>;
  created: string;
  semantic_status?: "pending" | "done";
}

export interface ChatRequest {
  message: string;
  request_id?: string;
  conv_id?: string;
  model?: string;
  attachments?: string[];
  web_search?: boolean;
  deep_research?: boolean;
  reasoning_effort?: string;
  temperature?: number;
  max_tokens?: number;
  system_prompt?: string;
  context_depth?: number;
  knowledge_context?: ChatKnowledgeContextRequest;
  include_memory?: boolean;
  include_graph_memory?: boolean;
  include_history?: boolean;
  trace_context?: boolean;
  stream?: boolean;
}

// Only fields supported by the native ContextRequest. Omitting text uses the
// current message. Graph reach and representation detail are independent.
export interface ChatKnowledgeContextRequest {
  text?: string;
  targets?: string[];
  project?: string;
  budget_tokens?: number;
  goal_type?: string | null;
  run?: string;
  lang?: string;
  relation_hops?: number;
  detail_resolution?: "label" | "summary" | "full" | "raw" | null;
}

// This records compiled messages, not a complete provider request or a receipt
// proving delivery. Keep additional native fields available to the inspector.
export interface ChatContextTrace extends Record<string, unknown> {
  kind: "compiled_messages";
  version: number;
  messages: { role: string; content: unknown }[];
  messages_sha256?: string;
  history_message_ids?: string[];
  selection?: Record<string, unknown>;
  knowledge_context_request?: ChatKnowledgeContextRequest | null;
  knowledge_context?: Record<string, unknown> | null;
}

export type ChatChunk =
  | { type: "start"; request_id: string; conv_id: string; user_message_id: string }
  | { type: "delta"; text: string }
  | { type: "reasoning"; text: string }
  | {
      type: "done";
      message_id: string;
      conv_id: string;
      text: string;
      usage?: Record<string, unknown>;
      model?: string;
      title?: string;
      request_id?: string;
      cancelled?: boolean;
      context_trace?: ChatContextTrace;
    }
  | { type: "error"; code: string; message: string };

export interface GraphNode {
  id: string;
  kind: string;
  label: string;
  content?: string;
  tags?: string[];
  metadata?: Record<string, unknown>;
  created?: string;
}

export interface GraphEdge {
  id?: string;
  src: string;
  dst: string;
  type?: string;
  link_type?: string;
  weight?: number;
  metadata?: Record<string, unknown>;
}

export interface GraphData {
  nodes: GraphNode[];
  edges: GraphEdge[];
}

export interface ContextSet {
  items: unknown[];
  prompt_text: string;
  token_estimate: number;
  truncated: boolean;
}

export interface SemanticStatusInfo {
  pending: number;
  processed: number;
  errors: number;
  mode: string;
  rate: string;
  batch_id?: string | null;
  batch_submitted?: number;
  running: boolean;
  paused: boolean;
}

export interface MemoryNode {
  id: string;
  content: string;
  parent_id?: string | null;
  node_type: "text" | "folder" | "file" | "dir";
  active: boolean;
  depth: number;
  weight: number;
  tags?: string[];
  created: string;
  metadata?: Record<string, unknown>;
}

export type ImportChunk =
  | { type: "progress"; current: number; total: number; status: string }
  | ({ type: "done" } & Record<string, unknown>)
  | { type: "error"; code: string; message: string };

export interface ModelInfo {
  id: string;
  name?: string;
  context_length?: number;
  pricing?: Record<string, unknown>;
  [key: string]: unknown;
}

export interface ProviderInfo {
  id: string;
  available: boolean;
  capabilities: string[];
}

export interface Task {
  id: string;
  kind: string;
  status: string;
  parent_id?: string | null;
  [key: string]: unknown;
}

export interface LoomEventEnvelope {
  event: string;
  payload: unknown;
}

export type ConfigMap = Record<string, unknown>;
