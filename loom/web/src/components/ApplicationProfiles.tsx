import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { CSSProperties } from "react";
import { api } from "../api";
import ChatView from "./ChatView";
import { BUILTIN_PROFILE_DOCUMENTS, builtinProfileSource } from "../profiles/builtins";
import { createLoomProfileRegistry } from "../profiles/loom-adapter";
import type { LoomProfileUi } from "../profiles/loom-adapter";
import {
  createProfileSession, executeProfileAction, profileAvailability, registerProfile,
  serializeProfileSession,
} from "../profiles/runtime";
import type { ApplicationProfile, ProfileRegistry, ProfileSession } from "../profiles/runtime";
import { makeApplicationProfileGraphAcceptance, parseApplicationProfileSource, readProfileFromReceipt } from "../profiles/graph";
import { asRecord } from "../api/knowledge";
import "./application-profiles.css";

const STORAGE_KEY = "loom.application.views.v1";
interface View { id: string; profile: string }
interface ProfileSource { profile: string; text: string; sourceRef: string }
const key = (profile: ApplicationProfile) => JSON.stringify([profile.id, profile.profile_revision]);
const errorText = (error: unknown) => error instanceof Error ? error.message : String(error);

export function profileStyle(profile: ApplicationProfile): CSSProperties {
  const p = profile.presentation;
  // The native profile continues to inherit the existing Dark/AMOLED theme.
  return {
    ...(profile.id !== "loom-default" && {
      "--bg": p.tokens.background, "--card": p.tokens.surface, "--input-bg": p.tokens.background,
      "--text": p.tokens.text, "--dim": p.tokens.muted, "--accent": p.tokens.accent,
      "--border": p.tokens.border, "--user": p.tokens.surface, "--ai": p.tokens.surface,
    }),
    "--profile-content-width": `${p.content_width}px`,
    "--profile-sidebar-width": `${p.sidebar.width}px`,
  } as CSSProperties;
}

interface Props {
  convId: string | null; onConversationCreated: (id: string) => void;
  refreshKey: number; onMessagesChanged: () => void; ui: LoomProfileUi;
  onPrimaryProfile: (profile: ApplicationProfile) => void;
}

