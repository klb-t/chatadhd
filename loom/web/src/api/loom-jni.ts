// loom-jni.ts — LoomApi implementation for the Android WebView shell.
//
// The native side installs `window.LoomBridge` (a Kotlin/Java object with
// @JavascriptInterface methods) before the page loads. Every method takes
// and returns *strings* (WebView JS bridges can't pass structured objects
// reliably), so JSON is serialised on both sides - mirroring loom.h itself:
// `method` is the loom_* function name with the `loom_` prefix stripped
// (e.g. "list_conversations", "chat_ex", "import_file"), `argsJson` is a
// JSON object of its non-context arguments, keyed by name.
//
// Streaming calls (chat_ex, import_file, subscribe) go through
// startStream(): the native side runs the call on a background thread and
// invokes `window.__loomCallbacks[callbackId](chunkJson, done)` for every
// chunk, exactly like the C ABI's LoomStreamCallback/LoomProgressCallback/
// LoomEventCallback (`done` is 1 on the final call, matching loom.h).
import type { LoomApi, ContextSelectRequest, SearchOptions, SearchResult, StreamHandlers, Unsubscribe } from "./loom-api";
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
import { isLoomError } from "./types";
import type { KnowledgeApi } from "./knowledge";

interface NativeBridge {
  call(method: string, argsJson: string): string;
  startStream(method: string, argsJson: string, callbackId: string): void;
  cancelStream(callbackId: string): void;
}

declare global {
  interface Window {
    LoomBridge?: NativeBridge;
    __loomCallbacks?: Record<string, (chunkJson: string, done: number) => void>;
  }
}

export function hasNativeBridge(): boolean {
  return typeof window !== "undefined" && !!window.LoomBridge;
}

let callbackSeq = 0;
function nextCallbackId(): string {
  callbackSeq += 1;
  return `cb_${Date.now()}_${callbackSeq}`;
}

export class LoomJniApi implements LoomApi {
  readonly knowledge: KnowledgeApi = {
    listRuns: () => this.callAsync("kb_runs", { limit: 50 }),
    query: (what, filters = {}) => this.callAsync("kb_query", { query: { ...filters, what } }),
    run: (config) => this.background("knowledge_run", { config }),
    cancel: async () => { this.call("knowledge_cancel"); },
    buildContext: (request) => this.background("context_build", { request }),
    catalogUnits: (query = {}) => this.callAsync("catalog_query", { query }),
    catalogScan: (config) => this.background("catalog_scan", { config }),
    catalogPreview: (unit_id) => this.callAsync("catalog_preview", { unit_id }),
    catalogOverride: (override) => this.callAsync("catalog_override", { override }),
    catalogImport: (options) => this.background("catalog_import", { options }),
  };
  private bridge(): NativeBridge {
    if (!window.LoomBridge) throw new Error("LoomBridge is not installed (not running inside the Android shell)");
    return window.LoomBridge;
  }

  private call<T>(method: string, args: Record<string, unknown> = {}): T {
    const raw = this.bridge().call(method, JSON.stringify(args));
    const parsed = JSON.parse(raw);
    if (isLoomError(parsed)) throw new Error(parsed.error.message);
    return parsed as T;
  }

  private async callAsync<T>(method: string, args: Record<string, unknown> = {}): Promise<T> {
    return this.call<T>(method, args);
  }

  private callInt(method: string, args: Record<string, unknown> = {}): number {
    // Native dispatch returns JSON for every method. Parse errors through
    // the same firewall; negative native errors must never look successful.
    const result = this.call<{ ok?: boolean; value?: boolean }>(method, args);
    return result.value === undefined ? 0 : Number(result.value);
  }

  private background<T>(method: string, args: Record<string, unknown>): Promise<T> {
    return new Promise((resolve, reject) => {
      let result: T | undefined;
      this.stream<T>(method, args, {
        onChunk: (chunk) => { result = chunk; },
        onDone: () => result === undefined ? reject(new Error(`${method} returned no result`)) : resolve(result),
        onError: (message) => reject(new Error(message)),
      });
    });
  }

