import { useState } from "react";
import type { ProfileRegistry } from "../profiles/runtime";
import {
  loadWorkflowSnapshots, makeWorkflowSnapshot, parseWorkflowSnapshot, reconcileWorkflowSnapshot,
  saveWorkflowSnapshot, workflowSnapshotCanContinue,
} from "../profiles/workflow-snapshot";
import type { WorkflowSnapshot, WorkflowSnapshotApi, WorkflowSnapshotDraft } from "../profiles/workflow-snapshot";

export interface WorkflowRecoveryPanelProps {
  api: WorkflowSnapshotApi;
  registry: ProfileRegistry;
  getValue: () => WorkflowSnapshotDraft;
  /** Host applies navigation/session state only; unresolved/abandoned sessions must remain blocked. */
  onRestore: (snapshot: WorkflowSnapshot) => void | Promise<void>;
}
const errorText = (error: unknown) => error instanceof Error ? error.message : String(error);

/** Explicit persistence and read-only recovery; this component has no operation dispatcher. */
export default function WorkflowRecoveryPanel({ api, registry, getValue, onRestore }: WorkflowRecoveryPanelProps) {
  const [label, setLabel] = useState("Workflow session");
  const [snapshots, setSnapshots] = useState<WorkflowSnapshot[]>([]);
  const [selectedId, setSelectedId] = useState("");
  const [staged, setStaged] = useState<WorkflowSnapshot | null>(null);
  const [retained, setRetained] = useState<WorkflowSnapshot | null>(null);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");
  const selected = staged ?? snapshots.find(snapshot => snapshot.id === selectedId) ?? null;

  async function save(candidate?: WorkflowSnapshot) {
    setBusy(true); setMessage(""); setError("");
    try {
      const snapshot = candidate ?? makeWorkflowSnapshot(getValue(), registry, { id: crypto.randomUUID(), label: label.trim() });
      // Keep the complete local source/trace even when transport or readback fails.
      setRetained(snapshot);
      const saved = await saveWorkflowSnapshot(api, snapshot, registry);
      setSnapshots(previous => [...previous.filter(item => item.id !== saved.id), saved]);
      setSelectedId(saved.id); setStaged(null);
      setMessage("Session snapshot saved and read back. Concurrent writers can still overwrite it; there is no compare-and-swap.");
    } catch (err) { setError(errorText(err)); }
    finally { setBusy(false); }
  }
  async function load() {
    setBusy(true); setMessage(""); setError("");
    try {
      const saved = await loadWorkflowSnapshots(api, registry);
      setSnapshots(saved); setStaged(null);
      setSelectedId(previous => saved.some(item => item.id === previous) ? previous : saved[0]?.id ?? "");
      setMessage(saved.length ? `Read ${saved.length} session snapshot(s). Choose one to inspect or restore.` : "No saved workflow sessions.");
    } catch (err) { setError(`${errorText(err)} Local state and server source bytes were preserved.`); }
    finally { setBusy(false); }
  }
  async function restore() {
    if (!selected) return;
    setBusy(true); setMessage(""); setError("");
    try {
      const checked = parseWorkflowSnapshot(selected, registry);
      setRetained(checked);
      await onRestore(checked);
      setMessage(checked.sessions.some(entry => !workflowSnapshotCanContinue(entry))
        ? "Navigation restored. Unresolved or abandoned workflows stay blocked; no operation was dispatched."
        : "Session restored without dispatching or retrying an operation.");
    } catch (err) { setError(errorText(err)); }
    finally { setBusy(false); }
  }
  function reconcile(viewId: string, profile: string, choice: "verified_not_applied" | "abandon") {
    if (!selected) return;
    setError(""); setMessage("");
    try {
      const updated = reconcileWorkflowSnapshot(selected, viewId, profile, choice, registry);
      setStaged(updated); setRetained(updated);
      setMessage("Recovery choice staged. Save the inspected snapshot to retain the decision, then restore it to apply locally. Saved state and trace have not advanced.");
    } catch (err) { setError(errorText(err)); }
  }
  function download() {
    if (!retained) return;
    const url = URL.createObjectURL(new Blob([JSON.stringify(retained, null, 2)], { type: "application/json" }));
    const anchor = document.createElement("a"); anchor.href = url; anchor.download = `workflow-session-${retained.id.replace(/[^a-zA-Z0-9_-]/g, "_")}.json`;
    anchor.click(); URL.revokeObjectURL(url);
  }
  return <details className="profile-workflow-recovery" data-testid="workflow-recovery-panel">
    <summary>Workflow session snapshots</summary>
    <p>Save definitions, source text, views, validated state, variables and trace on this server.
      Remote execution checkpoints and exactly-once delivery are unavailable. Restoration never dispatches an action.</p>
    <label>Snapshot label <input aria-label="Workflow snapshot label" value={label} disabled={busy} onChange={event => setLabel(event.target.value)} /></label>
    <button data-testid="save-workflow-snapshot" disabled={busy || !label.trim()} onClick={() => void save()}>Save current session snapshot</button>
    <button data-testid="load-workflow-snapshots" disabled={busy} onClick={() => void load()}>Read saved snapshots</button>
    {snapshots.length > 0 && <label>Saved snapshot <select aria-label="Saved workflow snapshot" value={selectedId} disabled={busy}
      onChange={event => { setSelectedId(event.target.value); setStaged(null); setMessage(""); setError(""); }}>
      <option value="">Choose a snapshot</option>
      {snapshots.map(snapshot => <option key={snapshot.id} value={snapshot.id}>{snapshot.label} · {snapshot.savedAt}</option>)}
    </select></label>}
    {selected && <div className="profile-workflow-snapshot-inspector">
      <p>{selected.views.length} view(s), {selected.profiles.length} profile revision(s), {selected.sessions.length} workflow session(s).</p>
      <button data-testid="restore-workflow-snapshot" disabled={busy} onClick={() => void restore()}>Restore session/navigation</button>
      {staged && <button disabled={busy} onClick={() => void save(staged)}>Save inspected recovery decision</button>}
      {selected.sessions.filter(entry => !workflowSnapshotCanContinue(entry)).map(entry => <fieldset key={JSON.stringify([entry.viewId, entry.profile])}>
        <legend>{entry.viewId}: {entry.recovery.status === "abandoned" ? "abandoned operation — workflow blocked" : "remote outcome unknown — workflow blocked"}</legend>
        <p>Pending action: {entry.recovery.pending?.action}. Inspect the remote conversation or operation receipt before making a choice.
          A saved local trace cannot prove whether remote effects happened.</p>
        {entry.recovery.status === "unknown" && <>
          <button data-testid="reconcile-workflow-not-applied" disabled={busy} onClick={() => reconcile(entry.viewId, entry.profile, "verified_not_applied")}>I verified the operation was not applied</button>
          <button data-testid="abandon-workflow-operation" disabled={busy} onClick={() => reconcile(entry.viewId, entry.profile, "abandon")}>Abandon pending operation; keep workflow blocked</button>
        </>}
      </fieldset>)}
      <details><summary>Inspect source and workflow snapshot JSON</summary><pre>{JSON.stringify(selected, null, 2)}</pre></details>
    </div>}
    {retained && <button disabled={busy} onClick={download}>Download retained local snapshot/source</button>}
    {message && <p role="status">{message}</p>}
    {error && <p role="alert" className="profile-error">{error}</p>}
  </details>;
}
