import { useEffect, useState } from "react";
import type { CSSProperties } from "react";
import { message, presentationStyle, PresentationError, resolvePresentation, vocabulary } from "./presentation.mjs";
import { ErrorDisplay, OrderedControls, PresentationFailure, PresentationProvider, usePresentation } from "./presentation-context";
import { draftValue, mayAsk, OnboardingController, parseDraft } from "./controller";
import type { ControllerState } from "./controller";
import type { JsonValue, OnboardingAdapter, OnboardingSnapshot, PrivacyRule, ProfileCandidate, ScenarioField } from "./types";
import "./onboarding.css";

export function useOnboarding(adapter: OnboardingAdapter) {
  const [controller, setController] = useState<OnboardingController | null>(null);
  const [state, setState] = useState<ControllerState>({ snapshot: null, reply: null, busy: false, error: null });
  useEffect(() => {
    const next = new OnboardingController(adapter);
    setController(next);
    const unsubscribe = next.subscribe(setState);
    void next.load();
    return () => { unsubscribe(); next.dispose(); };
  }, [adapter]);
  return { controller, ...state };
}

function JsonReadout({ value }: { value: unknown }) {
  const p = usePresentation();
  return <pre className="onboarding-json">{JSON.stringify(value, null, p.defaults.json_indent)}</pre>;
}

function FieldEditor({ field, snapshot, controller, busy, mode }: {
  field: ScenarioField; snapshot: OnboardingSnapshot; controller: OnboardingController;
  busy: boolean; mode: "conversation" | "form";
}) {
  const p = usePresentation();
  const record = snapshot.fields[field.id];
  const current = draftValue(record?.value, field.input ?? p.defaults.input, p);
  const [draft, setDraft] = useState(current);
  const [error, setError] = useState<unknown>(null);
  useEffect(() => { setDraft(current); setError(null); }, [current]);
  const status = record?.status ?? "unknown";
  const disposition = record?.question_disposition ?? status;
  const blocked = disposition === "never" || disposition === "declined";
  const submit = async () => {
    try {
      const value = parseDraft(draft, field.input ?? p.defaults.input);
      setError(null);
      const result = await controller.dispatch({ op: "answer", field: field.id, value,
        provenance: mode === "form" ? "form" : "user_stated",
        id: crypto.randomUUID(), time: new Date().toISOString(), source_refs: [] });
      if (result) setDraft("");
    } catch (failure) { setError(failure); }
  };
  return <fieldset className="onboarding-field" disabled={busy}>
    <legend>{message(p, "field.label_prefix", { label: field.label })}<small>{message(p, "field.state", { status: vocabulary(p, "status", status), suffix: disposition !== status ? message(p, "field.questions_suffix", { disposition: vocabulary(p, "status", disposition) }) : "" })}</small></legend>
    {blocked ? <p>{disposition === "never" ? message(p, "field.never") : message(p, "field.declined")}</p> : <>
      <label htmlFor={`onboarding-${field.id}`}>{field.question ?? field.label}</label>
      {(field.input ?? p.defaults.input) === "select" || (field.input ?? p.defaults.input) === "boolean" ? <select id={`onboarding-${field.id}`} value={draft} onChange={(event) => setDraft(event.target.value)}>
        <option value="">{message(p, "field.choose")}</option>
        {((field.input ?? p.defaults.input) === "boolean" ? p.defaults.boolean_options.map(option => ({ ...option, label: message(p, option.label) })) : field.options ?? []).map((option, index) =>
          <option key={index} value={JSON.stringify(option.value)}>{option.label}</option>)}
      </select> : (field.input ?? p.defaults.input) === "multiline" || (field.input ?? p.defaults.input) === "json" ?
        <textarea id={`onboarding-${field.id}`} rows={p.defaults.rows.field} value={draft} onChange={(event) => setDraft(event.target.value)} /> :
        <input id={`onboarding-${field.id}`} type={(field.input ?? p.defaults.input) === "number" ? "number" : "text"} value={draft} onChange={(event) => setDraft(event.target.value)} />}
      <button onClick={() => { void submit(); }}>{message(p, "field.submit")}</button>
    </>}
    {record?.value !== undefined && <details><summary>{message(p, "field.recorded")}</summary><JsonReadout value={record} /></details>}
    {error !== null && <ErrorDisplay error={error} />}
    <div className="onboarding-actions"><OrderedControls group="field_status" entries={{
      unknown: disposition !== "unknown" && <button onClick={() => { void controller.dispatch({ op: "status", field: field.id, status: "unknown" }); }}>{message(p, "status.reopen")}</button>,
      declined: disposition !== "declined" && <button onClick={() => { void controller.dispatch({ op: "status", field: field.id, status: "declined" }); }}>{message(p, "status.decline")}</button>,
      never: disposition !== "never" && <button onClick={() => { void controller.dispatch({ op: "status", field: field.id, status: "never" }); }}>{message(p, "status.never_action")}</button>,
    }} /></div>
  </fieldset>;
}