  private stream<TChunk>(method: string, args: Record<string, unknown>, handlers: StreamHandlers<TChunk>): Unsubscribe {
    const id = nextCallbackId();
    if (!window.__loomCallbacks) window.__loomCallbacks = {};
    window.__loomCallbacks[id] = (chunkJson: string, done: number) => {
      let failed = false;
      try {
        const chunk = JSON.parse(chunkJson) as TChunk;
        if (isLoomError(chunk)) {
          failed = true;
          handlers.onError?.(chunk.error.message);
        } else handlers.onChunk?.(chunk);
      } catch (error) {
        failed = true;
        handlers.onError?.(error instanceof Error ? error.message : String(error));
      }
      if (done || failed) {
        delete window.__loomCallbacks?.[id];
        if (!failed) handlers.onDone?.();
      }
    };
    try {
      this.bridge().startStream(method, JSON.stringify(args), id);
    } catch (err) {
      delete window.__loomCallbacks?.[id];
      handlers.onError?.(err instanceof Error ? err.message : String(err));
    }
    return () => {
      this.bridge().cancelStream(id);
      delete window.__loomCallbacks?.[id];
    };
  }

  setAuthToken(): void {
    // No-op: the native shell talks to the in-process LoomContext directly,
    // there is no bearer-token HTTP boundary to authenticate.
  }

  // ── Conversations ────────────────────────────────────────────────────

  listConversations(limit = 50): Promise<Conversation[]> {
    return this.callAsync("list_conversations", { limit });
  }
  createConversation(title?: string): Promise<Conversation> {
    return this.callAsync("create_conversation", { title });
  }
  getConversation(id: string): Promise<Conversation> {
    return this.callAsync("get_conversation", { conv_id: id });
  }
  updateConversation(id: string, patch: Partial<Conversation>): Promise<Conversation> {
    return this.callAsync("update_conversation", { conv_id: id, patch });
  }
  async deleteConversation(id: string): Promise<void> {
    this.callInt("delete_conversation", { conv_id: id });
  }

  // ── Messages ─────────────────────────────────────────────────────────

  getMessages(convId: string, all = false): Promise<Message[]> {
    return this.callAsync(all ? "get_messages_ex" : "get_messages", { conv_id: convId, include_all: all ? 1 : 0 });
  }
  getMessage(id: string): Promise<Message> {
    return this.callAsync("get_message", { msg_id: id });
  }
  editMessage(id: string, newText: string): Promise<Message> {
    return this.callAsync("edit_message", { msg_id: id, new_text: newText });
  }
  async restoreVersion(id: string): Promise<void> {
    this.callInt("restore_version", { msg_id: id });
  }
  getVersions(id: string): Promise<Message[]> {
    return this.callAsync("get_versions", { msg_or_group_id: id });
  }
  async setMessageStatus(id: string, status: Message["status"]): Promise<void> {
    this.callInt("set_message_status", { msg_id: id, status });
  }
  async updateMessage(id: string, patch: Record<string, unknown>): Promise<Message> {
    this.callInt("update_message", { msg_id: id, patch });
    return this.getMessage(id);
  }

  search(query: string, opts: SearchOptions = {}): Promise<SearchResult> {
    return this.callAsync("search", { query, options: opts });
  }

  // ── Chat ─────────────────────────────────────────────────────────────

  chat(req: ChatRequest, handlers: StreamHandlers<ChatChunk>): Unsubscribe {
    return this.stream("chat_ex", { request: req }, handlers);
  }
  async cancelChat(requestId: string): Promise<void> {
    this.callInt("chat_cancel", { request_id: requestId });
  }

  // ── Models & providers ───────────────────────────────────────────────

  getModels(): Promise<ModelInfo[]> {
    return this.callAsync<{ models?: ModelInfo[] } | ModelInfo[]>("get_models").then((r) =>
      Array.isArray(r) ? r : r.models ?? [],
    );
  }
  refreshModels(): Promise<{ count: number }> {
    return this.callAsync("refresh_models");
  }
  getProviders(): Promise<ProviderInfo[]> {
    return this.callAsync<{ providers: ProviderInfo[] }>("get_providers").then((r) => r.providers ?? []);
  }

  // ── Config & secrets ─────────────────────────────────────────────────

  getConfig(): Promise<ConfigMap> {
    return this.callAsync("get_config");
  }
  async setConfig(patch: ConfigMap): Promise<ConfigMap> {
    this.callInt("set_config_json", { patch });
    return this.getConfig();
  }
  async setConfigKey(key: string, value: unknown): Promise<ConfigMap> {
    this.call("set_config", { key, value: typeof value === "string" ? value : JSON.stringify(value) });
    return this.getConfig();
  }
  listSecretKeys(): Promise<string[]> {
    return this.callAsync("list_secret_keys");
  }
  async setSecret(key: string, value: string): Promise<void> {
    this.callInt("set_secret", { key, value });
  }
  async hasSecret(key: string): Promise<boolean> {
    return this.callInt("has_secret", { key }) === 1;
  }
  async deleteSecret(key: string): Promise<void> {
    this.callInt("delete_secret", { key });
  }

