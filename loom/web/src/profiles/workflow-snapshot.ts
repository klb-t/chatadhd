import { parseApplicationProfileSource } from "./graph";
import { createProfileRegistry, parseProfileSession, registerProfile } from "./runtime";
import type { ApplicationProfile, ProfileJsonValue, ProfileRegistry, ProfileSession } from "./runtime";
import { readApplicationView } from "./view-state";
import type { ApplicationViewState } from "./view-state";

export const WORKFLOW_SNAPSHOT_SCHEMA = "loom.application_workflow_snapshot/1";
export const WORKFLOW_SNAPSHOT_STORE_SCHEMA = "loom.application_workflow_snapshots/1";
/** An open configuration key; writes replace this value as a whole. There is no CAS. */
export const WORKFLOW_SNAPSHOT_CONFIG_KEY = "application_workflow_snapshots_v1";
export interface WorkflowSnapshotSource { profile: string; text: string; sourceRef: string }
export interface WorkflowPendingOperation { action: string; workflowId?: string; event?: string; reference?: string }
export interface WorkflowRecovery {
  status: "ready" | "unknown" | "reconciled" | "abandoned";
  pending?: WorkflowPendingOperation;
  decision?: { choice: "verified_not_applied" | "abandon"; at: string };
}
export interface WorkflowSnapshotSession {
  viewId: string;
  profile: string;
  session: ProfileSession;
  /** Explicit host-selected JSON references; no request/model settings are captured implicitly. */
  references?: Record<string, ProfileJsonValue>;
  recovery: WorkflowRecovery;
}
export interface WorkflowSnapshotDraft {
  profiles: ApplicationProfile[];
  sources: WorkflowSnapshotSource[];
  views: ApplicationViewState[];
  /** Exactly one session for the profile currently selected in each view; historical sessions are not restored. */
  sessions: WorkflowSnapshotSession[];
  sharedConversationId: string | null;
}
export interface WorkflowSnapshot extends WorkflowSnapshotDraft {
  schema: typeof WORKFLOW_SNAPSHOT_SCHEMA;
  id: string;
  label: string;
  savedAt: string;
}
export interface WorkflowSnapshotStore {
  schema: typeof WORKFLOW_SNAPSHOT_STORE_SCHEMA;
  snapshots: WorkflowSnapshot[];
}
export interface WorkflowSnapshotApi {
  getConfig(): Promise<Record<string, unknown>>;
  setConfig(patch: Record<string, unknown>): Promise<Record<string, unknown>>;
}
const identity = (profile: ApplicationProfile) => JSON.stringify([profile.id, profile.profile_revision]);
function record(value: unknown, field: string, keys: string[], optional: string[] = []): asserts value is Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw Error(`${field} must be an object.`);
  const prototype = Object.getPrototypeOf(value);
  if (prototype !== Object.prototype && prototype !== null) throw Error(`${field} must be plain JSON data.`);
  for (const key of keys) if (!Object.prototype.hasOwnProperty.call(value, key)) throw Error(`${field}.${key} is missing.`);
  for (const key of Reflect.ownKeys(value)) {
    const descriptor = Object.getOwnPropertyDescriptor(value, key)!;
    if (typeof key !== "string" || ![...keys, ...optional].includes(key) || !("value" in descriptor) || !descriptor.enumerable) {
      throw Error(`${field} has an unknown or non-data field ${String(key)}.`);
    }
  }
}
function text(value: unknown, field: string): asserts value is string {
  if (typeof value !== "string" || !value.trim()) throw Error(`${field} must be a nonempty string.`);
}
function list(value: unknown, field: string): asserts value is unknown[] {
  if (!Array.isArray(value) || Object.getPrototypeOf(value) !== Array.prototype || Reflect.ownKeys(value).length !== value.length + 1) {
    throw Error(`${field} must be a dense JSON array.`);
  }
  for (let index = 0; index < value.length; index++) {
    const descriptor = Object.getOwnPropertyDescriptor(value, index);
    if (!descriptor || !("value" in descriptor) || !descriptor.enumerable) throw Error(`${field} must contain data items.`);
  }
}
function json(value: unknown, field: string, ancestors = new Set<object>()): void {
  if (typeof value === "string") {
    for (let index = 0; index < value.length; index++) {
      const unit = value.charCodeAt(index);
      if (unit >= 0xd800 && unit <= 0xdbff) {
        const next = value.charCodeAt(++index);
        if (!(next >= 0xdc00 && next <= 0xdfff)) throw Error(`${field} contains invalid Unicode and cannot preserve UTF-8 bytes.`);
      } else if (unit >= 0xdc00 && unit <= 0xdfff) throw Error(`${field} contains invalid Unicode and cannot preserve UTF-8 bytes.`);
    }
    return;
  }
  if (value === null || typeof value === "boolean" || (typeof value === "number" && Number.isFinite(value))) return;
  if (!value || typeof value !== "object" || ancestors.has(value)) throw Error(`${field} must be finite, acyclic JSON data.`);
  ancestors.add(value);
  try {
    if (Array.isArray(value)) { list(value, field); value.forEach((child, index) => json(child, `${field}[${index}]`, ancestors)); }
    else {
      const keys = Reflect.ownKeys(value);
      if (keys.some(key => typeof key !== "string")) throw Error(`${field} must have JSON field names.`);
      record(value, field, keys as string[]);
      for (const key of keys as string[]) { json(key, `${field} key`, ancestors); json((value as Record<string, unknown>)[key], `${field}.${key}`, ancestors); }
    }
  } finally { ancestors.delete(value); }
}
function canonical(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (value && typeof value === "object") return `{${Object.keys(value).sort().map(key => `${JSON.stringify(key)}:${canonical((value as Record<string, unknown>)[key])}`).join(",")}}`;
  return JSON.stringify(value);
}
function date(value: unknown, field: string): asserts value is string {
  text(value, field);
  if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$/.test(value) || !Number.isFinite(Date.parse(value)) ||
      new Date(value).toISOString().slice(0, 19) !== value.slice(0, 19)) throw Error(`${field} must be a valid UTC timestamp.`);
}
function clone<T>(value: T): T { return JSON.parse(JSON.stringify(value)) as T; }

