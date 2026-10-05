import { useEffect, useState } from "react";
import type { OnboardingController } from "./controller";
import { CandidateReview, JsonReadout, useOnboarding } from "./OnboardingPanel";
import type { EffectiveDefault, JsonValue, OnboardingAdapter, ProfileField } from "./types";

function KnowledgeEntry({ id, field, label, controller, busy }: {
  id: string; field: ProfileField; label: string; controller: OnboardingController; busy: boolean;
}) {
  const current = JSON.stringify(field.value ?? null, null, 2);
  const [draft, setDraft] = useState(current);
  const [error, setError] = useState<string | null>(null);
  const disposition = field.question_disposition ?? field.status;
  useEffect(() => { setDraft(current); }, [current]);
  return <fieldset className="onboarding-field" disabled={busy}>
    <legend>{label} · {field.status}{disposition !== field.status ? ` · questions: ${disposition}` : ""}</legend>
    <JsonReadout value={field} />
    {disposition !== "never" && field.status !== "declined" && <details><summary>Edit recorded information</summary>
      <label htmlFor={`knowledge-${id}`}>Value (JSON)</label>
      <textarea id={`knowledge-${id}`} rows={3} value={draft} onChange={(event) => setDraft(event.target.value)} />
      <button onClick={() => {
        try {
          const value = JSON.parse(draft) as JsonValue;
          setError(null);
          void controller.dispatch({ op: "answer", field: id, value, provenance: "form", id: crypto.randomUUID(), time: new Date().toISOString(), source_refs: [] });
        } catch (failure) { setError(failure instanceof Error ? failure.message : String(failure)); }
      }}>Propose correction</button>
      {error && <p role="alert">{error}</p>}
    </details>}
    <div className="onboarding-actions">
      <button onClick={() => { void controller.dispatch({ op: "delete", field: id }); }}>Delete recorded information</button>
      {disposition !== "unknown" && <button onClick={() => { void controller.dispatch({ op: "status", field: id, status: "unknown" }); }}>Allow questions again</button>}
      {disposition !== "declined" && <button onClick={() => { void controller.dispatch({ op: "status", field: id, status: "declined" }); }}>Decline questions</button>}
      {disposition !== "never" && <button onClick={() => { void controller.dispatch({ op: "status", field: id, status: "never" }); }}>Never ask or infer</button>}
    </div>
  </fieldset>;
}

function DefaultEntry({ entry, controller, busy, connected }: {
  entry: EffectiveDefault; controller: OnboardingController; busy: boolean; connected: boolean;
}) {
  const current = JSON.stringify(entry.value ?? null, null, 2);
  const [draft, setDraft] = useState(current);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => { setDraft(current); }, [current]);
  return <fieldset className="onboarding-field" disabled={busy || !connected}>
    <legend>{entry.label ?? entry.id} · {entry.area}</legend>
    <p>Layer: {entry.layer}. {entry.enabled ? "Enabled" : "Disabled"}. {entry.excluded && "Permanently excluded."}</p>
    <p>{entry.reason}</p>
    <JsonReadout value={entry.value} />
    <div className="onboarding-actions">
      <button onClick={() => { void controller.dispatchLayer({ op: entry.enabled ? "disable" : "reenable", key: entry.key }); }}>{entry.enabled ? "Disable" : entry.excluded ? "Remove permanent exclusion" : "Enable"}</button>
      <button onClick={() => { void controller.dispatchLayer({ op: "exclude", key: entry.key }); }}>Exclude permanently</button>
      <button onClick={() => { void controller.dispatchLayer({ op: "clear_override", key: entry.key }); }}>Clear user override</button>
      {entry.status === "proposal" && <button onClick={() => { void controller.dispatchLayer({ op: "accept_proposal", key: entry.key }); }}>Accept this proposal</button>}
      {entry.area_mode && <>
        <button onClick={() => { void controller.dispatchLayer({ op: "set_area_mode", area: entry.area, mode: "proposal" }); }}>Propose new defaults in this area</button>
        <button onClick={() => { void controller.dispatchLayer({ op: "set_area_mode", area: entry.area, mode: "direct" }); }}>Apply new defaults in this area</button>
      </>}
    </div>
    <details><summary>Override value</summary>
      <label htmlFor={`default-${entry.id}`}>User-layer value (JSON)</label>
      <textarea id={`default-${entry.id}`} rows={3} value={draft} onChange={(event) => setDraft(event.target.value)} />
      <button onClick={() => {
        try {
          const value = JSON.parse(draft) as JsonValue;
          setError(null); void controller.dispatchLayer({ op: "override", key: entry.key, value });
        } catch (failure) { setError(failure instanceof Error ? failure.message : String(failure)); }
      }}>Save override</button>
      {error && <p role="alert">{error}</p>}
    </details>
    {entry.history && <details><summary>Layer history</summary><JsonReadout value={entry.history} /></details>}
  </fieldset>;
}

export interface WhatAppKnowsProps { adapter: OnboardingAdapter }

export function WhatAppKnows({ adapter }: WhatAppKnowsProps) {
  const { controller, snapshot, busy, error } = useOnboarding(adapter);
  const [filter, setFilter] = useState("");
  if (!snapshot || !controller) return <div className="onboarding-panel" aria-busy={busy}>{error ? <><p role="alert">{error}</p><button disabled={busy || !controller} onClick={() => { void controller?.load(); }}>Retry loading</button></> : <p>Loading profile…</p>}</div>;
  const labels = new Map(snapshot.scenario.sections.flatMap((section) => section.fields.map((field) => [field.id, field.label] as const)));
  const needle = filter.toLocaleLowerCase();
  const fields = Object.entries(snapshot.fields).filter(([id, field]) => `${id} ${labels.get(id) ?? ""} ${field.category}`.toLocaleLowerCase().includes(needle));
  const defaults = snapshot.defaults?.filter((entry) => `${entry.id} ${entry.label ?? ""} ${entry.area}`.toLocaleLowerCase().includes(needle));
  return <div className="onboarding-panel" data-testid="what-app-knows" aria-busy={busy}>
    <h2>What the application knows about me</h2>
    {error && <p role="alert">{error}</p>}
    <label htmlFor="knowledge-profile-filter">Find a field, category or default</label>
    <input id="knowledge-profile-filter" type="search" value={filter} onChange={(event) => setFilter(event.target.value)} />
    <section><h3>Recorded information</h3>
      {fields.map(([id, field]) => <KnowledgeEntry key={id} id={id} field={field} label={labels.get(id) ?? id} controller={controller} busy={busy} />)}
    </section>
    <section><h3>Pending confirmations</h3>
      {Object.values(snapshot.candidates).filter((candidate) => candidate.review === "pending").map((candidate) => <CandidateReview key={candidate.id} candidate={candidate} controller={controller} busy={busy} />)}
    </section>
    <section><h3>Defaults and user layers</h3>
      <p>Each effective value includes its source layer and the reason it applies. Permanent exclusions remain until you explicitly remove them.</p>
      {!adapter.dispatchLayer && <p>The host has not connected the native layer editor.</p>}
      {defaults?.map((entry) => <DefaultEntry key={entry.id} entry={entry} controller={controller} busy={busy} connected={Boolean(adapter.dispatchLayer)} />)}
      {snapshot.defaults === undefined && <p>Layer inspection is waiting for the host adapter.</p>}
    </section>
    <details><summary>Change history, including rejected proposals and deletions</summary><JsonReadout value={snapshot.history} /></details>
  </div>;
}
