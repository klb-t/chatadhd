/** Versioned UI/workflow data. This module never reads or writes model settings. */
export const PROFILE_SCHEMA = "loom.application_profile/1";
export const PROFILE_SESSION_SCHEMA = "loom.application_profile_session/1";
export const PROFILE_STORAGE_KEY = "loom.application.profile.session.v1";
export const CANONICAL_OPERATIONS = ["chat.create", "chat.select", "chat.rename", "chat.send", "chat.cancel",
  "message.edit", "message.restore", "message.exclude", "knowledge.open", "settings.open"] as const;

export interface ProfileTarget { application_id: string; version: string | null; platform: string }
export interface ApplicationProfile {
  schema: typeof PROFILE_SCHEMA;
  id: string;
  profile_revision: number;
  label: string;
  target: ProfileTarget;
  evidence: { status: "inspired" | "partial" | "verified";
    sources: { url: string; revision?: string; note?: string }[]; gaps: string[] };
  presentation: { renderer: string;
    tokens: { background: string; surface: string; text: string; muted: string; accent: string; border: string };
    sidebar: { side: "left" | "right"; width: number }; content_width: number; message_style: "plain" | "bubble" };
  composer: { submit: "enter" | "mod-enter"; placeholder: string };
  actions: ProfileAction[];
  workflows: ProfileWorkflow[];
}
export interface ProfileAction { id: string; label: string; operation: string; capability: string; required: boolean }
export interface ProfileTransition { from: string; event: string; action: string; to: string; requires?: string[] }
export interface ProfileWorkflow { id: string; initial: string; states: string[]; transitions: ProfileTransition[] }
export interface OperationAdapter { operation: string; capability: string; execute: (payload: unknown) => unknown | Promise<unknown> }
export interface ProfileGap { action_id: string; capability: string; reason: string }
export interface ProfileAvailability { supported: boolean; requiredGaps: ProfileGap[]; optionalGaps: ProfileGap[]; capabilities: string[] }
export interface ProfileRegistry { readonly renderers: readonly string[]; readonly operations: readonly string[] }
export interface ActionReceipt { sequence: number; action: string; operation: string;
  workflow?: { id: string; event: string; from: string; to: string } }
export interface ProfileSession {
  schema: typeof PROFILE_SESSION_SCHEMA;
  profile: { id: string; profile_revision: number; target: ProfileTarget; definition: string };
  workflows: Record<string, string>;
  sequence: number;
  history: ActionReceipt[];
}
interface RegistryData { adapters: Map<string, OperationAdapter>; profiles: Map<string, Map<number, ApplicationProfile>> }
const registries = new WeakMap<ProfileRegistry, RegistryData>();
const pending = new WeakSet<ProfileSession>();
const consumed = new WeakSet<ProfileSession>();
export class ProfileError extends Error {
  constructor(public readonly code: string, message: string) { super(message); this.name = "ProfileError"; }
}
function fail(code: string, message: string): never { throw new ProfileError(code, message); }
function data(registry: ProfileRegistry): RegistryData {
  return registries.get(registry) ?? fail("registry", "Unknown application profile registry.");
}
function adapterKey(operation: string, capability: string): string { return JSON.stringify([operation, capability]); }
function nonempty(value: unknown, path: string): asserts value is string {
  if (typeof value !== "string" || !value.trim()) fail("contract", `${path} must be a nonempty string.`);
}
function object(value: unknown, path: string, required: string[], optional: string[] = []): asserts value is Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) fail("contract", `${path} must be an object.`);
  const prototype = Object.getPrototypeOf(value);
  if (prototype !== Object.prototype && prototype !== null) fail("contract", `${path} must contain plain JSON data.`);
  for (const key of required) if (!Object.prototype.hasOwnProperty.call(value, key)) fail("contract", `${path}.${key} is required.`);
  for (const key of Reflect.ownKeys(value)) {
    if (typeof key !== "string" || ![...required, ...optional].includes(key)) fail("contract", `${path} has unknown field ${String(key)}.`);
    if (!("value" in Object.getOwnPropertyDescriptor(value, key)!)) fail("contract", `${path}.${key} must contain JSON data, not an accessor.`);
  }
}
function array(value: unknown, path: string): asserts value is unknown[] {
  if (!Array.isArray(value)) fail("contract", `${path} must be an array.`);
  if (Object.getPrototypeOf(value) !== Array.prototype || Reflect.ownKeys(value).length !== value.length + 1) fail("contract", `${path} must be a dense JSON array.`);
  for (const key of Reflect.ownKeys(value)) {
    const descriptor = Object.getOwnPropertyDescriptor(value, key)!;
    if (typeof key !== "string" || (key !== "length" && (!/^\d+$/.test(key) || Number(key) >= value.length)) || !("value" in descriptor)) fail("contract", `${path} must be a dense JSON array without accessors or extra properties.`);
  }
}
function strings(value: unknown, path: string): asserts value is string[] {
  array(value, path); value.forEach((entry, i) => nonempty(entry, `${path}[${i}]`));
}
function unique(values: string[], path: string) {
  if (new Set(values).size !== values.length) fail("contract", `${path} contains duplicate identifiers.`);
}
function positive(value: unknown, path: string) {
  if (typeof value !== "number" || !Number.isFinite(value) || value <= 0) fail("contract", `${path} must be a positive finite pixel value.`);
}
function revision(value: unknown, path: string) {
  if (!Number.isSafeInteger(value) || (value as number) < 1) fail("contract", `${path} must be a positive safe integer.`);
}
function target(value: unknown, path: string): asserts value is ProfileTarget {
  object(value, path, ["application_id", "version", "platform"]);
  nonempty(value.application_id, `${path}.application_id`); nonempty(value.platform, `${path}.platform`);
  if (value.version !== null) nonempty(value.version, `${path}.version`);
}
function sameTarget(a: ProfileTarget, b: ProfileTarget): boolean {
  return a.application_id === b.application_id && a.version === b.version && a.platform === b.platform;
}
function freeze<T>(value: T): T {
  if (value && typeof value === "object") { Object.values(value).forEach(freeze); Object.freeze(value); }
  return value;
}
function copy<T>(value: T): T { return JSON.parse(JSON.stringify(value)) as T; }
function canonical(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (value && typeof value === "object") return `{${Object.entries(value).sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0)
    .map(([key, entry]) => `${JSON.stringify(key)}:${canonical(entry)}`).join(",")}}`;
  return JSON.stringify(value);
}