/** Restore is validation only. It never invokes an operation adapter or retries work. */
export function parseWorkflowSnapshot(raw: unknown, registry: ProfileRegistry): WorkflowSnapshot {
  const value: unknown = typeof raw === "string" ? JSON.parse(raw) : raw;
  record(value, "snapshot", ["schema", "id", "label", "savedAt", "profiles", "sources", "views", "sessions", "sharedConversationId"]);
  if (value.schema !== WORKFLOW_SNAPSHOT_SCHEMA) throw Error("Unsupported workflow snapshot schema.");
  text(value.id, "snapshot.id"); text(value.label, "snapshot.label"); date(value.savedAt, "snapshot.savedAt");
  json(value, "snapshot");
  list(value.profiles, "snapshot.profiles"); list(value.sources, "snapshot.sources");
  list(value.views, "snapshot.views"); list(value.sessions, "snapshot.sessions");
  if (!value.views.length) throw Error("A workflow snapshot needs at least one application view.");
  if (value.sharedConversationId !== null) text(value.sharedConversationId, "snapshot.sharedConversationId");
  // Validate definitions/source data without changing the host registry first.
  const temporary = createProfileRegistry({ renderers: [...registry.renderers], operations: [...registry.operations] });
  const profiles = value.profiles.map(profile => registerProfile(temporary, profile));
  const profileMap = new Map(profiles.map(profile => [identity(profile), profile]));
  if (profileMap.size !== profiles.length) throw Error("Workflow snapshot repeats a profile identity.");
  const sourceProfiles = new Set<string>();
  for (const source of value.sources) {
    record(source, "snapshot.source", ["profile", "text", "sourceRef"]);
    text(source.profile, "source.profile"); text(source.text, "source.text"); text(source.sourceRef, "source.sourceRef");
    const profile = profileMap.get(source.profile);
    if (!profile) throw Error("Snapshot source refers to a missing profile.");
    const sourceProfile = registerProfile(temporary, parseApplicationProfileSource(source.text));
    if (identity(sourceProfile) !== source.profile || canonical(sourceProfile) !== canonical(profile)) {
      throw Error("Snapshot source bytes differ from the canonical profile definition.");
    }
    sourceProfiles.add(source.profile);
  }
  if (profiles.some(profile => !sourceProfiles.has(identity(profile)))) throw Error("Every snapshot profile needs its retained source text (or an explicitly labelled derivative).");
  const views = value.views.map(readApplicationView);
  const viewMap = new Map(views.map(view => [view.id, view]));
  if (viewMap.size !== views.length || views.some(view => !profileMap.has(view.profile))) throw Error("Snapshot view identity is duplicated or refers to a missing profile.");
  const sessions = new Set<string>();
  for (const entry of value.sessions) {
    record(entry, "snapshot.session", ["viewId", "profile", "session", "recovery"], ["references"]);
    text(entry.viewId, "session.viewId"); text(entry.profile, "session.profile");
    if (!viewMap.has(entry.viewId) || !profileMap.has(entry.profile)) throw Error("Workflow session needs an existing view and profile.");
    if (viewMap.get(entry.viewId)!.profile !== entry.profile) throw Error("Workflow session profile must match the profile currently selected in its view; historical or extra profile sessions cannot be restored.");
    if (sessions.has(entry.viewId)) throw Error("Snapshot repeats a workflow session identity; each view must have exactly one selected-profile session.");
    sessions.add(entry.viewId);
    if (entry.references !== undefined) {
      if (!entry.references || typeof entry.references !== "object" || Array.isArray(entry.references)) throw Error("Session references must be a JSON object.");
      json(entry.references, "session.references");
    }
    record(entry.recovery, "session.recovery", ["status"], ["pending", "decision"]);
    const recovery = entry.recovery;
    if (!["ready", "unknown", "reconciled", "abandoned"].includes(recovery.status as string)) throw Error("Unknown workflow recovery status.");
    if (recovery.status === "ready" && (recovery.pending !== undefined || recovery.decision !== undefined)) throw Error("A ready workflow cannot hide a pending operation or reconciliation.");
    if (recovery.status !== "ready") {
      record(recovery.pending, "recovery.pending", ["action"], ["workflowId", "event", "reference"]);
      const pendingOperation = recovery.pending;
      text(pendingOperation.action, "pending.action");
      const profile = profileMap.get(entry.profile)!;
      if (!profile.actions.some(action => action.id === pendingOperation.action)) throw Error("Pending operation is not a declared profile action.");
      if ((pendingOperation.workflowId === undefined) !== (pendingOperation.event === undefined)) throw Error("Pending workflow ID and event must appear together.");
      if (pendingOperation.workflowId !== undefined) {
        text(pendingOperation.workflowId, "pending.workflowId"); text(pendingOperation.event, "pending.event");
        const workflow = profile.workflows.find(workflow => workflow.id === pendingOperation.workflowId);
        if (!workflow?.transitions.some(step => step.event === pendingOperation.event && step.action === pendingOperation.action)) throw Error("Pending operation is not a declared workflow transition.");
      }
      if (pendingOperation.reference !== undefined) text(pendingOperation.reference, "pending.reference");
    }
    if (recovery.status === "unknown" && recovery.decision !== undefined) throw Error("Unresolved work cannot contain a reconciliation decision.");
    if (recovery.status === "reconciled" || recovery.status === "abandoned") {
      record(recovery.decision, "recovery.decision", ["choice", "at"]); date(recovery.decision.at, "decision.at");
      if (recovery.decision.choice !== (recovery.status === "reconciled" ? "verified_not_applied" : "abandon")) throw Error("Recovery decision does not match its status.");
    }
  }
  if (views.some(view => !sessions.has(view.id))) throw Error("Each snapshot view must have exactly one session for its selected profile; a missing session cannot retain older local workflow state.");
  // The host registry detects same-revision drift and checks its actual adapters.
  for (const profile of profiles) profileMap.set(identity(profile), registerProfile(registry, profile));
  const parsedSessions = value.sessions.map(entry => {
    const row = entry as unknown as WorkflowSnapshotSession;
    const profile = profileMap.get(row.profile)!;
    const session = parseProfileSession(row.session, profile, registry);
    const pending = row.recovery.pending;
    if (pending?.workflowId && !profile.workflows.find(workflow => workflow.id === pending.workflowId)?.transitions.some(step =>
      step.from === session.workflows[pending.workflowId!] && step.event === pending.event && step.action === pending.action)) {
      throw Error("Pending operation does not match the saved workflow state.");
    }
    return { ...row, session };
  });
  return clone({ ...value, profiles, views, sessions: parsedSessions }) as unknown as WorkflowSnapshot;
}

