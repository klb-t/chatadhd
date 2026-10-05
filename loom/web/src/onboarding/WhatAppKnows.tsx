import { useEffect, useState } from "react";
import type { CSSProperties } from "react";
import { layerExplanation, message, presentationStyle, resolvePresentation, vocabulary } from "./presentation.mjs";
import { ErrorDisplay, OrderedControls, PresentationFailure, PresentationProvider, usePresentation } from "./presentation-context";
import type { OnboardingController } from "./controller";
import { CandidateReview, JsonReadout, useOnboarding } from "./OnboardingPanel";
import type { EffectiveDefault, JsonValue, OnboardingAdapter, OnboardingSnapshot, ProfileField } from "./types";

function KnowledgeEntry({ id, field, label, controller, busy }: {
  id: string; field: ProfileField; label: string; controller: OnboardingController; busy: boolean;
}) {
  const p = usePresentation();
  const current = JSON.stringify(field.value ?? null, null, p.defaults.json_indent);
  const [draft, setDraft] = useState(current);
  const [error, setError] = useState<unknown>(null);
  const disposition = field.question_disposition ?? field.status;
  useEffect(() => { setDraft(current); }, [current]);
  return <fieldset className="onboarding-field" disabled={busy}>
    <legend>{message(p, "knowledge.legend", { label, status: vocabulary(p, "status", field.status), suffix: disposition !== field.status ? message(p, "knowledge.questions_suffix", { disposition: vocabulary(p, "status", disposition) }) : "" })}</legend>
    <JsonReadout value={field} />
    {disposition !== "never" && field.status !== "declined" && <details><summary>{message(p, "knowledge.edit")}</summary>
      <label htmlFor={`knowledge-${id}`}>{message(p, "knowledge.value")}</label>
      <textarea id={`knowledge-${id}`} rows={p.defaults.rows.knowledge} value={draft} onChange={(event) => setDraft(event.target.value)} />
      <button onClick={() => {
        try {
          const value = JSON.parse(draft) as JsonValue;
          setError(null);
          void controller.dispatch({ op: "answer", field: id, value, provenance: "form", id: crypto.randomUUID(), time: new Date().toISOString(), source_refs: [] });
        } catch (failure) { setError(failure); }
      }}>{message(p, "knowledge.correction")}</button>
      {error !== null && <ErrorDisplay error={error} />}
    </details>}
    <div className="onboarding-actions"><OrderedControls group="knowledge_status" entries={{
      delete: <button onClick={() => { void controller.dispatch({ op: "delete", field: id }); }}>{message(p, "knowledge.delete")}</button>,
      unknown: disposition !== "unknown" && <button onClick={() => { void controller.dispatch({ op: "status", field: id, status: "unknown" }); }}>{message(p, "status.reopen")}</button>,
      declined: disposition !== "declined" && <button onClick={() => { void controller.dispatch({ op: "status", field: id, status: "declined" }); }}>{message(p, "status.decline_questions")}</button>,
      never: disposition !== "never" && <button onClick={() => { void controller.dispatch({ op: "status", field: id, status: "never" }); }}>{message(p, "status.never_action")}</button>,
    }} /></div>
  </fieldset>;
}

function DefaultEntry({ entry, controller, busy, connected }: {
  entry: EffectiveDefault; controller: OnboardingController; busy: boolean; connected: boolean;
}) {
  const p = usePresentation();
  const current = JSON.stringify(entry.value ?? null, null, p.defaults.json_indent);
  const [draft, setDraft] = useState(current);
  const [error, setError] = useState<unknown>(null);
  useEffect(() => { setDraft(current); }, [current]);
  return <fieldset className="onboarding-field" disabled={busy || !connected}>
    <legend>{message(p, "default.legend", { label: entry.label ?? entry.id, area: entry.area })}</legend>
    <p>{message(p, "default.summary", { layer: vocabulary(p, "layer_name", entry.layer), enabled: message(p, entry.enabled ? "default.enabled" : "default.disabled"), excluded: entry.excluded ? message(p, "default.excluded") : "" })}</p>
    <p>{layerExplanation(p, entry)}</p>
    <JsonReadout value={entry.value} />
    <div className="onboarding-actions"><OrderedControls group="default" entries={{
      toggle: <button onClick={() => { void controller.dispatchLayer({ op: entry.enabled ? "disable" : "reenable", key: entry.key }); }}>{message(p, entry.enabled ? "default.disable" : entry.excluded ? "default.remove_exclusion" : "default.reenable")}</button>,
      exclude: <button onClick={() => { void controller.dispatchLayer({ op: "exclude", key: entry.key }); }}>{message(p, "default.exclude")}</button>,
      clear_override: <button onClick={() => { void controller.dispatchLayer({ op: "clear_override", key: entry.key }); }}>{message(p, "default.clear_override")}</button>,
      accept_proposal: entry.status === "proposal" && <button onClick={() => { void controller.dispatchLayer({ op: "accept_proposal", key: entry.key }); }}>{message(p, "default.accept_proposal")}</button>,
      proposal: entry.area_mode && <button onClick={() => { void controller.dispatchLayer({ op: "set_area_mode", area: entry.area, mode: "proposal" }); }}>{message(p, "default.proposal")}</button>,
      direct: entry.area_mode && <button onClick={() => { void controller.dispatchLayer({ op: "set_area_mode", area: entry.area, mode: "direct" }); }}>{message(p, "default.direct")}</button>,
    }} /></div>
    <details><summary>{message(p, "default.override")}</summary>
      <label htmlFor={`default-${entry.id}`}>{message(p, "default.value")}</label>
      <textarea id={`default-${entry.id}`} rows={p.defaults.rows.default} value={draft} onChange={(event) => setDraft(event.target.value)} />
      <button onClick={() => {
        try {
          const value = JSON.parse(draft) as JsonValue;
          setError(null); void controller.dispatchLayer({ op: "override", key: entry.key, value });
        } catch (failure) { setError(failure); }
      }}>{message(p, "default.save")}</button>
      {error !== null && <ErrorDisplay error={error} />}
    </details>
    {entry.history && <details><summary>{message(p, "default.history")}</summary><JsonReadout value={entry.history} /></details>}
  </fieldset>;
}

