import { useCallback, useEffect, useRef, useState } from "react";
import { api } from "../api";
import type { Conversation } from "../api/types";

interface Props {
  activeConvId: string | null;
  onSelect: (id: string) => void;
  onCreated: (id: string) => void;
  onActivity?: (delta: number) => void;
}

export default function ConversationList({ activeConvId, onSelect, onCreated, onActivity }: Props) {
  const [conversations, setConversations] = useState<Conversation[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const mounted = useRef(true);
  const refreshGeneration = useRef(0);
  useEffect(() => { mounted.current = true; return () => { mounted.current = false; }; }, []);

  const refresh = useCallback(async () => {
    const generation = ++refreshGeneration.current;
    if (!mounted.current) return;
    setLoading(true);
    try {
      const list = await api.listConversations(100);
      if (!mounted.current || generation !== refreshGeneration.current) return;
      setConversations(list);
      setError(null);
    } catch (err) {
      if (mounted.current && generation === refreshGeneration.current) setError(err instanceof Error ? err.message : String(err));
    } finally {
      if (mounted.current && generation === refreshGeneration.current) setLoading(false);
    }
  }, []);

  useEffect(() => {
    refresh();
  }, [refresh]);

  const createConversation = useCallback(async () => {
    onActivity?.(1);
    try {
      const conv = await api.createConversation();
      await refresh();
      if (mounted.current) onCreated(conv.id);
    } catch (err) {
      if (mounted.current) setError(err instanceof Error ? err.message : String(err));
    } finally { onActivity?.(-1); }
  }, [refresh, onCreated, onActivity]);

  const deleteConversation = useCallback(
    async (id: string, ev: React.MouseEvent) => {
      ev.stopPropagation();
      if (!confirm("Delete this conversation?")) return;
      onActivity?.(1);
      try {
        await api.deleteConversation(id);
        await refresh();
      } catch (err) {
        if (mounted.current) setError(err instanceof Error ? err.message : String(err));
      } finally { onActivity?.(-1); }
    },
    [refresh, onActivity],
  );

  return (
    <div>
      <div style={{ padding: "10px 16px", borderBottom: "1px solid var(--border)" }}>
        <button className="primary" style={{ width: "100%" }} data-testid="new-conversation" onClick={createConversation}>
          + New conversation
        </button>
      </div>
      {error && <div className="empty-state" style={{ color: "var(--err)" }}>{error}</div>}
      {loading ? (
        <div className="empty-state">Loading…</div>
      ) : conversations.length === 0 ? (
        <div className="empty-state">No conversations yet.</div>
      ) : (
        conversations.map((c) => (
          <div
            key={c.id}
            className={`conv-item${c.id === activeConvId ? " active" : ""}`}
            onClick={() => onSelect(c.id)}
            data-testid="conv-item"
          >
            <div className="title">{c.title || "Untitled"}</div>
            <div className="sub">
              {new Date(c.updated).toLocaleString()}
              <button
                className="icon-btn"
                style={{ float: "right", width: 22, height: 22, padding: 0, fontSize: 12 }}
                onClick={(e) => deleteConversation(c.id, e)}
                aria-label="Delete conversation"
              >
                ×
              </button>
            </div>
          </div>
        ))
      )}
    </div>
  );
}
