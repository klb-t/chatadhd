// M1-W0 (4), package P6: Modele › Role, "Kto wykonuje którą pracę" (spec §3.6). The table draws the roles roles.resolved answers, in the
// order of the data; a tap opens the card of a role: why it exists, which model does it now and how the model was chosen (the chain
// of the policy, the kernel's own sentence for every step, the step that decides marked), the class of the model WITH the reason, the
// flow of every class (the class decides which flow runs; "kalibracja" where the data says so), and EVERY flow that reads the role's
// setting. This is the answer to "why can I choose a model for the semantic analysis and how do its flows differ".
// Nothing here lists a role, a class or a flow (words around the data: presentation.roles; a missing word shows its id). A change of
// the model opens the node card of the setting (NodeCard.tsx), so there is still one place that writes a value. Back: the open card
// is one history layer (Android Back / browser Back / Escape close it first).
import { useCallback, useEffect, useMemo, useState } from "react";
import { api } from "../api";
import type { RoleResolved, RolesResolved } from "../api/graph-projection";
import type { UserProfileHost } from "../api/onboarding-host";
import { shellTranslator, type ShellT } from "../shell/text";
import { useLayer } from "../shell/use-layer";
import Disclosure from "../shell/Disclosure";
import { usePresentationFeature } from "./ConversationSourceView";
import NodeCard from "./NodeCard";
import { chainIsRead, chainSteps, flowsByClass, flowView, modeView, priceText, readers, roleRows, settingAddress, stateChip, todaySteps, weekText, type FlowView } from "./role-card-state";
import "./graph-navigator.css";
import "./role-card.css";

interface TableProps {
  profileHost?: UserProfileHost;
  convId: string | null;
  /** The language of the words (ui_language or the browser's), the same the kernel is asked for. */
  language: string;
  /** A related node tapped on a setting's card: the navigator opens there. */
  onNavigate?: (address: string) => void;
  /** A role card opened or closed: the screen around hides what is not the card while one is open (one thing on a phone at a time). */
  onOpenChange?: (open: boolean) => void;
  /** The Polish label of every settings field by key (from settings.fields): the readers list names the key a flow reads in words. */
  fieldLabels?: Record<string, string>;
}

