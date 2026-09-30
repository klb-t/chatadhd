import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { marked } from "marked";
import DOMPurify from "dompurify";
import { api } from "../api";
import type { ChatChunk, ChatContextTrace, ChatKnowledgeContextRequest, ChatRequest, Message, ModelInfo } from "../api/types";
import "./chat-context.css";
import ContextPlanEditor from "./ContextPlanEditor";
import { buildRetrievalPlan, newPlan } from "../context/retrieval-plan";

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
  convId: string | null;
  onConversationCreated: (id: string) => void;
}

interface StreamState {
  reasoning: string;
  text: string;
  requestId: string | null;
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

export default function ChatView({ convId, onConversationCreated }: Props) {
  const [messages, setMessages] = useState<Message[]>([]);
  const [input, setInput] = useState("");
  const [models, setModels] = useState<ModelInfo[]>([]);
  const [model, setModel] = useState<string>("");
  const [pendingUserText, setPendingUserText] = useState<string | null>(null);
  const [stream, setStream] = useState<StreamState | null>(null);
  const [editingId, setEditingId] = useState<string | null>(null);
  const [editText, setEditText] = useState("");
  const [versionsById, setVersionsById] = useState<Record<string, Message[]>>({});
  const [error, setError] = useState<string | null>(null);
  const [useKnowledge, setUseKnowledge] = useState(false);
  const [contextQuery, setContextQuery] = useState("");
  const [contextProject, setContextProject] = useState("");
  const [contextTargets, setContextTargets] = useState("");
  const [contextRun, setContextRun] = useState("");
  const [contextLanguage, setContextLanguage] = useState("");
  const [contextBudget, setContextBudget] = useState("4000");
  const [contextHops, setContextHops] = useState("1");
  const [contextDetail, setContextDetail] = useState<NonNullable<ChatKnowledgeContextRequest["detail_resolution"]> | "auto">("auto");
  const [usePlan, setUsePlan] = useState(false);
  const [planDraft, setPlanDraft] = useState(newPlan);
  const [includeMemory, setIncludeMemory] = useState(true);
  const [includeGraphMemory, setIncludeGraphMemory] = useState(true);
  const [includeHistory, setIncludeHistory] = useState(true);
  const [traceContext, setTraceContext] = useState<"auto" | "on" | "off">("auto");
  const [lastTrace, setLastTrace] = useState<{ convId: string; userId: string | null; trace: ChatContextTrace } | null>(null);
  const scrollRef = useRef<HTMLDivElement>(null);
  const unsubRef = useRef<(() => void) | null>(null);
  const currentUserIdRef = useRef<string | null>(null);
  const currentConvIdRef = useRef<string | null>(null);

  useEffect(() => {
    api
      .getModels()
      .then((list) => setModels(list))
      .catch(() => setModels([]));
  }, []);

  const refreshMessages = useCallback(async (id: string) => {
    try {
      const list = await api.getMessages(id, false);
      setMessages(list);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, []);

  useEffect(() => {
    if (convId) refreshMessages(convId);
    else setMessages([]);
  }, [convId, refreshMessages]);

  useEffect(() => {
    scrollRef.current?.scrollTo({ top: scrollRef.current.scrollHeight, behavior: "smooth" });
  }, [messages, stream, pendingUserText]);

  useEffect(() => () => unsubRef.current?.(), []);

  const send = useCallback(() => {
    const text = input.trim();
    if (!text || stream) return;
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
    let plan: ChatKnowledgeContextRequest["plan"];
    if (useKnowledge && usePlan) {
      try { plan = buildRetrievalPlan(planDraft); }
      catch (err) { setError(err instanceof Error ? err.message : String(err)); return; }
    }
    const request: ChatRequest = {
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
      } }),
    };
    setInput("");
    currentUserIdRef.current = null;
    currentConvIdRef.current = convId;
    setPendingUserText(text);
    setStream({ reasoning: "", text: "", requestId: null });
    setError(null);

    const unsub = api.chat(
      request,
      {
        onChunk: (chunk: ChatChunk) => {
          if (chunk.type === "start") {
            currentUserIdRef.current = chunk.user_message_id;
            currentConvIdRef.current = chunk.conv_id;
            setStream((s) => (s ? { ...s, requestId: chunk.request_id } : s));
            if (!convId) onConversationCreated(chunk.conv_id);
          } else if (chunk.type === "reasoning") {
            setStream((s) => (s ? { ...s, reasoning: s.reasoning + chunk.text } : s));
          } else if (chunk.type === "delta") {
            setStream((s) => (s ? { ...s, text: s.text + chunk.text } : s));
          } else if (chunk.type === "done") {
            if (chunk.context_trace) setLastTrace({ convId: chunk.conv_id, userId: currentUserIdRef.current, trace: chunk.context_trace });
            setStream(null);
            setPendingUserText(null);
            refreshMessages(chunk.conv_id);
          } else if (chunk.type === "error") {
            setStream(null);
            setPendingUserText(null);
            setError(chunk.message);
            if (currentConvIdRef.current) refreshMessages(currentConvIdRef.current);
          }
        },
        onError: (msg) => {
          setStream(null);
          setPendingUserText(null);
          setError(msg);
          if (currentConvIdRef.current) refreshMessages(currentConvIdRef.current);
        },
      },
    );
    unsubRef.current = unsub;
  }, [input, stream, convId, model, onConversationCreated, refreshMessages,
    useKnowledge, contextQuery, contextProject, contextTargets, contextRun,
    contextLanguage, contextBudget, contextHops, contextDetail, usePlan, planDraft, includeMemory, includeGraphMemory, includeHistory, traceContext]);

