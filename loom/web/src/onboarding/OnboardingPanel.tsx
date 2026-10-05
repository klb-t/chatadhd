import { useEffect, useState } from "react";
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
  return <pre className="onboarding-json">{JSON.stringify(value, null, 2)}</pre>;
}

function FieldEditor({ field, snapshot, controller, busy, mode }: {
  field: ScenarioField; snapshot: OnboardingSnapshot; controller: OnboardingController;
  busy: boolean; mode: "conversation" | "form";
}) {
  const record = snapshot.fields[field.id];
  const current = draftValue(record?.value, field.input);
  const [draft, setDraft] = useState(current);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => { setDraft(current); setError(null); }, [current]);
  const status = record?.status ?? "unknown";
  const disposition = record?.question_disposition ?? status;
  const blocked = disposition === "never" || disposition === "declined";
  const submit = async () => {
    try {
      const value = parseDraft(draft, field.input);
      setError(null);
      const result = await controller.dispatch({ op: "answer", field: field.id, value,
        provenance: mode === "form" ? "form" : "user_stated",
        id: crypto.randomUUID(), time: new Date().toISOString(), source_refs: [] });
      if (result) setDraft("");
    } catch (failure) { setError(failure instanceof Error ? failure.message : String(failure)); }
  };
  return <fieldset className="onboarding-field" disabled={busy}>
    <legend>{field.label} <small>({status}{disposition !== status ? `; questions: ${disposition}` : ""})</small></legend>
    {blocked ? <p>{disposition === "never" ? "Questions and inference are excluded for this field." : "You declined this question; the interview will not ask it again."}</p> : <>
      <label htmlFor={`onboarding-${field.id}`}>{field.question ?? field.label}</label>
      {field.input === "select" || field.input === "boolean" ? <select id={`onboarding-${field.id}`} value={draft} onChange={(event) => setDraft(event.target.value)}>
        <option value="">Choose a value</option>
        {(field.input === "boolean" ? [{ label: "True", value: true }, { label: "False", value: false }] : field.options ?? []).map((option, index) =>
          <option key={index} value={JSON.stringify(option.value)}>{option.label}</option>)}
      </select> : field.input === "multiline" || field.input === "json" ?
        <textarea id={`onboarding-${field.id}`} rows={3} value={draft} onChange={(event) => setDraft(event.target.value)} /> :
        <input id={`onboarding-${field.id}`} type={field.input === "number" ? "number" : "text"} value={draft} onChange={(event) => setDraft(event.target.value)} />}
      <button onClick={() => { void submit(); }}>Submit for confirmation</button>
    </>}
    {record?.value !== undefined && <details><summary>Recorded value and origin</summary><JsonReadout value={record} /></details>}
    {error && <p role="alert">{error}</p>}
    <div className="onboarding-actions">
      {disposition !== "unknown" && <button onClick={() => { void controller.dispatch({ op: "status", field: field.id, status: "unknown" }); }}>Allow questions again</button>}
      {disposition !== "declined" && <button onClick={() => { void controller.dispatch({ op: "status", field: field.id, status: "declined" }); }}>Decline</button>}
      {disposition !== "never" && <button onClick={() => { void controller.dispatch({ op: "status", field: field.id, status: "never" }); }}>Never ask or infer</button>}
    </div>
  </fieldset>;
}

