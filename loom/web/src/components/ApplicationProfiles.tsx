import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { CSSProperties } from "react";
import { api } from "../api";
import ChatView from "./ChatView";
import { BUILTIN_PROFILE_DOCUMENTS } from "../profiles/builtins";
import { createLoomProfileRegistry } from "../profiles/loom-adapter";
import type { LoomProfileUi } from "../profiles/loom-adapter";
import {
  createProfileSession, executeProfileAction, profileAvailability, registerProfile,
  serializeProfileSession,
} from "../profiles/runtime";
import type { ApplicationProfile, ProfileRegistry, ProfileSession } from "../profiles/runtime";
import "./application-profiles.css";

const STORAGE_KEY = "loom.application.views.v1";
interface View { id: string; profile: string }
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
      if (!raw) return { profiles, views: defaultViews, error: "" };
      const saved = JSON.parse(raw);
      if (saved.schema !== "loom.application.views/1" || !Array.isArray(saved.custom_profiles) ||
          !Array.isArray(saved.views) || !saved.views.length) throw new Error("Invalid saved application views.");
      for (const doc of saved.custom_profiles) profiles.push(registerProfile(registry, doc));
      const ids = new Set<string>();
      for (const view of saved.views) {
        if (!view || typeof view.id !== "string" || !view.id || ids.has(view.id) ||
            !profiles.some(p => key(p) === view.profile && profileAvailability(p, registry).supported)) {
          throw new Error("A saved application view has a missing or unsupported profile revision.");
        }
        ids.add(view.id);
      }
      return { profiles, views: saved.views as View[], error: "" };
    } catch (error) {
      return { profiles, views: defaultViews, error: `${errorText(error)} Saved bytes were preserved; showing the native view.` };
    }
  });
  const [profiles, setProfiles] = useState(initial.profiles);
  const [views, setViews] = useState(initial.views);
  const [error, setError] = useState(initial.error);
  const primary = profiles.find(p => key(p) === views[0].profile)!;
  // Callback runs on explicit edits below; restore also informs the surrounding sidebar.
  useEffect(() => props.onPrimaryProfile(primary), [primary, props.onPrimaryProfile]);

  function persist(nextProfiles: ApplicationProfile[], nextViews: View[]) {
    try {
      const builtinKeys = new Set(BUILTIN_PROFILE_DOCUMENTS.map(doc => key(registerProfile(registry, doc))));
      localStorage.setItem(STORAGE_KEY, JSON.stringify({ schema: "loom.application.views/1", views: nextViews,
        custom_profiles: nextProfiles.filter(p => !builtinKeys.has(key(p))) }));
      setError("");
    } catch (err) { setError(`Views work in this session; saving failed: ${errorText(err)}`); }
  }
  function choose(viewId: string, profileKey: string) {
    const next = views.map(v => v.id === viewId ? { ...v, profile: profileKey } : v);
    setViews(next); persist(profiles, next);
  }
  async function importProfile(file: File, viewId: string) {
    try {
      const profile = registerProfile(registry, JSON.parse(await file.text()));
      const availability = profileAvailability(profile, registry);
      const nextProfiles = profiles.some(p => key(p) === key(profile)) ? profiles : [...profiles, profile];
      setProfiles(nextProfiles);
      if (!availability.supported) {
        persist(nextProfiles, views);
        setError(`Profile retained but cannot run: ${availability.requiredGaps.map(g => g.reason).join(" ")}`);
        return;
      }
      const nextViews = views.map(v => v.id === viewId ? { ...v, profile: key(profile) } : v);
      setViews(nextViews); persist(nextProfiles, nextViews);
    } catch (err) { setError(errorText(err)); }
  }

  return <section className="application-views" aria-label="Application profile views">
    <div className="profile-workspace-controls">
      <button onClick={() => {
        const next = [...views, { id: crypto.randomUUID(), profile: key(primary) }];
        setViews(next); persist(profiles, next);
      }} data-testid="add-profile-view">Add application view</button>
      <span>Views share the selected conversation. Models and context remain independent.</span>
    </div>
    {error && <p role="alert" className="profile-error">{error}</p>}
    <div className="application-view-grid">
      {views.map(view => <ApplicationView key={view.id} {...props} viewId={view.id}
        profile={profiles.find(p => key(p) === view.profile)!} profiles={profiles} registry={registry}
        choose={p => choose(view.id, p)} importProfile={file => importProfile(file, view.id)}
        remove={views.length > 1 ? () => {
          const next = views.filter(v => v.id !== view.id); setViews(next); persist(profiles, next);
        } : undefined} />)}
    </div>
  </section>;
}

function ApplicationView({ profile, profiles, registry, viewId, choose, importProfile, remove, ...props }: Props & {
  profile: ApplicationProfile; profiles: ApplicationProfile[]; registry: ProfileRegistry; viewId: string;
  choose: (key: string) => void; importProfile: (file: File) => Promise<void>; remove?: () => void;
}) {
  const sessionRef = useRef<ProfileSession | null>(null);
  const [session, setSession] = useState<ProfileSession | null>(null);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const availability = profileAvailability(profile, registry);
  const sessionKey = `loom.application.workflow.v1:${JSON.stringify([viewId, profile.id, profile.profile_revision])}`;
  const runOperation = useCallback(async (operation: string, payload?: unknown) => {
    const action = profile.actions.find(a => a.operation === operation);
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
      const next = await executeProfileAction(current, registry, action, { workflowId, event,
        payload: { title: "Profile conversation", id: props.convId } });
      sessionRef.current = next.session; setSession(next.session);
      try { localStorage.setItem(sessionKey, serializeProfileSession(next.session)); }
      catch (err) { setError(`Operation completed; saving workflow failed: ${errorText(err)}`); }
    } catch (err) { setError(errorText(err)); }
    finally { setBusy(false); }
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
        {profile.evidence.gaps.map((gap, index) => <p key={index}>{gap}</p>)}
        <ul>{profile.evidence.sources.map((source, i) => <li key={i}><a href={source.url} target="_blank" rel="noopener noreferrer">Reference {i + 1}</a>{source.revision && ` @ ${source.revision}`} {source.note}</li>)}</ul>
        <p>Registered Loom operations are local equivalents. Live inference requires the selected provider; original-service parity is unverified.</p>
        <ul>{profile.actions.map(action => <li key={action.id}>{action.label}: {
          [...availability.requiredGaps, ...availability.optionalGaps].some(g => g.action_id === action.id) ? "unavailable" : "equivalent (Loom adapter)"
        }</li>)}</ul>
        {profile.workflows.map(workflow => {
          const state = currentProfileSession?.workflows[workflow.id] ?? workflow.initial;
          return <div key={workflow.id} data-testid="profile-workflow"><span>{workflow.id}: {state}</span>
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
