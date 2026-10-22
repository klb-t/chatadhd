// LoomApi — the one interface every UI component talks to. Two
// implementations satisfy it: loom-http.ts (fetch + SSE, used on the web,
// talking to loom-server) and loom-jni.ts (Android WebView -> window.LoomBridge,
// used when the app is embedded). src/api/index.ts picks one at runtime.
import type { ContextModifyRequest, ContextReceiptRequest } from "./context-receipt";
import type { ProfileConfirmRequest, ProfileInterviewRequest, ProfileSetRequest, ProfileShowRequest } from "./profile-interview";
import type { SettingsFieldsDoc } from "../settings/field-meta";
import type {
  ActionAnswer,
  ActionRun,
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
  SemanticRequeueResult,
  SemanticStatusInfo,
  Task,
} from "./types";
import type { KnowledgeApi } from "./knowledge";
import type { GraphEntryOptions, GraphEntryView } from "./graph-entry";
import type {
  GraphCardResult,
  GraphNavigateRequest,
  GraphNavigateResult,
  RolesResolved,
  RomOverrideRequest,
  SettingsResolved,
} from "./graph-projection";
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

/** Options every write of the user's own rules and definitions takes: `dry_run` validates and writes nothing; `propose` stores the write as a
 *  proposal with its receipt and executes nothing (the answer is `actions.confirm` with that receipt); `confirmed` is the user's explicit yes to
 *  exactly this write and is honoured only with `receipt_id` = the `plan_receipt` of a proposal of the same write - a bare `confirmed: true` is
 *  only a proposal (the rules of the scope still apply, a deny is never overridden). */
export interface OverlayWriteOptions { dry_run?: boolean; propose?: boolean; confirmed?: boolean; receipt_id?: string; ref?: string; context?: Record<string, unknown> }
/** A scoped rule or value (rules.set): written only to the user's overlay profiles/action_rules.pack. */
export interface RuleRequest extends OverlayWriteOptions {
  scope?: string; scope_id?: string; effect?: string; key?: string; value?: unknown; action?: string;
  target?: Record<string, unknown>; reason?: string; initiator?: string; locked?: boolean; id?: string;
}
export interface RuleDisableRequest extends OverlayWriteOptions { id: string; restore?: boolean }
export interface RulesListRequest { scope?: string; scope_id?: string; user_only?: boolean; include_disabled?: boolean }
export interface RulesList { schema?: string; rules: Record<string, unknown>[] }
/** A new action of the user's own (actions.define): a composite with use_model steps, its phrases and where it came from. */
export interface DefineActionRequest extends OverlayWriteOptions {
  id?: string; instruction?: string; steps?: Record<string, unknown>[]; title?: string; requires?: string[];
  phrases?: Record<string, string[]>; lang?: string; provenance?: Record<string, unknown>;
}
/** A named bundle of traits (groups.define). */
export interface DefineGroupRequest extends OverlayWriteOptions {
  id?: string; traits?: string[]; label?: unknown; element_match?: unknown; provenance?: Record<string, unknown>;
}
/** Takes one of the user's own definitions back out of the overlay (actions.remove, groups.remove). */
export interface RemoveRequest extends OverlayWriteOptions { id: string }

/** What a client says about itself and the devices it can drive right now (client.declare). A capability is a name the catalogue's
 *  device actions need (captures_audio, plays_audio, shows_notifications, clipboard_read ...), with its permission state
 *  (granted | prompt | denied) and how the client does it. */
export interface ClientDeclareRequest {
  client_id?: string;
  client?: { kind?: string; platform?: string; label?: string };
  capabilities: Record<string, string | { permission: string; via?: string; detail?: string }>;
}
export interface ClientDeclaration {
  client_id: string;
  capabilities: Record<string, { permission: string; runnable: boolean; actions: string[]; via?: string }>;
  runnable_actions: string[];
  refused: Record<string, string>;
  [key: string]: unknown;
}
/** The answer of a client to the step a run is paused on (actions.client_result). Media name a blob that was uploaded before
 *  (`clientBlobUpload`); bytes never travel inside this JSON. */
