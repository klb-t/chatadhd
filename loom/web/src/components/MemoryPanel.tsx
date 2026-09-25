import { useCallback, useEffect, useState } from "react";
import { api } from "../api";
import type { MemoryNode } from "../api/types";

export default function MemoryPanel() {
  const [nodes, setNodes] = useState<MemoryNode[]>([]);
  const [newContent, setNewContent] = useState("");
  const [newParent, setNewParent] = useState("");
  const [editingId, setEditingId] = useState<string | null>(null);
  const [editContent, setEditContent] = useState("");
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(() => {
    api
      .listMemory()
      .then(setNodes)
      .catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, []);

  useEffect(refresh, [refresh]);

  const add = useCallback(async () => {
    if (!newContent.trim()) return;
    try {
      await api.createMemory({ content: newContent, parent_id: newParent || undefined, node_type: "text" });
      setNewContent("");
      refresh();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, [newContent, newParent, refresh]);

  const toggleActive = useCallback(
    async (n: MemoryNode) => {
      await api.updateMemory(n.id, { active: !n.active });
      refresh();
    },
    [refresh],
  );

  const remove = useCallback(
    async (n: MemoryNode) => {
      if (!confirm(`Delete "${n.content.slice(0, 40)}" and its descendants?`)) return;
      await api.deleteMemory(n.id);
      refresh();
    },
    [refresh],
  );

  const save = useCallback(
    async (id: string) => {
      await api.updateMemory(id, { content: editContent });
      setEditingId(null);
      refresh();
    },
    [editContent, refresh],
  );

  return (
    <div data-testid="memory-panel">
      <div className="section-title">Add node</div>
      <div className="form-row">
        <textarea
          rows={2}
          placeholder="Memory content…"
          value={newContent}
          onChange={(e) => setNewContent(e.target.value)}
          data-testid="memory-new-content"
        />
      </div>
      <div className="form-row">
        <label htmlFor="memory-parent">Parent (optional)</label>
        <select id="memory-parent" value={newParent} onChange={(e) => setNewParent(e.target.value)}>
          <option value="">(root)</option>
          {nodes.map((n) => (
            <option key={n.id} value={n.id}>
              {"— ".repeat(n.depth)}
              {n.content.slice(0, 30)}
            </option>
          ))}
        </select>
      </div>
      <button className="primary" onClick={add} data-testid="memory-add">
        Add
      </button>

      {error && <div className="empty-state" style={{ color: "var(--err)" }}>{error}</div>}

      <div className="section-title">Tree</div>
      {nodes.length === 0 && <div className="empty-state">No memory nodes yet.</div>}
      {nodes.map((n) => (
        <div key={n.id} style={{ paddingLeft: n.depth * 14, borderBottom: "1px solid var(--border)", padding: "6px 0" }} data-testid="memory-node">
          {editingId === n.id ? (
            <div>
              <textarea rows={2} value={editContent} onChange={(e) => setEditContent(e.target.value)} />
              <div className="actions">
                <button className="primary" onClick={() => save(n.id)}>
                  Save
                </button>
                <button onClick={() => setEditingId(null)}>Cancel</button>
              </div>
            </div>
          ) : (
            <>
              <div style={{ fontSize: 13, opacity: n.active ? 1 : 0.5 }}>{n.content}</div>
              <div className="actions">
                <button
                  onClick={() => {
                    setEditingId(n.id);
                    setEditContent(n.content);
                  }}
                >
                  Edit
                </button>
                <button onClick={() => toggleActive(n)} data-testid="memory-toggle-active">
                  {n.active ? "Deactivate" : "Activate"}
                </button>
                <button className="danger" onClick={() => remove(n)}>
                  Delete
                </button>
              </div>
            </>
          )}
        </div>
      ))}
    </div>
  );
}