export function CandidateReview({ candidate, controller, busy }: {
  candidate: ProfileCandidate; controller: OnboardingController; busy: boolean;
}) {
  const [correction, setCorrection] = useState(JSON.stringify(candidate.value, null, 2));
  const [error, setError] = useState<string | null>(null);
  useEffect(() => { setCorrection(JSON.stringify(candidate.value, null, 2)); }, [candidate.value]);
  return <fieldset className="onboarding-field" disabled={busy}>
    <legend>{candidate.field} · {candidate.provenance} · {candidate.review}</legend>
    <JsonReadout value={candidate.value} />
    {candidate.review === "pending" && <>
      <div className="onboarding-actions">
        <button onClick={() => { void controller.dispatch({ op: "review", id: candidate.id, decision: "confirmed" }); }}>Confirm</button>
        <button onClick={() => { void controller.dispatch({ op: "review", id: candidate.id, decision: "rejected" }); }}>Reject</button>
      </div>
      <details><summary>Correct before confirming</summary>
        <label htmlFor={`correction-${candidate.id}`}>Corrected value (JSON)</label>
        <textarea id={`correction-${candidate.id}`} rows={3} value={correction} onChange={(event) => setCorrection(event.target.value)} />
        <button onClick={() => {
          try {
            const value = JSON.parse(correction) as JsonValue;
            setError(null);
            void controller.dispatch({ op: "review", id: candidate.id, decision: "confirmed", value });
          } catch (failure) { setError(failure instanceof Error ? failure.message : String(failure)); }
        }}>Confirm correction</button>
        {error && <p role="alert">{error}</p>}
      </details>
    </>}
    {candidate.source_refs && <details><summary>Sources</summary><JsonReadout value={candidate.source_refs} /></details>}
  </fieldset>;
}

function PrivacyEditor({ rule, controller, busy }: { rule: PrivacyRule; controller: OnboardingController; busy: boolean }) {
  const encoded = JSON.stringify(rule);
  const [draft, setDraft] = useState(rule);
  const [detail, setDetail] = useState(JSON.stringify(rule.max_detail));
  const [sensitivity, setSensitivity] = useState(JSON.stringify(rule.max_sensitivity));
  const [retention, setRetention] = useState(JSON.stringify(rule.retention));
  const [providers, setProviders] = useState(rule.providers.join("\n"));
  const [error, setError] = useState<string | null>(null);
  useEffect(() => {
    setDraft(rule); setDetail(JSON.stringify(rule.max_detail)); setSensitivity(JSON.stringify(rule.max_sensitivity));
    setRetention(JSON.stringify(rule.retention)); setProviders(rule.providers.join("\n")); setError(null);
  }, [encoded]); // Values originate from the native snapshot.
  return <fieldset className="onboarding-field" disabled={busy}>
    <legend>{rule.category}</legend>
    <label className="onboarding-check"><input type="checkbox" checked={draft.store} onChange={(event) => setDraft({ ...draft, store: event.target.checked })} />Allow storage</label>
    <label htmlFor={`detail-${rule.category}`}>Detail (JSON value)</label>
    <input id={`detail-${rule.category}`} type="text" value={detail} onChange={(event) => setDetail(event.target.value)} />
    <label htmlFor={`sensitivity-${rule.category}`}>Sensitivity (JSON value)</label>
    <input id={`sensitivity-${rule.category}`} type="text" value={sensitivity} onChange={(event) => setSensitivity(event.target.value)} />
    <label htmlFor={`providers-${rule.category}`}>Allowed provider IDs, one per line</label>
    <textarea id={`providers-${rule.category}`} value={providers} onChange={(event) => setProviders(event.target.value)} />
    <label className="onboarding-check"><input type="checkbox" checked={draft.infer} onChange={(event) => setDraft({ ...draft, infer: event.target.checked })} />Allow inference</label>
    <label className="onboarding-check"><input type="checkbox" checked={draft.explicit_only} onChange={(event) => setDraft({ ...draft, explicit_only: event.target.checked })} />Only store when I state it explicitly</label>
    <label htmlFor={`retention-${rule.category}`}>Retention (JSON value)</label>
    <input id={`retention-${rule.category}`} type="text" value={retention} onChange={(event) => setRetention(event.target.value)} />
    <button onClick={() => {
      try {
        const next = { ...draft, max_detail: JSON.parse(detail) as JsonValue, max_sensitivity: JSON.parse(sensitivity) as JsonValue,
          retention: JSON.parse(retention) as JsonValue, providers: providers.split("\n").map((item) => item.trim()).filter(Boolean) };
        setError(null); void controller.dispatch({ op: "privacy", rule: next });
      } catch (failure) { setError(failure instanceof Error ? failure.message : String(failure)); }
    }}>Save privacy rule</button>
    {error && <p role="alert">{error}</p>}
  </fieldset>;
}

