import { useMemo, useState, useSyncExternalStore } from "react";
import type { UserProfileHost } from "../api/onboarding-host";
import { conversationViewWireJson, resourceReadChoices, type ConversationView } from "../api/conversation-view";
import { PresentationFailure } from "../onboarding/presentation-context";
import { featureMessage, message, PresentationError, resolvePresentation, resolvePresentationFeature } from "../onboarding/presentation.mjs";
import featurePack from "../onboarding/generated/conversation-view.json";

const noSubscription = () => () => undefined;
const noIdentity = Object.freeze({ userId: null, session: null });
const bootstrapIdentity = () => noIdentity;

export function useConversationSourcePresentation(profileHost?: UserProfileHost) {
  const identity = useSyncExternalStore<{ userId: string | null; session: unknown }>(profileHost?.subscribe ?? noSubscription,
    profileHost?.getState ?? bootstrapIdentity, profileHost?.getState ?? bootstrapIdentity);
  return useMemo(() => {
    let base;
    try {
      const snapshot = profileHost?.currentSnapshot();
      if (identity.userId !== null && !snapshot?.presentation)
        throw new PresentationError("error.presentation", { reason: "profile_snapshot_unavailable" }, true);
      base = resolvePresentation(identity.userId === null ? undefined : snapshot!.presentation);
      const entry = snapshot?.defaults?.find(row => row.key === "presentation.conversation_view") ?? null;
      return { base, feature: resolvePresentationFeature(base, "conversation_view", identity.userId === null ? undefined : entry),
        missingEntry: false, error: null };
    } catch (error) {
      const snapshot = profileHost?.currentSnapshot();
      return { base, feature: null, missingEntry: Boolean(base && identity.userId !== null && snapshot &&
        !snapshot.defaults?.some(row => row.key === "presentation.conversation_view")), error };
    }
  }, [profileHost, identity]);
}

type Catalog = ReturnType<typeof useConversationSourcePresentation>;
interface Props {
  view: ConversationView;
  profileHost?: UserProfileHost;
  catalog: Catalog;
  busy: boolean;
  onRead: (options: Record<string, unknown>) => Promise<void>;
  onReload: () => void;
}
export default function ConversationSourceView({ view, profileHost, catalog, busy, onRead, onReload }: Props) {
  const [overrides, setOverrides] = useState<Record<string, string>>({});
  const [installError, setInstallError] = useState<unknown>(null);
  const [installing, setInstalling] = useState(false);
  const configuration = useMemo(() => {
    try {
      const choices = resourceReadChoices(view);
      if (catalog.feature) for (const option of choices) {
        featureMessage(catalog.feature, option.key);
        for (const choice of option.choices) featureMessage(catalog.feature, choice);
      }
      return { choices, error: null };
    }
    catch (error) { return { choices: [], error }; }
  }, [view, catalog.feature]);
  if (!catalog.feature || !catalog.base) return <section data-testid="conversation-sources" aria-live="polite">
    {catalog.base && <h3>{message(catalog.base, "review.sources")}</h3>}
    <PresentationFailure error={catalog.error} />
    {catalog.missingEntry && profileHost && <>
      <p>{featurePack.entries[0].label}</p>
      <button type="button" data-testid="install-conversation-source-catalog" disabled={installing}
        onClick={() => { setInstalling(true); setInstallError(null);
          void profileHost.installDefaultEntries(featurePack).catch(setInstallError).finally(() => setInstalling(false)); }}>
        {message(catalog.base!, "default.accept_proposal")}
      </button>
    </>}
    {installError !== null && <PresentationFailure error={installError} />}
  </section>;
  const f = catalog.feature;
  const text = (id: string) => featureMessage(f, id);
  const valid = !configuration.error && configuration.choices.every(option =>
    option.choices.includes(overrides[option.key] ?? option.value));
  return <section className="chat-context-controls" data-testid="conversation-sources" aria-live="polite">
    <h3>{text("title")}</h3>
    <p data-testid="conversation-view-status" data-status={view.status}>{text(view.status)}</p>
    <p>{text("read_scope")}</p>
    <p>{text("egress_unavailable")}</p>
    <fieldset disabled={busy}>
      <legend>{text("configuration")}</legend>
      {configuration.choices.map(option => <label key={option.key}>{text(option.key)}
        <select data-testid={`source-read-${option.key}`} value={overrides[option.key] ?? option.value}
          onChange={event => setOverrides(current => ({ ...current, [option.key]: event.target.value }))}>
          {option.choices.map(value => <option key={value} value={value}>{text(value)}</option>)}
        </select>
      </label>)}
      <p>{text("retention")}</p>
      {configuration.error !== null && <><p role="status">{text("configuration_unavailable")}</p><PresentationFailure error={configuration.error} /></>}
      <button type="button" onClick={() => void onRead(overrides)} disabled={!valid || busy} data-testid="read-conversation-sources">
        {text(busy ? "reading" : "local_read")}
      </button>
      <button type="button" onClick={onReload} disabled={busy} data-testid="reload-conversation-source-metadata">{text("reload")}</button>
    </fieldset>
    {view.resources.map((resource, index) => <div key={String(resource.unit_id ?? index)} data-testid="conversation-source-status">
      <code>{JSON.stringify({ unit_id: resource.unit_id, source: resource.source, status: resource.status,
        current: resource.current, mapping_status: resource.mapping_status, coverage: resource.coverage })}</code>
    </div>)}
    <details data-testid="conversation-view-evidence"><summary>{text("details")}</summary>
      <p>{text("decoded_projection")}</p>
      <h4>{text("exact_wire")}</h4>
      <pre data-testid="conversation-view-exact-json">{conversationViewWireJson(view) ?? text("wire_unavailable")}</pre>
    </details>
  </section>;
}
