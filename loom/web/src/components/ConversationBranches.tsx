import { useMemo, useState } from "react";
import type { Message, MessageStatus } from "../api/types";
import "./conversation-branches.css";

export interface BranchPath {
  messages: Message[];
  missingParent: string | null;
  cycleAt: string | null;
}
export interface BranchRow {
  message: Message;
  depth: number;
  children: number;
  missingParent: string | null;
  disconnected: boolean;
}
export interface BranchIndex {
  rows: BranchRow[];
  duplicateIds: string[];
  cycleEdges: Array<{ parent: string; child: string }>;
}

/** Native parent IDs only. Source-provider keys are preserved metadata, not guessed edges. */
export function conversationBranchPath(messages: Message[], id: string): BranchPath {
  const byId = new Map<string, Message>();
  messages.forEach(message => { if (!byId.has(message.id)) byId.set(message.id, message); });
  const path: Message[] = [];
  const seen = new Set<string>();
  let current: string | null = id;
  let missingParent: string | null = null;
  let cycleAt: string | null = null;
  while (current) {
    if (seen.has(current)) { cycleAt = current; break; }
    seen.add(current);
    const message = byId.get(current);
    if (!message) { missingParent = current; break; }
    path.push(message);
    current = message.parent_id ?? null;
  }
  return { messages: path.reverse(), missingParent, cycleAt };
}

/** Iterative traversal keeps arbitrary-depth descendants accessible without recursive rendering. */
export function indexConversationBranches(messages: Message[]): BranchIndex {
  const byId = new Map<string, Message>();
  const duplicateIds: string[] = [];
  for (const message of messages) {
    if (byId.has(message.id)) duplicateIds.push(message.id);
    else byId.set(message.id, message);
  }
  const children = new Map<string, Message[]>();
  const roots: Message[] = [];
  for (const message of byId.values()) {
    if (message.parent_id && byId.has(message.parent_id)) {
      const group = children.get(message.parent_id) ?? [];
      group.push(message); children.set(message.parent_id, group);
    } else roots.push(message);
  }
  const rows: BranchRow[] = [];
  const seen = new Set<string>();
  const cycleEdges: BranchIndex["cycleEdges"] = [];
  const walk = (root: Message, disconnected: boolean) => {
    const pending = [{ message: root, depth: 0 }];
    while (pending.length) {
      const entry = pending.pop()!;
      if (seen.has(entry.message.id)) continue;
      seen.add(entry.message.id);
      const descendants = children.get(entry.message.id) ?? [];
      rows.push({ ...entry, children: descendants.length, disconnected,
        missingParent: entry.message.parent_id && !byId.has(entry.message.parent_id) ? entry.message.parent_id : null });
      for (let i = descendants.length - 1; i >= 0; i--) {
        const child = descendants[i];
        if (seen.has(child.id)) cycleEdges.push({ parent: entry.message.id, child: child.id });
        else pending.push({ message: child, depth: entry.depth + 1 });
      }
    }
  };
  roots.forEach(root => walk(root, false));
  // Rootless components (cycles) remain navigable; never silently omit retained rows.
  byId.forEach(message => { if (!seen.has(message.id)) walk(message, true); });
  return { rows, duplicateIds, cycleEdges };
}

export interface ConversationBranchesProps {
  messages: Message[];
  selectedId?: string | null;
  onSelect: (message: Message, path: Message[], diagnostics: Omit<BranchPath, "messages">) => void;
  initialStatuses?: MessageStatus[];
}

/** Read-only navigation. Selecting a descendant never restores versions or changes chat context. */
export default function ConversationBranches({ messages, selectedId, onSelect, initialStatuses }: ConversationBranchesProps) {
  const index = useMemo(() => indexConversationBranches(messages), [messages]);
  const [statuses, setStatuses] = useState(new Set<MessageStatus>(initialStatuses ?? ["active", "excluded", "version", "deleted"]));
  const [query, setQuery] = useState("");
  const rows = index.rows.filter(row => statuses.has(row.message.status) && (!query || `${row.message.text} ${row.message.id} ${row.message.role}`.toLocaleLowerCase().includes(query.toLocaleLowerCase())));
  const select = (message: Message) => {
    const path = conversationBranchPath(messages, message.id);
    onSelect(message, path.messages, { missingParent: path.missingParent, cycleAt: path.cycleAt });
  };
  const selected = selectedId ? conversationBranchPath(messages, selectedId) : null;
  return <nav className="conversation-branches" aria-label="Conversation branches" data-testid="conversation-branches">
    <details>
      <summary>Conversation branches · {index.rows.length} retained messages</summary>
      <p>Browse complete descendant paths. This changes the displayed selection only; message status and request context stay under their existing controls.</p>
      <div className="conversation-branch-controls">
        {(["active", "excluded", "version", "deleted"] as const).map(status => <label key={status}>
          <input type="checkbox" checked={statuses.has(status)} onChange={event => setStatuses(current => {
            const next = new Set(current); if (event.target.checked) next.add(status); else next.delete(status); return next;
          })} />{status}
        </label>)}
        <label>Find message <input value={query} onChange={event => setQuery(event.target.value)} placeholder="Text, role or ID" /></label>
      </div>
      {(index.duplicateIds.length > 0 || index.cycleEdges.length > 0 || index.rows.some(row => row.missingParent)) && <p role="status">Incomplete topology: {index.rows.filter(row => row.missingParent).length} missing parents, {index.cycleEdges.length} cycle edges, {index.duplicateIds.length} duplicate IDs. Source rows remain inspectable.</p>}
      <p>{rows.length} / {index.rows.length} messages shown</p>
      {selected && selected.messages.length > 0 && <div className="conversation-branch-path" aria-label="Selected ancestor path">
        {selected.messages.map(message => <button type="button" key={message.id} onClick={() => select(message)} aria-current={message.id === selectedId ? "location" : undefined}>{message.role} · {message.id}</button>)}
        {selected.missingParent && <span>Missing parent: {selected.missingParent}</span>}
        {selected.cycleAt && <span>Cycle at: {selected.cycleAt}</span>}
      </div>}
      <ol className="conversation-branch-list">
        {rows.map(({ message, depth, children, missingParent, disconnected }) => <li key={message.id} data-message-id={message.id} data-depth={depth}>
          <button type="button" onClick={() => select(message)} aria-current={message.id === selectedId ? "location" : undefined}>
            <span className="conversation-branch-position">Depth {depth} · {children} children</span>
            <span>{message.role} · {message.status}{message.version_num !== undefined && ` · version ${message.version_num}`}</span>
            <span className="conversation-branch-message">{message.text || "(No message text)"}</span>
            <code>{message.id}</code>
            {missingParent && <span>Missing parent: {missingParent}</span>}
            {disconnected && <span>Rootless component</span>}
          </button>
        </li>)}
      </ol>
      {!rows.length && <p>No retained messages match these display filters.</p>}
    </details>
  </nav>;
}