export function makeWorkflowSnapshot(draft: WorkflowSnapshotDraft, registry: ProfileRegistry,
  options: { id: string; label: string; savedAt?: string }): WorkflowSnapshot {
  return parseWorkflowSnapshot({ ...draft, schema: WORKFLOW_SNAPSHOT_SCHEMA, id: options.id, label: options.label,
    savedAt: options.savedAt ?? new Date().toISOString() }, registry);
}
export function workflowSnapshotCanContinue(entry: WorkflowSnapshotSession): boolean {
  return entry.recovery.status === "ready" || entry.recovery.status === "reconciled";
}
/** User attestation changes only the recovery flag. It never fabricates a success receipt or advances state. */
export function reconcileWorkflowSnapshot(snapshot: WorkflowSnapshot, viewId: string, profile: string,
  choice: "verified_not_applied" | "abandon", registry: ProfileRegistry): WorkflowSnapshot {
  let changed = false;
  const sessions = snapshot.sessions.map(entry => {
    if (entry.viewId !== viewId || entry.profile !== profile || entry.recovery.status !== "unknown") return entry;
    changed = true;
    return { ...entry, recovery: { ...entry.recovery, status: choice === "abandon" ? "abandoned" as const : "reconciled" as const,
      decision: { choice, at: new Date().toISOString() } } };
  });
  if (!changed) throw Error("No unresolved operation matches that session.");
  return parseWorkflowSnapshot({ ...snapshot, sessions }, registry);
}

