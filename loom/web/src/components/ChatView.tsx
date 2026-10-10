import { useCallback, useEffect, useId, useMemo, useRef, useState } from "react";
import { marked } from "marked";
import DOMPurify from "dompurify";
import { api } from "../api";
import type { UserProfileHost } from "../api/onboarding-host";
import { conversationViewMessages, messageCan, type ConversationView } from "../api/conversation-view";
import ConversationSourceView, { useConversationSourcePresentation, useLayeredDefault, usePresentationFeature } from "./ConversationSourceView";
import { chatSendGate, SEND_BLOCK_DEFAULT_KEY, SEND_GATE_FEATURE, sendGateTexts, type ViewFailure } from "../context/send-gate";
import { featureMessage } from "../onboarding/presentation.mjs";
import type { ChatChunk, ChatContextTrace, ChatKnowledgeContextRequest, ChatRequest, Message, ModelInfo } from "../api/types";
import "./chat-context.css";
import ContextPlanEditor from "./ContextPlanEditor";
import { buildRetrievalPlan } from "../context/retrieval-plan";
import { shouldSubmit, type ApplicationProfile } from "../profiles/runtime";
import ImportedMessageContent from "./ImportedMessageContent";
import { projectImportedMessage } from "../content/imported-message";
import ConversationBranches, { conversationBranchPath } from "./ConversationBranches";
import { parseCandidateChannels, readChatSettings, storedPlan, writeChatSettings } from "../context/chat-settings";
import GraphReplyWorkbench from "./GraphReplyWorkbench";
import GraphChatSettings from "../graph/GraphChatSettings";
import { nativeFragmentPrompt, nativeGraphDisplayText, recordedGraphReply } from "../graph/chat-graph";

marked.setOptions({ breaks: true });

// Model output and imported files are untrusted: marked only turns Markdown
// into HTML, it does not sanitize it (raw HTML in the source passes right
// through). DOMPurify strips anything dangerous — script tags, event
// handlers like onerror=, javascript: URLs — before it ever reaches
// dangerouslySetInnerHTML. The afterSanitizeAttributes hook then makes every
// surviving link open safely in a new tab.
DOMPurify.addHook("afterSanitizeAttributes", (node) => {
  if (node.tagName === "A" && node.hasAttribute("href")) {
    node.setAttribute("target", "_blank");
    node.setAttribute("rel", "noopener noreferrer");
  }
});

function renderMarkdown(text: string): { __html: string } {
  let html: string;
  try {
    html = marked.parse(text, { async: false }) as string;
  } catch {
    html = text.replace(/</g, "&lt;");
  }
  const clean = DOMPurify.sanitize(html, {
    ADD_ATTR: ["target", "rel"],
    FORBID_TAGS: ["style"],
  });
  return { __html: clean };
}

interface Props {
  profileHost?: UserProfileHost;
  settingsKey?: string;
  convId: string | null;
  onConversationCreated: (id: string) => void;
  profile?: ApplicationProfile;
  runProfileOperation?: (operation: string, payload?: unknown) => Promise<unknown>;
  availableOperations?: string[];
  refreshKey?: number;
  onMessagesChanged?: () => void;
}

interface StreamState {
  reasoning: string;
  text: string;
  requestId: string | null;
  convId: string | null;
}

function recordedContext(value: unknown): ChatContextTrace | null {
  if (!value || typeof value !== "object" || Array.isArray(value)) return null;
  const record = value as Record<string, unknown>;
  return record.kind === "compiled_messages" && Array.isArray(record.messages)
    ? record as ChatContextTrace : null;
}

function ContextTrace({ trace }: { trace: ChatContextTrace }) {
  return (
    <details className="chat-context-trace" data-testid="context-trace">
      <summary>Recorded context · {trace.messages.length} assembled messages</summary>
      <p>Exact assembled messages and knowledge selection recorded for this turn. Provider settings and delivery status are separate.</p>
      <pre data-testid="context-trace-json">{JSON.stringify(trace, null, 2)}</pre>
    </details>
  );
}