/** Extensions are trusted code registrations; profile data cannot install adapters or scripts. */
export function createProfileRegistry(options: { renderers?: string[]; operations?: string[]; adapters?: OperationAdapter[] } = {}): ProfileRegistry {
  const renderers = [...new Set(["chat", ...(options.renderers ?? [])])];
  const operations = [...new Set([...CANONICAL_OPERATIONS, ...(options.operations ?? [])])];
  strings(renderers, "registry.renderers"); strings(operations, "registry.operations");
  const adapters = new Map<string, OperationAdapter>();
  for (const adapter of options.adapters ?? []) {
    nonempty(adapter.operation, "adapter.operation"); nonempty(adapter.capability, "adapter.capability");
    if (!operations.includes(adapter.operation)) fail("operation", `Unknown operation ${adapter.operation}; register its vocabulary explicitly.`);
    if (typeof adapter.execute !== "function") fail("adapter", `Adapter ${adapter.operation} has no executable handler.`);
    const key = adapterKey(adapter.operation, adapter.capability);
    if (adapters.has(key)) fail("adapter", `Duplicate adapter ${adapter.operation}/${adapter.capability}.`);
    adapters.set(key, { ...adapter });
  }
  const registry = freeze({ renderers, operations });
  registries.set(registry, { adapters, profiles: new Map() });
  return registry;
}