function parseStore(raw: unknown, registry: ProfileRegistry): WorkflowSnapshotStore {
  if (raw === undefined) return { schema: WORKFLOW_SNAPSHOT_STORE_SCHEMA, snapshots: [] };
  record(raw, "snapshot store", ["schema", "snapshots"]);
  if (raw.schema !== WORKFLOW_SNAPSHOT_STORE_SCHEMA) throw Error("Unsupported workflow snapshot store schema; stored value was preserved.");
  list(raw.snapshots, "store.snapshots");
  const snapshots = raw.snapshots.map(snapshot => parseWorkflowSnapshot(snapshot, registry));
  if (new Set(snapshots.map(snapshot => snapshot.id)).size !== snapshots.length) throw Error("Snapshot store repeats an ID; stored value was preserved.");
  return { schema: WORKFLOW_SNAPSHOT_STORE_SCHEMA, snapshots };
}
export async function loadWorkflowSnapshots(api: WorkflowSnapshotApi, registry: ProfileRegistry): Promise<WorkflowSnapshot[]> {
  return parseStore((await api.getConfig())[WORKFLOW_SNAPSHOT_CONFIG_KEY], registry).snapshots;
}
export class WorkflowSnapshotWriteConflict extends Error {
  constructor(public readonly localSnapshot: WorkflowSnapshot) {
    super("Snapshot readback differs from the write. Local snapshot and source bytes are retained. Concurrent writes require manual reconciliation; this API has no compare-and-swap.");
    this.name = "WorkflowSnapshotWriteConflict";
  }
}
/** Read/replace/readback is a conflict detector, not a lock or exactly-once persistence guarantee. */
export async function saveWorkflowSnapshot(api: WorkflowSnapshotApi, snapshot: WorkflowSnapshot, registry: ProfileRegistry): Promise<WorkflowSnapshot> {
  const candidate = parseWorkflowSnapshot(snapshot, registry);
  const config = await api.getConfig();
  const previous = parseStore(config[WORKFLOW_SNAPSHOT_CONFIG_KEY], registry);
  const next: WorkflowSnapshotStore = { schema: WORKFLOW_SNAPSHOT_STORE_SCHEMA,
    snapshots: [...previous.snapshots.filter(item => item.id !== candidate.id), candidate] };
  // Only patch our open key. Passing all config could overwrite unrelated edits.
  await api.setConfig({ [WORKFLOW_SNAPSHOT_CONFIG_KEY]: next });
  const readback = (await api.getConfig())[WORKFLOW_SNAPSHOT_CONFIG_KEY];
  try { json(readback, "snapshot store readback"); } catch { throw new WorkflowSnapshotWriteConflict(candidate); }
  if (canonical(readback) !== canonical(next)) throw new WorkflowSnapshotWriteConflict(candidate);
  return candidate;
}
