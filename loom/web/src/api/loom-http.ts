// loom-http.ts — LoomApi implementation for the browser: plain fetch for
// request/response endpoints, fetch + ReadableStream for SSE endpoints
// (chat, import progress, live events). Talks to loom-server's /api/*.
import type { LoomApi, NativeUiRequestOptions, ContextSelectRequest, SearchOptions, SearchResult, StreamHandlers, Unsubscribe } from "./loom-api";
import { createOperationsApi } from "./operations";
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
import type { GraphPacketStoreRequest, GraphPacketStoreResult } from "./graph-packets";

import { readConversationView, type ConversationReadRequest, type ConversationView } from "./conversation-view";

const TOKEN_KEY = "loom.auth_token";

class HttpError extends Error {
  status: number;
  constructor(status: number, message: string) {
    super(message);
    this.status = status;
  }
}

export class LoomHttpApi implements LoomApi {
  private token: string | null = null;
  onboarding(command: Record<string, unknown>, options?: NativeUiRequestOptions): Promise<Record<string, unknown>> {
    return this.req("POST", "/api/onboarding", command, false, options?.signal);
  }
  methods(command: Record<string, unknown>, options?: NativeUiRequestOptions): Promise<Record<string, unknown>> {
    // Mounting a view reads configuration; it never crosses a write endpoint.
    if (Object.keys(command).length === 1 &&
        (command.action === "chat_settings" || command.operation === "chat_settings"))
      return this.req("GET", "/api/methods/chat-settings", undefined, false, options?.signal);
    return this.req("POST", "/api/methods", command, false, options?.signal);
  }
  analysis(command: Record<string, unknown>, options?: NativeUiRequestOptions): Promise<Record<string, unknown>> {
    return this.req("POST", "/api/analysis", command, false, options?.signal);
  }
  graphReply(command: Record<string, unknown>, options?: NativeUiRequestOptions): Promise<Record<string, unknown>> {
    return this.req("POST", "/api/graph-reply", command, false, options?.signal);
  }
  readonly operations = createOperationsApi(
    <T,>(method: string, path: string, body?: unknown) => this.req<T>(method, path, body),
    async <T,>(path: string, body: FormData): Promise<T> => {
      const response = await fetch(path, { method: "POST", headers: this.headers(), body });
      const result: unknown = await response.json();
      if (!response.ok || isLoomError(result)) {
        throw new HttpError(response.status, isLoomError(result) ? result.error.message : `HTTP ${response.status}`);
      }
      return result as T;
    },
  );
  usagePolicy(command: Record<string, unknown>): Promise<Record<string, unknown>> {
    return this.req("POST", "/api/usage-policy", command);
  }
  packet(command: Record<string, unknown>): Promise<Record<string, unknown>> {
    // Packet failures contain the immutable first-response capture. Preserve
    // that envelope for inspection even when native validation returns HTTP 400.
    return this.req("POST", "/api/packet", command, true);
  }
  graphPacketStore(request: GraphPacketStoreRequest): Promise<GraphPacketStoreResult> {
    return this.req("POST", "/api/graph/packets/store", request);
  }
  readonly knowledge: KnowledgeApi = {
    listRuns: () => this.req("GET", "/api/knowledge/runs"),
    query: (what, filters = {}) => this.req("POST", "/api/knowledge/query", { ...filters, what }),
    run: (config) => this.req("POST", "/api/knowledge/run", config),
    cancel: async () => { await this.req("POST", "/api/knowledge/cancel"); },
    buildContext: (request) => this.req("POST", "/api/context/build", request),
    catalogUnits: (query = {}) => this.req("POST", "/api/catalog/query", query),
    catalogScan: (config) => this.req("POST", "/api/catalog/scan", config),
    catalogScore: (config) => this.req("POST", "/api/catalog/score", config),
    catalogSelect: (run_id = "") => this.req("POST", "/api/catalog/select", { run_id }),
    catalogPreview: (id) => this.req("GET", `/api/catalog/units/${encodeURIComponent(id)}`),
    catalogOverride: (override) => this.req("POST", "/api/catalog/override", override),
    catalogImport: (options) => this.req("POST", "/api/catalog/import", options),
  };

  constructor() {
    try {
      this.token = sessionStorage.getItem(TOKEN_KEY);
    } catch {
      this.token = null;
    }
  }

  setAuthToken(token: string | null): void {
    this.token = token;
    try {
      if (token) sessionStorage.setItem(TOKEN_KEY, token);
      else sessionStorage.removeItem(TOKEN_KEY);
    } catch {
      // sessionStorage unavailable (private mode etc.) - token stays in memory only.
    }
  }

  private headers(extra?: Record<string, string>): Record<string, string> {
    const h: Record<string, string> = { ...extra };
    if (this.token) h["Authorization"] = `Bearer ${this.token}`;
    return h;
  }