export function RolesTable({ profileHost, convId, language, onNavigate, onOpenChange, fieldLabels }: TableProps) {
  const words = usePresentationFeature(profileHost, "roles");
  const graphWords = usePresentationFeature(profileHost, "graph_screen");
  const t = useMemo(() => shellTranslator(words.feature, language), [words.feature, language]);
  const graphT = useMemo(() => shellTranslator(graphWords.feature, language), [graphWords.feature, language]);
  const [doc, setDoc] = useState<RolesResolved | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [generation, setGeneration] = useState(0);
  const [openRole, setOpenRole] = useState<string | null>(null);

  useEffect(() => {
    let alive = true;
    const actions = api.actions;
    if (!actions) { setError(t("error.no_actions")); return; }   // a catalogue sentence, never a developer id
    actions.rolesResolved({ lang: language, ...(convId ? { conv_id: convId } : {}) })
      .then(resolved => { if (alive) { setDoc(resolved); setError(null); } })
      .catch(failure => { if (alive) setError(failure instanceof Error ? failure.message : String(failure)); });
    return () => { alive = false; };
  }, [language, convId, generation]);

  const reload = useCallback(() => setGeneration(g => g + 1), []);
  useLayer("role-card", openRole !== null, () => setOpenRole(null));
  useEffect(() => { onOpenChange?.(openRole !== null); }, [openRole, onOpenChange]);
  const role = doc && openRole !== null ? doc.roles.find(r => r.id === openRole) ?? null : null;
  const rows = useMemo(() => doc ? roleRows(doc, t) : [], [doc, t]);

  return (
    <section className="rolc" data-testid="roles" aria-label={t("title")}>
      {!doc && !error && <p className="gnav-note" data-testid="roles-loading">{t("loading")}</p>}
      {error && (
        <div className="gnav-note gnav-error" role="alert" data-testid="roles-error">
          <p>{t("error", { reason: error })}</p>
          <button type="button" className="gnav-bar-btn" onClick={reload} data-testid="roles-retry">{t("retry")}</button>
        </div>
      )}
      {doc && role && <RoleCard role={role} roles={doc.roles} t={t} graphT={graphT} language={language} convId={convId} fieldLabels={fieldLabels}
        onBack={() => setOpenRole(null)} onNavigate={onNavigate} onChanged={reload} />}
      {doc && !role && (
        <>
          <h3 className="rolc-title" data-testid="roles-title">{t("title")}</h3>
          <p className="gnav-why" data-testid="roles-intro">{t("intro")}</p>
          <p className="gnav-why" data-testid="roles-chips-help">{t("chips.help")}</p>
          <ul className="gnav-rows" data-testid="roles-table" aria-label={t("table.aria")}>
            {rows.map(row => (
              <li key={row.id} className="gnav-row" data-testid={`role-row-${row.id}`} data-tier={row.tierId} data-state={row.state?.id}>
                <button type="button" className="gnav-row-main rolc-row-main" aria-label={t("row.open", { label: row.label })} data-testid={`role-open-${row.id}`}
                  onClick={() => setOpenRole(row.id)}>
                  <span className="gnav-row-top">
                    <span className="gnav-label" data-testid={`role-label-${row.id}`}>{row.label}</span>
                    <span className="gnav-chevron" aria-hidden="true">›</span>
                  </span>
                  <span className="rolc-model" data-testid={`role-model-${row.id}`}><span className="rolc-key">{t("role.model")}: </span>{row.modelText}</span>
                  <span className="gnav-chips">
                    <span className="gnav-chip" data-testid={`role-tier-${row.id}`}>{row.tierLabel}</span>
                    {row.state && <span className="gnav-chip gnav-chip-status" data-testid={`role-state-${row.id}`}>{row.state.text}</span>}
                  </span>
                  <span className="gnav-why rolc-why-line">{row.why}</span>
                </button>
              </li>))}
          </ul>
          {doc.issues && doc.issues.length > 0 && (
            <ul className="gnav-note" data-testid="roles-issues">{doc.issues.map(issue => <li key={issue.what}>{issue.reason_human || issue.reason}</li>)}</ul>)}
        </>
      )}
    </section>
  );
}

interface CardProps {
  role: RoleResolved;
  roles: RoleResolved[];
  t: ShellT;
  graphT: ShellT;
  language: string;
  convId: string | null;
  onBack: () => void;
  onNavigate?: (address: string) => void;
  onChanged?: () => void;
  fieldLabels?: Record<string, string>;
}

function FlowBlock({ flow, t, testId }: { flow: FlowView; t: ShellT; testId: string }) {
  return (
    <div className="rolc-flow" data-testid={testId}>
      <p className="rolc-flow-head">
        <span className="rolc-flow-title" data-testid={`${testId}-title`}>{flow.title}</span>
        {flow.status && <span className={`gnav-chip${flow.status.id === "calibration" ? " gnav-chip-status" : ""}`} data-testid={`${testId}-status`} data-status={flow.status.id}>{flow.status.text}</span>}
      </p>
      {flow.calls && <p className="gnav-why" data-testid={`${testId}-calls`}>{flow.calls}</p>}
      {flow.does.length > 0 && (
        <p className="rolc-lines" data-testid={`${testId}-does`}><b>{t("flow.does")}: </b>{flow.does.join(" · ")}</p>)}
      {flow.planned.length > 0 && (
        <p className="rolc-lines rolc-planned" data-testid={`${testId}-planned`}><b>{t("flow.planned")}: </b>{flow.planned.join(" · ")}</p>)}
    </div>
  );
}

