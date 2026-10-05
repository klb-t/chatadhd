import { createProfileSession, type ApplicationProfile, type ProfileRegistry } from "./runtime";
import { makeWorkflowSnapshot, type WorkflowSnapshotSession } from "./workflow-snapshot";

function checked(entry: WorkflowSnapshotSession, profile: ApplicationProfile, registry: ProfileRegistry): WorkflowSnapshotSession {
  return makeWorkflowSnapshot({ profiles: [profile], sources: [{ profile: entry.profile, text: JSON.stringify(profile), sourceRef: "derived:checkpoint-validation" }],
    views: [{ id: entry.viewId, profile: entry.profile, conversation: { coupling: "coupled", selectedId: null, sidebarOpen: false } }],
    sessions: [entry], sharedConversationId: null }, registry, { id: "checkpoint-validation", label: "Checkpoint validation" }).sessions[0];
}

/** One localStorage write binds pre/post state and outcome atomically. */
export function writeWorkflowCheckpoint(key: string, entry: WorkflowSnapshotSession, profile: ApplicationProfile, registry: ProfileRegistry): void {
  const value = checked(entry, profile, registry);
  localStorage.setItem(`${key}:checkpoint`, JSON.stringify({ schema: "loom.workflow_checkpoint/1", entry: value }));
}

/** Validate the entire restore before writing; retain prior bytes if a write fails. */
export function writeWorkflowCheckpoints(entries: { key: string; entry: WorkflowSnapshotSession; profile: ApplicationProfile }[], registry: ProfileRegistry): void {
  const prepared = entries.map(({ key, entry, profile }) => ({ key: `${key}:checkpoint`,
    value: JSON.stringify({ schema: "loom.workflow_checkpoint/1", entry: checked(entry, profile, registry) }) }));
  if (new Set(prepared.map(value => value.key)).size !== prepared.length) throw Error("Duplicate workflow checkpoint identity.");
  const originals = prepared.map(value => ({ key: value.key, value: localStorage.getItem(value.key) }));
  let written = 0;
  try {
    for (const value of prepared) { localStorage.setItem(value.key, value.value); written++; }
  } catch (error) {
    try {
      for (const original of originals.slice(0, written)) {
        if (original.value === null) localStorage.removeItem(original.key);
        else localStorage.setItem(original.key, original.value);
      }
    } catch {
      throw Error("Workflow restore failed and prior checkpoints could not all be restored. Inspect stored sessions before continuing.");
    }
    throw error;
  }
}

/** Earlier successful-session records remain readable; malformed bytes stay untouched. */
export function readWorkflowCheckpoint(key: string, viewId: string, profile: ApplicationProfile, registry: ProfileRegistry): WorkflowSnapshotSession {
  const raw = localStorage.getItem(`${key}:checkpoint`);
  if (raw !== null) {
    const value = JSON.parse(raw);
    if (value.schema !== "loom.workflow_checkpoint/1" || value.entry?.viewId !== viewId || value.entry?.profile !== JSON.stringify([profile.id, profile.profile_revision])) throw Error("Invalid stored workflow checkpoint; original bytes preserved.");
    return checked(value.entry, profile, registry);
  }
  return { viewId, profile: JSON.stringify([profile.id, profile.profile_revision]),
    session: createProfileSession(profile, registry, localStorage.getItem(key) ?? undefined), recovery: { status: "ready" } };
}