  private async req<T>(method: string, path: string, body?: unknown, keepError = false, signal?: AbortSignal, cache?: RequestCache, captureWire?: (text: string) => void): Promise<T> {
    const res = await fetch(path, {
      method,
      headers: body !== undefined ? this.headers({ "Content-Type": "application/json" }) : this.headers(),
      body: body !== undefined ? JSON.stringify(body) : undefined,
      signal, cache,
    });
    const text = await res.text();
    let parsed: unknown;
    try { parsed = text ? JSON.parse(text) : {}; }
    catch (cause) {
      // Source-view parse diagnostics must not echo snippets of its payload.
      if (captureWire) throw new HttpError(res.status, "native_response_json_invalid");
      throw cause;
    }
    if (keepError && isLoomError(parsed)) return parsed as T;
    if (!res.ok || isLoomError(parsed)) {
      const msg = isLoomError(parsed) ? parsed.error.message : `HTTP ${res.status}`;
      throw new HttpError(res.status, msg);
    }
    captureWire?.(text);
    return parsed as T;
  }

  private qs(params: Record<string, string | number | boolean | undefined>): string {
    const parts: string[] = [];
    for (const [k, v] of Object.entries(params)) {
      if (v === undefined) continue;
      parts.push(`${encodeURIComponent(k)}=${encodeURIComponent(String(v))}`);
    }
    return parts.length ? `?${parts.join("&")}` : "";
  }

  async readConversationView(convId: string, request: ConversationReadRequest, options?: NativeUiRequestOptions): Promise<ConversationView> {
    if (request.sourceAccess !== "metadata" && request.sourceAccess !== "local_read") throw new Error("conversation_view_access_invalid");
    if (request.sourceAccess === "metadata" && request.readOptions !== undefined) throw new Error("conversation_view_metadata_options_invalid");
    const localRead = request.sourceAccess === "local_read";
    let nativeWire: string | undefined;
    const result = await this.req<unknown>(localRead ? "POST" : "GET", `/api/conversations/${encodeURIComponent(convId)}/view`,
      localRead ? { ...(request.readOptions === undefined ? {} : { read_options: request.readOptions }) } : undefined,
      false, options?.signal, "no-store", text => { nativeWire = text; });
    return readConversationView(result, convId, nativeWire);
  }

  // Generic SSE reader shared by chat/import/events. Returns an unsubscribe
  // function that aborts the underlying fetch (which the server sees as a
  // disconnect and uses to cancel/cleanup on its side).
  private streamSse<TChunk>(
    method: string,
    path: string,
    body: unknown | undefined,
    handlers: StreamHandlers<TChunk>,
  ): Unsubscribe {
    const controller = new AbortController();
    (async () => {
      try {
        const res = await fetch(path, {
          method,
          headers: body !== undefined ? this.headers({ "Content-Type": "application/json" }) : this.headers(),
          body: body !== undefined ? JSON.stringify(body) : undefined,
          signal: controller.signal,
        });
        if (!res.ok || !res.body) {
          handlers.onError?.(`HTTP ${res.status}`);
          return;
        }
        const reader = res.body.getReader();
        const decoder = new TextDecoder();
        let buf = "";
        for (;;) {
          const { value, done } = await reader.read();
          if (done) break;
          buf += decoder.decode(value, { stream: true });
          let sep: number;
          while ((sep = buf.indexOf("\n\n")) !== -1) {
            const rawEvent = buf.slice(0, sep);
            buf = buf.slice(sep + 2);
            const line = rawEvent.split("\n").find((l) => l.startsWith("data:"));
            if (!line) continue;
            const jsonText = line.slice(5).trim();
            if (!jsonText) continue;
            try {
              handlers.onChunk?.(JSON.parse(jsonText) as TChunk);
            } catch {
              // Malformed chunk - skip it rather than killing the stream.
            }
          }
        }
        handlers.onDone?.();
      } catch (err) {
        if ((err as { name?: string })?.name === "AbortError") return;
        handlers.onError?.(err instanceof Error ? err.message : String(err));
      }
    })();
    return () => controller.abort();
  }

  // ── Conversations ────────────────────────────────────────────────────

  listConversations(limit = 50): Promise<Conversation[]> {
    return this.req("GET", `/api/conversations${this.qs({ limit })}`);
  }
  createConversation(title?: string): Promise<Conversation> {
    return this.req("POST", "/api/conversations", { title });
  }
  getConversation(id: string): Promise<Conversation> {
    return this.req("GET", `/api/conversations/${encodeURIComponent(id)}`);
  }
  updateConversation(id: string, patch: Partial<Conversation>): Promise<Conversation> {
    return this.req("PATCH", `/api/conversations/${encodeURIComponent(id)}`, patch);
  }
  async deleteConversation(id: string): Promise<void> {
    await this.req("DELETE", `/api/conversations/${encodeURIComponent(id)}`);
  }