export default function RoleCard({ role, roles, t, graphT, language, convId, onBack, onNavigate, onChanged, fieldLabels }: CardProps) {
  const [changeKey, setChangeKey] = useState<string | null>(null);
  const state = stateChip(role, t);
  const steps = useMemo(() => chainSteps(role, roles, t), [role, roles, t]);
  const today = useMemo(() => todaySteps(role, t), [role, t]);
  const byClass = useMemo(() => flowsByClass(role, t), [role, t]);
  const mode = modeView(role, t);
  const reading = readers(role);
  const read = chainIsRead(role);
  const single = role.flow ? flowView(role.flow, t) : null;
  const status = role.status;
  // The label of a key a flow reads: the role's own settings first, then every settings field (the key itself only when no label exists).
  const keyLabel = (key: string) => role.settings?.find(s => s.key === key)?.label ?? fieldLabels?.[key] ?? key;

  return (
    <article className="rolc-card" data-testid="role-card" data-role={role.id} data-tier={role.tier.id}>
      <button type="button" className="shum-back" data-testid="role-back" onClick={onBack}>{t("card.back")}</button>
      <h3 className="rolc-title" data-testid="role-card-title">{role.label}</h3>
      <div className="gnav-chips">
        <span className="gnav-chip" data-testid="role-card-tier">{role.tier.label}</span>
        {state && <span className="gnav-chip gnav-chip-status" data-testid="role-card-state" data-state={state.id}>{state.text}</span>}
      </div>
      <p className="gnav-why rolc-why" data-testid="role-why"><b>{t("why")}: </b>{role.why}</p>
      {status?.today && <p className="rolc-lines" data-testid="role-today"><b>{t("today")}: </b>{status.today}</p>}
      {status?.reason_human && <p className="rolc-lines gnav-why" data-testid="role-status-reason"><b>{t("state.reason")}: </b>{status.reason_human}</p>}
      {status?.reason && (
        // "Dziś" and "Dlaczego tak jest" answer in words; the kernel's engineering note (file names, keys) waits folded under them, for whoever wants it.
        <Disclosure testId="role-status-details" labels={{ show: t("state.details"), hide: t("state.details") }}>
          <p className="rolc-lines gnav-why" data-testid="role-status-technical"><b>{t("state.technical")}: </b>{status.reason}</p>
        </Disclosure>)}

      <h4 className="rolc-h">{t("role.model")}</h4>
      <p data-testid="role-model"><span className="rolc-model-name">{role.model_human}</span>
        {role.model && role.model !== role.model_human && <span className="gnav-why"> ({role.model})</span>}</p>

      <h4 className="rolc-h">{t("class")}</h4>
      <p data-testid="role-tier-line"><span className="gnav-chip" data-testid="role-tier-chip">{role.tier.label}</span> <span data-testid="role-tier-reason">{role.tier.reason}</span></p>
      <p className="gnav-why">{t("class.help")}</p>
      <ul className="rolc-planned-list" data-testid="role-class-planned">
        <li><span className="gnav-chip gnav-chip-status">{t("planned")}</span> {t("class.assign")}</li>
        <li><span className="gnav-chip gnav-chip-status">{t("planned")}</span> {t("class.edit")}</li>
      </ul>
      <p className="gnav-why" data-testid="role-class-planned-reason">{t("class.planned")}</p>

      <h4 className="rolc-h">{t("chain")}</h4>
      <p className="gnav-why">{t("chain.help")}</p>
      {!read && <p className="gnav-note" data-testid="role-chain-not-read">{t("chain.described_not_read")}</p>}
      <ol className={`rolc-chain${read ? "" : " rolc-chain-described"}`} data-testid="role-chain" data-read={read ? "1" : "0"}>
        {steps.map(step => (
          <li key={step.n} className="rolc-step" data-testid={`role-chain-step-${step.n}`} data-type={step.type} data-decides={step.decides ? "1" : undefined}>
            <p className="rolc-step-head">
              <span className="rolc-step-title" data-testid={`role-chain-title-${step.n}`}>{step.title}</span>
              {step.decides && <span className="gnav-chip rolc-decides" data-testid={`role-chain-decides-${step.n}`}>{t("step.decides")}</span>}
            </p>
            <p className="gnav-why"><span className="rolc-key">{t("step.value")}: </span><span data-testid={`role-chain-value-${step.n}`}>{step.value}</span></p>
            <p className="rolc-lines" data-testid={`role-chain-meaning-${step.n}`}>{step.meaning}</p>
            {step.note && <p className="gnav-why" data-testid={`role-chain-note-${step.n}`}>{step.note}</p>}
          </li>))}
      </ol>
      {!read && today.length > 0 && (
        <>
          <h5 className="rolc-h rolc-h-sub">{t("chain.today")}</h5>
          <ol className="rolc-chain" data-testid="role-today-chain">
            {today.map(step => (
              <li key={step.n} className="rolc-step" data-testid={`role-today-step-${step.n}`} data-decides={step.decides ? "1" : undefined}>
                <p className="rolc-step-head">
                  <span className="rolc-step-title">{step.title}</span>
                  {step.decides && <span className="gnav-chip rolc-decides">{t("step.decides")}</span>}
                </p>
                <p className="gnav-why"><span className="rolc-key">{t("step.value")}: </span>{step.value}</p>
                <p className="rolc-lines">{step.meaning}</p>
              </li>))}
          </ol>
        </>)}

      <h4 className="rolc-h">{t("flow.by_class")}</h4>
      {byClass.length > 0 ? (
        <>
          <p className="gnav-why">{t("flow.by_class.help")}</p>
          <ul className="rolc-classes" data-testid="role-flow-by-class">
            {byClass.map(entry => (
              <li key={entry.tierId} className="rolc-class" data-testid={`role-class-${entry.tierId}`} data-current={entry.current ? "1" : undefined}>
                <p className="rolc-class-head">
                  <span className="gnav-chip" data-testid={`role-class-label-${entry.tierId}`}>{entry.tierLabel}</span>
                  {entry.current && <span className="gnav-chip rolc-decides" data-testid="role-flow-current">{t("flow.current")}</span>}
                </p>
                <FlowBlock flow={entry.flow} t={t} testId={`role-flow-${entry.tierId}`} />
              </li>))}
          </ul>
        </>
      ) : single ? (
        <FlowBlock flow={single} t={t} testId="role-flow" />
      ) : (
        <p className="gnav-note" data-testid="role-flow-none">{t("flow.none")}</p>
      )}

      <h4 className="rolc-h">{t("readers")}</h4>
      {reading.total > 0 ? (
        <>
          <p className="gnav-why">{t("readers.help")}</p>
          <ul className="rolc-readers" data-testid="role-readers" data-count={reading.total}>
            {reading.own.map(flow => <li key={flow.id} data-testid={`role-reader-${flow.id}`} data-flow={flow.id}>{flow.title}</li>)}
            {reading.also.map(flow => (
              <li key={flow.id} data-testid={`role-reader-${flow.id}`} data-flow={flow.id} data-also="1" data-key={flow.key}>{flow.title} <span className="gnav-why" title={flow.key}>({t("readers.also", { key: keyLabel(flow.key) })})</span></li>))}
          </ul>
          <p className="gnav-why" data-testid="role-readers-total">{t("readers.total", { n: String(reading.total) })}</p>
        </>
      ) : <p className="gnav-note" data-testid="role-readers-none">{t("readers.none")}</p>}

      {mode && (
        <>
          <h4 className="rolc-h">{t("mode")}</h4>
          <p data-testid="role-mode"><b>{t("mode.modes")}: </b>{mode.modes.join(" · ")}</p>
          {mode.threshold && <p className="gnav-why" data-testid="role-mode-threshold">{mode.threshold}</p>}
          {mode.batchNoKey && <p className="gnav-note" data-testid="role-mode-no-key">{mode.batchNoKey}</p>}
        </>)}

      <h4 className="rolc-h">{t("cost")}</h4>
      <p data-testid="role-cost-price">{priceText(role, t)}</p>
      <p className="gnav-why" data-testid="role-cost-week">{weekText(role, t)}</p>

      {role.settings.length > 0 && (
        <>
          <h4 className="rolc-h">{t("technical_name")}</h4>
          <ul className="rolc-settings" data-testid="role-settings">
            {role.settings.map(setting => (
              <li key={setting.key} className="rolc-setting" data-testid={`role-setting-${setting.key}`}>
                <p className="rolc-setting-line"><span>{setting.label}</span> <code data-testid={`role-setting-key-${setting.key}`}>{setting.key}</code></p>
                <button type="button" className="gnav-bar-btn rolc-change" data-testid={`role-change-${setting.key}`} aria-expanded={changeKey === setting.key}
                  onClick={() => setChangeKey(changeKey === setting.key ? null : setting.key)}>{t("setting.change", { label: setting.label })}</button>
                {changeKey === setting.key && (
                  <NodeCard key={setting.key} address={settingAddress(setting.key)} language={language} convId={convId} t={graphT}
                    onClose={() => setChangeKey(null)} onNavigate={address => onNavigate?.(address)} onChanged={onChanged} />
                )}
              </li>))}
          </ul>
        </>)}
    </article>
  );
}