export function registerProfile(registry: ProfileRegistry, input: unknown): ApplicationProfile {
  const store = data(registry);
  object(input, "profile", ["schema", "id", "profile_revision", "label", "target", "evidence", "presentation", "composer", "actions", "workflows"]);
  if (input.schema !== PROFILE_SCHEMA) fail("schema", `Unsupported application profile schema ${String(input.schema)}.`);
  nonempty(input.id, "profile.id"); revision(input.profile_revision, "profile.profile_revision"); nonempty(input.label, "profile.label");
  target(input.target, "profile.target");
  object(input.evidence, "profile.evidence", ["status", "sources", "gaps"]);
  if (!["inspired", "partial", "verified"].includes(input.evidence.status as string)) fail("contract", "Unknown evidence status.");
  array(input.evidence.sources, "profile.evidence.sources"); strings(input.evidence.gaps, "profile.evidence.gaps");
  input.evidence.sources.forEach((source, i) => {
    object(source, `source[${i}]`, ["url"], ["revision", "note"]); nonempty(source.url, `source[${i}].url`);
    let url: URL; try { url = new URL(source.url); } catch { fail("contract", `source[${i}].url must be an absolute HTTP(S) provenance URL.`); }
    if (/\s/.test(source.url) || !["https:", "http:"].includes(url.protocol) || url.username || url.password) fail("contract", `source[${i}].url must be an HTTP(S) provenance URL without whitespace or credentials.`);
    if (source.revision !== undefined) nonempty(source.revision, `source[${i}].revision`);
    if (source.note !== undefined && typeof source.note !== "string") fail("contract", `source[${i}].note must be a string.`);
  });
  if (input.evidence.status === "verified" && (!input.evidence.sources.length || input.target.version === null || input.evidence.gaps.length)) {
    fail("evidence", "A verified mapping requires a known target version, provenance sources and no declared gaps.");
  }
  object(input.presentation, "profile.presentation", ["renderer", "tokens", "sidebar", "content_width", "message_style"]);
  nonempty(input.presentation.renderer, "profile.presentation.renderer");
  if (!registry.renderers.includes(input.presentation.renderer)) fail("renderer", `Unsupported renderer ${input.presentation.renderer}.`);
  object(input.presentation.tokens, "profile.presentation.tokens", ["background", "surface", "text", "muted", "accent", "border"]);
  for (const [key, value] of Object.entries(input.presentation.tokens)) {
    if (typeof value !== "string" || /\s/.test(value) || !/^#(?:[\da-fA-F]{3}|[\da-fA-F]{4}|[\da-fA-F]{6}|[\da-fA-F]{8})$/.test(value)) fail("contract", `presentation.tokens.${key} must be a hex colour, not arbitrary CSS.`);
  }
  object(input.presentation.sidebar, "profile.presentation.sidebar", ["side", "width"]);
  if (!["left", "right"].includes(input.presentation.sidebar.side as string)) fail("contract", "Unknown sidebar side.");
  positive(input.presentation.sidebar.width, "presentation.sidebar.width"); positive(input.presentation.content_width, "presentation.content_width");
  if (!["plain", "bubble"].includes(input.presentation.message_style as string)) fail("contract", "Unknown message style.");
  object(input.composer, "profile.composer", ["submit", "placeholder"]);
  if (!["enter", "mod-enter"].includes(input.composer.submit as string)) fail("contract", "Unknown composer submit behavior.");
  if (typeof input.composer.placeholder !== "string") fail("contract", "Composer placeholder must be a string.");
  array(input.actions, "profile.actions");
  input.actions.forEach((action, i) => {
    object(action, `action[${i}]`, ["id", "label", "operation", "capability", "required"]);
    for (const field of ["id", "label", "operation", "capability"]) nonempty(action[field], `action[${i}].${field}`);
    if (!registry.operations.includes(action.operation as string)) fail("operation", `Unknown canonical operation ${String(action.operation)}.`);
    if (typeof action.required !== "boolean") fail("contract", `action[${i}].required must be boolean.`);
  });
  const actions = input.actions as unknown as ProfileAction[];
  unique(actions.map(a => a.id), "actions");
  array(input.workflows, "profile.workflows");
  input.workflows.forEach((workflow, i) => {
    object(workflow, `workflow[${i}]`, ["id", "initial", "states", "transitions"]);
    nonempty(workflow.id, `workflow[${i}].id`); nonempty(workflow.initial, `workflow[${i}].initial`);
    strings(workflow.states, `workflow[${i}].states`); unique(workflow.states, `workflow[${i}].states`);
    const workflowStates = workflow.states;
    if (!workflowStates.includes(workflow.initial)) fail("workflow", `Workflow ${workflow.id} has an unknown initial state.`);
    array(workflow.transitions, `workflow[${i}].transitions`);
    const keys: string[] = [];
    workflow.transitions.forEach((transition, j) => {
      object(transition, `transition[${i}][${j}]`, ["from", "event", "action", "to"], ["requires"]);
      for (const field of ["from", "event", "action", "to"]) nonempty(transition[field], `transition[${i}][${j}].${field}`);
      if (!workflowStates.includes(transition.from as string) || !workflowStates.includes(transition.to as string)) fail("workflow", `Workflow ${workflow.id} references an unknown state.`);
      if (!actions.some(a => a.id === transition.action)) fail("workflow", `Workflow ${workflow.id} references an unknown action.`);
      if (transition.requires !== undefined) { strings(transition.requires, "transition.requires"); unique(transition.requires, "transition.requires"); }
      keys.push(JSON.stringify([transition.from, transition.event]));
    });
    unique(keys, `workflow ${workflow.id} transition events`);
  });
  const workflows = input.workflows as unknown as ProfileWorkflow[]; unique(workflows.map(w => w.id), "workflows");
  const profile = freeze(copy(input)) as unknown as ApplicationProfile;
  const revisions = store.profiles.get(profile.id) ?? new Map<number, ApplicationProfile>();
  const prior = revisions.values().next().value as ApplicationProfile | undefined;
  if (prior && !sameTarget(prior.target, profile.target)) fail("identity", `Profile ${profile.id} cannot change its target application/version/platform; use a separate profile ID.`);
  const existing = revisions.get(profile.profile_revision);
  if (existing && canonical(existing) !== canonical(profile)) fail("revision", `Profile ${profile.id} revision ${profile.profile_revision} already has different contents.`);
  if (existing) return existing;
  revisions.set(profile.profile_revision, profile); store.profiles.set(profile.id, revisions);
  return profile;
}

function registered(profile: ApplicationProfile, registry: ProfileRegistry): ApplicationProfile {
  const found = data(registry).profiles.get(profile.id)?.get(profile.profile_revision);
  if (found !== profile) fail("revision", "Use the registered profile revision returned by registerProfile.");
  return found;
}
export function profileAvailability(profile: ApplicationProfile, registry: ProfileRegistry): ProfileAvailability {
  registered(profile, registry);
  const store = data(registry);
  const capabilities = [...new Set([...store.adapters.values()].map(a => a.capability))].sort();
  const gaps = profile.actions.filter(a => !store.adapters.has(adapterKey(a.operation, a.capability)))
    .map(a => ({ action_id: a.id, capability: a.capability, reason: `No registered adapter for ${a.operation} with capability ${a.capability}.`, required: a.required }));
  const requiredGaps = gaps.filter(g => g.required).map(({ required: _required, ...gap }) => gap);
  const optionalGaps = gaps.filter(g => !g.required).map(({ required: _required, ...gap }) => gap);
  return { supported: !requiredGaps.length, requiredGaps, optionalGaps, capabilities };
}
function assertAvailable(profile: ApplicationProfile, registry: ProfileRegistry) {
  const availability = profileAvailability(profile, registry);
  if (!availability.supported) fail("capability", `Required capabilities are unavailable: ${availability.requiredGaps.map(g => g.reason).join(" ")}`);
}
function initialStates(profile: ApplicationProfile): Record<string, string> {
  return Object.fromEntries(profile.workflows.map(workflow => [workflow.id, workflow.initial]));
}
export function createProfileSession(profile: ApplicationProfile, registry: ProfileRegistry, restore?: unknown): ProfileSession {
  assertAvailable(profile, registry);
  if (restore !== undefined) return parseProfileSession(restore, profile, registry);
  return freeze({ schema: PROFILE_SESSION_SCHEMA, profile: { id: profile.id, profile_revision: profile.profile_revision, target: copy(profile.target), definition: canonical(profile) },
    workflows: initialStates(profile), sequence: 0, history: [] });
}
export function serializeProfileSession(session: ProfileSession): string { return JSON.stringify(session); }

/** Restore only the exact target/revision and replay the finite-state trace; no silent migration. */
export function parseProfileSession(raw: unknown, profile: ApplicationProfile, registry: ProfileRegistry): ProfileSession {
  assertAvailable(profile, registry);
  let value: unknown = raw;
  if (typeof raw === "string") { try { value = JSON.parse(raw); } catch { fail("restore", "Application profile session is not valid JSON; original stored bytes remain untouched."); } }
  object(value, "session", ["schema", "profile", "workflows", "sequence", "history"]);
  if (value.schema !== PROFILE_SESSION_SCHEMA) fail("schema", "Unsupported application profile session schema.");
  object(value.profile, "session.profile", ["id", "profile_revision", "target", "definition"]);
  target(value.profile.target, "session.profile.target");
  if (value.profile.id !== profile.id || value.profile.profile_revision !== profile.profile_revision || !sameTarget(value.profile.target, profile.target)) fail("revision", "Saved session belongs to a different profile, revision or application target.");
  if (value.profile.definition !== canonical(profile)) fail("revision", "Saved profile definition differs from this revision; explicit migration or a new session is required.");
  const states = initialStates(profile);
  object(value.workflows, "session.workflows", Object.keys(states));
  array(value.history, "session.history");
  if (!Number.isSafeInteger(value.sequence) || value.sequence !== value.history.length) fail("restore", "Saved sequence does not match action history.");
  value.history.forEach((receipt, index) => {
    object(receipt, `history[${index}]`, ["sequence", "action", "operation"], ["workflow"]);
    const action = profile.actions.find(a => a.id === receipt.action);
    if (!action || receipt.operation !== action.operation || receipt.sequence !== index + 1) fail("restore", "Saved action receipt does not match this profile's operation vocabulary/order.");
    if (receipt.workflow !== undefined) {
      object(receipt.workflow, "receipt.workflow", ["id", "event", "from", "to"]);
      const step = receipt.workflow;
      const definition = profile.workflows.find(w => w.id === step.id);
      const transition = definition?.transitions.find(t => t.from === states[definition.id] && t.event === step.event && t.action === action.id);
      if (!definition || !transition || step.from !== transition.from || step.to !== transition.to) fail("restore", "Saved workflow history violates the declared transition order.");
      states[definition.id] = transition.to;
    }
  });
  for (const [id, state] of Object.entries(states)) if (value.workflows[id] !== state) fail("restore", `Saved workflow ${id} does not match its replayed history.`);
  return freeze(copy(value)) as unknown as ProfileSession;
}

/** One action at a time per immutable snapshot. Commit workflow state only after adapter success. */
export async function executeProfileAction(session: ProfileSession, registry: ProfileRegistry, actionId: string,
  options: { workflowId?: string; event?: string; payload?: unknown } = {}): Promise<{ session: ProfileSession; result: unknown }> {
  const profile = data(registry).profiles.get(session.profile.id)?.get(session.profile.profile_revision)
    ?? fail("revision", "This profile revision is not registered.");
  if (!sameTarget(session.profile.target, profile.target)) fail("identity", "Session target differs from the registered application target.");
  assertAvailable(profile, registry);
  if (pending.has(session)) fail("busy", "An application profile action is already running for this session.");
  if (consumed.has(session)) fail("stale", "This profile session has already advanced; execute against the returned session.");
  // Reject structurally inconsistent snapshots. Local history is not a server authorization record.
  parseProfileSession(session, profile, registry);
  const action = profile.actions.find(a => a.id === actionId) ?? fail("action", `Unknown action ${actionId}.`);
  const adapter = data(registry).adapters.get(adapterKey(action.operation, action.capability))
    ?? fail("capability", `Action ${action.id} has no registered ${action.operation}/${action.capability} adapter.`);
  let step: ActionReceipt["workflow"];
  if ((options.workflowId === undefined) !== (options.event === undefined)) fail("workflow", "Workflow ID and event must be provided together.");
  if (options.workflowId !== undefined) {
    const workflow = profile.workflows.find(w => w.id === options.workflowId) ?? fail("workflow", `Unknown workflow ${options.workflowId}.`);
    const transition = workflow.transitions.find(t => t.from === session.workflows[workflow.id] && t.event === options.event)
      ?? fail("workflow", `Event ${options.event} is not available from ${session.workflows[workflow.id]}.`);
    if (transition.action !== action.id) fail("workflow", "The declared transition requires a different action.");
    const capabilities = new Set(profileAvailability(profile, registry).capabilities);
    const missing = (transition.requires ?? []).filter(capability => !capabilities.has(capability));
    if (missing.length) fail("capability", `Workflow guard requires unavailable capabilities: ${missing.join(", ")}.`);
    step = { id: workflow.id, event: transition.event, from: transition.from, to: transition.to };
  }
  const nextSequence = session.sequence + 1;
  if (!Number.isSafeInteger(nextSequence)) fail("sequence", "Profile session sequence cannot be represented safely.");
  pending.add(session);
  try {
    const result = await adapter.execute(options.payload);
    const receipt: ActionReceipt = { sequence: nextSequence, action: action.id, operation: action.operation, ...(step ? { workflow: step } : {}) };
    const next = freeze({ ...copy(session), sequence: nextSequence,
      workflows: { ...session.workflows, ...(step ? { [step.id]: step.to } : {}) }, history: [...session.history, receipt] });
    consumed.add(session);
    return { session: next, result };
  } finally { pending.delete(session); }
}

export function shouldSubmit(submit: ApplicationProfile["composer"]["submit"], event: {
  key: string; shiftKey?: boolean; ctrlKey?: boolean; metaKey?: boolean; isComposing?: boolean;
}): boolean {
  return event.key === "Enter" && !event.shiftKey && !event.isComposing &&
    (submit === "enter" || (submit === "mod-enter" && !!(event.ctrlKey || event.metaKey)));
}