  // ── Messages ─────────────────────────────────────────────────────────

  getMessages(convId: string, all = false): Promise<Message[]> {
    return this.req("GET", `/api/conversations/${encodeURIComponent(convId)}/messages${this.qs({ all: all ? 1 : undefined })}`);
  }
  getMessage(id: string): Promise<Message> {
    return this.req("GET", `/api/messages/${encodeURIComponent(id)}`);
  }
  editMessage(id: string, newText: string): Promise<Message> {
    return this.req("POST", `/api/messages/${encodeURIComponent(id)}/edit`, { text: newText });
  }
  async restoreVersion(id: string): Promise<void> {
    await this.req("POST", `/api/messages/${encodeURIComponent(id)}/restore`);
  }
  getVersions(id: string): Promise<Message[]> {
    return this.req("GET", `/api/messages/${encodeURIComponent(id)}/versions`);
  }
  async setMessageStatus(id: string, status: Message["status"]): Promise<void> {
    await this.req("POST", `/api/messages/${encodeURIComponent(id)}/status`, { status });
  }
  updateMessage(id: string, patch: Record<string, unknown>): Promise<Message> {
    return this.req("PATCH", `/api/messages/${encodeURIComponent(id)}`, patch);
  }

  search(query: string, opts: SearchOptions = {}): Promise<SearchResult> {
    return this.req("GET", `/api/search${this.qs({ q: query, ...opts })}`);
  }

  // ── Chat ─────────────────────────────────────────────────────────────

  chat(req: ChatRequest, handlers: StreamHandlers<ChatChunk>): Unsubscribe {
    return this.streamSse("POST", "/api/chat", req, handlers);
  }
  async cancelChat(requestId: string): Promise<void> {
    await this.req("POST", "/api/chat/cancel", { request_id: requestId });
  }

  // ── Models & providers ───────────────────────────────────────────────

  getModels(): Promise<ModelInfo[]> {
    return this.req<{ models?: ModelInfo[] } | ModelInfo[]>("GET", "/api/models").then((r) =>
      Array.isArray(r) ? r : r.models ?? [],
    );
  }
  refreshModels(): Promise<{ count: number }> {
    return this.req("POST", "/api/models/refresh");
  }
  getProviders(): Promise<ProviderInfo[]> {
    return this.req<{ providers: ProviderInfo[] }>("GET", "/api/providers").then((r) => r.providers ?? []);
  }

  // ── Config & secrets ─────────────────────────────────────────────────

  getConfig(): Promise<ConfigMap> {
    return this.req("GET", "/api/config");
  }
  setConfig(patch: ConfigMap): Promise<ConfigMap> {
    return this.req("PATCH", "/api/config", patch);
  }
  setConfigKey(key: string, value: unknown): Promise<ConfigMap> {
    return this.req("PUT", `/api/config/${encodeURIComponent(key)}`, { value });
  }
  listSecretKeys(): Promise<string[]> {
    return this.req("GET", "/api/secrets");
  }
  async setSecret(key: string, value: string): Promise<void> {
    await this.req("POST", `/api/secrets/${encodeURIComponent(key)}`, { value });
  }
  hasSecret(key: string): Promise<boolean> {
    return this.req<{ has: boolean }>("GET", `/api/secrets/${encodeURIComponent(key)}/has`).then((r) => r.has);
  }
  async deleteSecret(key: string): Promise<void> {
    await this.req("DELETE", `/api/secrets/${encodeURIComponent(key)}`);
  }

  // ── Graph & context ──────────────────────────────────────────────────

  getNodes(filter: { kind?: string; label?: string; limit?: number } = {}): Promise<GraphNode[]> {
    return this.req("GET", `/api/graph/nodes${this.qs(filter)}`);
  }
  getEdges(filter: { node_id?: string; link_type?: string; limit?: number } = {}): Promise<GraphEdge[]> {
    return this.req("GET", `/api/graph/edges${this.qs(filter)}`);
  }
  expandGraph(seedIds: string[], depth: number): Promise<GraphData> {
    return this.req("POST", "/api/graph/expand", { seed_ids: seedIds, depth });
  }
  getGraphData(convId?: string): Promise<GraphData> {
    return this.req("GET", `/api/graph/data${this.qs({ conv_id: convId })}`);
  }
  reindexGraph(convId?: string): Promise<{ reindexed: number }> {
    return this.req("POST", "/api/graph/reindex", { conv_id: convId });
  }
  selectContext(req: ContextSelectRequest): Promise<ContextSet> {
    return this.req("POST", "/api/context/select", req);
  }

  // ── Semantic worker ──────────────────────────────────────────────────