function ScenarioEditor({ source, controller, busy, supported }: { source: JsonValue; controller: OnboardingController; busy: boolean; supported: boolean }) {
  const current = JSON.stringify(source, null, 2);
  const [draft, setDraft] = useState(current);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => { setDraft(current); }, [current]);
  return <details><summary>Expert: scenario method document</summary>
    <label htmlFor="onboarding-scenario-source">Questions, order, categories and prompts (native method JSON)</label>
    <textarea id="onboarding-scenario-source" rows={14} value={draft} onChange={(event) => setDraft(event.target.value)} />
    <button disabled={busy || !supported} onClick={() => {
      try { const value = JSON.parse(draft) as JsonValue; setError(null); void controller.saveScenario(value); }
      catch (failure) { setError(failure instanceof Error ? failure.message : String(failure)); }
    }}>Save method version</button>
    {!supported && <p>Graph-method editing is waiting for the host adapter.</p>}
    {error && <p role="alert">{error}</p>}
  </details>;
}

function NewPrivacyRule({ controller, busy }: { controller: OnboardingController; busy: boolean }) {
  const [draft, setDraft] = useState("");
  const [error, setError] = useState<string | null>(null);
  return <details><summary>Add or replace another category rule</summary>
    <label htmlFor="new-privacy-rule">Complete rule (JSON)</label>
    <textarea id="new-privacy-rule" rows={6} value={draft} onChange={(event) => setDraft(event.target.value)} />
    <p>Include id, category, store, max_detail, max_sensitivity, providers, infer, explicit_only and retention. Native validation applies the same policy as the category controls.</p>
    <button disabled={busy} onClick={async () => {
      try {
        const value: unknown = JSON.parse(draft);
        if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("Enter a rule object.");
        setError(null);
        if (await controller.dispatch({ op: "privacy", rule: value as PrivacyRule })) setDraft("");
      } catch (failure) { setError(failure instanceof Error ? failure.message : String(failure)); }
    }}>Save category rule</button>
    {error && <p role="alert">{error}</p>}
  </details>;
}

export interface OnboardingPanelProps { adapter: OnboardingAdapter; providerChoices?: { id: string; label: string }[] }