export default function ChatView({ profileHost, settingsKey = "default", convId, onConversationCreated, profile, runProfileOperation, availableOperations, refreshKey, onMessagesChanged }: Props) {
  const [stored] = useState(() => readChatSettings(settingsKey));
  const storedString = (key: string, fallback: string) => typeof stored.value[key] === "string" ? stored.value[key] as string : fallback;
  const storedBool = (key: string, fallback: boolean) => typeof stored.value[key] === "boolean" ? stored.value[key] as boolean : fallback;
  const [settingsError, setSettingsError] = useState(stored.error ?? "");
  const [branchLeaf, setBranchLeaf] = useState<string | null>(null);
  const [showBranches, setShowBranches] = useState(false);
  const [branchMessages, setBranchMessages] = useState<Message[]>([]);
  const [conversationView, setConversationView] = useState<ConversationView | null>(null);
  const metadataViewRef = useRef<ConversationView | null>(null);
  const [sourceBusy, setSourceBusy] = useState(false);
  const sourceCatalog = useConversationSourcePresentation(profileHost);
  const sendCatalog = usePresentationFeature(profileHost, SEND_GATE_FEATURE);
  const blockPreset = useLayeredDefault(profileHost, SEND_BLOCK_DEFAULT_KEY);
  // An explicit choice in this view wins; otherwise only an effective layered `true`
  // blocks. A missing, disabled or excluded default has no initiator and never blocks (R43).
  const [blockChoice, setBlockChoice] = useState<boolean | undefined>(() =>
    typeof stored.value.blockSendWithoutSourceHistory === "boolean" ? stored.value.blockSendWithoutSourceHistory : undefined);
  const blockWithoutSourceHistory = blockChoice ?? (blockPreset.status === "effective" && blockPreset.value === true);
  const [viewFailure, setViewFailure] = useState<ViewFailure | null>(null);
  const gate = useMemo(() => chatSendGate({ conversationId: convId, readsView: Boolean(api.readConversationView), view: conversationView,
    failure: viewFailure, sourceReadInFlight: sourceBusy, blockWithoutSourceHistory }),
  [convId, conversationView, viewFailure, sourceBusy, blockWithoutSourceHistory]);
  const gateTexts = useMemo(() => sendGateTexts(sendCatalog.feature, gate), [sendCatalog.feature, gate]);
  const gateDescriptionId = useId();
  const [channels, setChannels] = useState(() => storedString("channels", "[]"));
  const [scanLimit, setScanLimit] = useState(() => storedString("scanLimit", "10000"));
  const [counterEvidence, setCounterEvidence] = useState(() => storedBool("counterEvidence", false));
  const [lexicalShadow, setLexicalShadow] = useState(() => storedBool("lexicalShadow", false));
  const [requestOverride, setRequestOverride] = useState<string | null>(null);
  const [messages, setMessages] = useState<Message[]>([]);
  const [showAllMessages, setShowAllMessages] = useState(false);
  const [input, setInput] = useState("");
  const [models, setModels] = useState<ModelInfo[]>([]);
  const [model, setModel] = useState<string>(() => storedString("model", ""));
  const [pendingUserText, setPendingUserText] = useState<{ text: string; convId: string | null } | null>(null);
  const [stream, setStream] = useState<StreamState | null>(null);
  const [editingId, setEditingId] = useState<string | null>(null);
  const [editText, setEditText] = useState("");
  const [versionsById, setVersionsById] = useState<Record<string, Message[]>>({});
  const [error, setError] = useState<string | null>(null);
  const [useKnowledge, setUseKnowledge] = useState(() => storedBool("useKnowledge", false));
  const [contextQuery, setContextQuery] = useState(() => storedString("contextQuery", ""));
  const [contextProject, setContextProject] = useState(() => storedString("contextProject", ""));
  const [contextTargets, setContextTargets] = useState(() => storedString("contextTargets", ""));
  const [contextRun, setContextRun] = useState(() => storedString("contextRun", ""));
  const [contextLanguage, setContextLanguage] = useState(() => storedString("contextLanguage", ""));
  const [contextBudget, setContextBudget] = useState(() => storedString("contextBudget", "4000"));
  const [contextHops, setContextHops] = useState(() => storedString("contextHops", "1"));
  const [contextDetail, setContextDetail] = useState<NonNullable<ChatKnowledgeContextRequest["detail_resolution"]> | "auto">(() => {
    const value = storedString("contextDetail", "auto");
    return ["auto", "label", "summary", "full", "raw"].includes(value) ? value as NonNullable<ChatKnowledgeContextRequest["detail_resolution"]> | "auto" : "auto";
  });
  const [usePlan, setUsePlan] = useState(() => storedBool("usePlan", false));
  const [planDraft, setPlanDraft] = useState(() => storedPlan(stored.value.planDraft));
  const [includeMemory, setIncludeMemory] = useState(() => storedBool("includeMemory", true));
  const [includeGraphMemory, setIncludeGraphMemory] = useState(() => storedBool("includeGraphMemory", true));
  const [includeHistory, setIncludeHistory] = useState(() => storedBool("includeHistory", true));
  const [traceContext, setTraceContext] = useState<"auto" | "on" | "off">(() => {
    const value = storedString("traceContext", "auto");
    return ["auto", "on", "off"].includes(value) ? value as "auto" | "on" | "off" : "auto";
  });
  const [lastTrace, setLastTrace] = useState<{ convId: string; userId: string | null; trace: ChatContextTrace } | null>(null);
  const scrollRef = useRef<HTMLDivElement>(null);
  const unsubRef = useRef<(() => void) | null>(null);
  const currentUserIdRef = useRef<string | null>(null);
  const currentConvIdRef = useRef<string | null>(null);
  const selectedConvRef = useRef(convId);
  selectedConvRef.current = convId;
  const mountedRef = useRef(true);
  const sendEpochRef = useRef(0);
  const fetchEpochRef = useRef(0);
  const available = useMemo(() => availableOperations ? new Set(availableOperations) : null, [availableOperations]);
  const supports = useCallback((operation: string) => !available || available.has(operation), [available]);

  useEffect(() => {
    if (stored.error) return;
    try {
      writeChatSettings(settingsKey, { model, useKnowledge, contextQuery, contextProject, contextTargets,
        contextRun, contextLanguage, contextBudget, contextHops, contextDetail, includeMemory, includeGraphMemory,
        includeHistory, traceContext, channels, scanLimit, counterEvidence, lexicalShadow, usePlan, planDraft,
        blockSendWithoutSourceHistory: blockChoice });
      setSettingsError("");
    } catch (err) { setSettingsError(`Cannot save view settings: ${err instanceof Error ? err.message : String(err)}`); }
  }, [settingsKey, stored.error, model, useKnowledge, contextQuery, contextProject, contextTargets, contextRun,
    contextLanguage, contextBudget, contextHops, contextDetail, includeMemory, includeGraphMemory, includeHistory,
    traceContext, channels, scanLimit, counterEvidence, lexicalShadow, usePlan, planDraft, blockChoice]);

  useEffect(() => { setBranchLeaf(null); }, [convId, refreshKey]);

  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
      sendEpochRef.current++;
      fetchEpochRef.current++;
      unsubRef.current?.();
    };
  }, []);

  useEffect(() => {
    api
      .getModels()
      .then((list) => setModels(list))
      .catch(() => setModels([]));
  }, []);

  const refreshMessages = useCallback(async (id: string) => {
    if (!mountedRef.current || selectedConvRef.current !== id) return;
    const epoch = ++fetchEpochRef.current;
    // This request is now the one in flight; an earlier failure no longer describes it.
    setViewFailure(current => current?.conversationId === id ? null : current);
    try {
      const view = api.readConversationView ? await api.readConversationView(id, { sourceAccess: "metadata" }) : null;
      const list = view ? view.messages : await api.getMessages(id, true);
      if (mountedRef.current && selectedConvRef.current === id && epoch === fetchEpochRef.current) {
        metadataViewRef.current = view;
        setConversationView(view); setMessages(list); setBranchMessages(list); setSourceBusy(false);
      }
    } catch (err) {
      if (mountedRef.current && selectedConvRef.current === id && epoch === fetchEpochRef.current) {
        const reason = err instanceof Error ? err.message : String(err);
        // A failed view degrades sending to stored messages; it does not stop it (R43).
        if (api.readConversationView) setViewFailure({ conversationId: id, reason,
          lastKnown: metadataViewRef.current?.conversation_id === id ? metadataViewRef.current : null });
        // Do not continue showing a successful transient read after a failed refresh.
        setMessages(current => current.filter(row => row.storage !== "reference"));
        setBranchMessages(current => current.filter(row => row.storage !== "reference"));
        setConversationView(null); setSourceBusy(false);
        setError(reason);
      }
    }
  }, []);

  const readSources = useCallback(async (options: Record<string, unknown>) => {
    if (!convId || !api.readConversationView || sourceBusy || !sourceCatalog.feature) return;
    const id = convId, epoch = ++fetchEpochRef.current;
    setSourceBusy(true); setError(null);
    // A new read invalidates the preceding transient display before dispatch.
    const metadata = metadataViewRef.current?.conversation_id === id ? metadataViewRef.current : null;
    setConversationView(metadata); setMessages(metadata?.messages ?? []); setBranchMessages(metadata?.messages ?? []);
    setBranchLeaf(null); setEditingId(null); setVersionsById({});
    try {
      const view = await api.readConversationView(id, { sourceAccess: "local_read", readOptions: options });
      if (mountedRef.current && selectedConvRef.current === id && epoch === fetchEpochRef.current) {
        setConversationView(view); setMessages(view.messages); setBranchMessages(view.messages);
      }
    } catch (cause) {
      if (mountedRef.current && selectedConvRef.current === id && epoch === fetchEpochRef.current)
        setError(cause instanceof Error ? cause.message : String(cause));
    } finally {
      if (mountedRef.current && selectedConvRef.current === id && epoch === fetchEpochRef.current) setSourceBusy(false);
    }
  }, [convId, sourceBusy, sourceCatalog.feature]);

  useEffect(() => {
    setMessages([]); setBranchMessages([]); setConversationView(null); metadataViewRef.current = null; setSourceBusy(false);
    setViewFailure(null);
    setEditingId(null);
    setVersionsById({});
    setError(null);
    fetchEpochRef.current++;
  }, [convId]);

  useEffect(() => {
    if (convId) refreshMessages(convId);
    else setMessages([]);
  }, [convId, refreshMessages, refreshKey]);

  useEffect(() => {
    scrollRef.current?.scrollTo({ top: scrollRef.current.scrollHeight, behavior: "smooth" });
  }, [messages, stream, pendingUserText]);

  const send = useCallback((mode: "send" | "preview" = "send") => {
    const text = input.trim();
    // Only a user choice or a transient integrity condition refuses; the reason is shown (R43).
    if (gate.blocked) { setError(gateTexts.blocked); return; }
    if (branchLeaf) { setError("Return to current conversation before sending from a branch projection."); return; }
    if (stream || !supports("chat.send")) return;
    if (!text && !(mode === "send" && requestOverride !== null)) return;
    let request: ChatRequest;
    if (mode === "send" && requestOverride !== null) {
      try {
        const parsed: unknown = JSON.parse(requestOverride);
        if (!parsed || typeof parsed !== "object" || Array.isArray(parsed) ||
            typeof (parsed as Record<string, unknown>).message !== "string" || !(parsed as Record<string, unknown>).message) {
          throw new Error("One-call request must be an object with a nonempty message string.");
        }
        request = parsed as ChatRequest;
      } catch (err) { setError(err instanceof Error ? err.message : String(err)); return; }
    } else {
    const budget = Number(contextBudget);
    if (useKnowledge && (!Number.isSafeInteger(budget) || budget < 1 || budget > 2147483647)) {
      setError("Knowledge token budget must be a positive whole number within the supported integer range.");
      return;
    }
    const hops = Number(contextHops);
    if (useKnowledge && (!contextHops.trim() || !Number.isSafeInteger(hops) || hops < 0 || hops > 2147483647)) {
      setError("Graph reach must be a non-negative whole number within the supported integer range.");
      return;
    }
    let candidateChannels: ChatKnowledgeContextRequest["candidate_channels"];
    const candidateScan = Number(scanLimit);
    if (useKnowledge) {
      try { candidateChannels = parseCandidateChannels(channels); }
      catch (err) { setError(err instanceof Error ? err.message : String(err)); return; }
      if (!scanLimit.trim() || !Number.isSafeInteger(candidateScan) || candidateScan < 1 || candidateScan > 2147483647) {
        setError("Candidate scan limit must be a positive native integer."); return;
      }
    }
    let plan: ChatKnowledgeContextRequest["plan"];
    if (useKnowledge && usePlan) {
      try { plan = buildRetrievalPlan(planDraft); }
      catch (err) { setError(err instanceof Error ? err.message : String(err)); return; }
    }
    request = {
      message: text, conv_id: convId ?? undefined, model: model || undefined,
      // Omit unchanged options so the existing default request stays intact.
      ...(!includeMemory && { include_memory: false }),
      ...(!includeGraphMemory && { include_graph_memory: false }),
      ...(!includeHistory && { include_history: false }),
      ...(traceContext !== "auto" && { trace_context: traceContext === "on" }),
      ...(useKnowledge && { knowledge_context: {
        text: contextQuery.trim() || undefined,
        project: contextProject.trim() || undefined,
        targets: contextTargets.split(/[\s,]+/).filter(Boolean),
        budget_tokens: budget,
        run: contextRun.trim() || undefined,
        lang: contextLanguage.trim() || undefined,
        ...(hops !== 1 && { relation_hops: hops }),
        ...(contextDetail !== "auto" && { detail_resolution: contextDetail }),
        ...(plan && { plan }),
        ...(candidateChannels?.length && { candidate_channels: candidateChannels }),
        ...(candidateScan !== 10000 && { candidate_scan_limit: candidateScan }),
        ...(counterEvidence && { include_counter_evidence: true }),
        ...(lexicalShadow && { lexical_shadow: true }),
      } }),
    };
    if (mode === "preview") { setRequestOverride(JSON.stringify(request, null, 2)); setError(null); return; }
    }
    const epoch = ++sendEpochRef.current;
    const isCurrent = () => mountedRef.current && epoch === sendEpochRef.current;
    let terminal = false;
    let requestConvId = request.conv_id ?? null;
    const refreshShared = () => {
      if (requestConvId) void refreshMessages(requestConvId);
      onMessagesChanged?.();
    };
    const finishError = (message: string | null) => {
      if (!isCurrent() || terminal) return;
      terminal = true;
      setStream(null);
      setPendingUserText(null);
      if (message && selectedConvRef.current === requestConvId) setError(message);
      refreshShared();
    };
    setInput("");
    setRequestOverride(null);
    currentUserIdRef.current = null;
    currentConvIdRef.current = requestConvId;
    setPendingUserText({ text: request.message, convId: requestConvId });
    setStream({ reasoning: "", text: "", requestId: null, convId: requestConvId });
    setError(null);

    const handlers = {
        onChunk: (chunk: ChatChunk) => {
          if (!isCurrent() || terminal) return;
          if (chunk.type === "start") {
            currentUserIdRef.current = chunk.user_message_id;
            currentConvIdRef.current = chunk.conv_id;
            requestConvId = chunk.conv_id;
            setStream((s) => (s ? { ...s, requestId: chunk.request_id, convId: chunk.conv_id } : s));
            setPendingUserText((s) => s ? { ...s, convId: chunk.conv_id } : s);
            if (!convId && selectedConvRef.current === null) onConversationCreated(chunk.conv_id);
            onMessagesChanged?.();
          } else if (chunk.type === "reasoning") {
            setStream((s) => (s ? { ...s, reasoning: s.reasoning + chunk.text } : s));
          } else if (chunk.type === "delta") {
            setStream((s) => (s ? { ...s, text: s.text + chunk.text } : s));
          } else if (chunk.type === "done") {
            terminal = true;
            if (chunk.context_trace) setLastTrace({ convId: chunk.conv_id, userId: currentUserIdRef.current, trace: chunk.context_trace });
            setStream(null);
            setPendingUserText(null);
            requestConvId = chunk.conv_id;
            refreshShared();
          } else if (chunk.type === "error") {
            finishError(chunk.message);
          }
        },
        onError: (msg: string) => finishError(msg),
        onDone: () => finishError("Chat stream ended before a done result."),
      };
    if (runProfileOperation) {
      void runProfileOperation("chat.send", { request, handlers,
        onSubscription: (unsub: () => void) => { if (isCurrent()) unsubRef.current = unsub; else unsub(); },
      }).catch((err: unknown) => {
        finishError(err instanceof Error && err.name === "AbortError" ? null : err instanceof Error ? err.message : String(err));
      });
    } else {
      try { unsubRef.current = api.chat(request, handlers); }
      catch (err) { finishError(err instanceof Error ? err.message : String(err)); }
    }
  }, [input, stream, convId, model, onConversationCreated, refreshMessages,
    useKnowledge, contextQuery, contextProject, contextTargets, contextRun,
    contextLanguage, contextBudget, contextHops, contextDetail, usePlan, planDraft, includeMemory, includeGraphMemory, includeHistory, traceContext, runProfileOperation, onMessagesChanged, supports, channels, scanLimit, counterEvidence, lexicalShadow, branchLeaf, requestOverride, gate.blocked, gateTexts.blocked]);

  const cancelStreaming = useCallback(() => {
    const epoch = ++sendEpochRef.current;
    const requestId = stream?.requestId;
    const cancelledConv = stream?.convId ?? currentConvIdRef.current;
    const unsubscribe = unsubRef.current;
    unsubRef.current = null;
    // Abort locally before awaiting the cancellation endpoint. Old callbacks
    // cannot clear a later Send while that endpoint is still completing.
    unsubscribe?.();
    const cancel = runProfileOperation && supports("chat.cancel")
      ? runProfileOperation("chat.cancel", { requestId: requestId ?? undefined, unsubscribe })
      : requestId ? api.cancelChat(requestId) : Promise.resolve();
    void cancel.catch(err => {
      if (mountedRef.current && sendEpochRef.current === epoch && selectedConvRef.current === cancelledConv) {
        setError(err instanceof Error ? err.message : String(err));
      }
    }).finally(() => {
      if (!mountedRef.current) return;
      if (cancelledConv) void refreshMessages(cancelledConv);
      onMessagesChanged?.();
    });
    setStream(null);
    setPendingUserText(null);
  }, [stream, runProfileOperation, supports, refreshMessages, onMessagesChanged]);

  const startEdit = useCallback((m: Message) => {
    if (!supports("message.edit") || !messageCan(m, "edit")) return;
    setEditingId(m.id);
    setEditText(m.text);
  }, [supports]);

  const saveEdit = useCallback(
    async (id: string) => {
      if (!supports("message.edit") || !messageCan(messages.find(row => row.id === id), "edit")) return;
      try {
        if (runProfileOperation) await runProfileOperation("message.edit", { id, text: editText });
        else await api.editMessage(id, editText);
        if (mountedRef.current && selectedConvRef.current === convId) setEditingId(current => current === id ? null : current);
        if (convId) await refreshMessages(convId);
        onMessagesChanged?.();
      } catch (err) {
        if (mountedRef.current && selectedConvRef.current === convId) setError(err instanceof Error ? err.message : String(err));
      }
    },
    [editText, convId, refreshMessages, runProfileOperation, onMessagesChanged, supports, messages],
  );

  const toggleExclude = useCallback(
    async (m: Message) => {
      if (!supports("message.exclude") || !messageCan(m, "set_status")) return;
      try {
        const status = m.status === "excluded" ? "active" : "excluded";
        if (runProfileOperation) await runProfileOperation("message.exclude", { id: m.id, status });
        else await api.setMessageStatus(m.id, status);
        if (convId) await refreshMessages(convId);
        onMessagesChanged?.();
      } catch (err) {
        if (mountedRef.current && selectedConvRef.current === convId) setError(err instanceof Error ? err.message : String(err));
      }
    },
    [convId, refreshMessages, runProfileOperation, onMessagesChanged, supports],
  );

  const loadVersions = useCallback(
    async (m: Message) => {
      if (!m.version_group_id) return;
      if (m.storage === "reference") {
        setVersionsById(prev => ({ ...prev, [m.version_group_id!]: messages.filter(row => row.storage === "reference" && row.version_group_id === m.version_group_id) }));
        return;
      }
      if (!messageCan(m, "native_lookup")) return;
      try {
        const vs = await api.getVersions(m.version_group_id);
        if (mountedRef.current && selectedConvRef.current === m.conv_id) setVersionsById((prev) => ({ ...prev, [m.version_group_id as string]: vs }));
      } catch (err) {
        if (mountedRef.current && selectedConvRef.current === m.conv_id) setError(err instanceof Error ? err.message : String(err));
      }
    },
    [messages],
  );

  const switchVersion = useCallback(
    async (targetId: string) => {
      const target = messages.find(row => row.id === targetId) ?? Object.values(versionsById).flat().find(row => row.id === targetId);
      if (target?.storage === "reference") { setBranchLeaf(targetId); setShowBranches(true); return; }
      if (!supports("message.restore") || !messageCan(target, "restore")) return;
      try {
        if (runProfileOperation) await runProfileOperation("message.restore", { id: targetId });
        else await api.restoreVersion(targetId);
        if (convId) await refreshMessages(convId);
        onMessagesChanged?.();
      } catch (err) {
        if (mountedRef.current && selectedConvRef.current === convId) setError(err instanceof Error ? err.message : String(err));
      }
    },
    [convId, refreshMessages, runProfileOperation, onMessagesChanged, supports, messages, versionsById],
  );

  const visibleMessages = useMemo(() => branchLeaf
    ? conversationBranchPath(branchMessages, branchLeaf).messages
    : (conversationView ? conversationViewMessages(conversationView, showAllMessages) : messages.filter(row => showAllMessages || row.status === "active"))
      .filter(message => message.conv_id === convId), [messages, convId, branchLeaf, branchMessages, conversationView, showAllMessages]);
  const displayedStream = stream?.convId === convId ? stream : null;
  const displayedPending = pendingUserText?.convId === convId ? pendingUserText : null;

  return (
    <div className="chat-view" data-testid="chat-view">
      <label className="profile-workspace-controls"><input type="checkbox" checked={showAllMessages} onChange={e => setShowAllMessages(e.target.checked)} data-testid="show-all-messages" />Show excluded messages and saved versions</label>
      <label className="profile-workspace-controls"><input type="checkbox" checked={showBranches} onChange={e => { setShowBranches(e.target.checked); setBranchLeaf(null); }} data-testid="show-conversation-branches" />Browse retained descendant branches</label>
      {showBranches && <ConversationBranches messages={branchMessages} selectedId={branchLeaf} onSelect={message => setBranchLeaf(message.id)} />}
      {branchLeaf && <p role="status">Ancestor path projection <button onClick={() => setBranchLeaf(null)} data-testid="return-current-branch">Return to current conversation</button></p>}
      {settingsError && <p role="alert">{settingsError}</p>}
      {convId && !api.readConversationView && sourceCatalog.feature && <p role="status" data-testid="conversation-view-host-unavailable">
        {featureMessage(sourceCatalog.feature, "host_unavailable")}
      </p>}
      {conversationView && conversationView.resources.length > 0 && <ConversationSourceView key={convId} view={conversationView}
        profileHost={profileHost} catalog={sourceCatalog} busy={sourceBusy} onRead={readSources} onReload={() => { if (convId) void refreshMessages(convId); }} />}
      <div className="chat-scroll" ref={scrollRef}>
        {visibleMessages.length === 0 && !displayedPending && !displayedStream && !gate.viewLoading && gate.viewFailure === null && !conversationView?.resources.length && (
          <div className="empty-state">Say something to start the conversation.</div>
        )}
        {visibleMessages.map((m) => {
          const versions = m.version_group_id ? versionsById[m.version_group_id] : undefined;
          const trace = recordedContext(m.metadata?.context_trace);
          const graphReply = recordedGraphReply(m.metadata);
          return (
            <div key={m.id} className={`msg ${m.role}`} data-testid="message" data-role={m.role} data-status={m.status} data-storage={m.storage ?? "native"}>
              <div className="meta">
                <span>{m.role}</span>
                {m.model && <span>· {m.model}</span>}
                {m.status !== "active" && <span style={{ color: "var(--warn)" }}>{m.status}</span>}
                {m.version_group_id && (
                  <span className="version-switcher">
                    <button onClick={() => loadVersions(m)} data-testid="load-versions">
                      v{m.version_num ?? 1}
                    </button>
                    {versions && versions.length > 1 && (
                      <>
                        {versions.map((v) => (
                          <button
                            key={v.id}
                            disabled={v.id === m.id || (v.storage !== "reference" && (!supports("message.restore") || !messageCan(v, "restore")))}
                            onClick={() => switchVersion(v.id)}
                            data-testid="switch-version"
                          >
                            {v.version_num}
                          </button>
                        ))}
                      </>
                    )}
                  </span>
                )}
              </div>
              {editingId === m.id ? (
                <div>
                  <textarea value={editText} onChange={(e) => setEditText(e.target.value)} rows={3} />
                  <div className="actions">
                    <button className="primary" onClick={() => saveEdit(m.id)} data-testid="save-edit" disabled={!supports("message.edit") || !messageCan(m, "edit")}>
                      Save (new version)
                    </button>
                    <button onClick={() => setEditingId(null)}>Cancel</button>
                  </div>
                </div>
              ) : projectImportedMessage(m) ? (
                <ImportedMessageContent message={m} opaqueSourceEvidence={m.storage === "reference" ? {
                  description: sourceCatalog.feature ? featureMessage(sourceCatalog.feature, "decoded_projection") : "source_wire_evidence_required",
                } : undefined} />
              ) : m.storage === "reference" ? (
                <pre className="body">{m.text}</pre>
              ) : (
                <div className="body" dangerouslySetInnerHTML={renderMarkdown(m.role === "assistant" ? nativeGraphDisplayText(graphReply, m.text) : m.text)} />
              )}
              <div className="actions">
                {m.role === "user" && editingId !== m.id && (
                  <button onClick={() => startEdit(m)} data-testid="edit-message" disabled={!supports("message.edit") || !messageCan(m, "edit")}>
                    Edit
                  </button>
                )}
                <button onClick={() => toggleExclude(m)} data-testid="toggle-exclude" disabled={!supports("message.exclude") || !messageCan(m, "set_status") || m.status === "version" || m.status === "deleted"}>
                  {m.status === "excluded" ? "Restore" : "Exclude"}
                </button>
              </div>
              {m.storage === "reference" && sourceCatalog.feature && <details data-testid="source-message-reference">
                <summary>{featureMessage(sourceCatalog.feature, "source_message")}</summary>
                <p>{featureMessage(sourceCatalog.feature, "readonly")}</p>
                <pre>{JSON.stringify({ source_ref: m.source_ref, capabilities: m.capabilities }, null, sourceCatalog.base?.defaults.json_indent)}</pre>
              </details>}
              {trace && <ContextTrace trace={trace} />}
              {m.storage !== "reference" && (m.role === "assistant" || graphReply) && <details className="chat-context-controls"><summary>Graph reply · inspect and address fragments</summary>
                <GraphReplyWorkbench packet={api.packet?.bind(api)} usagePolicy={api.usagePolicy?.bind(api)}
                  responseText={m.role === "assistant" ? m.text : typeof graphReply?.retained_text === "string" ? graphReply.retained_text : ""} recordedReply={graphReply} graphReply={api.graphReply?.bind(api)}
                  onNativeAddressFragment={(action, fragment) => { setInput(nativeFragmentPrompt(action, fragment)); setRequestOverride(null); }}
                  conversationId={m.conv_id} turnId={m.id} requestId={String(m.metadata?.request_id ?? m.id)} model={m.model ?? ""}
                  onAddressFragment={(action, fragment) => { setInput(`${action === "expand" ? "Expand" : "Correct"} the addressed model fragment:\n${JSON.stringify(fragment, null, 2)}`); setRequestOverride(null); }} />
              </details>}
            </div>
          );
        })}
        {lastTrace && lastTrace.convId === convId && !visibleMessages.some((m) =>
          m.id === lastTrace.userId && recordedContext(m.metadata?.context_trace)) && (
          <ContextTrace trace={lastTrace.trace} />
        )}
        {displayedPending && !visibleMessages.some(message => message.id === currentUserIdRef.current) && (
          <div className="msg user">
            <div className="meta">user</div>
            <div className="body">{displayedPending.text}</div>
          </div>
        )}
        {displayedStream && (
          <div className="msg assistant" data-testid="streaming-message">
            {displayedStream.reasoning && (
              <details className="reasoning" open>
                <summary>Reasoning</summary>
                <div className="reasoning-text">{displayedStream.reasoning}</div>
              </details>
            )}
            <div className="meta">assistant · streaming…</div>
            <div className="body" dangerouslySetInnerHTML={renderMarkdown(displayedStream.text)} />
          </div>
        )}
      </div>

      {stream && !displayedStream && <p className="empty-state" role="status">A request continues in another conversation. Stop remains available.</p>}

      {error && (
        <div className="empty-state" style={{ color: "var(--err)" }} data-testid="chat-error">
          {error}
        </div>
      )}

      <GraphChatSettings transport={api} disabled={!!stream} />
      <details className="chat-context-controls" data-testid="chat-context-controls">
        <summary>Context for next message{useKnowledge ? " · knowledge enabled" : ""}</summary>
        <fieldset disabled={!!stream}>
          <legend>Sources included in the request</legend>
          <div className="chat-context-toggles">
            <label><input type="checkbox" checked={includeMemory} onChange={(e) => setIncludeMemory(e.target.checked)} data-testid="include-memory" />Active memory</label>
            <label><input type="checkbox" checked={includeGraphMemory} onChange={(e) => setIncludeGraphMemory(e.target.checked)} data-testid="include-graph-memory" />Legacy graph memory</label>
            <label><input type="checkbox" checked={includeHistory} onChange={(e) => setIncludeHistory(e.target.checked)} data-testid="include-history" />Conversation history</label>
            <label><input type="checkbox" checked={useKnowledge} onChange={(e) => setUseKnowledge(e.target.checked)} data-testid="use-knowledge-context" />Knowledge selection</label>
          </div>
          <p>These choices affect the next request. Saved messages and memory remain available.</p>
          <label><input type="checkbox" checked={blockWithoutSourceHistory} onChange={(e) => setBlockChoice(e.target.checked)}
            data-testid="block-send-without-source-history" />{gateTexts.setting ?? blockPreset.label ?? SEND_BLOCK_DEFAULT_KEY}</label>
          {gateTexts.settingHelp !== null && <p>{gateTexts.settingHelp}</p>}
          {useKnowledge && (
            <div className="chat-context-fields">
              <label>Selection query<input type="text" value={contextQuery} onChange={(e) => setContextQuery(e.target.value)} placeholder="Use the message being sent" data-testid="context-query" /></label>
              <label>Project ID<input type="text" value={contextProject} onChange={(e) => setContextProject(e.target.value)} placeholder="Optional project entity" data-testid="context-project" /></label>
              <label>Target entity IDs<input type="text" value={contextTargets} onChange={(e) => setContextTargets(e.target.value)} placeholder="Separate IDs with commas or spaces" data-testid="context-targets" /></label>
              <label>Knowledge token budget<input type="number" min="1" max="2147483647" step="1" value={contextBudget} onChange={(e) => setContextBudget(e.target.value)} data-testid="context-budget" /></label>
              <label>Graph reach (relationship hops)<input type="number" min="0" max="2147483647" step="1" value={contextHops} onChange={(e) => setContextHops(e.target.value)} data-testid="context-hops" /></label>
              <label>Item detail<select value={contextDetail} onChange={(e) => setContextDetail(e.target.value as typeof contextDetail)} data-testid="context-detail">
                <option value="auto">Use goal settings</option>
                <option value="label">Label</option>
                <option value="summary">Summary</option>
                <option value="full">Full</option>
                <option value="raw">Raw</option>
              </select></label>
              <label>Knowledge run ID<input type="text" value={contextRun} onChange={(e) => setContextRun(e.target.value)} placeholder="Latest completed run" data-testid="context-run" /></label>
              <label>Rendering language<input type="text" value={contextLanguage} onChange={(e) => setContextLanguage(e.target.value)} placeholder="Use message language" data-testid="context-language" /></label>
            </div>
          )}
          {useKnowledge && <div className="chat-context-fields">
            <label>Candidate channels JSON<textarea data-testid="context-channels" value={channels} onChange={e => setChannels(e.target.value)} placeholder='[{"id":"tfidf","limit":50,"min_score":0}]' /></label>
            <label>Candidate scan limit<input type="number" value={scanLimit} onChange={e => setScanLimit(e.target.value)} data-testid="context-scan-limit" /></label>
            <label><input type="checkbox" checked={counterEvidence} onChange={e => setCounterEvidence(e.target.checked)} data-testid="context-counter-evidence" />Follow recorded counter-evidence links</label>
            <label><input type="checkbox" checked={lexicalShadow} onChange={e => setLexicalShadow(e.target.checked)} data-testid="context-lexical-shadow" />Lexical shadow diagnostic</label>
            <p>Channel hits combine before selection. Local TF-IDF and lexical instruments work offline; other IDs report installed capability in the trace. Method weights require the method registry API.</p>
          </div>}
          {useKnowledge && <ContextPlanEditor enabled={usePlan} onEnabled={setUsePlan} draft={planDraft} onChange={setPlanDraft} />}
          <label className="chat-context-record">Context recording
            <select value={traceContext} onChange={(e) => setTraceContext(e.target.value as "auto" | "on" | "off")} data-testid="record-context">
              <option value="auto">Auto · record custom context</option>
              <option value="on">On · record every request</option>
              <option value="off">Off</option>
            </select>
          </label>
          <p>Recording saves the exact assembled messages and selection with the turn for later inspection.</p>
        </fieldset>
      </details>

      <details className="chat-context-controls" data-testid="expert-chat-request"><summary>Expert · inspect or edit one request</summary>
        <p>This is the client request sent to Loom. Compiled provider messages are inspected in the recorded context after execution.</p>
        <button onClick={() => send("preview")} disabled={!!stream || !!branchLeaf || Boolean(gate.blocked) || !input.trim()} data-testid="preview-chat-request"
          title={gateTexts.blocked ?? undefined} aria-describedby={gate.blocked ? gateDescriptionId : undefined}>Prepare client request</button>
        {requestOverride !== null && <>
          <textarea aria-label="One-call client request JSON" data-testid="one-call-request" rows={9} value={requestOverride} onChange={event => setRequestOverride(event.target.value)} disabled={!!stream} />
          <p>The next Send uses this exact JSON once. Profile, saved context and later calls keep their settings.</p>
          <button onClick={() => setRequestOverride(null)} data-testid="discard-one-call-request">Discard override</button>
        </>}
      </details>

      {gateTexts.viewFailure !== null && <p className="send-gate-notice" role="status" data-testid="send-notice-view-unavailable"
        data-reason={gate.viewFailure ?? undefined}>{gateTexts.viewFailure}</p>}
      {gateTexts.sourceHistory !== null && <div className="send-gate-notice" role="status" data-testid="send-notice-source-history"
        data-reason={gate.sourceHistory ?? undefined} data-initiator={gate.blocked?.initiator === "user" ? "user" : undefined}>
        <p>{gate.blocked?.initiator === "user" ? gateTexts.blocked : gateTexts.sourceHistory}</p>
        {gateTexts.sourceReason !== null && <p data-testid="send-notice-reason">{gateTexts.sourceReason}</p>}
        {gate.blocked?.initiator === "user" && gateTexts.unblock !== null && <button type="button" disabled={!!stream}
          onClick={() => setBlockChoice(false)} data-testid="send-gate-unblock">{gateTexts.unblock}</button>}
      </div>}
      {gate.blocked && <span id={gateDescriptionId} hidden data-testid="send-gate-explanation">{gateTexts.blocked}</span>}
      <div className="composer">
        <select
          value={model}
          onChange={(e) => setModel(e.target.value)}
          style={{ width: 150, flexShrink: 0 }}
          data-testid="model-picker"
        >
          <option value="">default model</option>
          {models.map((m) => (
            <option key={m.id} value={m.id}>
              {m.name ?? m.id}
            </option>
          ))}
        </select>
        <textarea
          value={input}
          placeholder={profile?.composer.placeholder ?? "Message Loom…"}
          rows={1}
          data-testid="chat-input"
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={(e) => {
            if (supports("chat.send") && shouldSubmit(profile?.composer.submit ?? "enter", { key: e.key, shiftKey: e.shiftKey,
              ctrlKey: e.ctrlKey, metaKey: e.metaKey, altKey: e.altKey, isComposing: e.nativeEvent.isComposing })) {
              e.preventDefault();
              send();
            }
          }}
        />
        {stream ? (
          <button className="danger" onClick={cancelStreaming} data-testid="cancel-chat">
            Stop
          </button>
        ) : (
          <button className="primary" onClick={() => send()} data-testid="send-chat" disabled={!!branchLeaf || Boolean(gate.blocked) || (requestOverride === null && !input.trim()) || !supports("chat.send")}
            title={gateTexts.blocked ?? undefined} aria-describedby={gate.blocked ? gateDescriptionId : undefined}
            data-gate-initiator={gate.blocked?.initiator} data-gate-reason={gate.blocked?.reason}>
            Send
          </button>
        )}
      </div>
    </div>
  );
}