export function CandidateReview({ candidate, controller, busy }: {
  candidate: ProfileCandidate; controller: OnboardingController; busy: boolean;
}) {
  const p = usePresentation();
  const [correction, setCorrection] = useState(JSON.stringify(candidate.value, null, p.defaults.json_indent));
  const [error, setError] = useState<unknown>(null);
  useEffect(() => { setCorrection(JSON.stringify(candidate.value, null, p.defaults.json_indent)); }, [candidate.value]);
  return <fieldset className="onboarding-field" disabled={busy}>
    <legend>{message(p, "candidate.legend", { field: candidate.field, provenance: vocabulary(p, "provenance", candidate.provenance), review: vocabulary(p, "review_state", candidate.review) })}</legend>
    <JsonReadout value={candidate.value} />
    {candidate.review === "pending" && <>
      <div className="onboarding-actions"><OrderedControls group="candidate_review" entries={{
        confirmed: <button onClick={() => { void controller.dispatch({ op: "review", id: candidate.id, decision: "confirmed" }); }}>{message(p, "review.confirmed")}</button>,
        rejected: <button onClick={() => { void controller.dispatch({ op: "review", id: candidate.id, decision: "rejected" }); }}>{message(p, "review.rejected")}</button>,
      }} /></div>
      <details><summary>{message(p, "review.correct")}</summary>
        <label htmlFor={`correction-${candidate.id}`}>{message(p, "review.corrected_value")}</label>
        <textarea id={`correction-${candidate.id}`} rows={p.defaults.rows.correction} value={correction} onChange={(event) => setCorrection(event.target.value)} />
        <button onClick={() => {
          try {
            const value = JSON.parse(correction) as JsonValue;
            setError(null);
            void controller.dispatch({ op: "review", id: candidate.id, decision: "confirmed", value });
          } catch (failure) { setError(failure); }
        }}>{message(p, "review.confirm_correction")}</button>
        {error !== null && <ErrorDisplay error={error} />}
      </details>
    </>}
    {candidate.source_refs && <details><summary>{message(p, "review.sources")}</summary><JsonReadout value={candidate.source_refs} /></details>}
  </fieldset>;
}