/** Profiles are simultaneous projections of the same conversation, not provider modes. */
export default function ApplicationProfiles(props: Props) {
  const registry = useMemo(() => createLoomProfileRegistry(api, props.ui), [props.ui]);
  const [initial] = useState(() => {
    const profiles = BUILTIN_PROFILE_DOCUMENTS.map(doc => registerProfile(registry, doc));
    const defaultViews = [{ id: "primary", profile: key(profiles[0]) }];
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) return { profiles, views: defaultViews, sources: [] as ProfileSource[], error: "" };
      const saved = JSON.parse(raw);
      if (saved.schema !== "loom.application.views/1" || !Array.isArray(saved.custom_profiles) ||
          !Array.isArray(saved.views) || !saved.views.length) throw new Error("Invalid saved application views.");
      for (const doc of saved.custom_profiles) profiles.push(registerProfile(registry, doc));
      const sources: ProfileSource[] = saved.sources ?? [];
      if (!Array.isArray(sources)) throw new Error("Invalid saved profile source records.");
      for (const source of sources) {
        if (!source || typeof source.text !== "string" || typeof source.sourceRef !== "string" ||
            !profiles.some(p => key(p) === source.profile)) throw new Error("Invalid saved profile source record.");
        const parsed = registerProfile(registry, parseApplicationProfileSource(source.text));
        if (key(parsed) !== source.profile) throw new Error("Profile source identity does not match its saved record.");
      }
      const ids = new Set<string>();
      for (const view of saved.views) {
        if (!view || typeof view.id !== "string" || !view.id || ids.has(view.id) ||
            !profiles.some(p => key(p) === view.profile && profileAvailability(p, registry).supported)) {
          throw new Error("A saved application view has a missing or unsupported profile revision.");
        }
        ids.add(view.id);
      }
      return { profiles, views: saved.views as View[], sources, error: "" };
    } catch (error) {
      return { profiles, views: defaultViews, sources: [] as ProfileSource[], error: `${errorText(error)} Saved bytes were preserved; showing the native view.` };
    }
  });
  const [workspace, setWorkspace] = useState({ profiles: initial.profiles, views: initial.views, sources: initial.sources });
  const workspaceRef = useRef(workspace);
  workspaceRef.current = workspace;
  const viewIntents = useRef(new Map<string, number>());
  const { profiles, views, sources } = workspace;
  function nextIntent(viewId: string) {
    const intent = (viewIntents.current.get(viewId) ?? 0) + 1;
    viewIntents.current.set(viewId, intent); return intent;
  }
  function commitWorkspace(next: typeof workspace) {
    workspaceRef.current = next; setWorkspace(next);
    persist(next.profiles, next.views, next.sources);
  }
  const [error, setError] = useState(initial.error);
  const [receiptInput, setReceiptInput] = useState("");
  const [loadingReceipt, setLoadingReceipt] = useState(false);
  const [nativeReceipts, setNativeReceipts] = useState<{ id: string; target: string }[]>([]);
  const primary = profiles.find(p => key(p) === views[0].profile)!;
  // Callback runs on explicit edits below; restore also informs the surrounding sidebar.
  useEffect(() => props.onPrimaryProfile(primary), [primary, props.onPrimaryProfile]);

  function persist(nextProfiles: ApplicationProfile[], nextViews: View[], nextSources = sources) {
    try {
      const builtinKeys = new Set(BUILTIN_PROFILE_DOCUMENTS.map(doc => key(registerProfile(registry, doc))));
      localStorage.setItem(STORAGE_KEY, JSON.stringify({ schema: "loom.application.views/1", views: nextViews,
        custom_profiles: nextProfiles.filter(p => !builtinKeys.has(key(p))), sources: nextSources }));
      setError("");
    } catch (err) { setError(`Views work in this session; saving failed: ${errorText(err)}`); }
  }
  function choose(viewId: string, profileKey: string) {
    nextIntent(viewId);
    const current = workspaceRef.current;
    commitWorkspace({ ...current, views: current.views.map(v => v.id === viewId ? { ...v, profile: profileKey } : v) });
  }
  function retainProfile(profile: ApplicationProfile, text: string, sourceRef: string, viewId: string, intent: number) {
    const current = workspaceRef.current;
    const availability = profileAvailability(profile, registry);
    const nextProfiles = current.profiles.some(p => key(p) === key(profile)) ? current.profiles : [...current.profiles, profile];
    const source = { profile: key(profile), text, sourceRef };
    const nextSources = current.sources.some(s => s.profile === source.profile && s.text === text && s.sourceRef === source.sourceRef)
      ? current.sources : [...current.sources, source];
    const activate = availability.supported && viewIntents.current.get(viewId) === intent;
    const nextViews = activate ? current.views.map(v => v.id === viewId ? { ...v, profile: key(profile) } : v) : current.views;
    commitWorkspace({ profiles: nextProfiles, sources: nextSources, views: nextViews });
    if (!availability.supported) setError(`Profile retained but cannot run: ${availability.requiredGaps.map(g => g.reason).join(" ")}`);
  }
  async function importProfile(file: File, viewId: string) {
    const intent = nextIntent(viewId);
    try {
      const text = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(await file.arrayBuffer());
      retainProfile(registerProfile(registry, parseApplicationProfileSource(text)), text, `import:${file.name}`, viewId, intent);
    } catch (err) { setError(errorText(err)); }
  }
  async function loadReceipt() {
    const viewId = workspaceRef.current.views[0].id;
    const intent = nextIntent(viewId);
    setLoadingReceipt(true); setError("");
    try {
      if (!api.graphPacketStore) throw new Error("This transport has no native GraphPacket adapter.");
      const stored = await readProfileFromReceipt(await api.graphPacketStore({ operation: "read", receipt_id: receiptInput.trim() }), registry);
      retainProfile(stored.profile, stored.source.text, stored.source.sourceRef, viewId, intent);
    } catch (err) { setError(errorText(err)); }
    finally { setLoadingReceipt(false); }
  }
  async function findNativeProfiles() {
    setLoadingReceipt(true); setError("");
    try {
      if (!api.knowledge || !api.graphPacketStore) throw new Error("This transport has no native profile storage adapter.");
      const runs = await api.knowledge.listRuns();
      const found = runs.flatMap(run => {
        const target = asRecord(run.inputs).graph_packet_target;
        const receipt = asRecord(run.summary).graph_packet_receipt;
        return run.status === "done" && typeof target === "string" && target.startsWith("application_profile:") && typeof receipt === "string"
          ? [{ id: receipt, target }] : [];
      });
      setNativeReceipts(found);
      if (!found.length) setError("No application profiles in the returned native run list. A known receipt ID can still be loaded directly.");
    } catch (err) { setError(errorText(err)); }
    finally { setLoadingReceipt(false); }
  }

  return <section className="application-views" aria-label="Application profile views">
    <div className="profile-workspace-controls">
      <button onClick={() => {
        const current = workspaceRef.current;
        commitWorkspace({ ...current, views: [...current.views, { id: crypto.randomUUID(), profile: current.views[0].profile }] });
      }} data-testid="add-profile-view">Add application view</button>
      <span>Views share the selected conversation. Models and context remain independent.</span>
      <details className="profile-native-storage"><summary>Saved profiles</summary><div>
      <label>Native profile receipt<input aria-label="Native profile receipt ID" value={receiptInput} onChange={e => setReceiptInput(e.target.value)} /></label>
      <button data-testid="load-profile-receipt" disabled={!api.graphPacketStore || loadingReceipt || !receiptInput.trim()} onClick={() => void loadReceipt()}>Load from graph</button>
      <button data-testid="find-native-profiles" disabled={!api.graphPacketStore || !api.knowledge || loadingReceipt} onClick={() => void findNativeProfiles()}>Find recent saved profiles</button>
      {!!nativeReceipts.length && <select aria-label="Recent native profile receipts" value={nativeReceipts.some(r => r.id === receiptInput) ? receiptInput : ""} onChange={e => setReceiptInput(e.target.value)}>
        <option value="">Select native receipt</option>{nativeReceipts.map(r => <option key={r.id} value={r.id}>{r.id}</option>)}
      </select>}
      </div></details>
    </div>
    {error && <p role="alert" className="profile-error">{error}</p>}
    <div className="application-view-grid">
      {views.map(view => <ApplicationView key={view.id} {...props} viewId={view.id}
        profile={profiles.find(p => key(p) === view.profile)!} profiles={profiles} registry={registry}
        source={[...sources].reverse().find(s => s.profile === view.profile) ?? builtinProfileSource(profiles.find(p => key(p) === view.profile)!.id, profiles.find(p => key(p) === view.profile)!.profile_revision)}
        choose={p => choose(view.id, p)} importProfile={file => importProfile(file, view.id)}
        remove={views.length > 1 ? () => {
          nextIntent(view.id);
          const current = workspaceRef.current;
          if (current.views.length > 1) commitWorkspace({ ...current, views: current.views.filter(v => v.id !== view.id) });
        } : undefined} />)}
    </div>
  </section>;
}