  getSemanticStatus(): Promise<SemanticStatusInfo> {
    return this.req("GET", "/api/semantic/status");
  }
  pauseSemantic(): Promise<SemanticStatusInfo> {
    return this.req("POST", "/api/semantic/pause");
  }
  resumeSemantic(): Promise<SemanticStatusInfo> {
    return this.req("POST", "/api/semantic/resume");
  }
  wakeSemantic(): Promise<SemanticStatusInfo> {
    return this.req("POST", "/api/semantic/wake");
  }

  // ── Memory tree ──────────────────────────────────────────────────────

  listMemory(): Promise<MemoryNode[]> {
    return this.req<{ nodes: MemoryNode[] }>("GET", "/api/memory").then((r) => r.nodes ?? []);
  }
  createMemory(node: Partial<MemoryNode>): Promise<MemoryNode> {
    return this.req("POST", "/api/memory", node);
  }
  updateMemory(id: string, patch: Partial<MemoryNode>): Promise<MemoryNode> {
    return this.req("PATCH", `/api/memory/${encodeURIComponent(id)}`, patch);
  }
  async deleteMemory(id: string): Promise<void> {
    await this.req("DELETE", `/api/memory/${encodeURIComponent(id)}`);
  }
  getMemoryContext(maxChars?: number): Promise<string> {
    return this.req<{ context: string }>("GET", `/api/memory/context${this.qs({ max_chars: maxChars })}`).then(
      (r) => r.context,
    );
  }

  // ── Import / export ──────────────────────────────────────────────────

  importFile(file: File, title: string | undefined, handlers: StreamHandlers<ImportChunk>): Unsubscribe {
    const controller = new AbortController();
    (async () => {
      try {
        const form = new FormData();
        form.append("file", file, file.name);
        const url = `/api/import${title ? this.qs({ title }) : ""}`;
        const res = await fetch(url, { method: "POST", headers: this.headers(), body: form, signal: controller.signal });
        if (!res.ok || !res.body) {
          handlers.onError?.(`HTTP ${res.status}`);
          return;
        }
        const reader = res.body.getReader();
        const decoder = new TextDecoder();
        let buf = "";
        for (;;) {
          const { value, done } = await reader.read();
          if (done) break;
          buf += decoder.decode(value, { stream: true });
          let sep: number;
          while ((sep = buf.indexOf("\n\n")) !== -1) {
            const rawEvent = buf.slice(0, sep);
            buf = buf.slice(sep + 2);
            const line = rawEvent.split("\n").find((l) => l.startsWith("data:"));
            if (!line) continue;
            const jsonText = line.slice(5).trim();
            if (!jsonText) continue;
            try {
              handlers.onChunk?.(JSON.parse(jsonText) as ImportChunk);
            } catch {
              // ignore malformed chunk
            }
          }
        }
        handlers.onDone?.();
      } catch (err) {
        if ((err as { name?: string })?.name === "AbortError") return;
        handlers.onError?.(err instanceof Error ? err.message : String(err));
      }
    })();
    return () => controller.abort();
  }

  exportConversationUrl(convId: string, format: "json" | "markdown" | "text" | "html"): string {
    return `/api/conversations/${encodeURIComponent(convId)}/export${this.qs({ format })}`;
  }

  // ── Provenance, events, tasks ────────────────────────────────────────

  listSources(limit?: number): Promise<unknown[]> {
    return this.req("GET", `/api/sources${this.qs({ limit })}`);
  }
  getProvenance(subjectId: string): Promise<unknown> {
    return this.req("GET", `/api/provenance/${encodeURIComponent(subjectId)}`);
  }
  queryEvents(query: Record<string, unknown> = {}): Promise<unknown[]> {
    return this.req(
      "GET",
      `/api/events${this.qs(query as Record<string, string | number | boolean | undefined>)}`,
    );
  }
  subscribeEvents(event: string, handlers: StreamHandlers<LoomEventEnvelope>): Unsubscribe {
    return this.streamSse("GET", `/api/events/stream${this.qs({ event })}`, undefined, handlers);
  }
  listTasks(filter: Record<string, unknown> = {}): Promise<Task[]> {
    return this.req("GET", `/api/tasks${this.qs(filter as Record<string, string | number | boolean | undefined>)}`);
  }
  getTask(id: string): Promise<Task> {
    return this.req("GET", `/api/tasks/${encodeURIComponent(id)}`);
  }
  resumeTasks(): Promise<{ recovered: number }> {
    return this.req("POST", "/api/tasks/resume");
  }
  async cancelTask(id: string): Promise<void> {
    await this.req("POST", `/api/tasks/${encodeURIComponent(id)}/cancel`);
  }

  // ── Logs ─────────────────────────────────────────────────────────────

  getLogs(maxLines = 200): Promise<string[]> {
    return this.req("GET", `/api/logs${this.qs({ max_lines: maxLines })}`);
  }
}
