import { useEffect, useMemo, useRef, useState, useSyncExternalStore, type ReactNode } from "react";
import type { UserProfileHost } from "../api/onboarding-host";
import type { KnowledgeApi, KnowledgeRecord } from "../api/knowledge";
import type { JsonValue, LayerAction } from "../onboarding/types";
import { NativeLayerClient, nativeResolutionFromDefaults } from "../graph-perspectives/native-layers";
import type { NativeResolution, SelectionResult } from "../graph-perspectives/types";
import { createKnowledgePerspectiveSource, perspectiveEnabledKey, perspectivePack, perspectiveText as t, selectKnowledgePerspective } from "./knowledge-perspective";

export interface PerspectiveControlsProps {
  host: UserProfileHost; knowledge: KnowledgeApi; run: string; limit: number; focus: string;
  entities: KnowledgeRecord[]; claims: KnowledgeRecord[]; errors: string[]; loaded: boolean;
  onFocus(entity: KnowledgeRecord): void;
  children(selection: SelectionResult | null, active: boolean): ReactNode;
}
export default function PerspectiveControls({ host, knowledge, run, limit, focus, entities, claims, errors, loaded, onFocus, children }: PerspectiveControlsProps) {
  const identity = useSyncExternalStore(host.subscribe, host.getState, host.getState);
  const adapter = host.getAdapter();
  const lastEpoch = useRef<string | null>(null);
  const writesBlocked = Boolean(identity.session?.reloadRequired);
  const client = useMemo(() => adapter ? new NativeLayerClient(adapter) : null, [adapter]);
  const [resolutionOwner, setResolutionOwner] = useState<string | null>(null);
  const [resolution, setResolution] = useState<NativeResolution | null>(null);
  const [error, setError] = useState("");
  const [pending, setPending] = useState(false);
  const [selection, setSelection] = useState<SelectionResult | null>(null);
  const [component, setComponent] = useState(perspectiveEnabledKey);
  const [draft, setDraft] = useState("");
  const enabled = resolutionOwner === identity.userId ? resolution?.components.find(row => row.id === perspectiveEnabledKey) : undefined;
  const active = enabled?.status === "effective" && enabled.value === true;
  const missing = Boolean(resolution && perspectivePack.entries.some(entry => !resolution.components.some(row => row.id === entry.key)));
  const selectedComponent = resolution?.components.find(row => row.id === component);
  const reload = async () => {
    if (!client) return;
    setPending(true); setError("");
    try { setResolution(await client.resolve()); setResolutionOwner(identity.userId); }
    catch (cause) { setError(String(cause)); setResolution(null); }
    finally { setPending(false); }
  };
  useEffect(() => {
    let live = true;
    const epoch = JSON.stringify([identity.userId, identity.viewEpoch]);
    const explicitReload = lastEpoch.current !== null && lastEpoch.current !== epoch;
    lastEpoch.current = epoch;
    if (identity.session?.reloadRequired && !explicitReload) {
      setError(identity.session.error ?? t.reload); setPending(false);
      return () => { live = false; };
    }
    setError("");
    if (!client) { setResolution(null); setSelection(null); }
    if (client) {
      const snapshot = !explicitReload ? host.currentSnapshot() : null;
      if (snapshot?.defaults) {
        setResolution(nativeResolutionFromDefaults(snapshot.defaults)); setResolutionOwner(identity.userId); setPending(false);
        return () => { live = false; };
      }
      setPending(true);
      // Several panes share this session. Its initial read may establish the
      // revision while another pane is queued; unmounting/re-rendering a view
      // must not cancel that session's read or turn it into an uncertain state.
      client.resolve().then(value => { if (live) { setResolution(value); setResolutionOwner(identity.userId); } })
        .catch(cause => { if (live) setError(String(cause)); }).finally(() => { if (live) setPending(false); });
    }
    return () => { live = false; };
  }, [host, client, identity.userId, identity.viewEpoch, identity.session?.revision]);
  useEffect(() => { setDraft(selectedComponent && "value" in selectedComponent ? JSON.stringify(selectedComponent.value, null, 2) : ""); }, [selectedComponent]);
  const source = useMemo(() => run && loaded ? createKnowledgePerspectiveSource(knowledge, run, limit, {
    entities, claims, partial: entities.length >= limit || claims.length >= limit, errors,
  }) : null, [knowledge, run, limit, entities, claims, errors, loaded]);
  useEffect(() => {
    let live = true; const controller = new AbortController();
    setSelection(null);
    if (!active || !resolution || !source || !focus) return () => controller.abort();
    setError("");
    const sourceId = source.adapter.descriptor.id;
    selectKnowledgePerspective({ schema: "loom.graph_perspective/1", id: `workspace:${run}`, components: {}, focus: source.ref(focus) }, resolution, source,
      { id: `authenticated-local-view:${run}`, canRead: ref => ref.source === sourceId }, controller.signal)
      .then(value => { if (live) setSelection(value); }).catch(cause => { if (live) setError(String(cause)); });
    return () => { live = false; controller.abort(); };
  }, [active, resolution, source, focus, run]);
  const dispatch = async (action: LayerAction) => {
    if (!client) return;
    setPending(true); setError("");
    try { setResolution(await client.dispatch(action)); setResolutionOwner(identity.userId); }
    catch (cause) { setError(String(cause)); }
    finally { setPending(false); }
  };
  const install = async () => {
    setPending(true); setError("");
    try { await host.installDefaultEntries(perspectivePack); if (client) setResolution(await client.resolve()); }
    catch (cause) { setError(String(cause)); }
    finally { setPending(false); }
  };
  const apply = () => {
    try { const value: JsonValue = JSON.parse(draft); void dispatch({ op: "override", key: component, value }); }
    catch (cause) { setError(String(cause)); }
  };
  return <div data-testid="product-perspective">
    <details className="kb-detail"><summary>{t.title}</summary>
      {!client ? <p>{t.unavailable}</p> : <>
        <p>{t.profile}: <code>{identity.userId}</code></p>
        {missing && <><p>{t.missing}</p><button type="button" disabled={pending || writesBlocked} onClick={() => void install()}>{t.install}</button></>}
        <label><input type="checkbox" aria-label={t.enable} checked={active} disabled={pending || writesBlocked || !enabled || enabled.status !== "effective"}
          onChange={event => void dispatch({ op: "override", key: perspectiveEnabledKey, value: event.target.checked })} />{t.enable}</label>
        <button type="button" disabled={pending} onClick={() => void reload()}>{t.reload}</button>
        {active && <label>{t.focus}<select aria-label={t.focus} value={focus} onChange={event => { const entity = entities.find(row => row.id === event.target.value); if (entity) onFocus(entity); }}>
          {!entities.some(row => row.id === focus) && <option value={focus}>{focus || t.choose}</option>}
          {entities.map(entity => <option key={String(entity.id)} value={String(entity.id)}>{String(entity.label || entity.id)}</option>)}
        </select></label>}
        <p>{t.localOnly}</p>
        <details className="kb-detail"><summary>{t.advanced}</summary>
          <label>{t.component}<select aria-label={t.component} value={component} onChange={event => setComponent(event.target.value)}>
            {perspectivePack.entries.map(entry => <option key={entry.key} value={entry.key}>{entry.label}</option>)}
          </select></label>
          <label>{t.value}<textarea aria-label={t.value} value={draft} onChange={event => setDraft(event.target.value)} /></label>
          <button type="button" disabled={pending || writesBlocked || !selectedComponent} onClick={apply}>{t.apply}</button>
          {(["disable", "exclude", "reenable", "clear_override", "accept_proposal"] as const).map(op => <button key={op} type="button" disabled={pending || writesBlocked || !selectedComponent}
            onClick={() => void dispatch({ op, key: component })}>{t[op === "clear_override" ? "clear" : op]}</button>)}
        </details>
      </>}
      <p>{t.limited}</p>
      <details className="kb-detail"><summary>{t.receipt}</summary><pre data-testid="product-perspective-receipt">{JSON.stringify({ profile: identity.userId,
        profile_state: identity.session, resolution, selection: selection && { plan: selection.plan, permission: selection.permissionId, omissions: selection.omissions,
          complete: selection.complete, metrics: selection.metrics, visible: selection.objects.map(item => item.ref), analysis: selection.analysis } }, null, 2)}</pre></details>
    </details>
    {error && <p role="alert">{error}</p>}
    {active && !focus && <p role="status">{t.noFocus}</p>}
    {active && focus && !selection && !error && <p role="status">{t.loading}</p>}
    {selection && !selection.complete && <p role="status">{t.partial}</p>}
    {children(writesBlocked ? null : selection, active || writesBlocked || Boolean(client && error && (!resolution || resolutionOwner !== identity.userId)))}
  </div>;
}
