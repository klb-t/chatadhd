import { useCallback, useEffect, useId, useMemo, useRef, useState, useSyncExternalStore } from "react";
import { marked } from "marked";
import DOMPurify from "dompurify";
import { api } from "../api";
import { readWebPageSizes } from "../settings/page-sizes";
import type { UserProfileHost } from "../api/onboarding-host";
import { conversationPage, conversationViewMessages, messageCan, type ConversationPage, type ConversationView } from "../api/conversation-view";
import ConversationSourceView, { useConversationSourcePresentation, useLayeredDefault, usePresentationFeature } from "./ConversationSourceView";
import { chatSendGate, SEND_BLOCK_DEFAULT_KEY, SEND_GATE_FEATURE, sendGateTexts, type ViewFailure } from "../context/send-gate";
import { queueReason, queueStep, useCreationInFlight, VIEW_LOADING_REASON, type QueuedSend } from "../context/send-queue";
import UsagePendingBlock from "./UsagePendingBlock";
import AgentProposalBlock from "./AgentProposalBlock";
import { agentLoopOf, agentLoopView, resumeRequest } from "../context/agent-proposal";
import ActionProposalBlock, { ProposalOffer, ProposalWritten } from "./ActionProposalBlock";
import ClientStepBlock from "./ClientStepBlock";
import { CLIENT_STEP_FEATURE, type BlobLoader, type BlobUploader } from "../context/client-device";
import { browserHost } from "../context/browser-host";
import { clientIdentity, pageClientSession } from "../context/client-session";
import { ACTION_PROPOSAL_FEATURE, answerRequest, offerRequest, offerState, messageLanguage, offerTexts, openOffer, pendingAnswer, producedText, producedView, proposalState, routeConfirmedRequest, undoCall, undoConfirmation, undoRequest, writtenView, type OfferView, type PendingAnswer, type WrittenView } from "../context/action-proposal";
import { confirmedRequest, USAGE_PENDING_FEATURE, usageNoticeLines, type UsagePendingReceipt } from "../context/usage-pending";
import { ROUTE_ISSUE_FEATURE, routeIssueNotes, routeIssues } from "../context/route-issue";
import { OPEN_SETTINGS_FEATURE, SETTINGS_PANEL, openSettingsHint, openSettingsTexts, requestOpenPanel } from "../context/open-settings";
import { watchClientSteps } from "../context/client-step-events";
import { SEMANTIC_ANSWER_REF, SEMANTIC_QUESTION_FEATURE, semanticAnsweredText, semanticFailedText, semanticQuestion, semanticQuestionOpen, type SemanticQuestion } from "../context/semantic-question";
import { featureInLanguage, featureMessage } from "../onboarding/presentation.mjs";
import type { ChatChunk, ChatContextTrace, ChatKnowledgeContextRequest, ChatRequest, Conversation, Message, ModelInfo } from "../api/types";
import type { ClientResult } from "../api/loom-api";
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
import ScopeExplain from "./ScopeExplain";
import MicButton from "../shell/MicButton";
import Disclosure from "../shell/Disclosure";
import { needsProviderKey, useShellLanguage, useShellText } from "../shell/text";
import { sourceHistoryUnavailable } from "../shell/source-notice";
import { requestOpenPanel as requestPanel } from "../context/open-settings";
import { createPortal } from "react-dom";
import ComposerMenu from "../shell/ComposerMenu";
import ContextReceipt from "./ContextReceipt";
import { useReceiptWords } from "./use-receipt-words";
import { loadSelectorProfiles, useContextModify } from "./use-context-modify";
import { modifySentence, profileChoices, type ProfileChoice } from "./context-receipt-state";
import { InterviewCardDecision, InterviewChips, InterviewInfoLine, InterviewLevelCards, InterviewStartCard, useInterviewCard, useInterviewWords, useIsInterviewConversation } from "./InterviewCards";
import { levelCards, waitingQuestion } from "../context/interview";
import ContextStrip from "../shell/ContextStrip";
import ModelProvenance from "../shell/ModelProvenance";
import { lastReceiptProfile, pendingParts } from "../shell/context-strip";
import { shortModelName, type ModelSource } from "../shell/model-chip";
import { clearPendingTurn, pendingTurn, subscribePendingTurn } from "../shell/turn-pending";
import { lineText, recordedReceipt, requestOf } from "./context-receipt-state";
import { useLayer } from "../shell/use-layer";
import { attachmentName, attachmentRef, conversationModel, effectiveMenu, MODEL_SETTING, labelText, onSwitches, refusedAttachment, requestFields, switchToggle, withConversationModel, type MenuFlag, type MenuFlags, type MenuIssue } from "../shell/composer-menu";
import featurePack from "../onboarding/generated/conversation-view.json";
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
  /** "chat": the full-screen chat app (R56); "workshop" (default): every control as before. */
  variant?: "chat" | "workshop";
  /** M1-W1a: "Pokaż w grafie" of the sheet "Co wysłaliśmy" - the app opens the navigator on the request with the perspective. */
  onShowInGraph?: (focus: string, perspectives: string[]) => void;
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

function safeNotice(feature: Parameters<typeof featureMessage>[0], id: string): string | null {
  try { return featureMessage(feature, id, {}); } catch { return null; }
}

// Messages read per page: the setting loom_web_message_window (preset 500, 0 = no cut). Older pages are always reachable, so nothing is cut.

interface ComposerAttachment { key: string; name: string; state: "uploading" | "ready" | "failed"; ref?: string; preview?: string; reason?: string }
let attachmentCounter = 0;