export interface WhatAppKnowsProps { adapter: OnboardingAdapter; locale?: string }

export function WhatAppKnows({ adapter, locale }: WhatAppKnowsProps) {
  const loaded = useOnboarding(adapter);
  let p;
  try { p = resolvePresentation(loaded.snapshot?.presentation, locale); }
  catch (failure) { return <div className="onboarding-panel" data-testid="onboarding-unavailable"><PresentationFailure error={failure} /></div>; }
  return <PresentationProvider presentation={p}>
    {loaded.snapshot && loaded.controller ? <KnowledgeBody controller={loaded.controller} snapshot={loaded.snapshot} busy={loaded.busy} error={loaded.error} errorCause={loaded.errorCause} adapter={adapter} /> :
      <div className="onboarding-panel" style={presentationStyle(p) as CSSProperties} aria-busy={loaded.busy}>{loaded.error ? <><ErrorDisplay error={loaded.errorCause ?? loaded.error} /><button disabled={loaded.busy || !loaded.controller} onClick={() => { void loaded.controller?.load(); }}>{message(p, "profile.retry")}</button></> : <p>{message(p, "profile.loading")}</p>}</div>}
  </PresentationProvider>;
}
function KnowledgeBody({ adapter, controller, snapshot, busy, error, errorCause }: WhatAppKnowsProps & { controller: OnboardingController; snapshot: OnboardingSnapshot; busy: boolean; error: string | null; errorCause?: unknown }) {
  const p = usePresentation();
  const [filter, setFilter] = useState("");
  const labels = new Map(snapshot.scenario.sections.flatMap((section) => section.fields.map((field) => [field.id, field.label] as const)));
  const needle = filter.toLocaleLowerCase();
  const fields = Object.entries(snapshot.fields).filter(([id, field]) => `${id} ${labels.get(id) ?? ""} ${field.category}`.toLocaleLowerCase().includes(needle));
  const defaults = snapshot.defaults?.filter((entry) => `${entry.id} ${entry.label ?? ""} ${entry.area}`.toLocaleLowerCase().includes(needle));
  return <div className="onboarding-panel" data-testid="what-app-knows" style={presentationStyle(p) as CSSProperties} aria-busy={busy}>
    <h2>{message(p, "knowledge.title")}</h2>
    {error && <ErrorDisplay error={errorCause ?? error} />}
    <label htmlFor="knowledge-profile-filter">{message(p, "knowledge.filter")}</label>
    <input id="knowledge-profile-filter" type="search" value={filter} onChange={(event) => setFilter(event.target.value)} />
    <section><h3>{message(p, "knowledge.recorded")}</h3>
      {fields.map(([id, field]) => <KnowledgeEntry key={id} id={id} field={field} label={labels.get(id) ?? id} controller={controller} busy={busy} />)}
    </section>
    <section><h3>{message(p, "knowledge.pending")}</h3>
      {Object.values(snapshot.candidates).filter((candidate) => candidate.review === "pending").map((candidate) => <CandidateReview key={candidate.id} candidate={candidate} controller={controller} busy={busy} />)}
    </section>
    <section><h3>{message(p, "knowledge.defaults")}</h3>
      <p>{message(p, "knowledge.layer_help")}</p>
      {!adapter.dispatchLayer && <p>{message(p, "knowledge.layer_unavailable")}</p>}
      {defaults?.map((entry) => <DefaultEntry key={entry.id} entry={entry} controller={controller} busy={busy} connected={Boolean(adapter.dispatchLayer)} />)}
      {snapshot.defaults === undefined && <p>{message(p, "knowledge.inspection_unavailable")}</p>}
    </section>
    <details><summary>{message(p, "knowledge.history")}</summary><JsonReadout value={snapshot.history} /></details>
  </div>;
}
