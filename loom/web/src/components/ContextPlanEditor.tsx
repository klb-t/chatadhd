import type { PlanDraft, ThesisDraft } from "../context/retrieval-plan";
import { newThesis } from "../context/retrieval-plan";

interface Props {
  enabled: boolean;
  onEnabled: (value: boolean) => void;
  draft: PlanDraft;
  onChange: (value: PlanDraft) => void;
}

export default function ContextPlanEditor({ enabled, onEnabled, draft, onChange }: Props) {
  function update(key: number, patch: Partial<ThesisDraft>) {
    onChange({ ...draft, theses: draft.theses.map((item) => item.key === key ? { ...item, ...patch } : item) });
  }
  return (
    <div className="chat-context-plan">
      <label className="chat-context-record"><input type="checkbox" checked={enabled} onChange={(e) => onEnabled(e.target.checked)} data-testid="use-context-plan" />Explicit retrieval plan</label>
      <p>Optional queries you write for this request. No task or thesis is inferred. Editing the plan makes no requests.</p>
      {enabled && <>
        <div className="chat-context-fields">
          <label>Plan ID<input value={draft.id} onChange={(e) => onChange({ ...draft, id: e.target.value })} data-testid="context-plan-id" /></label>
          <label>Source reference (optional JSON)<textarea value={draft.sourceRef} onChange={(e) => onChange({ ...draft, sourceRef: e.target.value })} rows={2} placeholder={'{"note":"my outline"}'} data-testid="context-plan-source" /></label>
        </div>
        <p>The source reference is your declaration of origin, not verified provenance. Theses share the knowledge token budget in order, weighted below; unused capacity passes forward. Detailed items can leave less room.</p>
        {draft.theses.map((item, index) => <fieldset key={item.key} className="chat-plan-thesis" data-testid="context-plan-thesis">
          <legend>Thesis {index + 1}</legend>
          <div className="chat-context-fields">
            <label>Thesis ID<input value={item.id} onChange={(e) => update(item.key, { id: e.target.value })} data-testid="thesis-id" /></label>
            <label>Thesis query<textarea value={item.text} onChange={(e) => update(item.key, { text: e.target.value })} rows={2} data-testid="thesis-text" /></label>
          </div>
          <details>
            <summary>Thesis anchors, reach and budget</summary>
            <div className="chat-context-fields">
              <label>Entity anchors<select value={item.targetsMode} onChange={(e) => update(item.key, { targetsMode: e.target.value as ThesisDraft["targetsMode"] })} data-testid="thesis-targets-mode">
                <option value="inherit">Inherit request targets</option><option value="custom">Specify IDs · empty clears targets</option>
              </select></label>
              {item.targetsMode === "custom" && <label>Entity IDs · one per line<textarea value={item.targets} onChange={(e) => update(item.key, { targets: e.target.value })} rows={2} data-testid="thesis-targets" /></label>}
              <label>Claim anchors<select value={item.claimsMode} onChange={(e) => update(item.key, { claimsMode: e.target.value as ThesisDraft["claimsMode"] })} data-testid="thesis-claims-mode">
                <option value="inherit">Inherit request claims</option><option value="custom">Specify IDs · empty clears claims</option>
              </select></label>
              {item.claimsMode === "custom" && <label>Claim IDs · one per line<textarea value={item.claims} onChange={(e) => update(item.key, { claims: e.target.value })} rows={2} data-testid="thesis-claims" /></label>}
              <label>Thesis graph reach<input type="number" min="0" max="2147483647" step="1" value={item.hops} onChange={(e) => update(item.key, { hops: e.target.value })} placeholder="Inherit request reach" data-testid="thesis-hops" /></label>
              <label>Thesis item detail<select value={item.detail} onChange={(e) => update(item.key, { detail: e.target.value as ThesisDraft["detail"] })} data-testid="thesis-detail">
                <option value="inherit">Inherit request detail</option><option value="label">Label</option><option value="summary">Summary</option><option value="full">Full</option><option value="raw">Raw</option>
              </select></label>
              <label>Budget weight<input type="number" min="0" step="any" value={item.weight} onChange={(e) => update(item.key, { weight: e.target.value })} data-testid="thesis-weight" /></label>
            </div>
            <label className="chat-context-record"><input type="checkbox" checked={item.counter} onChange={(e) => update(item.key, { counter: e.target.checked })} data-testid="thesis-counter" />Follow recorded counter-evidence links</label>
            <p>Uses existing stored relations. It does not ask a model to prove contradictions; missing links do not establish consistency.</p>
          </details>
          <button type="button" onClick={() => onChange({ ...draft, theses: draft.theses.filter((other) => other.key !== item.key) })}>Remove thesis {index + 1}</button>
        </fieldset>)}
        <button type="button" data-testid="add-context-thesis" onClick={() => onChange({ ...draft, theses: [...draft.theses, newThesis(draft.theses.reduce((max, item) => Math.max(max, item.key), 0) + 1)] })}>Add thesis</button>
      </>}
    </div>
  );
}