export interface ClientResultPart { kind: string; mime?: string; blob_hash?: string; text?: string; name?: string; value?: unknown }
/** A blob the kernel keeps for this user. */
export interface ClientBlob { hash: string; size: number; mime: string }
export interface ClientResult {
  run_id: string;
  receipt_id: string;
  state: "ok" | "failed" | "declined" | "unavailable";
  parts?: ClientResultPart[];
  data?: unknown;
  error?: { code?: string; message: string };
  ref?: string;
}

/** The agent (agent.run): a model that uses the actions of the catalogue. A run that stopped on proposals is resumed with the user's answers. */
export interface AgentApi {
  run(request: Record<string, unknown>): Promise<Record<string, unknown>>;
}

/** Running actions (R54): the same actions the chat window proposes, reachable without it. */
export interface ActionsApi {
  /** The kind of client this transport is (web page, Android shell): what client.declare says about it. */
  readonly clientKind?: string;
  /** Declare the devices this client can drive; the kernel keeps it for this client only (device actions become runnable for it). */
  clientDeclare?(request: ClientDeclareRequest): Promise<ClientDeclaration>;
  clientCapabilities?(request: { client_id?: string }): Promise<Record<string, unknown>>;
  /** Answer the client step a run is paused on; the run continues with the next step. */
  clientResult?(result: ClientResult): Promise<ActionRun>;
  /** Hands bytes to the kernel (a recording, a picture, a file): streamed to disk on the web, in bounded chunks on the native shell; the
   *  answer names the blob by its hash. */
  clientBlobUpload?(blob: Blob, options?: { name?: string }): Promise<ClientBlob>;
  /** The bytes of a stored blob of this user (a recording to play). */
  clientBlobFetch?(hash: string): Promise<Blob>;
  run(request: Record<string, unknown>): Promise<ActionRun>;
  /** Answer the question a run is paused on; the receipt must be the pending one. */
  confirm(answer: ActionAnswer): Promise<ActionRun>;
  cancel(request: { run_id: string; ref?: string }): Promise<ActionRun>;
  status(request: { run_id?: string; list?: boolean; status?: string; conv_id?: string; limit?: number }): Promise<ActionRun>;
  route(request: { text: string; conv_id?: string; method?: string; lang?: string }): Promise<Record<string, unknown>>;
  /** The user's own rules and definitions (P11). A write is a run that passed the rules of the scope: it may wait for a confirmation
   *  (answered with `confirm`), and result.data says what was written and where. Only the user's overlays change. */
  rulesSet(request: RuleRequest): Promise<ActionRun>;
  rulesDisable(request: RuleDisableRequest): Promise<ActionRun>;
  rulesList(request?: RulesListRequest): Promise<RulesList>;
  actionsDefine(request: DefineActionRequest): Promise<ActionRun>;
  actionsRemove(request: RemoveRequest): Promise<ActionRun>;
  groupsDefine(request: DefineGroupRequest): Promise<ActionRun>;
  groupsRemove(request: RemoveRequest): Promise<ActionRun>;
  /** M1-K0 graph projection (read only, nothing written to the graph): one level of the navigable graph around a focus. */
  graphNavigate(request?: GraphNavigateRequest): Promise<GraphNavigateResult>;
  /** Everything about one node: value and why, inheritance, relations by edge kind, the actions that fit it, evidence per property. */
  graphCard(request: { address: string; lang?: string; conv_id?: string }): Promise<GraphCardResult>;
  /** Every setting in its group with the value in words, the level that supplied it and the whole chain. */
  settingsResolved(request?: { conv_id?: string; lang?: string }): Promise<SettingsResolved>;
  /** Who does which job: every model role with its policy chain, model, class, flow and cost. */
  rolesResolved(request?: { conv_id?: string; lang?: string }): Promise<RolesResolved>;
  /** Your copy of a ROM item in the overlay: a run gated by confirm:rom.override (answer it with `confirm`). */
  romOverride(request: RomOverrideRequest): Promise<ActionRun>;
  /** M1-K1b: the receipt of one request ("Co wysłaliśmy"); read only. The answer is a run: read it with readReceiptRun (./context-receipt). */
  contextReceipt(request: ContextReceiptRequest): Promise<ActionRun>;
  /** M1-K1b: a change of the context for the NEXT message only (szerzej, węziej, głębiej, płycej, a selector profile, or a reset). May wait for the
   *  x10 question of the usage guard (status awaiting_usage; answer with `confirm`). Read it with readModifyRun (./context-receipt). */
  contextModify(request: ContextModifyRequest): Promise<ActionRun>;
  /** M1-W2a: the interview about the person, held in the conversation window (form mode, no model). `{op: "start"}` without conv_id opens the
   *  conversation "Poznajmy się" (result.data.conv_id); `{op: "status"}` answers not_found while the interview was never started. Read it with
   *  readInterviewStart / readInterviewStatus (./profile-interview). */
  profileInterview(request: ProfileInterviewRequest): Promise<ActionRun>;
  /** What the app knows about the person, slot by slot, with the state (known, unknown, declined, never); read only. */
  profileShow(request?: ProfileShowRequest): Promise<ActionRun>;
  /** Writes one answer as the person's own (the kernel asks first: confirm:profile.set). */
  profileSet(request: ProfileSetRequest): Promise<ActionRun>;
  /** Applies what the interview understood (a remark, a section card); the kernel asks first (confirm:profile.confirm). */
  profileConfirm(request: ProfileConfirmRequest): Promise<ActionRun>;
}

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
  // Additive capability (loom_invoke ports actions and chat); older hosts leave it unavailable.
  readonly actions?: ActionsApi;
  readonly agent?: AgentApi;
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

  // Metadata GET never grants source access; local_read requires an explicit host action.
  readConversationView?(convId: string, request: import("./conversation-view").ConversationReadRequest, options?: NativeUiRequestOptions): Promise<import("./conversation-view").ConversationView>;

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
  getSettingsFields(): Promise<SettingsFieldsDoc>;
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
  getGraphData(convId?: string, page?: import("./types").GraphDataPage): Promise<GraphData>;
  getGraphEntry(options?: GraphEntryOptions): Promise<GraphEntryView>;
  reindexGraph(convId?: string): Promise<{ reindexed: number }>;
  /** P24 views: the perspectives, several rendered side by side, and where a setting's value comes from. */
  listPerspectives(): Promise<import("./graph-views").PerspectivesList>;
  renderPerspectives(request: import("./graph-views").PerspectiveRequest): Promise<import("./graph-views").PerspectivesSideBySide>;
  resolveScope(request: import("./graph-views").ScopeResolveRequest): Promise<import("./graph-views").ScopeResolution>;
  /** P28 visualization engine: the view declarations, and one evaluated around a focus node into a layout-independent view model. */
  listViewDeclarations(): Promise<import("./view-model").ViewEngineList>;
  evaluateView(request: import("./view-model").ViewModelRequest): Promise<import("./view-model").ViewModel>;
  /** P28 step 2: the effective input bindings of a device (empty = every device). */
  getInteractionBindings(device?: string): Promise<import("./view-model").InteractionBindings>;
  selectContext(req: ContextSelectRequest): Promise<ContextSet>;

  // Semantic worker
  getSemanticStatus(): Promise<SemanticStatusInfo>;
  pauseSemantic(): Promise<SemanticStatusInfo>;
  resumeSemantic(): Promise<SemanticStatusInfo>;
  wakeSemantic(): Promise<SemanticStatusInfo>;
  /** Answers a batch submission paused by the usage guard (HTTP host only). */
  confirmSemanticUsage?(approved: boolean, receiptId: string, ref: string): Promise<SemanticStatusInfo>;
  /** Requeues every failed and/or stalled item in one operation (HTTP host only). */
  requeueSemantic?(filter?: { [key: string]: unknown }): Promise<SemanticRequeueResult>;
  /** Durable user setting: explicit_requeue | automatic (HTTP host only). */
  setSemanticRetryMode?(mode: string): Promise<SemanticStatusInfo>;

  // Memory tree
  listMemory(): Promise<MemoryNode[]>;
  createMemory(node: Partial<MemoryNode>): Promise<MemoryNode>;
  updateMemory(id: string, patch: Partial<MemoryNode>): Promise<MemoryNode>;
  deleteMemory(id: string): Promise<void>;
  getMemoryContext(maxChars?: number): Promise<string>;

  // Import / export
  importFile(file: File, title: string | undefined, handlers: StreamHandlers<ImportChunk>): Unsubscribe;
  /** A host that owns the files (the Android shell) opens its own picker: a file or a folder, imported with progress. Optional. */
  pickImport?(kind: "file" | "folder", title: string | undefined, handlers: StreamHandlers<ImportChunk>): Unsubscribe;
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
