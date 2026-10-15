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
import type { SettingsFieldsDoc } from "../settings/field-meta";
import type { ActionsApi, ClientBlob, LoomApi, ContextSelectRequest, SearchOptions, SearchResult, StreamHandlers, Unsubscribe } from "./loom-api";
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
import { readGraphEntry, type GraphEntryOptions, type GraphEntryView } from "./graph-entry";

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
/** One chunk across the string bridge (a multiple of 3, so every chunk but the last is whole base64 groups). */
const BLOB_CHUNK = 196608;

// Bytes <-> base64 for the one transport that has no other way to move them: the shell's string bridge. Always one bounded chunk.
/** A binary string of the bytes in slices, so a long chunk does not overflow the argument limit of the call. */
const SLICE = 0x8000;
function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  for (let at = 0; bytes.length > at; at += SLICE) binary += String.fromCharCode(...bytes.subarray(at, at + SLICE));
  return btoa(binary);
}
function base64ToBytes(encoded: string): Uint8Array {
  const binary = atob(encoded);
  const bytes = new Uint8Array(binary.length);
  for (let at = 0; binary.length > at; at += 1) bytes[at] = binary.charCodeAt(at);
  return bytes;
}

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
    catalogScore: (config) => this.background("catalog_score", { config }),
    catalogSelect: (run_id = "") => this.callAsync("catalog_select", { run_id }),
    catalogPreview: (unit_id) => this.callAsync("catalog_preview", { unit_id }),
    catalogOverride: (override) => this.callAsync("catalog_override", { override }),
    catalogImport: (options) => this.background("catalog_import", { options }),
  };
  // R54: running actions through the one entry point (loom_invoke, ports actions and chat).
  readonly actions: ActionsApi = {
    clientKind: "android",
    clientDeclare: (request) => this.callAsync("invoke", { port: "actions", action: "client.declare", request }),
    clientCapabilities: (request) => this.callAsync("invoke", { port: "actions", action: "client.capabilities", request }),
    clientResult: (result) => this.callAsync("invoke", { port: "actions", action: "client_result", request: result }),
    // No HTTP in the shell: the bytes cross the string bridge in bounded chunks that the kernel appends to an upload and then stores by hash
    // (client.blob_begin | append | commit), and come back the same way (client.blob_read). Nothing is carried in client_result.
    clientBlobUpload: async (blob) => {
      const call = <T,>(action: string, request: Record<string, unknown>) => this.callAsync<T>("invoke", { port: "actions", action, request });
      const begun = await call<{ upload_id: string }>("client.blob_begin", {});
      try {
        for (let at = 0; blob.size > at; at += BLOB_CHUNK) {
          const chunk = new Uint8Array(await blob.slice(at, at + BLOB_CHUNK).arrayBuffer());
          await call("client.blob_append", { upload_id: begun.upload_id, data_base64: bytesToBase64(chunk) });
        }
        return await call<ClientBlob>("client.blob_commit", { upload_id: begun.upload_id, mime: blob.type || "application/octet-stream" });
      } catch (failure) {
        try { await call("client.blob_commit", { upload_id: begun.upload_id, abort: true }); } catch { /* the upload is already gone */ }
        throw failure;
      }
    },
    clientBlobFetch: async (hash) => {
      const pieces: Uint8Array[] = [];
      let mime = "";
      for (let offset = 0, last = false; !last;) {
        const piece = await this.callAsync<{ length: number; eof: boolean; mime: string; data_base64: string }>("invoke",
          { port: "actions", action: "client.blob_read", request: { hash, offset, length: BLOB_CHUNK } });
        pieces.push(base64ToBytes(piece.data_base64));
        mime = piece.mime;
        offset += piece.length;
        last = piece.eof || piece.length === 0;
      }
      return new Blob(pieces as BlobPart[], { type: mime });
    },
    run: (request) => this.callAsync("invoke", { port: "actions", action: "run", request }),
    confirm: (answer) => this.callAsync("invoke", { port: "actions", action: "confirm", request: answer }),
    cancel: (request) => this.callAsync("invoke", { port: "actions", action: "cancel", request }),
    status: (request) => this.callAsync("invoke", { port: "actions", action: "status", request }),
    route: (request) => this.callAsync("invoke", { port: "chat", action: "route", request }),
    rulesSet: (request) => this.callAsync("invoke", { port: "actions", action: "rules.set", request }),
    rulesDisable: (request) => this.callAsync("invoke", { port: "actions", action: "rules.disable", request }),
    rulesList: (request = {}) => this.callAsync("invoke", { port: "actions", action: "rules.list", request }),
    actionsDefine: (request) => this.callAsync("invoke", { port: "actions", action: "actions.define", request }),
    actionsRemove: (request) => this.callAsync("invoke", { port: "actions", action: "actions.remove", request }),
    groupsDefine: (request) => this.callAsync("invoke", { port: "actions", action: "groups.define", request }),
    groupsRemove: (request) => this.callAsync("invoke", { port: "actions", action: "groups.remove", request }),
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

  async getSettingsFields(): Promise<SettingsFieldsDoc> {
    return this.callAsync("settings_fields");
  }
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
  getGraphData(convId?: string, page?: { limit?: number; before?: string }): Promise<GraphData> {
    const paged = convId && (page?.limit !== undefined || page?.before);
    return this.callAsync("get_graph_data", { conv_id: paged ? JSON.stringify({ conv_id: convId, ...page }) : convId ?? null });
  }
  getGraphEntry(options: GraphEntryOptions = {}): Promise<GraphEntryView> {
    return this.callAsync("graph_entry", { request: JSON.stringify(options) }).then(readGraphEntry);
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
