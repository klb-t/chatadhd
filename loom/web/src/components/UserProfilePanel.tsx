import { useEffect, useState, useSyncExternalStore } from "react";
import { OnboardingPanel, WhatAppKnows } from "../onboarding";
import type { UserProfileHost } from "../api/onboarding-host";
import "./user-profile-panel.css";

export interface UserProfilePanelProps {
  view: "onboarding" | "knowledge";
  host: UserProfileHost;
}

export default function UserProfilePanel({ view, host }: UserProfilePanelProps) {
  const state = useSyncExternalStore(host.subscribe, host.getState, host.getState);
  const [draft, setDraft] = useState(state.userId ?? "");
  const [error, setError] = useState<string | null>(null);
  useEffect(() => { setDraft(state.userId ?? ""); setError(null); }, [state.userId]);
  const choose = (userId: string | null) => {
    try { host.selectUser(userId); setError(null); }
    catch (cause) { setError(cause instanceof Error ? cause.message : String(cause)); }
  };
  const adapter = host.getAdapter();
  return <section className="user-profile-host" data-testid="user-profile-host">
    <form className="user-profile-selector" onSubmit={event => { event.preventDefault(); choose(draft); }}>
      <label htmlFor="user-profile-identity">Native profile ID</label>
      <input id="user-profile-identity" data-testid="user-profile-identity" value={draft} onChange={event => setDraft(event.target.value)} autoComplete="off" />
      <div className="user-profile-actions">
        <button type="submit" disabled={!host.available || !draft.trim()} data-testid="user-profile-open">Open profile</button>
        <button type="button" disabled={!host.available} onClick={() => choose(crypto.randomUUID())} data-testid="user-profile-new">Create new profile ID</button>
        {state.userId && <button type="button" onClick={() => choose(null)} data-testid="user-profile-unload">Unload profile</button>}
      </div>
    </form>
    {error && <p role="alert">{error}</p>}
    {state.storageError && <p role="alert">{state.storageError}</p>}
    {!host.available && <p>This host has no native onboarding bridge.</p>}
    {host.available && !state.userId && <p>Choose an existing profile ID or create a new one. Profile data and history are stored in the native database; only the selected ID is saved in browser settings.</p>}
    {state.userId && adapter && <>
      <div className="user-profile-status">
        <span>Profile: <code data-testid="user-profile-selected">{state.userId}</code>. Revision: <code data-testid="user-profile-revision">{state.session?.revision ?? "not loaded"}</code>.</span>
        <button type="button" disabled={Boolean(state.session?.active)} onClick={() => host.reload()} data-testid="user-profile-reload">Reload native profile</button>
      </div>
      {state.session?.error && <p role="alert" data-testid="user-profile-host-error">{state.session.error}</p>}
      {state.session?.outcomeUnknown && <p data-testid="user-profile-unknown-outcome">A previous native write has an unverified outcome. Inspect the native state before repeating it.</p>}
      {view === "onboarding" ? <OnboardingPanel key={`${state.userId}:${state.viewEpoch}:onboarding`} adapter={adapter} />
        : <WhatAppKnows key={`${state.userId}:${state.viewEpoch}:knowledge`} adapter={adapter} />}
    </>}
  </section>;
}
