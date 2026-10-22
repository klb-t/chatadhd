// M1-W1b: the sheet behind the model chip: which level of the scope chain set the model (or that this window picked it), the class and the
// price. The chain comes from the same call the chat uses for the conversation's model (resolveScope default_model), the class and the price
// from roles.resolved (the role "Odpowiedź na wiadomość") and the model list; a missing piece is said as unknown with its reason.
import { useEffect, useMemo, useRef, useState } from "react";
import { api } from "../api";
import type { ScopeResolution } from "../api/graph-views";
import type { RoleResolved } from "../api/graph-projection";
import type { ModelInfo } from "../api/types";
import { useDialogFocus } from "./use-dialog-focus";
import { useLayer } from "./use-layer";
import { useShellLanguage, useShellText } from "./text";
import { ANSWER_ROLE, provenanceView, type ModelSource } from "./model-chip";
import "../components/context-receipt.css";

interface Props { convId: string | null; model: string; source: ModelSource; models: ModelInfo[]; onClose: () => void }

export const MODEL_KEY = "default_model";

export default function ModelProvenance({ convId, model, source, models, onClose }: Props) {
  const t = useShellText();
  const language = useShellLanguage();
  const ref = useRef<HTMLElement>(null);
  const [resolution, setResolution] = useState<ScopeResolution | null>(null);
  const [resolutionError, setResolutionError] = useState<string | null>(null);
  const [role, setRole] = useState<RoleResolved | null>(null);
  const [ready, setReady] = useState(false);
  useLayer("model-provenance", true, onClose);
  useDialogFocus(true, ref);
  useEffect(() => {
    let live = true;
    const reason = (e: unknown) => (e instanceof Error ? e.message : String(e));
    const chain = api.resolveScope({ key: MODEL_KEY, ...(convId ? { conversation_id: convId } : {}) })
      .then(r => { if (live) { setResolution(r); setResolutionError(null); } }, e => { if (live) setResolutionError(reason(e)); });
    const roles = api.actions
      ? api.actions.rolesResolved({ lang: language, ...(convId ? { conv_id: convId } : {}) })
        .then(doc => { if (live) setRole(doc.roles.find(r => r.id === ANSWER_ROLE) ?? null); }, () => { if (live) setRole(null); })
      : Promise.resolve();
    void Promise.all([chain, roles]).then(() => { if (live) setReady(true); });
    return () => { live = false; };
  }, [convId, language]);
  const view = useMemo(() => provenanceView({ model, source, resolution, resolutionError, role, info: models.find(m => m.id === (model || (typeof resolution?.value === "string" ? resolution.value : ""))) }, t, language),
    [model, source, resolution, resolutionError, role, models, t, language]);
  return (
    <>
      <div className="crs-backdrop" data-testid="model-provenance-backdrop" onClick={onClose} />
      <section className="crs" role="dialog" aria-modal="true" aria-label={t("mp_title")} data-testid="model-provenance" data-state={ready ? "ready" : "loading"}
        data-model={view.effective || undefined} ref={ref} tabIndex={-1}>
        <div className="crs-head">
          <h3 data-testid="model-provenance-title">{t("mp_title")}</h3>
          <button type="button" className="crs-close" aria-label={t("mp_close")} data-testid="model-provenance-close" onClick={onClose}>×</button>
        </div>
        {!ready && <p className="crs-note" data-testid="model-provenance-loading">{t("mp_loading")}</p>}
        {ready && <>
          <ul className="mp-lines" data-testid="model-provenance-lines">
            {view.lines.map(line => <li key={line.id} data-testid={`model-provenance-${line.id}`}>{line.text}</li>)}
          </ul>
          {view.chain.length > 0 && <>
            <p className="crs-note" data-testid="model-provenance-chain-title">{t("mp_chain")}</p>
            <ul className="mp-chain" data-testid="model-provenance-chain">{view.chain.map(l => <li key={l.level} data-level={l.level}>{l.text}</li>)}</ul>
          </>}
          {view.chainRest && <p className="crs-note" data-testid="model-provenance-chain-rest">{view.chainRest}</p>}
          <p className="crs-note" data-testid="model-provenance-change">{t("mp_change")}</p>
        </>}
      </section>
    </>
  );
}