export default function ChatView({ profileHost, settingsKey = "default", convId, onConversationCreated, profile, runProfileOperation, availableOperations, refreshKey, onMessagesChanged, variant = "workshop" , onShowInGraph }: Props) {
  const chat = variant === "chat";
  const t = useShellText();
  const shellLanguage = useShellLanguage();
  // M1-W1a: the sheet "Co wysłaliśmy" (message id it is open for), the words of it, and the setting receipt_link (visible = a line under every answer).
  const receiptT = useReceiptWords(profileHost, shellLanguage);
  const [receiptFor, setReceiptFor] = useState<string | null>(null);
  const [receiptLink, setReceiptLink] = useState<string>("visible");
  useEffect(() => {
    let live = true;
    api.getConfig().then(cfg => { if (live && typeof cfg.receipt_link === "string") setReceiptLink(cfg.receipt_link); }, () => undefined);
    return () => { live = false; };
  }, [refreshKey]);
  const [convRecord, setConvRecord] = useState<Conversation | null>(null);
  const [hasKey, setHasKey] = useState<boolean | null>(null);
  // M1-W2a: the interview about the person (form mode, no model): its words, the card of the empty chat, and whether this conversation is one.
  const interviewT = useInterviewWords(profileHost, shellLanguage);
  const interviewConv = useIsInterviewConversation(convId);
  const interviewCard = useInterviewCard(shellLanguage, onConversationCreated, chat);
  const [actionsFor, setActionsFor] = useState<string | null>(null);
  const [copied, setCopied] = useState<string | null>(null);
  const pressTimer = useRef<number | null>(null);
  const [stored] = useState(() => readChatSettings(settingsKey));
  const storedString = (key: string, fallback: string) => typeof stored.value[key] === "string" ? stored.value[key] as string : fallback;
  const storedBool = (key: string, fallback: boolean) => typeof stored.value[key] === "boolean" ? stored.value[key] as boolean : fallback;
  const [settingsError, setSettingsError] = useState(stored.error ?? "");
  const [branchLeaf, setBranchLeaf] = useState<string | null>(null);
  const [showBranches, setShowBranches] = useState(false);
  const [branchMessages, setBranchMessages] = useState<Message[]>([]);
  const [conversationView, setConversationView] = useState<ConversationView | null>(null);
  const metadataViewRef = useRef<ConversationView | null>(null);
  // P19 paging: the newest loom_web_message_window messages load first; older ones come page by page on scroll up (or the button) and are kept
  // in `olderMessages` until the next refresh, which re-reads at least as many as are shown.
  const [olderMessages, setOlderMessages] = useState<Message[]>([]);
  const [pageInfo, setPageInfo] = useState<ConversationPage | null>(null);
  const [loadingOlder, setLoadingOlder] = useState(false);
  const loadedCountRef = useRef(0);
  const loadingOlderRef = useRef(false);
  const [sourceBusy, setSourceBusy] = useState(false);
  const sourceCatalog = useConversationSourcePresentation(profileHost);
  const sendCatalog = usePresentationFeature(profileHost, SEND_GATE_FEATURE);
  const usageCatalog = usePresentationFeature(profileHost, USAGE_PENDING_FEATURE);
  const routeIssueCatalog = usePresentationFeature(profileHost, ROUTE_ISSUE_FEATURE);
  const openSettingsCatalog = usePresentationFeature(profileHost, OPEN_SETTINGS_FEATURE);
  const proposalCatalog = usePresentationFeature(profileHost, ACTION_PROPOSAL_FEATURE);
  const clientStepCatalog = usePresentationFeature(profileHost, CLIENT_STEP_FEATURE);
  const semanticCatalog = usePresentationFeature(profileHost, SEMANTIC_QUESTION_FEATURE);
  // The question of the background analysis being answered (its receipt), and why an answer did not go through.
  const [semanticAnswering, setSemanticAnswering] = useState<string | null>(null);
  const [semanticFailure, setSemanticFailure] = useState<{ receiptId: string; reason: string } | null>(null);
  // This page as a client of the kernel (P13): the devices it can drive are declared, so the device actions are runnable for it.
  const clientHost = useMemo(() => browserHost(), []);
  const clientSession = useMemo(() => pageClientSession({
    actions: api.actions, host: clientHost, label: navigator.userAgent,
    id: clientIdentity(typeof sessionStorage === "undefined" ? undefined : sessionStorage),
  }), [clientHost]);
  // A command answered in the conversation (R54): the run being answered, and why an answer could not be sent.
  const [answering, setAnswering] = useState<string | null>(null);
  const [agentBusy, setAgentBusy] = useState<string | null>(null);
  const [agentFailure, setAgentFailure] = useState<{ messageId: string; reason: string } | null>(null);
  const [answerFailure, setAnswerFailure] = useState<{ runId: string; reason: string } | null>(null);
  // The offer to send a rejected command's sentence as a message: why it could not be taken.
  const [offerFailure, setOfferFailure] = useState<{ proposalId: string; reason: string } | null>(null);
  // A definition the user accepted can be taken back from the proposal that wrote it: the one being removed, and why it could not be.
  const [undoing, setUndoing] = useState<string | null>(null);
  const [undoFailure, setUndoFailure] = useState<{ id: string; reason: string } | null>(null);
  const [usagePending, setUsagePending] = useState<{ request: ChatRequest; receipt: UsagePendingReceipt; convId: string } | null>(null);
  const [usageNotice, setUsageNotice] = useState<string | null>(null);
  // Routing the last input had a problem it worked around (the model route failed, the local rules decided): a quiet note, nothing was lost.
  const [routeNotes, setRouteNotes] = useState<string[]>([]);
  const blockPreset = useLayeredDefault(profileHost, SEND_BLOCK_DEFAULT_KEY);
  const menuBase = useLayeredDefault(profileHost, "chat.composer_menu");
  const menuOps = useLayeredDefault(profileHost, "chat.composer_menu_ops");
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
  // A send typed while the conversation is still being made, or while its view loads, waits and is sent once when it can be; it is never
  // dropped. The text stays in the field the whole time; a conversation that does not load says so and keeps the text.
  const creating = useCreationInFlight();
  const queuedRef = useRef<QueuedSend | null>(null);
  const [queuedNote, setQueuedNote] = useState<"waiting" | "failed" | null>(null);
  const [channels, setChannels] = useState(() => storedString("channels", "[]"));
  const [scanLimit, setScanLimit] = useState(() => storedString("scanLimit", "10000"));
  const [counterEvidence, setCounterEvidence] = useState(() => storedBool("counterEvidence", false));
  const [lexicalShadow, setLexicalShadow] = useState(() => storedBool("lexicalShadow", false));
  const [requestOverride, setRequestOverride] = useState<string | null>(null);
  const [messages, setMessages] = useState<Message[]>([]);
  const lastProfile = useMemo(() => lastReceiptProfile(messages, shellLanguage), [messages, shellLanguage]);
  const [showAllMessages, setShowAllMessages] = useState(false);
  const [input, setInput] = useState("");
  // P33: the + menu. Tools are on until turned off (chips above the field say which); files are chips too.
  const [menuOpen, setMenuOpen] = useState(false);
  // M1-W1b: the context steps and the selector profile of the + menu (the same context.modify as the sheet), the model chip's sheet.
  // M1-K1c1: what the next message will be compiled with - the text in the field and the options the send passes (assigned below, once they exist).
  const pendingRef = useRef<{ text: string; options: Record<string, unknown> }>({ text: "", options: {} });
  const pendingNext = useCallback(() => pendingRef.current, []);
  const modify = useContextModify(convId ?? null, receiptT("sheet.no_actions"), pendingNext);
  const waitingTurn = useSyncExternalStore(subscribePendingTurn, () => pendingTurn(convId ?? null));
  const [profiles, setProfiles] = useState<ProfileChoice[] | null>(null);
  const [profilesError, setProfilesError] = useState<string | null>(null);
  const loadProfiles = useCallback(() => {
    if (profiles !== null) return;
    setProfilesError(null);
    loadSelectorProfiles().then(data => setProfiles(profileChoices(data, shellLanguage)), failure => setProfilesError(failure instanceof Error ? failure.message : String(failure)));
  }, [profiles, shellLanguage]);
  const [modelSheet, setModelSheet] = useState(false);
  // The field switches that are on (web search, deep research, deep thinking, an action ...), by the id of their entry in the menu's data.
  const [switches, setSwitches] = useState<Record<string, boolean>>({});
  const [attachments, setAttachments] = useState<ComposerAttachment[]>([]);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const cameraInputRef = useRef<HTMLInputElement>(null);
  const plusRef = useRef<HTMLButtonElement>(null);
  const [models, setModels] = useState<ModelInfo[]>([]);
  const [model, setModel] = useState<string>(() => storedString("model", ""));  // the person's pick (saved with the view's settings)
  // P33: the model the open conversation has of its own (scope chain, P24). Kept apart from the person's pick: shown and sent only while
  // that conversation is open, never saved as the person's choice.
  const [ownModel, setOwnModel] = useState<{ convId: string; model: string } | null>(null);
  const shownModel = chat && convId !== null && ownModel?.convId === convId ? ownModel.model : model;
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
  const errorForRef = useRef<string | null>(null);
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
    setConvRecord(null);
    if (!convId || api.readConversationView) return;
    let live = true;
    api.getConversation(convId).then(c => { if (live) setConvRecord(c); }, () => { if (live) setConvRecord(null); });
    return () => { live = false; };
  }, [convId, refreshKey]);
  // The notes of a send that made this very conversation belong to it: moving the page into it does not wipe them.
  const notesForRef = useRef<string | null>(null);
  useEffect(() => {
    if (notesForRef.current !== null && notesForRef.current === convId) return;
    notesForRef.current = null;
    setRouteNotes([]);
  }, [convId]);

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
      const windowSize = api.readConversationView ? (await readWebPageSizes()).messageWindow : 0;
      const view = api.readConversationView
        ? await api.readConversationView(id, { sourceAccess: "metadata", limit: windowSize === 0 ? 0 : Math.max(windowSize, loadedCountRef.current) }) : null;
      const list = view ? view.messages : await api.getMessages(id, true);
      if (mountedRef.current && selectedConvRef.current === id && epoch === fetchEpochRef.current) {
        metadataViewRef.current = view;
        loadedCountRef.current = list.length;
        setOlderMessages([]); setPageInfo(conversationPage(view));
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

  // A question for this device (or its closing) that comes from elsewhere - another tab, another client, the CLI - reaches the open conversation
  // as an event from the kernel: the messages are read again. One feed for the page, no polling; the question of the background analysis held by
  // the x10 guard comes on the same feed (run id "semantic:<receipt>").
  useEffect(() => {
    return watchClientSteps(api, (event) => { void refreshMessages(event.convId); }) ?? undefined;
  }, [refreshMessages]);

  const loadOlder = useCallback(async () => {
    const id = convId, cursor = pageInfo?.next_before;
    if (!id || !cursor || !api.readConversationView || loadingOlderRef.current) return;
    loadingOlderRef.current = true; setLoadingOlder(true);
    try {
      const view = await api.readConversationView(id, { sourceAccess: "metadata", limit: (await readWebPageSizes()).messageWindow, before: cursor });
      if (mountedRef.current && selectedConvRef.current === id) {
        const page = conversationPage(view);
        const el = scrollRef.current, before = el ? el.scrollHeight - el.scrollTop : 0;
        loadedCountRef.current += view.messages.length;
        setOlderMessages(current => [...view.messages, ...current]);
        setPageInfo(page);
        // Keep the reader where they were: the new rows are inserted above the viewport.
        requestAnimationFrame(() => { if (el) el.scrollTop = el.scrollHeight - before; });
      }
    } catch (cause) {
      if (mountedRef.current && selectedConvRef.current === id) setError(cause instanceof Error ? cause.message : String(cause));
    } finally {
      loadingOlderRef.current = false;
      if (mountedRef.current) setLoadingOlder(false);
    }
  }, [convId, pageInfo]);

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
    setOlderMessages([]); setPageInfo(null); loadedCountRef.current = 0;
    setViewFailure(null);
    setEditingId(null);
    setVersionsById({});
    // An error of the send that made this very conversation belongs to it: moving the page into it does not wipe what that send said.
    if (errorForRef.current !== null && errorForRef.current === convId) { /* keep */ } else { errorForRef.current = null; setError(null); }
    fetchEpochRef.current++;
  }, [convId]);

  useEffect(() => {
    if (convId) refreshMessages(convId);
    else setMessages([]);
  }, [convId, refreshMessages, refreshKey]);

  useEffect(() => {
    if (loadingOlderRef.current) return;  // paging in older rows must not jump to the bottom
    scrollRef.current?.scrollTo({ top: scrollRef.current.scrollHeight, behavior: "smooth" });
  }, [messages, stream, pendingUserText]);

  // Files chosen in the + menu go to the kernel's blob store at once (the answer names them by hash); the message then carries that reference.
  const addFiles = useCallback((files: FileList | File[]) => {
    for (const file of Array.from(files)) {
      const key = `a${++attachmentCounter}`;
      const name = file.name || "file";
      const preview = file.type.startsWith("image/") ? URL.createObjectURL(file) : undefined;
      const upload = api.actions?.clientBlobUpload;
      if (!upload) {
        setAttachments((list) => [...list, { key, name, state: "failed", preview, reason: t("cm_reason_upload") }]);
        continue;
      }
      setAttachments((list) => [...list, { key, name, state: "uploading", preview }]);
      upload.call(api.actions, file, { name }).then(
        (blob) => { if (mountedRef.current) setAttachments((list) => list.map(a => a.key === key ? { ...a, state: "ready", ref: attachmentRef(blob.hash, name) } : a)); },
        (err: unknown) => { if (mountedRef.current) setAttachments((list) => list.map(a => a.key === key ? { ...a, state: "failed", reason: err instanceof Error ? err.message : String(err) } : a)); },
      );
    }
  }, [t]);
  const removeAttachment = useCallback((key: string) => {
    setAttachments((list) => { const gone = list.find(a => a.key === key); if (gone?.preview) URL.revokeObjectURL(gone.preview); return list.filter(a => a.key !== key); });
  }, []);
  const menuFlags: MenuFlags = { memory: includeMemory, knowledge: useKnowledge };
  const toggleFlag = useCallback((flag: MenuFlag) => {
    if (flag === "memory") setIncludeMemory(v => !v);
    else setUseKnowledge(v => !v);
  }, []);
  // The menu as data: the person's own menu while its default is effective (else the shipped one, and the menu says so), their changes on top.
  const shippedMenu = useMemo(() => featurePack.entries.find(entry => entry.key === "chat.composer_menu")?.value, []);
  const composedMenu = useMemo(() => effectiveMenu(menuBase, menuOps, shippedMenu), [menuBase, menuOps, shippedMenu]);
  const toggleSwitch = useCallback((id: string) => setSwitches(on => switchToggle(composedMenu.items, on, id)), [composedMenu]);
  // Entries this screen cannot run right now keep their row and say why (a preview needs the text of a message and an idle composer; a card needs the navigator).
  const menuBlocked = useMemo<Record<string, unknown>>(() => {
    const blocked: Record<string, unknown> = {};
    if (!input.trim()) blocked.preview_request = "cm_reason_preview_empty";
    else if (stream || branchLeaf || gate.blocked) blocked.preview_request = "cm_reason_preview_busy";
    if (!onShowInGraph) blocked.flow_text_graph = "cm_reason_no_navigator";
    return blocked;
  }, [input, stream, branchLeaf, gate.blocked, onShowInGraph]);
  const switchesOn = useMemo(() => onSwitches(composedMenu.items, switches), [composedMenu, switches]);
  // An action (the agent, painting) takes only the words: files staged with it are said next to Send, not dropped (the kernel refuses them too).
  const actionSwitch = switchesOn.find(i => i.field === "action") ?? null;
  const filesWithAction = actionSwitch !== null && attachments.some(a => a.state !== "failed");
  // The settings some entries need (painting needs an image model), read when the menu opens. Not read = not checked, and the menu says so.
  const [menuSettings, setMenuSettings] = useState<Record<string, unknown> | null>(null);
  const [menuSettingsError, setMenuSettingsError] = useState<string | null>(null);
  useEffect(() => {
    if (!menuOpen) return;
    let live = true;
    api.getConfig().then(
      (cfg) => { if (live) { setMenuSettings(cfg); setMenuSettingsError(null); } },
      (err: unknown) => { if (live) { setMenuSettings(null); setMenuSettingsError(err instanceof Error ? err.message : String(err)); } });
    return () => { live = false; };
  }, [menuOpen]);
  const menuIssues = useMemo<MenuIssue[]>(() => menuSettingsError === null ? composedMenu.issues
    : [...composedMenu.issues, { code: "settings_unread", params: { reason: menuSettingsError } }], [composedMenu, menuSettingsError]);
  const closeMenu = useCallback(() => { setMenuOpen(false); plusRef.current?.focus(); }, []);
  useLayer("composer-menu", menuOpen, closeMenu);  // Back / Escape close the + menu first
  // P33: in the chat shell a conversation can have a model of its own, through the scope chain (P24). Choosing one (the + menu or the picker
  // above, one state) in an open conversation writes it at the conversation level (conversations.metadata.settings.default_model); with no
  // conversation open it is the person's pick, and the conversation the first message makes keeps it when it differs from what the chain
  // gives. Opening a conversation shows the model it has of its own, else the person's pick. A failure to read or keep it is said under the
  // field. Workshop views keep their own model each (several views may show one conversation with different models): they neither write nor read it.
  const [modelNote, setModelNote] = useState<string | null>(null);
  const shellTextRef = useRef(t);
  shellTextRef.current = t;
  // Explicit choices made on this page: a resolution of the chain that started before one is stale and is not applied.
  const modelChoicesRef = useRef(0);
  const saveConversationModel = useCallback((target: string, id: string) =>
    api.getConversation(target).then((conv) => api.updateConversation(target, { metadata: withConversationModel(conv.metadata, MODEL_SETTING, id) })), []);
  const chooseModel = useCallback((id: string) => {
    const choices = ++modelChoicesRef.current;
    setModelNote(null);
    const target = convId;
    if (!chat || !target) { setModel(id); return; }
    // In an open conversation the choice is the conversation's own; the person's pick stays for the other conversations.
    setOwnModel(id ? { convId: target, model: id } : null);
    saveConversationModel(target, id)
      .then(() => id === "" ? api.resolveScope({ key: MODEL_SETTING, conversation_id: target }) : null)
      .then((resolution) => {
        // The default model removes the conversation's own: what the chain gives it now (its project's, or the person's pick) is shown.
        if (!resolution || !mountedRef.current || modelChoicesRef.current !== choices) return;
        const own = conversationModel(resolution);
        setOwnModel(own === null ? null : { convId: target, model: own });
      })
      .then(undefined, (err: unknown) => { if (mountedRef.current && selectedConvRef.current === target) setModelNote(t("cm_model_not_saved", { reason: err instanceof Error ? err.message : String(err) })); });
  }, [chat, convId, saveConversationModel, t]);
  // A conversation this page's send created keeps the model it was made with, when the chain would give it another one. A send that names no
  // conversation and does not ask for a new one (a forced or one-call request without new_conversation) may land in the one the kernel
  // remembers (ChatEngine::send): that one is not new, holds earlier messages, and is left alone.
  const keepNewConversationModel = useCallback((target: string, chosen: string, userMessageId: string | null) => {
    if (!chat || !chosen) return;
    const choices = ++modelChoicesRef.current;
    api.getMessages(target)
      .then((rows) => rows.length === 0 || rows[0].id === userMessageId ? api.resolveScope({ key: MODEL_SETTING, conversation_id: target }) : null)
      .then((resolution) => !resolution || conversationModel(resolution) !== null || resolution.value === chosen ? false
        : saveConversationModel(target, chosen).then(() => true))
      .then((saved) => { if (saved && mountedRef.current && modelChoicesRef.current === choices) setOwnModel({ convId: target, model: chosen }); },
        (err: unknown) => { if (mountedRef.current && selectedConvRef.current === target) setModelNote(shellTextRef.current("cm_model_not_saved", { reason: err instanceof Error ? err.message : String(err) })); });
  }, [chat, saveConversationModel]);
  useEffect(() => {
    setModelNote(null);  // what was said about one conversation's model does not follow the person into the next
    if (!chat || !convId) return;
    let live = true;
    const choices = modelChoicesRef.current;
    api.resolveScope({ key: MODEL_SETTING, conversation_id: convId }).then(
      (resolution) => {
        if (!live || modelChoicesRef.current !== choices) return;  // a choice made meanwhile wins
        const own = conversationModel(resolution);
        setOwnModel(own === null ? null : { convId, model: own });
      },
      (err: unknown) => { if (live) setModelNote(shellTextRef.current("cm_model_not_read", { reason: err instanceof Error ? err.message : String(err) })); });
    return () => { live = false; };
  }, [chat, convId]);
  // A refused send gives its files back as chips; the one the kernel named is marked with why it cannot go (ChatEngine::check_attachments).
  const restoredChips = useCallback((refs: string[], refused: { ref: string; why: "missing" | "unreadable" } | null): ComposerAttachment[] =>
    refs.map(ref => ref === refused?.ref
      ? { key: `a${++attachmentCounter}`, name: attachmentName(ref), state: "failed" as const, ref, reason: t(refused.why === "unreadable" ? "att_unreadable" : "att_missing") }
      : { key: `a${++attachmentCounter}`, name: attachmentName(ref), state: "ready" as const, ref }), [t]);

  // `again`: a regeneration — the composer's options with an earlier message's words, files and model; the field and its chips stay as they are.
  pendingRef.current = {
    text: input,
    options: {
      ...(shownModel ? { model: shownModel } : {}),
      ...Object.fromEntries(Object.entries(requestFields(composedMenu.items, switches)).filter(([field]) => field !== "action")),
      ...(!includeMemory && { include_memory: false }),
      ...(!includeGraphMemory && { include_graph_memory: false }),
      ...(!includeHistory && { include_history: false }),
    },
  };
  const send = useCallback((mode: "send" | "preview" = "send", forced?: ChatRequest, again?: { text: string; attachments: string[]; model: string }) => {
    const text = again ? again.text : input.trim();
    // The request is built from the composer's options (a message written in the field, or a regeneration) ...
    const fromComposer = !forced && (again !== undefined || requestOverride === null);
    // ... and only a message written in the field takes the field's words, its files and the action switch (and empties them).
    const composed = fromComposer && again === undefined;
    const ready = again ? again.attachments : attachments.flatMap(a => a.state === "ready" && a.ref ? [a.ref] : []);
    // Files alone are a message too (as in the popular chat apps): words or at least one ready file.
    const content = text !== "" || ready.length > 0;
    if (mode === "send" && composed && content && !stream) {
      if (queuedRef.current) return;  // already waiting: the text goes once
      if (queueReason({ convId, creating, gate }) !== null) {
        queuedRef.current = { convId };
        setQueuedNote("waiting");
        return;
      }
    }
    // Only a user choice or a transient integrity condition refuses; the reason is shown (R43).
    if (gate.blocked) { setError(gateTexts.blocked); return; }
    if (branchLeaf) { setError("Return to current conversation before sending from a branch projection."); return; }
    if (stream || !supports("chat.send")) return;
    // The chips say a file is still being added, or that files do not go with an action; Send waits and says why.
    if (composed && (attachments.some(a => a.state === "uploading") || filesWithAction)) return;
    if (!forced && !(mode === "send" && requestOverride !== null) && !content) return;
    let request: ChatRequest;
    if (forced) {
      request = forced;
    } else if (!again && mode === "send" && requestOverride !== null) {
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
      // A blank page asks for a new conversation (new_conversation, R35): the kernel never writes into the one it remembers. The model is the
      // conversation's own (P33 scope chain), or the one that answered the message being regenerated.
      message: text, conv_id: convId ?? undefined, new_conversation: convId ? undefined : true, model: (again ? again.model : shownModel) || undefined,
      // Omit unchanged options so the existing default request stays intact.
      ...(ready.length > 0 && { attachments: ready }),
      // The switches of the + menu: each sets the request field its entry names (data). A regeneration is a message again, never an action.
      ...(Object.fromEntries(Object.entries(requestFields(composedMenu.items, switches)).filter(([field]) => composed || field !== "action")) as Partial<ChatRequest>),
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
    // started: the kernel stored the person's message (the start chunk came). produced: words of an answer arrived.
    let started = false;
    let produced = false;
    let notesAwaitStart = false;  // the route notes came before the conversation was known (a message on a blank page): they follow it
    const refreshShared = () => {
      if (requestConvId) void refreshMessages(requestConvId);
      onMessagesChanged?.();
    };
    const finishError = (raw: string | null, code?: string) => {
      if (!isCurrent() || terminal) return;
      terminal = true;
      setStream(null);
      setPendingUserText(null);
      let message = raw;
      // Nothing was stored: the words go back into the field, so a failed send loses nothing and can be sent again (a regeneration is the
      // earlier message, still in the conversation: the field keeps what is being written).
      if (message && !started && !again) {
        setInput((current) => current || request.message);
        // The files go back as chips too (without their previews); a file the kernel would not take is marked with why.
        const sent = request.attachments ?? [];
        const refused = refusedAttachment(code, message, sent);
        if (sent.length) setAttachments((list) => list.length ? list : restoredChips(sent, refused));
        if (refused) message = t("att_not_sent", { name: attachmentName(refused.ref), reason: t(refused.why === "unreadable" ? "att_unreadable" : "att_missing") });
      }
      // The error is said in the conversation the message went to. That is the one on the screen unless the person has gone to another
      // one; a conversation this very send created is not yet selected when an error follows its start at once.
      const here = selectedConvRef.current;
      if (message && (here === requestConvId || here === null || requestConvId === null)) { errorForRef.current = requestConvId; setError(message); }
      refreshShared();
    };
    if (!again) setInput("");
    // The chips go with a composed message only: a re-send (taking an offer, confirming usage, a regeneration) leaves the next one's files.
    if (composed) setAttachments((list) => { for (const a of list) if (a.preview) URL.revokeObjectURL(a.preview); return []; });
    setQueuedNote(null);
    setRequestOverride(null);
    setRouteNotes([]);
    currentUserIdRef.current = null;
    currentConvIdRef.current = requestConvId;
    setPendingUserText({ text: request.message, convId: requestConvId });
    setStream({ reasoning: "", text: "", requestId: null, convId: requestConvId });
    setError(null);

    const handlers = {
        onChunk: (chunk: ChatChunk) => {
          if (!isCurrent() || terminal) return;
          if (chunk.type === "start") {
            started = true;
            clearPendingTurn(convId ?? null, chunk.conv_id, null);  // the kernel took what waited for this request (the turn carrier is empty after a send)
            if (notesAwaitStart) { notesForRef.current = chunk.conv_id; notesAwaitStart = false; }
            currentUserIdRef.current = chunk.user_message_id;
            currentConvIdRef.current = chunk.conv_id;
            requestConvId = chunk.conv_id;
            setStream((s) => (s ? { ...s, requestId: chunk.request_id, convId: chunk.conv_id } : s));
            setPendingUserText((s) => s ? { ...s, convId: chunk.conv_id } : s);
            if (!convId && selectedConvRef.current === null) onConversationCreated(chunk.conv_id);
            if (!request.conv_id && request.model) keepNewConversationModel(chunk.conv_id, request.model, chunk.user_message_id);
            onMessagesChanged?.();
          } else if (chunk.type === "route_issue") {
            notesForRef.current = requestConvId;
            notesAwaitStart = requestConvId === null;
            setRouteNotes(routeIssueNotes(routeIssueCatalog.feature, routeIssues(chunk.issues)));
          } else if (chunk.type === "usage_notice") {
            // A spending limit that only tells (P18): the send went on; the notice is a quiet line like the route notes.
            notesForRef.current = requestConvId;
            notesAwaitStart = requestConvId === null;
            const lines = usageNoticeLines(chunk.notices, usageCatalog.feature?.locale ?? "en");
            setRouteNotes((current) => [...current, ...lines]);
          } else if (chunk.type === "reasoning") {
            setStream((s) => (s ? { ...s, reasoning: s.reasoning + chunk.text } : s));
          } else if (chunk.type === "delta") {
            if (chunk.text) produced = true;
            setStream((s) => (s ? { ...s, text: s.text + chunk.text } : s));
          } else if (chunk.type === "done") {
            terminal = true;

            if (chunk.context_trace) setLastTrace({ convId: chunk.conv_id, userId: currentUserIdRef.current, trace: chunk.context_trace });
            setStream(null);
            setPendingUserText(null);
            requestConvId = chunk.conv_id;
            // An answer with no words is said, not left as an empty bubble.
            if (!produced && chunk.text === "" && !chunk.cancelled) { errorForRef.current = requestConvId; setError(gateTexts.emptyAnswer ?? "empty_answer"); }
            refreshShared();
          } else if (chunk.type === "action_proposal" || chunk.type === "action_result") {
            // The input was a command: it is answered in the conversation, nothing went to the model.
            terminal = true;
            setStream(null);
            setPendingUserText(null);
            // The answer is in another conversation when the command deleted the one it was asked in: the page goes there.
            if (chunk.conv_id && requestConvId && chunk.conv_id !== requestConvId) onConversationCreated(chunk.conv_id);
            requestConvId = chunk.conv_id;
            refreshShared();
          } else if (chunk.type === "usage_pending") {
            // A state, not a failure: nothing was sent. Keep the text and ask.
            terminal = true;
            setStream(null);
            setPendingUserText(null);
            setInput((current) => current || request.message);
            // The files wait with the words: a refusal, or editing and sending again, loses none of them.
            const held = request.attachments ?? [];
            if (held.length) setAttachments((list) => list.length ? list : restoredChips(held, null));
            if (!request.conv_id && chunk.conv_id && request.model) keepNewConversationModel(chunk.conv_id, request.model, null);
            setUsageNotice(null);
            setUsagePending({ request: { ...request, conv_id: chunk.conv_id }, receipt: chunk.usage_pending, convId: chunk.conv_id });
            requestConvId = chunk.conv_id;
            refreshShared();
          } else if (chunk.type === "error") {
            finishError(chunk.message, chunk.code);
          }
        },
        onError: (msg: string) => finishError(msg),
        onDone: () => finishError(gateTexts.streamEnded ?? "stream_ended"),
      };
    const dispatch = () => {
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
    };
    // The devices of this page are declared to the kernel here, right before the first thing the person started that may need them (a command
    // that records, takes a picture ...), and again only when what the browser can do has changed: nothing is sent in the background.
    // A request the user wrote by hand (the one-call override) or a re-send of one is sent exactly as it is (`composed` above).
    // A declaration that fails, is refused or does not answer never holds the send back: it goes without a client id.
    void clientSession.ensure().catch(() => "").then((id) => {
      if (!isCurrent()) return;
      if (fromComposer && id !== "" && !request.client_id) request = { ...request, client_id: id };
      dispatch();
    });
  }, [input, attachments, switches, composedMenu, filesWithAction, restoredChips, keepNewConversationModel, t, stream, convId, creating, gate, shownModel, onConversationCreated, refreshMessages, clientSession, routeIssueCatalog.feature, usageCatalog.feature,
    useKnowledge, contextQuery, contextProject, contextTargets, contextRun,
    contextLanguage, contextBudget, contextHops, contextDetail, usePlan, planDraft, includeMemory, includeGraphMemory, includeHistory, traceContext, runProfileOperation, onMessagesChanged, supports, channels, scanLimit, counterEvidence, lexicalShadow, branchLeaf, requestOverride, gate.blocked, gateTexts.blocked, gateTexts.emptyAnswer, gateTexts.streamEnded]);

  // The queued send, each time what it waits for changes: sent once when nothing waits any more, said and kept when the conversation did not load,
  // dropped (the text stays) when the person went to another conversation.
  useEffect(() => {
    const queued = queuedRef.current;
    if (!queued) return;
    const step = queueStep(queued, { convId, creating, gate });
    if (step === "wait") {
      if (queued.convId === null && convId !== null) queuedRef.current = { convId };  // the conversation was made: this is the one it goes into
      return;
    }
    // A file still being added holds the queued message back (it stays "waiting"); it goes once every file is in.
    if (step === "send" && attachments.some(a => a.state === "uploading")) return;
    queuedRef.current = null;
    if (step === "send") { send("send"); return; }
    setQueuedNote(step === "failed" ? "failed" : null);
  }, [convId, creating, gate, send, queuedNote, attachments]);

  const confirmUsage = useCallback(() => {
    if (!usagePending) return;
    const { request, receipt } = usagePending;
    setUsagePending(null);
    // The files this confirmed message carries leave the composer with it; files staged since stay for the next one.
    const carried = request.attachments ?? [];
    if (carried.length) setAttachments((list) => list.filter(a => !(a.ref && carried.includes(a.ref))));
    const routed = (receipt as { stage?: string }).stage === "route_intent";
    send("send", (routed ? routeConfirmedRequest(request, receipt, true, "web") : confirmedRequest(request, receipt, true, "web")) as ChatRequest);
  }, [usagePending, send]);
  const rejectUsage = useCallback(() => {
    if (!usagePending) return;
    const { request, receipt } = usagePending;
    setUsagePending(null);
    // The files stay with the words in the composer, as chips (a refusal loses none of them).
    const held = request.attachments ?? [];
    if (held.length) setAttachments((list) => list.length ? list : restoredChips(held, null));
    // A refusal by a spending limit (status denied) has nothing to answer: the block is only dismissed, the text stays in the composer.
    if ((receipt as { status?: string }).status === "denied") {
      setUsageNotice(usageCatalog.feature ? safeNotice(usageCatalog.feature, "limit_dismissed") : null);
      return;
    }
    // The ledger records the denial; nothing is sent and the text stays in the composer.
    const routed = (receipt as { stage?: string }).stage === "route_intent";
    try { api.chat((routed ? routeConfirmedRequest(request, receipt, false, "web") : confirmedRequest(request, receipt, false, "web")) as ChatRequest, { onChunk: () => undefined, onError: () => undefined, onDone: () => undefined }); }
    catch (err) { setError(err instanceof Error ? err.message : String(err)); }
    setUsageNotice(usageCatalog.feature ? safeNotice(usageCatalog.feature, "rejected") : null);
  }, [usagePending, usageCatalog.feature, restoredChips]);

  // Takes the offer a "no" made: the sentence goes to the model as an ordinary message (the engine reads it from the conversation).
  const takeOffer = useCallback((offer: OfferView) => {
    const sentence = messages.find(row => row.id === offer.sentenceId);
    if (!convId || !sentence) { setOfferFailure({ proposalId: offer.proposalId, reason: offer.sentenceId }); return; }
    setOfferFailure(null);
    const draft = input;
    send("send", offerRequest(offer, sentence.text, convId));
    setInput(draft);  // what the user was typing is not the sentence being sent
  }, [convId, messages, send, input]);

  // M1-W2a: a tap on a level card or a quick reply is the person's message (the level's name, a control word): the same send as a typed one, no model.
  const sayInInterview = useCallback((word: string) => {
    if (!convId) return;
    const draft = input;
    send("send", { message: word, conv_id: convId });
    setInput(draft);
  }, [convId, send, input]);

  const undoWritten = useCallback(async (written: WrittenView) => {
    if (!api.actions) return;
    setUndoing(written.id);
    setUndoFailure(null);
    try {
      // First the receipt (the removal is proposed, nothing is removed), then the click's yes with that receipt.
      const proposed = await api.actions[undoCall(written)](undoRequest(written));
      const answer = undoConfirmation(proposed);
      if (!answer) throw new Error(proposed.error && proposed.error.message ? proposed.error.message : String(proposed.status));
      const run = await api.actions.confirm(answer);
      if (run.status !== "completed") throw new Error(run.error && run.error.message ? run.error.message : String(run.status));
      if (convId) await refreshMessages(convId);
      onMessagesChanged?.();
    } catch (err) {
      setUndoFailure({ id: written.id, reason: err instanceof Error ? err.message : String(err) });
    } finally {
      setUndoing(null);
    }
  }, [convId, refreshMessages, onMessagesChanged]);

  // The answer to the question of the background analysis goes to the worker's own usage confirmation, with the receipt the question carries.
  const answerSemantic = useCallback(async (question: SemanticQuestion, approved: boolean) => {
    if (!api.confirmSemanticUsage) return;
    setSemanticAnswering(question.receiptId);
    setSemanticFailure(null);
    try {
      await api.confirmSemanticUsage(approved, question.receiptId, SEMANTIC_ANSWER_REF);
      if (convId) await refreshMessages(convId);
      onMessagesChanged?.();
    } catch (err) {
      setSemanticFailure({ receiptId: question.receiptId, reason: err instanceof Error ? err.message : String(err) });
    } finally {
      setSemanticAnswering(null);
    }
  }, [convId, refreshMessages, onMessagesChanged]);

  const answerAction = useCallback(async (answer: PendingAnswer, approved: boolean) => {
    if (!api.actions) return;
    setAnswering(answer.runId);
    setAnswerFailure(null);
    try {
      const run = await api.actions.confirm(answerRequest(answer, approved, "web"));
      if (convId) await refreshMessages(convId);
      onMessagesChanged?.();
      // The setting asks for the rejected sentence to be sent at once: the call settled the proposal, the client sends.
      const resend = run.resend as { proposal_message_id?: string } | undefined;
      const offer = run.offer as { message_id?: string } | undefined;
      if (resend && resend.proposal_message_id && offer && offer.message_id) {
        takeOffer({ proposalId: resend.proposal_message_id, sentenceId: offer.message_id, auto: true });
      }
    } catch (err) {
      setAnswerFailure({ runId: answer.runId, reason: err instanceof Error ? err.message : String(err) });
    } finally {
      setAnswering(null);
    }
  }, [convId, refreshMessages, onMessagesChanged, takeOffer]);

  // The agent stopped on proposals: the answers resume the run (agent.run with `resume`), then the stored message records them.
  const resumeAgent = useCallback(async (message: Message, decided: Record<string, boolean>): Promise<boolean> => {
    const view = agentLoopView(message.metadata);
    if (!view || !api.agent) return false;
    setAgentBusy(message.id);
    setAgentFailure(null);
    try {
      const outcome = await api.agent.run(resumeRequest(view, decided, convId));
      const loop = agentLoopOf(message.metadata) as Record<string, unknown>;
      const finished = outcome.status === "finished" || !Array.isArray(outcome.proposals) || outcome.proposals.length === 0;
      const metadata = { ...message.metadata, agent_loop: { ...loop, answered: { ...view.answered, ...decided },
        status: finished ? "answered" : String(outcome.status ?? "needs_confirmation"),
        // more proposals: the next stop replaces the open ones, with the transcript to continue from
        ...(finished ? { proposals: [] } : { proposals: outcome.proposals, messages: outcome.messages, answered: {} }) } };
      await api.updateMessage(message.id, { metadata });
      if (convId) await refreshMessages(convId);
      onMessagesChanged?.();
      return true;
    } catch (err) {
      setAgentFailure({ messageId: message.id, reason: err instanceof Error ? err.message : String(err) });
      return false;
    } finally {
      setAgentBusy(null);
    }
  }, [convId, refreshMessages, onMessagesChanged]);

  // A recording stored in the kernel, as the page can play it.
  const loadBlob = useCallback<BlobLoader>(async (hash) => {
    if (!api.actions || !api.actions.clientBlobFetch) throw new Error("this transport has no blob access");
    return api.actions.clientBlobFetch(hash);
  }, []);
  // What a device step brought (a recording, a picture, a file) goes to the kernel as bytes; the answer then names it by its hash.
  const uploadBlob = useCallback<BlobUploader>(async (blob, name) => {
    if (!api.actions || !api.actions.clientBlobUpload) throw new Error("this transport cannot hand bytes to the kernel");
    return api.actions.clientBlobUpload(blob, { name });
  }, []);

  // The answer of this device to the step a run is paused on: the run continues in the kernel and the conversation hears the result.
  const answerClient = useCallback(async (runId: string, result: ClientResult) => {
    if (!api.actions || !api.actions.clientResult) return;
    setAnswering(runId);
    setAnswerFailure(null);
    try {
      await api.actions.clientResult(result);
      if (convId) await refreshMessages(convId);
      onMessagesChanged?.();
    } catch (err) {
      setAnswerFailure({ runId, reason: err instanceof Error ? err.message : String(err) });
    } finally {
      setAnswering(null);
    }
  }, [convId, refreshMessages, onMessagesChanged]);

  const cancelRun = useCallback(async (runId: string) => {
    if (!api.actions) return;
    setAnswering(runId);
    setAnswerFailure(null);
    try {
      await api.actions.cancel({ run_id: runId, ref: "web" });
      if (convId) await refreshMessages(convId);
      onMessagesChanged?.();
    } catch (err) {
      setAnswerFailure({ runId, reason: err instanceof Error ? err.message : String(err) });
    } finally {
      setAnswering(null);
    }
  }, [convId, refreshMessages, onMessagesChanged]);

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

  // A regeneration sends the earlier message again as it was (its words, its files, the model that answered it); the composer keeps what is
  // being written and its chips.
  const regenerateFrom = useCallback((m: Message) => {
    const at = messages.findIndex(x => x.id === m.id);
    const prior = [...messages.slice(0, at)].reverse().find(x => x.role === "user");
    if (!prior || !convId) return;
    const files = Array.isArray(prior.attachments) ? prior.attachments.filter((x): x is string => typeof x === "string") : [];
    const answeredBy = m.model && !m.model.startsWith("loom:") ? m.model : shownModel;
    send("send", undefined, { text: prior.text, attachments: files, model: answeredBy });
  }, [messages, convId, shownModel, send]);
  useEffect(() => { if (chat) api.hasSecret("api_key").then(setHasKey, () => setHasKey(null)); }, [chat, error, convId]);
  useEffect(() => {
    if (!chat) return;
    for (const m of messages) if (m.version_group_id && !versionsById[m.version_group_id] && (m.version_num ?? 1) > 1) void loadVersions(m);
  }, [chat, messages, versionsById, loadVersions]);

  const visibleMessages = useMemo(() => branchLeaf
    ? conversationBranchPath([...olderMessages, ...branchMessages], branchLeaf).messages
    : [...olderMessages.filter(row => showAllMessages || row.status === "active"),
       ...(conversationView ? conversationViewMessages(conversationView, showAllMessages) : messages.filter(row => showAllMessages || row.status === "active"))]
      .filter(message => message.conv_id === convId), [messages, convId, branchLeaf, branchMessages, conversationView, showAllMessages, olderMessages]);
  const displayedStream = stream?.convId === convId ? stream : null;
  const displayedPending = pendingUserText?.convId === convId ? pendingUserText : null;
  const waitingInterview = interviewConv ? waitingQuestion(visibleMessages) : null;

  return (
    <div className="chat-view" data-testid="chat-view">
      {!chat && <label className="profile-workspace-controls"><input type="checkbox" checked={showAllMessages} onChange={e => setShowAllMessages(e.target.checked)} data-testid="show-all-messages" />Show excluded messages and saved versions</label>}
      {!chat && <label className="profile-workspace-controls"><input type="checkbox" checked={showBranches} onChange={e => { setShowBranches(e.target.checked); setBranchLeaf(null); }} data-testid="show-conversation-branches" />Browse retained descendant branches</label>}
      {chat && messages.some(m => m.status !== "active") && <Disclosure testId="shell-excluded" open={showAllMessages} onToggle={setShowAllMessages}
        labels={{ show: t("show_excluded"), hide: t("hide_excluded") }}><p>{t("hidden_count", { n: String(messages.filter(m => m.status !== "active").length) })}</p></Disclosure>}
      {showBranches && <ConversationBranches messages={branchMessages} selectedId={branchLeaf} onSelect={message => setBranchLeaf(message.id)} />}
      {branchLeaf && <p role="status">Ancestor path projection <button onClick={() => setBranchLeaf(null)} data-testid="return-current-branch">Return to current conversation</button></p>}
      {settingsError && <p role="alert">{settingsError}</p>}
      {convId && !api.readConversationView && sourceCatalog.feature && sourceHistoryUnavailable(convRecord, [...olderMessages, ...messages]) && <p role="status" data-testid="conversation-view-host-unavailable">
        {featureMessage(featureInLanguage(sourceCatalog.feature, shellLanguage), "host_unavailable")}
      </p>}
      {conversationView && conversationView.resources.length > 0 && <ConversationSourceView key={convId} view={conversationView}
        profileHost={profileHost} catalog={sourceCatalog} busy={sourceBusy} onRead={readSources} onReload={() => { if (convId) void refreshMessages(convId); }} />}
      {interviewConv && convId && <InterviewInfoLine t={interviewT} convId={convId} refreshKey={messages.length} language={shellLanguage} variant={variant} />}
      <div className="chat-scroll" ref={scrollRef} onScroll={e => { if (e.currentTarget.scrollTop < 40 && pageInfo?.truncated) void loadOlder(); }}>
        {pageInfo?.truncated && <div className="chat-older" data-testid="chat-older">
          <span data-testid="chat-older-count">{olderMessages.length + messages.length} / {pageInfo.total}</span>{" "}
          <button onClick={() => void loadOlder()} disabled={loadingOlder} data-testid="chat-load-older">{loadingOlder ? "..." : t("load_older_messages")}</button>
        </div>}
        {visibleMessages.length === 0 && !displayedPending && !displayedStream && !gate.viewLoading && gate.viewFailure === null && !conversationView?.resources.length && (
          chat ? <div className="shell-empty" data-testid="shell-empty" data-interview={interviewCard.state}>
            {needsProviderKey(hasKey) && <div className="shell-card" data-testid="first-run-card" role="status">
              <strong>{t("first_run_title")}</strong><span>{t("first_run_body")}</span>
              <button className="primary" data-testid="first-run-open-settings" onClick={() => requestPanel("settings")}>{t("first_run_button")}</button>
            </div>}
            {interviewCard.visible && <InterviewStartCard t={interviewT} busy={interviewCard.busy} failure={interviewCard.failure}
              onStart={() => void interviewCard.start()} onLater={() => void interviewCard.later()} onNever={() => void interviewCard.never()} />}
            {!interviewCard.visible && interviewCard.note && <InterviewCardDecision t={interviewT} note={interviewCard.note} />}
            <h2>{t("empty_title")}</h2>
            <div className="shell-starters">{[1, 2, 3, 4].map(n => <button key={n} data-testid="shell-starter" onClick={() => setInput(t("starter_" + n))}>{t("starter_" + n)}</button>)}</div>
          </div> : <div className="empty-state">Say something to start the conversation.</div>
        )}
        {visibleMessages.map((m) => {
          const versions = m.version_group_id ? versionsById[m.version_group_id] : undefined;
          const trace = recordedContext(m.metadata?.context_trace);
          const graphReply = recordedGraphReply(m.metadata);
          return (
            <div key={m.id} className={`msg ${m.role}`} data-testid="message" onContextMenu={chat ? (e) => { e.preventDefault(); setActionsFor(m.id); } : undefined}
              onPointerDown={chat ? () => { pressTimer.current = window.setTimeout(() => setActionsFor(m.id), 450); } : undefined}
              onPointerUp={chat ? () => { if (pressTimer.current) window.clearTimeout(pressTimer.current); } : undefined}
              onPointerLeave={chat ? () => { if (pressTimer.current) window.clearTimeout(pressTimer.current); } : undefined} data-role={m.role} data-status={m.status} data-storage={m.storage ?? "native"}>
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
              ) : waitingInterview?.messageId === m.id && levelCards(m.metadata).length > 0 ? (
                <div className="body" data-testid="interview-level-question">
                  <p>{waitingInterview.question.text}</p>
                  <InterviewLevelCards t={interviewT} levels={levelCards(m.metadata)} disabled={!!stream} onPick={level => sayInInterview(level.label)} />
                </div>
              ) : (
                <div className="body" dangerouslySetInnerHTML={renderMarkdown(m.role === "assistant" ? nativeGraphDisplayText(graphReply, m.text) : m.text)} />
              )}
              {waitingInterview?.messageId === m.id && <InterviewChips t={interviewT} disabled={!!stream} onSay={sayInInterview} />}
              {m.role === "assistant" && (() => {
                const question = semanticQuestion(m.metadata);
                if (!question) return null;
                const feature = featureInLanguage(semanticCatalog.feature, question.lang);
                if (semanticQuestionOpen(question) && api.confirmSemanticUsage) {
                  return (<>
                    <UsagePendingBlock testId="chat-semantic-question" feature={feature} busy={semanticAnswering === question.receiptId} receipt={question.receipt}
                      onConfirm={() => void answerSemantic(question, true)} onReject={() => void answerSemantic(question, false)} />
                    {semanticFailure && semanticFailure.receiptId === question.receiptId &&
                      <p className="pill warn" data-testid="chat-semantic-failure">{semanticFailedText(feature, semanticFailure.reason)}</p>}
                  </>);
                }
                const label = semanticAnsweredText(feature, question);
                return label ? <span className="pill" data-testid="chat-semantic-state" data-state={question.status}>{label}</span> : null;
              })()}
              {m.role === "assistant" && api.actions && (() => {
                const answer = pendingAnswer(m.metadata);
                const state = proposalState(m.metadata);
                const agentView = agentLoopView(m.metadata);
                if (agentView && api.agent) {
                  return <AgentProposalBlock testId="chat-agent-proposal" view={agentView} t={t} busy={agentBusy === m.id}
                    failure={agentFailure && agentFailure.messageId === m.id ? agentFailure.reason : null}
                    onResume={(decided) => resumeAgent(m, decided)} />;
                }
                const spoken = messageLanguage(m.metadata);  // one language per answer: the page adds its lines in the language of the message
                const proposalFeature = featureInLanguage(proposalCatalog.feature, spoken);
                if (answer && answer.kind === "usage") {
                  return <UsagePendingBlock testId="chat-run-usage-pending" feature={featureInLanguage(usageCatalog.feature, spoken)} busy={answering === answer.runId}
                    receipt={{ ...(answer.usage ?? {}), receipt_id: answer.receiptId, operation_id: (m.metadata?.action_run as { pending?: { operation_id?: string | null } } | undefined)?.pending?.operation_id ?? null }}
                    onConfirm={() => void answerAction(answer, true)} onReject={() => void answerAction(answer, false)} />;
                }
                if (answer && answer.kind === "client" && answer.clientStep) {
                  return <ClientStepBlock testId="chat-client-step" feature={featureInLanguage(clientStepCatalog.feature, spoken)} step={answer.clientStep} runId={answer.runId}
                    receiptId={answer.receiptId} host={clientHost} load={loadBlob} upload={uploadBlob} clientRef={"web:" + clientSession.id} busy={answering === answer.runId}
                    failure={answerFailure && answerFailure.runId === answer.runId ? answerFailure.reason : null}
                    onAnswer={(result) => answerClient(answer.runId, result)} onCancel={() => void cancelRun(answer.runId)} />;
                }
                if (answer) {
                  return <ActionProposalBlock testId="chat-action-proposal" feature={proposalFeature} answer={answer} busy={answering === answer.runId}
                    failure={answerFailure && answerFailure.runId === answer.runId ? answerFailure.reason : null}
                    onConfirm={() => void answerAction(answer, true)} onReject={() => void answerAction(answer, false)} />;
                }
                if (state && proposalFeature) {
                  const label = safeNotice(proposalFeature, "status_" + state);
                  const written = writtenView(m.metadata);
                  const offer = openOffer(m.metadata, m.id);
                  const ended = offerState(m.metadata);
                  const endedLabel = ended ? offerTexts(proposalFeature).state(ended) : null;
                  return (<>
                    {label && <span className="pill" data-testid="chat-action-state" data-state={state}>{label}</span>}
                    {m.metadata?.route_trace != null && <details data-testid="chat-route-trace"><summary>route_trace</summary><pre>{JSON.stringify(m.metadata.route_trace, null, 2)}</pre></details>}
                    {written && <ProposalWritten testId="chat-action-written" feature={proposalFeature} written={written} busy={undoing === written.id}
                      failure={undoFailure && undoFailure.id === written.id ? undoFailure.reason : null} onUndo={() => void undoWritten(written)} />}
                    {offer && <ProposalOffer testId="chat-action-offer" feature={proposalFeature} offer={offer} busy={Boolean(stream)}
                      failure={offerFailure && offerFailure.proposalId === offer.proposalId ? offerFailure.reason : null} onSend={() => takeOffer(offer)} />}
                    {endedLabel && <span className="pill" data-testid="chat-action-offer-state" data-state={ended ?? ""}>{endedLabel}</span>}
                  </>);
                }
                return null;
              })()}
              {m.role === "user" && Array.isArray(m.attachments) && m.attachments.length > 0 && <p className="composer-chips" data-testid="message-attachments">
                {m.attachments.filter((x): x is string => typeof x === "string").map((ref, i) => <span key={i} className="composer-chip" data-testid="message-attachment"><span className="chip-name">{attachmentName(ref)}</span></span>)}</p>}
              {m.role === "assistant" && (() => {
                const made = producedView(m.metadata, m.id);
                const label = made ? producedText(featureInLanguage(proposalCatalog.feature, messageLanguage(m.metadata)), made) : null;
                return made && label ? <p className="pill" data-testid="chat-action-produced" data-node={made.forms[0]} data-target={made.target}>{label}</p> : null;
              })()}
              {m.role === "assistant" && (() => {
                const hint = openSettingsHint(m.metadata);
                if (!hint) return null;
                const words = openSettingsTexts(openSettingsCatalog.feature, hint);
                return (<p className="pill" data-testid="chat-open-settings" data-keys={hint.keys.join(",")}>
                  <span>{words.note}</span>{" "}
                  <button onClick={() => requestOpenPanel(SETTINGS_PANEL)} data-testid="chat-open-settings-button">{words.button}</button>
                </p>);
              })()}
              {chat && editingId !== m.id && <div className="shell-msg-actions" data-testid="shell-msg-actions" data-open={actionsFor === m.id ? "" : undefined}>
                <button onClick={() => { void navigator.clipboard?.writeText(m.text); setCopied(m.id); }} data-testid="msg-copy">{copied === m.id ? t("copied") : t("copy")}</button>
                {m.role === "user" && <button onClick={() => startEdit(m)} data-testid="msg-edit" disabled={!supports("message.edit") || !messageCan(m, "edit")}>{t("edit")}</button>}
                {m.role === "assistant" && <button onClick={() => regenerateFrom(m)} data-testid="msg-regenerate" disabled={!!stream}>{t("regenerate")}</button>}
                {versions && versions.length > 1 && (() => {
                  const at = versions.findIndex(v => v.id === m.id);
                  return <span className="version-switcher" data-testid="shell-versions">
                    <button aria-label={t("prev_version")} disabled={at <= 0} onClick={() => switchVersion(versions[at - 1].id)}>◀</button>
                    <span>{at + 1}/{versions.length}</span>
                    <button aria-label={t("next_version")} disabled={at < 0 || at >= versions.length - 1} onClick={() => switchVersion(versions[at + 1].id)}>▶</button>
                  </span>;
                })()}
                {!interviewConv && <button onClick={() => setReceiptFor(m.id)} data-testid="msg-receipt">{receiptT("menu.entry")}</button>}
                <button onClick={() => toggleExclude(m)} data-testid="msg-exclude" disabled={!supports("message.exclude") || !messageCan(m, "set_status") || m.status === "version" || m.status === "deleted"}>{m.status === "excluded" ? t("restore") : t("exclude")}</button>
              </div>}
              {chat && !interviewConv && m.role === "assistant" && editingId !== m.id && receiptLink === "visible" && (
                <p className="crs-line" data-testid="context-line" data-msg={m.id}>
                  <button type="button" data-testid="context-line-open" onClick={() => setReceiptFor(m.id)}>
                    {lineText(recordedReceipt(requestOf(visibleMessages, m.id)), receiptT, shellLanguage)}</button>
                </p>
              )}
              {!chat && <div className="actions">
                {m.role === "user" && editingId !== m.id && (
                  <button onClick={() => startEdit(m)} data-testid="edit-message" disabled={!supports("message.edit") || !messageCan(m, "edit")}>
                    Edit
                  </button>
                )}
                <button onClick={() => toggleExclude(m)} data-testid="toggle-exclude" disabled={!supports("message.exclude") || !messageCan(m, "set_status") || m.status === "version" || m.status === "deleted"}>
                  {m.status === "excluded" ? "Restore" : "Exclude"}
                </button>
              </div>}
              {m.storage === "reference" && sourceCatalog.feature && <details data-testid="source-message-reference">
                <summary>{featureMessage(sourceCatalog.feature, "source_message")}</summary>
                <p>{featureMessage(sourceCatalog.feature, "readonly")}</p>
                <pre>{JSON.stringify({ source_ref: m.source_ref, capabilities: m.capabilities }, null, sourceCatalog.base?.defaults.json_indent)}</pre>
              </details>}
              {!chat && trace && <ContextTrace trace={trace} />}
              {!chat && m.storage !== "reference" && (m.role === "assistant" || graphReply) && <details className="chat-context-controls"><summary>Graph reply · inspect and address fragments</summary>
                <GraphReplyWorkbench packet={api.packet?.bind(api)} usagePolicy={api.usagePolicy?.bind(api)}
                  responseText={m.role === "assistant" ? m.text : typeof graphReply?.retained_text === "string" ? graphReply.retained_text : ""} recordedReply={graphReply} graphReply={api.graphReply?.bind(api)}
                  onNativeAddressFragment={(action, fragment) => { setInput(nativeFragmentPrompt(action, fragment)); setRequestOverride(null); }}
                  conversationId={m.conv_id} turnId={m.id} requestId={String(m.metadata?.request_id ?? m.id)} model={m.model ?? ""}
                  onAddressFragment={(action, fragment) => { setInput(`${action === "expand" ? "Expand" : "Correct"} the addressed model fragment:\n${JSON.stringify(fragment, null, 2)}`); setRequestOverride(null); }} />
              </details>}
            </div>
          );
        })}
        {!chat && lastTrace && lastTrace.convId === convId && !visibleMessages.some((m) =>
          m.id === lastTrace.userId && recordedContext(m.metadata?.context_trace)) && (
          <ContextTrace trace={lastTrace.trace} />
        )}
        {chat && receiptFor && <ContextReceipt profileHost={profileHost} language={shellLanguage} convId={convId ?? null} msgId={receiptFor}
          onClose={() => setReceiptFor(null)} onShowInGraph={onShowInGraph} />}
        {displayedPending && !visibleMessages.some(message => message.id === currentUserIdRef.current) && (
          <div className="msg user">
            <div className="meta">user</div>
            <div className="body">{displayedPending.text}</div>
          </div>
        )}
        {displayedStream && (
          <div className="msg assistant" data-testid="streaming-message">
            {displayedStream.reasoning && (
              chat ? <Disclosure testId="shell-reasoning" className="reasoning" labels={{ show: t("show_thinking"), hide: t("hide_thinking") }} defaultOpen>
                <div className="reasoning-text">{displayedStream.reasoning}</div>
              </Disclosure> : <details className="reasoning" open>
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

      {!chat && <GraphChatSettings transport={api} disabled={!!stream} />}
      {!chat && <ScopeExplain convId={convId} />}
      {!chat && <details className="chat-context-controls" data-testid="chat-context-controls">
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
      </details>}

      {!chat && <details className="chat-context-controls" data-testid="expert-chat-request"><summary>Expert · inspect or edit one request</summary>
        <p>This is the client request sent to Loom. Compiled provider messages are inspected in the recorded context after execution.</p>
        <button onClick={() => send("preview")} disabled={!!stream || !!branchLeaf || Boolean(gate.blocked) || !input.trim()} data-testid="preview-chat-request"
          title={gateTexts.blocked ?? undefined} aria-describedby={gate.blocked ? gateDescriptionId : undefined}>Prepare client request</button>
        {requestOverride !== null && <>
          <textarea aria-label="One-call client request JSON" data-testid="one-call-request" rows={9} value={requestOverride} onChange={event => setRequestOverride(event.target.value)} disabled={!!stream} />
          <p>The next Send uses this exact JSON once. Profile, saved context and later calls keep their settings.</p>
          <button onClick={() => setRequestOverride(null)} data-testid="discard-one-call-request">Discard override</button>
        </>}
      </details>}

      {queuedNote !== null && <p className="send-gate-notice" role="status" data-testid="send-queued" data-state={queuedNote}>
        {queuedNote === "waiting" ? gateTexts.queuedWaiting ?? VIEW_LOADING_REASON : gateTexts.queuedFailed ?? gate.viewFailure ?? VIEW_LOADING_REASON}</p>}
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
      {chat && requestOverride !== null && <div className="chat-preview" data-testid="chat-preview-request">
        <p><strong>{t("pv_title")}</strong> — {t("pv_note")}</p>
        <textarea aria-label={t("pv_aria")} data-testid="one-call-request" rows={9} value={requestOverride} onChange={event => setRequestOverride(event.target.value)} disabled={!!stream} />
        <button type="button" onClick={() => setRequestOverride(null)} data-testid="discard-one-call-request">{t("pv_discard")}</button>
      </div>}
      {chat && !interviewConv && <ContextStrip convId={convId ?? null} language={shellLanguage} t={receiptT} lastProfile={lastProfile} messageCount={messages.length} />}
      <div className={chat ? "composer shell-composer" : "composer"}>
        {chat && !interviewConv && <span className="composer-plus">
          <button type="button" ref={plusRef} aria-label={t("attach")} title={t("attach")} aria-haspopup="menu" aria-expanded={menuOpen} data-testid="shell-attach" onClick={() => setMenuOpen(v => !v)}>+</button>
          {/* The photo entry is shown wherever files can go: the capture input opens the camera on a phone and a picture file elsewhere. */}
          {menuOpen && <ComposerMenu items={composedMenu.items} issues={menuIssues} ctx={{ upload: Boolean(api.actions?.clientBlobUpload), settings: menuSettings, blocked: menuBlocked }}
            modify={modify} profiles={profiles} profilesError={profilesError} onLoadProfiles={loadProfiles} usageFeature={usageCatalog.feature} waiting={Object.keys(waitingTurn).length > 0}
            onNode={(focus) => onShowInGraph?.(focus, [])} onPreview={() => send("preview")}
            appliedText={modify.answer?.kind === "applied" ? modifySentence(modify.answer, pendingParts(modify.answer.waiting, receiptT, id => profiles?.find(p => p.id === id)?.label ?? id).join(", "), receiptT) : ""}
            flags={menuFlags} switches={switches} model={shownModel} models={models}
            onAttach={(kind) => (kind === "camera" ? cameraInputRef : fileInputRef).current?.click()}
            onFlag={toggleFlag} onSwitch={toggleSwitch} onModel={chooseModel} onPanel={(panel) => requestPanel(panel)} onClose={closeMenu} />}
          <input ref={fileInputRef} type="file" multiple hidden data-testid="composer-file-input" onChange={(e) => { if (e.target.files) addFiles(e.target.files); e.target.value = ""; }} />
          <input ref={cameraInputRef} type="file" accept="image/*" capture="environment" hidden data-testid="composer-camera-input" onChange={(e) => { if (e.target.files) addFiles(e.target.files); e.target.value = ""; }} />
        </span>}
        {chat && (attachments.length > 0 || switchesOn.length > 0 || useKnowledge || !includeMemory) && <div className="composer-chips" role="list" aria-label={t("att_list")} data-testid="composer-chips">
          {attachments.map(a => <span key={a.key} role="listitem" className="composer-chip" data-testid="attachment-chip" data-state={a.state} data-name={a.name}>
            {a.preview && <img src={a.preview} alt={t("att_image", { name: a.name })} data-testid="attachment-preview" />}
            <span className="chip-name" title={a.state === "failed" ? t("att_failed", { name: a.name, reason: a.reason ?? "" }) : a.name}>
              {a.state === "uploading" ? t("att_uploading", { name: a.name }) : a.state === "failed" ? t("att_failed", { name: a.name, reason: a.reason ?? "" }) : a.name}</span>
            <button type="button" aria-label={t("att_remove", { name: a.name })} data-testid="attachment-remove" onClick={() => removeAttachment(a.key)}>×</button>
          </span>)}
          {switchesOn.map(item => { const name = t("tool_chip_on", { name: labelText(item.label, t, shellLanguage, item.id) });
            return <span key={item.id} role="listitem" className="composer-chip" data-testid={`tool-chip-${item.id}`}>
              <span className="chip-name">{name}</span>
              <button type="button" aria-label={t("tool_chip_remove", { name })} onClick={() => toggleSwitch(item.id)}>×</button></span>; })}
          {([["knowledge", useKnowledge, "tool_chip_knowledge", () => setUseKnowledge(false)], ["memory_off", !includeMemory, "tool_chip_memory_off", () => setIncludeMemory(true)]] as const)
            .filter(([, on]) => on).map(([id, , word, off]) => <span key={id} role="listitem" className="composer-chip" data-testid={`tool-chip-${id}`}>
              <span className="chip-name">{t(word)}</span>
              <button type="button" aria-label={t("tool_chip_remove", { name: t(word) })} onClick={off}>×</button></span>)}
        </div>}
        {chat && attachments.length > 0 && <p className="composer-note composer-hint" data-testid="attachment-scope">{t("att_scope")}</p>}
        {chat && filesWithAction && actionSwitch && <p className="composer-note composer-hint" role="status" data-testid="attachment-action-note">
          {t("cm_action_no_files", { name: labelText(actionSwitch.label, t, shellLanguage, actionSwitch.id) })}</p>}
        {(() => { const picker = <select
          value={shownModel}
          onChange={(e) => chooseModel(e.target.value)}
          style={chat ? { width: "auto", maxWidth: "38vw", borderRadius: 999, minHeight: 32, fontSize: 12, padding: "2px 8px" } : { width: 150, flexShrink: 0 }}
          aria-label={t("model_label")}
          data-testid="model-picker"
        >
          <option value="">{chat ? t("cm_model_default") : "default model"}</option>
          {models.map((m) => (
            <option key={m.id} value={m.id} title={m.id}>
              {chat ? shortModelName(m.id) : m.name ?? m.id}
            </option>
          ))}
          {/* A model the list does not have (a conversation's own, the person's pick from earlier) is still shown as what it is. */}
          {shownModel !== "" && !models.some(m => m.id === shownModel) && <option value={shownModel} title={shownModel}>{chat ? shortModelName(shownModel) : shownModel}</option>}
        </select>;
          if (interviewConv) return null;   // a form without a model: no model to choose, no price to show
          if (!chat) return picker;
          // The chip: the short name (the select shows it) and an info button that opens the sheet of where the model comes from.
          const chip = <span className="model-chip-wrap" data-testid="model-chip" data-model={shownModel || undefined}>{picker}
            <button type="button" className="model-chip-info" data-testid="model-provenance-open" aria-label={t("mp_open")} title={t("mp_open")} aria-haspopup="dialog" onClick={() => setModelSheet(true)}>ⓘ</button></span>;
          const slot = document.getElementById("shell-model-slot");
          return slot ? createPortal(chip, slot) : null; })()}
        {chat && modelSheet && <ModelProvenance convId={convId ?? null} model={shownModel} models={models} onClose={() => setModelSheet(false)}
          source={((): ModelSource => shownModel === "" ? "chain" : convId !== null && ownModel?.convId === convId ? "conversation" : "window")()} />}
        {modelNote && <div className="pill composer-note" role="status" data-testid="composer-model-note" style={{ width: "100%" }}>{modelNote}</div>}
        {routeNotes.length > 0 && <div className="composer-note" style={{ width: "100%" }}>
          {routeNotes.map((note, i) => <div className="pill" role="status" key={i} data-testid="chat-route-note">{note}</div>)}
        </div>}
        {(usagePending || usageNotice) && <div className="composer-note" style={{ width: "100%" }}>
          {usagePending && <UsagePendingBlock testId="chat-usage-pending" feature={usageCatalog.feature} receipt={usagePending.receipt}
            onConfirm={confirmUsage} onReject={rejectUsage} />}
          {usageNotice && <div className="pill" data-testid="chat-usage-notice">{usageNotice}</div>}
        </div>}
        <textarea
          value={input}
          placeholder={chat ? t("composer_placeholder") : profile?.composer.placeholder ?? "Message Loom…"}
          rows={1}
          data-testid="chat-input"
          onChange={(e) => { setInput(e.target.value); setQueuedNote((note) => (note === "failed" ? null : note)); }}
          onKeyDown={(e) => {
            if (supports("chat.send") && shouldSubmit(profile?.composer.submit ?? "enter", { key: e.key, shiftKey: e.shiftKey,
              ctrlKey: e.ctrlKey, metaKey: e.metaKey, altKey: e.altKey, isComposing: e.nativeEvent.isComposing })) {
              e.preventDefault();
              send();
            }
          }}
        />
        {chat && <MicButton onText={(x) => setInput(c => c ? c + " " + x : x)} />}
        {stream ? (
          <button className="danger" onClick={cancelStreaming} data-testid="cancel-chat">
            {chat ? t("stop") : "Stop"}
          </button>
        ) : (
          <button className="primary" onClick={() => send()} data-testid="send-chat" aria-busy={queuedNote === "waiting" || undefined}
            disabled={!!branchLeaf || (Boolean(gate.blocked) && gate.blocked?.reason !== VIEW_LOADING_REASON) || (requestOverride === null && !input.trim() && !attachments.some(a => a.state === "ready")) || attachments.some(a => a.state === "uploading") || filesWithAction || !supports("chat.send") || queuedNote === "waiting"}
            title={gateTexts.blocked ?? undefined} aria-describedby={gate.blocked ? gateDescriptionId : undefined}
            data-gate-initiator={gate.blocked?.initiator} data-gate-reason={gate.blocked?.reason}>
            {chat ? t("send") : "Send"}
          </button>
        )}
      </div>
    </div>
  );
}