export function OnboardingPanel({ adapter, providerChoices }: OnboardingPanelProps) {
  const { controller, snapshot, reply: ephemeralReply, busy, error } = useOnboarding(adapter);
  const [mode, setMode] = useState<"conversation" | "form">("conversation");
  const [provider, setProvider] = useState("");
  if (!snapshot || !controller) return <div className="onboarding-panel" aria-busy={busy}>{error ? <><p role="alert">{error}</p><button disabled={busy || !controller} onClick={() => { void controller?.load(); }}>Retry loading</button></> : <p>Loading profile…</p>}</div>;
  const section = snapshot.scenario.sections.find((item) => item.id === snapshot.session.section);
  const modelConnected = Boolean(adapter.modelRequest && adapter.completeModelRequest && adapter.ingestModelReply);
  const reply = ephemeralReply ?? snapshot.latest_reply;
  const questions = reply && reply.section === section?.id ? reply.questions.filter((item) => mayAsk(snapshot, item.field)) : [];
  const pending = Object.values(snapshot.candidates).filter((item) => item.review === "pending" && (!item.section || item.section === section?.id));
  return <div className="onboarding-panel" data-testid="onboarding-panel" aria-busy={busy}>
    <h2>{snapshot.scenario.title}</h2>
    <p>Session: {snapshot.session.status}. Method: <code>{snapshot.scenario.method_ref}</code></p>
    {error && <p role="alert">{error}</p>}
    <div className="onboarding-actions">
      <button aria-pressed={mode === "conversation"} onClick={() => setMode("conversation")}>Conversation</button>
      <button aria-pressed={mode === "form"} onClick={() => setMode("form")}>Form</button>
      <button onClick={() => { void controller.dispatch({ op: snapshot.session.status === "paused" ? "resume" : "pause" }); }}>{snapshot.session.status === "paused" ? "Resume" : "Pause"}</button>
    </div>
    <nav className="onboarding-actions" aria-label="Onboarding sections">
      {snapshot.scenario.sections.map((item) => <button key={item.id} disabled={busy} aria-current={item.id === section?.id ? "step" : undefined}
        onClick={() => { void controller.dispatch({ op: "repeat", section: item.id }); }}>{item.title} · {snapshot.session.sections[item.id] ?? "unknown"}</button>)}
    </nav>
    {section ? <section>
      <h3>{section.title}</h3>{section.description && <p>{section.description}</p>}
      {mode === "conversation" && <>
        <p>Submit each answer below. The interview method will summarise it and can ask follow-up questions; confirm or correct each proposed fact before it is recorded.</p>
        {questions.map((question, index) => <p className="onboarding-assistant" key={`${question.field}-${index}`}>{question.text}</p>)}
      </>}
      {section.fields.map((field) => <FieldEditor key={field.id} {...{ field, snapshot, controller, busy, mode }} />)}
      {mode === "conversation" && <fieldset className="onboarding-field" disabled={busy || snapshot.session.status === "paused"}>
        <legend>Interview model</legend>
        <label htmlFor="onboarding-provider">Provider</label>
        <input id="onboarding-provider" type="text" list="onboarding-providers" value={provider} onChange={(event) => setProvider(event.target.value)} />
        <datalist id="onboarding-providers">{providerChoices?.map((item) => <option key={item.id} value={item.id}>{item.label}</option>)}</datalist>
        <button disabled={!modelConnected || !provider.trim()} onClick={() => { void controller.requestModel(provider.trim()); }}>Ask model to summarise and continue</button>
        {!modelConnected && <p>The host must connect native privacy filtering and model transport to enable the interview.</p>}
      </fieldset>}
      {(snapshot.session.summary || reply?.section === section.id && reply.summary) && <div className="onboarding-assistant" role="status">
        <h4>What the model understood</h4><p>{snapshot.session.summary ?? reply?.summary}</p><p>Confirm the proposed facts below, correct them, or reject them.</p>
      </div>}
      {pending.map((candidate) => <CandidateReview key={candidate.id} {...{ candidate, controller, busy }} />)}
      <div className="onboarding-actions">
        <button disabled={busy} onClick={() => { void controller.dispatch({ op: "confirm_section", section: section.id, decision: "confirmed" }); }}>Confirm this section and continue</button>
        <button disabled={busy} onClick={() => { void controller.dispatch({ op: "confirm_section", section: section.id, decision: "corrected" }); }}>Section needs correction</button>
        <button disabled={busy} onClick={() => { void controller.dispatch({ op: "confirm_section", section: section.id, decision: "rejected" }); }}>Reject section summary</button>
        <button disabled={busy} onClick={() => { void controller.dispatch({ op: "skip", section: section.id }); }}>Skip this section</button>
        <button disabled={busy} onClick={() => { void controller.dispatch({ op: "repeat", section: section.id }); }}>Repeat this section</button>
      </div>
    </section> : <p>No active section. Choose a section to repeat or continue.</p>}
    <section><h3>Privacy by category</h3>
      {snapshot.privacy.map((rule) => <PrivacyEditor key={rule.category} {...{ rule, controller, busy }} />)}
      {snapshot.privacy.length === 0 && <p>No category rules were returned by the native profile.</p>}
      <NewPrivacyRule controller={controller} busy={busy} />
    </section>
    <section><h3>New preferences proposed during ordinary conversation</h3>
      <label htmlFor="onboarding-preference-mode">Save mode</label>
      <select id="onboarding-preference-mode" disabled={busy} value={snapshot.settings.preference_mode} onChange={(event) => {
        void controller.dispatch({ op: "settings", settings: { preference_mode: event.target.value as OnboardingSnapshot["settings"]["preference_mode"] } });
      }}><option value="ask">Ask me</option><option value="candidate">Keep as candidate</option><option value="automatic">Save automatically</option></select>
    </section>
    <ScenarioEditor source={snapshot.scenario.source} controller={controller} busy={busy} supported={Boolean(adapter.saveScenario)} />
  </div>;
}

export { JsonReadout };
