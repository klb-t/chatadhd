// LoomApi — the one interface every UI component talks to. Two
// implementations satisfy it: loom-http.ts (fetch + SSE, used on the web,
// talking to loom-server) and loom-jni.ts (Android WebView -> window.LoomBridge,
// used when the app is embedded). src/api/index.ts picks one at runtime.
import type {
  ChatChunk,
  ChatRequest,
  ConfigMap,
  Conversation,
  ContextSet,
  GraphData,
  GraphEdge,
  GraphNode,
  ImportChunk,
  LoomEventEnvelope,
  MemoryNode,
  Message,
  ModelInfo,
  ProviderInfo,
  SemanticStatusInfo,
  Task,
} from "./types";
import type { KnowledgeApi } from "./knowledge";
import type { GraphPacketStoreRequest, GraphPacketStoreResult } from "./graph-packets";

export interface SearchOptions {
  limit?: number;
  conv_id?: string;
  include_inactive?: boolean;
  mode?: "auto" | "fts5" | "like";
}

export interface SearchResult {
  mode: string;
  results: (Message & { score?: number; snippet?: string })[];
}

export interface ContextSelectRequest {
  text: string;
  depth?: number;
  max_tokens?: number;
  conv_id?: string;
  include_memory?: boolean;
  include_graph?: boolean;
  include_search?: boolean;
}

// Handed to streaming calls (chat, import, live events). Exactly one of
// onDone/onError fires, always last.
export interface StreamHandlers<TChunk> {
  onChunk?: (chunk: TChunk) => void;
  onDone?: () => void;
  onError?: (message: string) => void;
}

// Returned by every streaming call: call it to stop the stream / cancel the
// underlying operation.
export type Unsubscribe = () => void;

export interface NativeUiRequestOptions { signal?: AbortSignal; }

export interface LoomApi {
  // Server-owned static-kernel bridges. Older embedded hosts omit capabilities
  // until their actual native dispatch is available.
  onboarding?(command: Record<string, unknown>, options?: NativeUiRequestOptions): Promise<Record<string, unknown>>;
  methods?(command: Record<string, unknown>, options?: NativeUiRequestOptions): Promise<Record<string, unknown>>;
  analysis?(command: Record<string, unknown>, options?: NativeUiRequestOptions): Promise<Record<string, unknown>>;
  graphReply?(command: Record<string, unknown>, options?: NativeUiRequestOptions): Promise<Record<string, unknown>>;
  // Optional native extensions: older embedded hosts report unavailable.
  usagePolicy?(command: Record<string, unknown>): Promise<Record<string, unknown>>;
  packet?(command: Record<string, unknown>): Promise<Record<string, unknown>>;
  readonly operations?: import("./operations").OperationsApi;
  // Additive capability; older embedded hosts can leave it unavailable.
  readonly knowledge?: KnowledgeApi;
  // Optional: older native embedded hosts do not dispatch this ABI yet.
  graphPacketStore?(request: GraphPacketStoreRequest): Promise<GraphPacketStoreResult>;
  // Conversations
  listConversations(limit?: number): Promise<Conversation[]>;
  createConversation(title?: string): Promise<Conversation>;
  getConversation(id: string): Promise<Conversation>;
  updateConversation(id: string, patch: Partial<Conversation>): Promise<Conversation>;
  deleteConversation(id: string): Promise<void>;

  // Messages
  getMessages(convId: string, all?: boolean): Promise<Message[]>;
  getMessage(id: string): Promise<Message>;
  editMessage(id: string, newText: string): Promise<Message>;
  restoreVersion(id: string): Promise<void>;
  getVersions(id: string): Promise<Message[]>;
  setMessageStatus(id: string, status: Message["status"]): Promise<void>;
  updateMessage(id: string, patch: Record<string, unknown>): Promise<Message>;

  search(query: string, opts?: SearchOptions): Promise<SearchResult>;

  // Chat (streaming)
  chat(req: ChatRequest, handlers: StreamHandlers<ChatChunk>): Unsubscribe;
  cancelChat(requestId: string): Promise<void>;

  // Models & providers
  getModels(): Promise<ModelInfo[]>;
  refreshModels(): Promise<{ count: number }>;
  getProviders(): Promise<ProviderInfo[]>;

  // Config & secrets
  getConfig(): Promise<ConfigMap>;
  setConfig(patch: ConfigMap): Promise<ConfigMap>;
  setConfigKey(key: string, value: unknown): Promise<ConfigMap>;
  listSecretKeys(): Promise<string[]>;
  setSecret(key: string, value: string): Promise<void>;
  hasSecret(key: string): Promise<boolean>;
  deleteSecret(key: string): Promise<void>;

  // Graph & context
  getNodes(filter?: { kind?: string; label?: string; limit?: number }): Promise<GraphNode[]>;
  getEdges(filter?: { node_id?: string; link_type?: string; limit?: number }): Promise<GraphEdge[]>;
  expandGraph(seedIds: string[], depth: number): Promise<GraphData>;
  getGraphData(convId?: string): Promise<GraphData>;
  reindexGraph(convId?: string): Promise<{ reindexed: number }>;
  selectContext(req: ContextSelectRequest): Promise<ContextSet>;

  // Semantic worker
  getSemanticStatus(): Promise<SemanticStatusInfo>;
  pauseSemantic(): Promise<SemanticStatusInfo>;
  resumeSemantic(): Promise<SemanticStatusInfo>;
  wakeSemantic(): Promise<SemanticStatusInfo>;

  // Memory tree
  listMemory(): Promise<MemoryNode[]>;
  createMemory(node: Partial<MemoryNode>): Promise<MemoryNode>;
  updateMemory(id: string, patch: Partial<MemoryNode>): Promise<MemoryNode>;
  deleteMemory(id: string): Promise<void>;
  getMemoryContext(maxChars?: number): Promise<string>;

  // Import / export
  importFile(file: File, title: string | undefined, handlers: StreamHandlers<ImportChunk>): Unsubscribe;
  exportConversationUrl(convId: string, format: "json" | "markdown" | "text" | "html"): string;

  // Provenance, events, tasks
  listSources(limit?: number): Promise<unknown[]>;
  getProvenance(subjectId: string): Promise<unknown>;
  queryEvents(query?: Record<string, unknown>): Promise<unknown[]>;
  subscribeEvents(event: string, handlers: StreamHandlers<LoomEventEnvelope>): Unsubscribe;
  listTasks(filter?: Record<string, unknown>): Promise<Task[]>;
  getTask(id: string): Promise<Task>;
  resumeTasks(): Promise<{ recovered: number }>;
  cancelTask(id: string): Promise<void>;

  // Logs
  getLogs(maxLines?: number): Promise<string[]>;

  // Auth (web transport only; jni transport is a no-op)
  setAuthToken(token: string | null): void;
}