function PrivacyEditor({ rule, controller, busy }: { rule: PrivacyRule; controller: OnboardingController; busy: boolean }) {
  const p = usePresentation();
  const encoded = JSON.stringify(rule);
  const initial = () => Object.fromEntries(p.defaults.privacy_controls.map(control => [control.key,
    control.renderer === "checkbox" ? Boolean(rule[control.key]) : control.renderer === "provider-list" ?
      (Array.isArray(rule[control.key]) ? (rule[control.key] as JsonValue[]).map(String).join(p.defaults.provider_separator) : "") : JSON.stringify(rule[control.key]) ?? ""]));
  const [draft, setDraft] = useState<Record<string, string | boolean>>(initial);
  const [error, setError] = useState<unknown>(null);
  useEffect(() => { setDraft(initial()); setError(null); }, [encoded, p]);
  return <fieldset className="onboarding-field" disabled={busy}>
    <legend>{rule.category}</legend>
    {p.defaults.privacy_controls.map(control => <div key={control.key}>
      {control.renderer === "checkbox" ? <label className="onboarding-check"><input type="checkbox" checked={Boolean(draft[control.key])} onChange={event => setDraft({ ...draft, [control.key]: event.target.checked })} />{message(p, control.label)}</label> : <>
        <label htmlFor={`${control.key}-${rule.category}`}>{message(p, control.label)}</label>
        {control.renderer === "provider-list" ? <textarea id={`${control.key}-${rule.category}`} value={String(draft[control.key])} onChange={event => setDraft({ ...draft, [control.key]: event.target.value })} /> :
          <input id={`${control.key}-${rule.category}`} type="text" value={String(draft[control.key])} onChange={event => setDraft({ ...draft, [control.key]: event.target.value })} />}
      </>}
    </div>)}
    <button onClick={() => {
      try {
        const edits = p.defaults.privacy_controls.map(control => {
          const value = draft[control.key];
          return [control.key, control.renderer === "checkbox" ? Boolean(value) : control.renderer === "provider-list" ?
            String(value).split(p.defaults.provider_separator).map(item => item.trim()).filter(Boolean) : JSON.parse(String(value)) as JsonValue] as const;
        });
        const next = Object.fromEntries([...Object.entries(rule), ...edits]) as PrivacyRule;
        setError(null); void controller.dispatch({ op: "privacy", rule: next });
      } catch (failure) { setError(failure); }
    }}>{message(p, "privacy.save")}</button>
    {error !== null && <ErrorDisplay error={error} />}
  </fieldset>;
}

function ScenarioEditor({ source, controller, busy, supported }: { source: JsonValue; controller: OnboardingController; busy: boolean; supported: boolean }) {
  const p = usePresentation();
  const current = JSON.stringify(source, null, p.defaults.json_indent);
  const [draft, setDraft] = useState(current);
  const [error, setError] = useState<unknown>(null);
  useEffect(() => { setDraft(current); }, [current]);
  return <details><summary>{message(p, "scenario.expert")}</summary>
    <label htmlFor="onboarding-scenario-source">{message(p, "scenario.source")}</label>
    <textarea id="onboarding-scenario-source" rows={p.defaults.rows.scenario} value={draft} onChange={(event) => setDraft(event.target.value)} />
    <button disabled={busy || !supported} onClick={() => {
      try { const value = JSON.parse(draft) as JsonValue; setError(null); void controller.saveScenario(value); }
      catch (failure) { setError(failure); }
    }}>{message(p, "scenario.save")}</button>
    {!supported && <p>{message(p, "scenario.unavailable")}</p>}
    {error !== null && <ErrorDisplay error={error} />}
  </details>;
}

function NewPrivacyRule({ controller, busy }: { controller: OnboardingController; busy: boolean }) {
  const p = usePresentation();
  const [draft, setDraft] = useState("");
  const [error, setError] = useState<unknown>(null);
  return <details><summary>{message(p, "privacy.new")}</summary>
    <label htmlFor="new-privacy-rule">{message(p, "privacy.complete_rule")}</label>
    <textarea id="new-privacy-rule" rows={p.defaults.rows.privacy_rule} value={draft} onChange={(event) => setDraft(event.target.value)} />
    <p>{message(p, "privacy.rule_help")}</p>
    <button disabled={busy} onClick={async () => {
      try {
        const value: unknown = JSON.parse(draft);
        if (!value || typeof value !== "object" || Array.isArray(value)) throw new PresentationError("error.rule_object");
        setError(null);
        if (await controller.dispatch({ op: "privacy", rule: value as PrivacyRule })) setDraft("");
      } catch (failure) { setError(failure); }
    }}>{message(p, "privacy.save_category")}</button>
    {error !== null && <ErrorDisplay error={error} />}
  </details>;
}