function ApplicationView({ profile, profiles, registry, viewId, source, choose, importProfile, remove, ...props }: Props & {
  profile: ApplicationProfile; profiles: ApplicationProfile[]; registry: ProfileRegistry; viewId: string;
  source?: { text: string; sourceRef: string };
  choose: (key: string) => void; importProfile: (file: File) => Promise<void>; remove?: () => void;
}) {
  const sessionRef = useRef<ProfileSession | null>(null);
  const [session, setSession] = useState<ProfileSession | null>(null);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [workflowInputs, setWorkflowInputs] = useState("{}");
  const [savingGraph, setSavingGraph] = useState(false);
  const [graphReceipt, setGraphReceipt] = useState<{ profile: string; id: string; run: string } | null>(null);
  const availability = profileAvailability(profile, registry);
  const sessionKey = `loom.application.workflow.v1:${JSON.stringify([viewId, profile.id, profile.profile_revision])}`;
  const runOperation = useCallback(async (operation: string, payload?: unknown) => {
    const gaps = profileAvailability(profile, registry);
    const unavailable = new Set([...gaps.requiredGaps, ...gaps.optionalGaps].map(g => g.action_id));
    const action = profile.actions.find(a => a.operation === operation && !unavailable.has(a.id));
    if (!action) throw new Error(`This profile has no action for ${operation}.`);
    // Independent control events have their own snapshot, so Stop can interrupt Send.
    const current = createProfileSession(profile, registry);
    return (await executeProfileAction(current, registry, action.id, { payload })).result;
  }, [profile, registry]);

  function workflowSession(): ProfileSession {
    if (sessionRef.current?.profile.id === profile.id && sessionRef.current.profile.profile_revision === profile.profile_revision) return sessionRef.current;
    let raw: string | null = null;
    try { raw = localStorage.getItem(sessionKey); } catch { /* Workflows still execute in memory. */ }
    const loaded = createProfileSession(profile, registry, raw ?? undefined);
    sessionRef.current = loaded;
    return loaded;
  }
  async function runWorkflow(workflowId: string, event: string, action: string) {
    setBusy(true); setError("");
    try {
      const current = workflowSession();
      const inputs = JSON.parse(workflowInputs);
      const workflow = profile.workflows.find(w => w.id === workflowId)!;
      const transition = workflow.transitions.find(t => t.from === current.workflows[workflowId] && t.event === event)!;
      const next = await executeProfileAction(current, registry, action, { workflowId, event,
        bindings: { inputs, context: { conversation_id: props.convId } },
        ...(transition.payload === undefined ? { payload: inputs } : {}) });
      sessionRef.current = next.session; setSession(next.session);
      props.onMessagesChanged();
      try { localStorage.setItem(sessionKey, serializeProfileSession(next.session)); }
      catch (err) { setError(`Operation completed; saving workflow failed: ${errorText(err)}`); }
    } catch (err) { setError(errorText(err)); }
    finally { setBusy(false); }
  }
  async function saveGraph() {
    setSavingGraph(true); setError("");
    try {
      if (!api.graphPacketStore) throw new Error("This transport has no native GraphPacket adapter.");
      const retained = source ?? { text: JSON.stringify(profile), sourceRef: `derived:browser-profile:${key(profile)}` };
      const request = await makeApplicationProfileGraphAcceptance(profile, { ...retained, actor: "loom.profile.user" });
      const accepted = await api.graphPacketStore(request);
      setGraphReceipt({ profile: key(profile), id: accepted.receipt.id, run: accepted.receipt.run_id });
      await readProfileFromReceipt(await api.graphPacketStore({ operation: "read", receipt_id: accepted.receipt.id }), registry);
    } catch (err) { setError(errorText(err)); }
    finally { setSavingGraph(false); }
  }
  let current: ProfileSession | null = null;
  let restoreError = "";
  try { current = workflowSession(); } catch (err) { restoreError = errorText(err); }
  const currentProfileSession = session?.profile.id === profile.id && session.profile.profile_revision === profile.profile_revision ? session : current;

  return <section className={`application-profile-view profile-messages-${profile.presentation.message_style}`}
    style={profileStyle(profile)} data-testid="application-profile-view" data-profile-id={profile.id} data-profile-revision={profile.profile_revision}>
    <div className="application-profile-toolbar">
      <label>Application view<select aria-label="Application view profile" value={key(profile)} onChange={e => choose(e.target.value)}>
        {profiles.map(p => <option key={key(p)} value={key(p)} disabled={!profileAvailability(p, registry).supported}>
          {p.label} · {p.target.version ?? "version unverified"} · r{p.profile_revision}
        </option>)}
      </select></label>
      <label className="profile-file">Import profile<input aria-label="Import application profile" type="file" accept=".json,application/json" onChange={event => {
        const file = event.target.files?.[0]; event.target.value = ""; if (file) void importProfile(file);
      }} /></label>
      {remove && <button onClick={remove}>Close view</button>}
      <details className="profile-details"><summary>Profile and workflows</summary>
        <p>Declared mapping: {profile.evidence.status}; target {profile.target.application_id} / {profile.target.version ?? "unknown version"} / {profile.target.platform}.</p>
        <button data-testid="save-profile-graph" disabled={!api.graphPacketStore || savingGraph} onClick={() => void saveGraph()}>Save profile to graph</button>
        {!api.graphPacketStore && <p>This transport has no native profile storage adapter.</p>}
        <p>This saves the profile definition and source to native knowledge storage. Workflow inputs, conversation content and provider settings are separate.</p>
        <p>The profile has its own knowledge run. Automatic latest-run queries may select it; an explicitly selected context run stays unchanged.</p>
        {!source && <p>The original imported bytes were not saved by the older client. This write uses an explicitly labelled JSON derivative.</p>}
        {graphReceipt?.profile === key(profile) && <p data-testid="profile-graph-receipt">Native receipt: <code>{graphReceipt.id}</code> · run <code>{graphReceipt.run}</code></p>}
        {profile.evidence.gaps.map((gap, index) => <p key={index}>{gap}</p>)}
        <details><summary>Profile JSON and bindings</summary><pre>{source?.text ?? JSON.stringify(profile, null, 2)}</pre></details>
        <ul>{profile.evidence.sources.map((source, i) => <li key={i}><a href={source.url} target="_blank" rel="noopener noreferrer">Reference {i + 1}</a>{source.revision && ` @ ${source.revision}`} {source.note}</li>)}</ul>
        <p>Installed Loom adapters execute these operations. Live inference requires the selected provider; original-service parity is unverified.</p>
        <label>Workflow inputs (JSON)<textarea aria-label="Workflow inputs JSON" value={workflowInputs} onChange={e => setWorkflowInputs(e.target.value)} /></label>
        <p>Profiles select the fields stored in workflow variables. Input fields are passed only to the selected action; existing profiles without bindings use this JSON as their payload.</p>
        <ul>{profile.actions.map(action => <li key={action.id}>{action.label}: {
          [...availability.requiredGaps, ...availability.optionalGaps].some(g => g.action_id === action.id) ? "unavailable" : "Loom adapter available"
        }</li>)}</ul>
        {profile.workflows.map(workflow => {
          const state = currentProfileSession?.workflows[workflow.id] ?? workflow.initial;
          return <div key={workflow.id} data-testid="profile-workflow"><span>{workflow.id}: {state}</span>
            {currentProfileSession?.variables?.[workflow.id] && <details><summary>Selected workflow variables</summary><pre>{JSON.stringify(currentProfileSession.variables[workflow.id], null, 2)}</pre></details>}
            {workflow.transitions.filter(t => t.from === state).map(transition => {
              const action = profile.actions.find(a => a.id === transition.action)!;
              const gaps = [...availability.requiredGaps, ...availability.optionalGaps].filter(g => g.action_id === action.id).map(g => g.capability);
              const missing = [...gaps, ...(transition.requires ?? []).filter(cap => !availability.capabilities.includes(cap))];
              return <span key={transition.event}><button disabled={busy || !!missing.length} title={missing.length ? `Unavailable: ${missing.join(", ")}` : undefined}
                onClick={() => void runWorkflow(workflow.id, transition.event, transition.action)}>{action.label}</button>
                {!!missing.length && <span>Unavailable: {missing.join(", ")}</span>}</span>;
            })}
          </div>;
        })}
      </details>
    </div>
    {(error || restoreError) && <p role="alert" className="profile-error">{error || restoreError}</p>}
    <ChatView convId={props.convId} onConversationCreated={props.onConversationCreated} profile={profile}
      runProfileOperation={runOperation} refreshKey={props.refreshKey} onMessagesChanged={props.onMessagesChanged}
      availableOperations={profile.actions.filter(a => ![...availability.requiredGaps, ...availability.optionalGaps].some(g => g.action_id === a.id)).map(a => a.operation)} />
  </section>;
}
