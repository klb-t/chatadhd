import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { marked } from "marked";
import { api } from "../api";
import type { ChatChunk, Message, ModelInfo } from "../api/types";

marked.setOptions({ breaks: true });

function renderMarkdown(text: string): { __html: string } {
  try {
    return { __html: marked.parse(text, { async: false }) as string };
  } catch {
    return { __html: text.replace(/</g, "&lt;") };
  }
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
  const scrollRef = useRef<HTMLDivElement>(null);
  const unsubRef = useRef<(() => void) | null>(null);

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
    setInput("");
    setPendingUserText(text);
    setStream({ reasoning: "", text: "", requestId: null });
    setError(null);

    const unsub = api.chat(
      { message: text, conv_id: convId ?? undefined, model: model || undefined },
      {
        onChunk: (chunk: ChatChunk) => {
          if (chunk.type === "start") {
            setStream((s) => (s ? { ...s, requestId: chunk.request_id } : s));
            if (!convId) onConversationCreated(chunk.conv_id);
          } else if (chunk.type === "reasoning") {
            setStream((s) => (s ? { ...s, reasoning: s.reasoning + chunk.text } : s));
          } else if (chunk.type === "delta") {
            setStream((s) => (s ? { ...s, text: s.text + chunk.text } : s));
          } else if (chunk.type === "done") {
            setStream(null);
            setPendingUserText(null);
            refreshMessages(chunk.conv_id);
          } else if (chunk.type === "error") {
            setStream(null);
            setPendingUserText(null);
            setError(chunk.message);
          }
        },
        onError: (msg) => {
          setStream(null);
          setPendingUserText(null);
          setError(msg);
        },
      },
    );
    unsubRef.current = unsub;
  }, [input, stream, convId, model, onConversationCreated, refreshMessages]);

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
            </div>
          );
        })}
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