  // ── Graph & context ──────────────────────────────────────────────────

  getNodes(filter: { kind?: string; label?: string; limit?: number } = {}): Promise<GraphNode[]> {
    return this.callAsync("get_nodes", { filter });
  }
  getEdges(filter: { node_id?: string; link_type?: string; limit?: number } = {}): Promise<GraphEdge[]> {
    return this.callAsync("get_edges", { filter });
  }
  expandGraph(seedIds: string[], depth: number): Promise<GraphData> {
    return this.callAsync("expand_graph", { seed_ids: seedIds, depth });
  }
  getGraphData(convId?: string): Promise<GraphData> {
    return this.callAsync("get_graph_data", { conv_id: convId ?? null });
  }
  reindexGraph(convId?: string): Promise<{ reindexed: number }> {
    return this.callAsync("graph_reindex", { conv_id: convId ?? null });
  }
  selectContext(req: ContextSelectRequest): Promise<ContextSet> {
    return this.callAsync("select_context_ex", { request: req });
  }

  // ── Semantic worker ──────────────────────────────────────────────────

  getSemanticStatus(): Promise<SemanticStatusInfo> {
    return this.callAsync("semantic_status");
  }
  async pauseSemantic(): Promise<SemanticStatusInfo> {
    this.call("semantic_pause");
    return this.getSemanticStatus();
  }
  async resumeSemantic(): Promise<SemanticStatusInfo> {
    this.call("semantic_resume");
    return this.getSemanticStatus();
  }
  async wakeSemantic(): Promise<SemanticStatusInfo> {
    this.call("semantic_wake");
    return this.getSemanticStatus();
  }

  // ── Memory tree ──────────────────────────────────────────────────────

  listMemory(): Promise<MemoryNode[]> {
    return this.callAsync<{ nodes: MemoryNode[] }>("list_memory").then((r) => r.nodes ?? []);
  }
  createMemory(node: Partial<MemoryNode>): Promise<MemoryNode> {
    return this.callAsync("create_memory", node);
  }
  updateMemory(id: string, patch: Partial<MemoryNode>): Promise<MemoryNode> {
    return this.callAsync("update_memory", { id, ...patch });
  }
  async deleteMemory(id: string): Promise<void> {
    this.callInt("delete_memory", { id });
  }
  async getMemoryContext(maxChars?: number): Promise<string> {
    const r = await this.callAsync<{ context: string }>("get_memory_context", { max_chars: maxChars ?? 0 });
    return r.context;
  }

  // ── Import / export ──────────────────────────────────────────────────

  importFile(file: File, title: string | undefined, handlers: StreamHandlers<ImportChunk>): Unsubscribe {
    // The native shell owns the filesystem; the web layer can only hand it
    // a blob URL/path is not available from a WebView <input>. Real Android
    // integration wires file picking to a native intent that returns a
    // content:// path here instead of a browser File object.
    handlers.onError?.("File import from the browser file picker is not supported in the Android shell yet");
    void file;
    void title;
    return () => {};
  }
  exportConversationUrl(convId: string, format: "json" | "markdown" | "text" | "html"): string {
    // No HTTP server in the native shell; callers should use a native
    // share/save intent instead. Kept for interface parity.
    return `loom-bridge://export/${encodeURIComponent(convId)}?format=${format}`;
  }

  // ── Provenance, events, tasks ────────────────────────────────────────

  listSources(limit?: number): Promise<unknown[]> {
    return this.callAsync("list_sources", { limit: limit ?? 100 });
  }
  getProvenance(subjectId: string): Promise<unknown> {
    return this.callAsync("get_provenance", { subject_id: subjectId });
  }
  queryEvents(query: Record<string, unknown> = {}): Promise<unknown[]> {
    return this.callAsync("query_events", { query });
  }
  subscribeEvents(event: string, handlers: StreamHandlers<LoomEventEnvelope>): Unsubscribe {
    return this.stream("subscribe", { event }, handlers);
  }
  listTasks(filter: Record<string, unknown> = {}): Promise<Task[]> {
    return this.callAsync("list_tasks", { filter });
  }
  getTask(id: string): Promise<Task> {
    return this.callAsync("get_task", { task_id: id });
  }
  resumeTasks(): Promise<{ recovered: number }> {
    return this.callAsync("resume_tasks");
  }
  async cancelTask(id: string): Promise<void> {
    this.callInt("cancel_task", { task_id: id });
  }

  // ── Logs ─────────────────────────────────────────────────────────────

  getLogs(maxLines = 200): Promise<string[]> {
    return this.callAsync("get_logs", { max_lines: maxLines });
  }
}