  const cancelStreaming = useCallback(() => {
    if (stream?.requestId) api.cancelChat(stream.requestId).catch(() => {});
    unsubRef.current?.();
    setStream(null);
  }, [stream]);

  const startEdit = useCallback((m: Message) => {
    setEditingId(m.id);
    setEditText(m.text);
  }, []);

  const saveEdit = useCallback(
    async (id: string) => {
      try {
        await api.editMessage(id, editText);
        setEditingId(null);
        if (convId) await refreshMessages(convId);
      } catch (err) {
        setError(err instanceof Error ? err.message : String(err));
      }
    },
    [editText, convId, refreshMessages],
  );

  const toggleExclude = useCallback(
    async (m: Message) => {
      try {
        await api.setMessageStatus(m.id, m.status === "excluded" ? "active" : "excluded");
        if (convId) await refreshMessages(convId);
      } catch (err) {
        setError(err instanceof Error ? err.message : String(err));
      }
    },
    [convId, refreshMessages],
  );

  const loadVersions = useCallback(
    async (m: Message) => {
      if (!m.version_group_id) return;
      try {
        const vs = await api.getVersions(m.version_group_id);
        setVersionsById((prev) => ({ ...prev, [m.version_group_id as string]: vs }));
      } catch (err) {
        setError(err instanceof Error ? err.message : String(err));
      }
    },
    [],
  );

  const switchVersion = useCallback(
    async (targetId: string) => {
      try {
        await api.restoreVersion(targetId);
        if (convId) await refreshMessages(convId);
      } catch (err) {
        setError(err instanceof Error ? err.message : String(err));
      }
    },
    [convId, refreshMessages],
  );

  const visibleMessages = useMemo(() => messages, [messages]);

  return (
    <div className="chat-view" data-testid="chat-view">
      <div className="chat-scroll" ref={scrollRef}>
        {visibleMessages.length === 0 && !pendingUserText && !stream && (
          <div className="empty-state">Say something to start the conversation.</div>
        )}
        {visibleMessages.map((m) => {
          const versions = m.version_group_id ? versionsById[m.version_group_id] : undefined;
          const trace = recordedContext(m.metadata?.context_trace);
          return (
            <div key={m.id} className={`msg ${m.role}`} data-testid="message" data-role={m.role} data-status={m.status}>
              <div className="meta">
                <span>{m.role}</span>
                {m.model && <span>· {m.model}</span>}
                {m.status === "excluded" && <span style={{ color: "var(--warn)" }}>excluded</span>}
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
                            disabled={v.id === m.id}
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
                    <button className="primary" onClick={() => saveEdit(m.id)} data-testid="save-edit">
                      Save (new version)
                    </button>
                    <button onClick={() => setEditingId(null)}>Cancel</button>
                  </div>
                </div>
              ) : (
                <div className="body" dangerouslySetInnerHTML={renderMarkdown(m.text)} />
              )}
              <div className="actions">
                {m.role === "user" && editingId !== m.id && (
                  <button onClick={() => startEdit(m)} data-testid="edit-message">
                    Edit
                  </button>
                )}
                <button onClick={() => toggleExclude(m)} data-testid="toggle-exclude">
                  {m.status === "excluded" ? "Restore" : "Exclude"}
                </button>
              </div>
              {trace && <ContextTrace trace={trace} />}
            </div>
          );
        })}
        {lastTrace && lastTrace.convId === convId && !visibleMessages.some((m) =>
          m.id === lastTrace.userId && recordedContext(m.metadata?.context_trace)) && (
          <ContextTrace trace={lastTrace.trace} />
        )}
        {pendingUserText && (
          <div className="msg user">
            <div className="meta">user</div>
            <div className="body">{pendingUserText}</div>
          </div>
        )}
        {stream && (
          <div className="msg assistant" data-testid="streaming-message">
            {stream.reasoning && (
              <details className="reasoning" open>
                <summary>Reasoning</summary>
                <div className="reasoning-text">{stream.reasoning}</div>
              </details>
            )}
            <div className="meta">assistant · streaming…</div>
            <div className="body" dangerouslySetInnerHTML={renderMarkdown(stream.text)} />
          </div>
        )}
      </div>

      {error && (
        <div className="empty-state" style={{ color: "var(--err)" }} data-testid="chat-error">
          {error}
        </div>
      )}

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
          placeholder="Message Loom…"
          rows={1}
          data-testid="chat-input"
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter" && !e.shiftKey) {
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
          <button className="primary" onClick={send} data-testid="send-chat" disabled={!input.trim()}>
            Send
          </button>
        )}
      </div>
    </div>
  );
}