export interface OnboardingPanelProps { adapter: OnboardingAdapter; providerChoices?: { id: string; label: string }[]; locale?: string }

export function OnboardingPanel({ adapter, providerChoices, locale }: OnboardingPanelProps) {
  const loaded = useOnboarding(adapter);
  let p;
  try { p = resolvePresentation(loaded.snapshot?.presentation, locale); }
  catch (failure) { return <div className="onboarding-panel" data-testid="onboarding-unavailable"><PresentationFailure error={failure} /></div>; }
  return <PresentationProvider presentation={p}>
    {loaded.snapshot && loaded.controller ? <OnboardingBody {...loaded} snapshot={loaded.snapshot} controller={loaded.controller} adapter={adapter} providerChoices={providerChoices} /> :
      <div className="onboarding-panel" style={presentationStyle(p) as CSSProperties} aria-busy={loaded.busy}>{loaded.error ? <><ErrorDisplay error={loaded.errorCause ?? loaded.error} /><button disabled={loaded.busy || !loaded.controller} onClick={() => { void loaded.controller?.load(); }}>{message(p, "profile.retry")}</button></> : <p>{message(p, "profile.loading")}</p>}</div>}
  </PresentationProvider>;
}
function OnboardingBody({ adapter, providerChoices, controller, snapshot, reply: ephemeralReply, busy, error, errorCause }: OnboardingPanelProps & { controller: OnboardingController; snapshot: OnboardingSnapshot; reply: ControllerState["reply"]; busy: boolean; error: string | null; errorCause?: unknown }) {
  const p = usePresentation();
  const [mode, setMode] = useState(p.defaults.mode);
  const [provider, setProvider] = useState("");
  const section = snapshot.scenario.sections.find((item) => item.id === snapshot.session.section);
  const modelConnected = Boolean(adapter.modelRequest && adapter.completeModelRequest && adapter.ingestModelReply);
  const reply = ephemeralReply ?? snapshot.latest_reply;
  const questions = reply && reply.section === section?.id ? reply.questions.filter((item) => mayAsk(snapshot, item.field)) : [];
  const pending = Object.values(snapshot.candidates).filter((item) => item.review === "pending" && (!item.section || item.section === section?.id));
  return <div className="onboarding-panel" data-testid="onboarding-panel" style={presentationStyle(p) as CSSProperties} aria-busy={busy}>
    <h2>{snapshot.scenario.title}</h2>
    <p>{message(p, "session.prefix", { status: vocabulary(p, "session_status", snapshot.session.status) })}<code>{snapshot.scenario.method_ref}</code></p>
    {error && <ErrorDisplay error={errorCause ?? error} />}
    <div className="onboarding-actions">
      {p.defaults.modes.map(item => <button key={item.id} aria-pressed={mode === item.id} onClick={() => setMode(item.id)}>{message(p, item.label)}</button>)}
      <button onClick={() => { void controller.dispatch({ op: snapshot.session.status === "paused" ? "resume" : "pause" }); }}>{snapshot.session.status === "paused" ? message(p, "session.resume") : message(p, "session.pause")}</button>
    </div>
    <nav className="onboarding-actions" aria-label={message(p, "session.sections")}>
      {snapshot.scenario.sections.map((item) => <button key={item.id} disabled={busy} aria-current={item.id === section?.id ? "step" : undefined}
        onClick={() => { void controller.dispatch({ op: "repeat", section: item.id }); }}>{message(p, "session.section_label", { title: item.title, status: vocabulary(p, "session_status", snapshot.session.sections[item.id] ?? "unknown") })}</button>)}
    </nav>
    {section ? <section>
      <h3>{section.title}</h3>{section.description && <p>{section.description}</p>}
      {mode === "conversation" && <>
        <p>{message(p, "conversation.help")}</p>
        {questions.map((question, index) => <p className="onboarding-assistant" key={`${question.field}-${index}`}>{question.text}</p>)}
      </>}
      {section.fields.map((field) => <FieldEditor key={field.id} {...{ field, snapshot, controller, busy, mode }} />)}
      {mode === "conversation" && <fieldset className="onboarding-field" disabled={busy || snapshot.session.status === "paused"}>
        <legend>{message(p, "conversation.model")}</legend>
        <label htmlFor="onboarding-provider">{message(p, "conversation.provider")}</label>
        <input id="onboarding-provider" type="text" list="onboarding-providers" value={provider} onChange={(event) => setProvider(event.target.value)} />
        <datalist id="onboarding-providers">{providerChoices?.map((item) => <option key={item.id} value={item.id}>{item.label}</option>)}</datalist>
        <button disabled={!modelConnected || !provider.trim()} onClick={() => { void controller.requestModel(provider.trim()); }}>{message(p, "conversation.continue")}</button>
        {!modelConnected && <p>{message(p, "conversation.unavailable")}</p>}
      </fieldset>}
      {(snapshot.session.summary || reply?.section === section.id && reply.summary) && <div className="onboarding-assistant" role="status">
        <h4>{message(p, "conversation.understood")}</h4><p>{snapshot.session.summary ?? reply?.summary}</p><p>{message(p, "conversation.confirm_help")}</p>
      </div>}
      {pending.map((candidate) => <CandidateReview key={candidate.id} {...{ candidate, controller, busy }} />)}
      <div className="onboarding-actions"><OrderedControls group="section" entries={{
        confirmed: <button disabled={busy} onClick={() => { void controller.dispatch({ op: "confirm_section", section: section.id, decision: "confirmed" }); }}>{message(p, "section.confirmed")}</button>,
        corrected: <button disabled={busy} onClick={() => { void controller.dispatch({ op: "confirm_section", section: section.id, decision: "corrected" }); }}>{message(p, "section.corrected")}</button>,
        rejected: <button disabled={busy} onClick={() => { void controller.dispatch({ op: "confirm_section", section: section.id, decision: "rejected" }); }}>{message(p, "section.rejected")}</button>,
        skip: <button disabled={busy} onClick={() => { void controller.dispatch({ op: "skip", section: section.id }); }}>{message(p, "section.skip")}</button>,
        repeat: <button disabled={busy} onClick={() => { void controller.dispatch({ op: "repeat", section: section.id }); }}>{message(p, "section.repeat")}</button>,
      }} /></div>
    </section> : <p>{message(p, "section.none")}</p>}
    <section><h3>{message(p, "privacy.title")}</h3>
      {snapshot.privacy.map((rule) => <PrivacyEditor key={rule.category} {...{ rule, controller, busy }} />)}
      {snapshot.privacy.length === 0 && <p>{message(p, "privacy.empty")}</p>}
      <NewPrivacyRule controller={controller} busy={busy} />
    </section>
    <section><h3>{message(p, "preferences.title")}</h3>
      <label htmlFor="onboarding-preference-mode">{message(p, "preferences.save_mode")}</label>
      <select id="onboarding-preference-mode" disabled={busy} value={snapshot.settings.preference_mode} onChange={(event) => {
        void controller.dispatch({ op: "settings", settings: { preference_mode: event.target.value as OnboardingSnapshot["settings"]["preference_mode"] } });
      }}>{p.defaults.preference_modes.map(item => <option key={item.id} value={item.id}>{message(p, item.label)}</option>)}</select>
    </section>
    <ScenarioEditor source={snapshot.scenario.source} controller={controller} busy={busy} supported={Boolean(adapter.saveScenario)} />
  </div>;
}

export { JsonReadout };
