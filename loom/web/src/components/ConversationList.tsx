import { useCallback, useEffect, useState } from "react";
import { api } from "../api";
import type { Conversation } from "../api/types";

interface Props {
  activeConvId: string | null;
  onSelect: (id: string) => void;
  onCreated: (id: string) => void;
}

export default function ConversationList({ activeConvId, onSelect, onCreated }: Props) {
  const [conversations, setConversations] = useState<Conversation[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const list = await api.listConversations(100);
      setConversations(list);
      setError(null);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    refresh();
  }, [refresh]);

  const createConversation = useCallback(async () => {
    try {
      const conv = await api.createConversation();
      await refresh();
      onCreated(conv.id);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, [refresh, onCreated]);

  const deleteConversation = useCallback(
    async (id: string, ev: React.MouseEvent) => {
      ev.stopPropagation();
      if (!confirm("Delete this conversation?")) return;
      try {
        await api.deleteConversation(id);
        await refresh();
      } catch (err) {
        setError(err instanceof Error ? err.message : String(err));
      }
    },
    [refresh],
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
