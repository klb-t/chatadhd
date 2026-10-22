// M1-W1a: the sheet "Co wysłaliśmy" (spec §3.9). It draws the record the kernel made of ONE request (loom.context_receipt/1, actions context.receipt):
// the sections in the order they were sent as lists (never JSON), what each held, tokens as numbers (an estimate), what was left out and why, the
// profile, the changes of this turn, the stable prefix and the x10 guard. It says what it is: a compiled request, not proof that the provider got it.
// The buttons act on the NEXT message only (context.modify): szerzej / głębiej / węziej / płycej and a selector profile from the effective view; the
// x10 question is the kernel's (awaiting_usage) and is answered with actions.confirm. "Pokaż w grafie" asks the graph whether it can show the sent
// nodes; while the kernel has no source for the perspective "Wysłane do modelu" the button stays visible and disabled with its reason in words.
// Nothing here lists a section, a reason, a step or a profile: the words are presentation.context_receipt, the rest comes from the receipt and the kernel.
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { api } from "../api";
import type { UserProfileHost } from "../api/onboarding-host";
import { readReceiptRun, type ReceiptAnswer } from "../api/context-receipt";
import { featureInLanguage } from "../onboarding/presentation.mjs";
import { USAGE_PENDING_FEATURE, type UsagePendingReceipt } from "../context/usage-pending";
import { useDialogFocus } from "../shell/use-dialog-focus";
import { useLayer } from "../shell/use-layer";
import { usePresentationFeature } from "./ConversationSourceView";
import UsagePendingBlock from "./UsagePendingBlock";
import { modifySentence, profileChoices, sheetModel, stepIds, type ProfileChoice } from "./context-receipt-state";
import { pendingParts } from "../shell/context-strip";
import { useReceiptWords } from "./use-receipt-words";
import { loadSelectorProfiles, useContextModify } from "./use-context-modify";
import "./context-receipt.css";

/** The perspective the graph shows the sent nodes in (listed by graph_views.pack, relation sent_to_model). */
export const SENT_PERSPECTIVE = "sent_to_model";

interface Props {
  profileHost?: UserProfileHost;
  language: string;
  convId: string | null;
  /** The answer or the request it came from: the kernel finds the request of an answer. */
  msgId: string;
  onClose: () => void;
  /** Opens the navigator on the sent request (focus = the message) with the perspective SENT_PERSPECTIVE; absent = no navigator around. */
  onShowInGraph?: (focus: string, perspectives: string[]) => void;
}

type GraphSupport = { state: "checking" } | { state: "yes" } | { state: "no"; answer: string };

export default function ContextReceipt({ profileHost, language, convId, msgId, onClose, onShowInGraph }: Props) {
  const t = useReceiptWords(profileHost, language);
  const usage = usePresentationFeature(profileHost, USAGE_PENDING_FEATURE);
  const [answer, setAnswer] = useState<ReceiptAnswer | null>(null);
  const [generation, setGeneration] = useState(0);
  const { answer: modify, busy, run, decide } = useContextModify(convId, t("sheet.no_actions"));
  const [profiles, setProfiles] = useState<ProfileChoice[] | null>(null);
  const [profilesOpen, setProfilesOpen] = useState(false);
  const [graph, setGraph] = useState<GraphSupport>({ state: "checking" });
  const ref = useRef<HTMLElement>(null);
  useLayer("context-receipt", true, onClose);
  useDialogFocus(true, ref);

  // The record of the request (read only). A failure is shown with the kernel's own message and a retry, never as an empty sheet.
  useEffect(() => {
    let alive = true;
    setAnswer(null);
    const actions = api.actions;
    if (!actions) { setAnswer({ kind: "failed", message: t("sheet.no_actions") }); return; }
    actions.contextReceipt({ msg_id: msgId, ...(convId ? { conversation_id: convId } : {}) })
      .then(run => { if (alive) setAnswer(readReceiptRun(run)); })
      .catch(failure => { if (alive) setAnswer({ kind: "failed", message: failure instanceof Error ? failure.message : String(failure) }); });
    return () => { alive = false; };
  }, [msgId, convId, generation]);

  const requestId = answer && answer.kind !== "failed" ? answer.requestMsgId || answer.msgId : msgId;
  // Can the graph show what was sent? Ask it (a one-row read of the perspective); any refusal is the answer, shown as the reason of the disabled button.
  useEffect(() => {
    let alive = true;
    const actions = api.actions;
    if (!answer || answer.kind !== "receipt") { setGraph({ state: "checking" }); return; }
    if (!actions || !onShowInGraph) { setGraph({ state: "no", answer: "no_navigator" }); return; }
    actions.graphNavigate({ focus: `msg:${requestId}`, perspectives: [SENT_PERSPECTIVE], hops: 1, limit: 1, lang: language })
      .then(() => { if (alive) setGraph({ state: "yes" }); })
      .catch(failure => { if (alive) setGraph({ state: "no", answer: failure instanceof Error ? failure.message : String(failure) }); });
    return () => { alive = false; };
  }, [answer, requestId, language, Boolean(onShowInGraph)]);

  const model = useMemo(() => answer && answer.kind === "receipt" ? sheetModel(answer.receipt, t, language) : null, [answer, t, language]);
  const steps = useMemo(() => stepIds(t), [t]);
  const ready = answer !== null;
  const state = !ready ? "loading" : answer.kind;

  const openProfiles = useCallback(async () => {
    setProfilesOpen(open => !open);
    if (profiles !== null || !api.actions) return;
    try {
      setProfiles(profileChoices(await loadSelectorProfiles(), language));
    } catch { setProfiles([]); }
  }, [profiles, language]);

  const current = answer && answer.kind === "receipt" ? answer.receipt.profile?.id ?? null : null;
  const waitingWords = modify && modify.kind === "applied" ? Object.keys(modify.waiting).length > 0 : false;
  const usageFeature = usage.feature ? featureInLanguage(usage.feature, language) : null;

  return (
    <>
      <div className="crs-backdrop" data-testid="context-receipt-backdrop" onClick={onClose} />
      <section className="crs" role="dialog" aria-modal="true" aria-label={t("sheet.dialog")} data-testid="context-receipt" data-state={state}
        data-request-msg={requestId || undefined} ref={ref} tabIndex={-1}>
        <div className="crs-head">
          <h3 data-testid="context-receipt-title">{t("title")}</h3>
          <button type="button" className="crs-close" aria-label={t("sheet.close")} data-testid="context-receipt-close" onClick={onClose}>×</button>
        </div>
        {!ready && <p className="crs-note" data-testid="context-receipt-loading">{t("sheet.loading")}</p>}
        {answer?.kind === "failed" && (
          <div className="crs-result" data-kind="failed" role="alert" data-testid="context-receipt-error">
            <p>{t("sheet.error", { reason: answer.message })}</p>
            <button type="button" onClick={() => setGeneration(g => g + 1)} data-testid="context-receipt-retry">{t("sheet.retry")}</button>
          </div>
        )}
        {answer?.kind === "missing" && (
          <div data-testid="context-receipt-missing">
            <p data-testid="context-receipt-missing-word"><strong>{t("missing")}</strong></p>
            <p className="crs-note">{t("missing.also")}</p>
            <p className="crs-note">{t("disclaimer")}</p>
          </div>
        )}
        {model && answer?.kind === "receipt" && (
          <>
            <ul className="crs-lines" data-testid="context-receipt-header">
              <li data-testid="context-receipt-profile">{model.profileLine}</li>
              <li data-testid="context-receipt-modifiers">{model.modifiersLine}</li>
              {model.orderLine && <li data-testid="context-receipt-order" data-name={model.orderName ?? undefined}>{model.orderLine}</li>}
              {model.flowLine && <li data-testid="context-receipt-flow" data-name={model.flowName ?? undefined}>{model.flowLine}</li>}
              <li data-testid="context-receipt-prefix">{model.prefixLine}{model.prefixDetail ? ` — ${model.prefixDetail}` : ""}</li>
              <li data-testid="context-receipt-guard" data-state={model.guardState}>{model.guardLine}</li>
              <li data-testid="context-receipt-tokens">{model.tokensLine}</li>
              <li data-testid="context-receipt-cost">{model.costLine}</li>
              {model.truncated && <li data-testid="context-receipt-truncated">{model.truncated}</li>}
            </ul>
            <ol className="crs-sections" data-testid="context-receipt-sections" aria-label={t("sections.title")}>
              {model.sections.map(section => (
                <li key={section.id} className="crs-section" data-testid={`receipt-section-${section.id}`} data-class={section.id}
                  data-items={section.items.length} data-tokens={section.tokens}>
                  <h4><span data-testid={`receipt-section-title-${section.id}`}>{section.title}</span>
                    <span data-testid={`receipt-section-tokens-${section.id}`}>{t("tokens.short", { n: String(section.tokens) })}</span></h4>
                  <ul className="crs-items">
                    {section.items.map(item => (
                      <li key={item.key} className="crs-item" data-testid="receipt-item">
                        <span data-testid="receipt-item-text">{item.text}</span>
                        {item.chip && <span className="crs-chip" data-testid="receipt-item-chip">{item.chip}</span>}
                        {item.tokens !== null && <span className="crs-chip" data-testid="receipt-item-tokens">{t("tokens.short", { n: String(item.tokens) })}</span>}
                        {item.why && <p className="crs-why" data-testid="receipt-item-why">{item.why}</p>}
                      </li>
                    ))}
                    {section.omitted > 0 && <li className="crs-item crs-note" data-testid="receipt-item-omitted">{t("truncated")} — {t("items.count", { n: String(section.omitted) })}</li>}
                  </ul>
                </li>
              ))}
            </ol>
            {model.emptyLine && <p className="crs-note" data-testid="context-receipt-empty">{model.emptyLine}</p>}
            <h4 data-testid="context-receipt-dropped-title">{model.droppedTitle}</h4>
            {model.dropped.length > 0
              ? <ul className="crs-dropped" data-testid="context-receipt-dropped">{model.dropped.map(d => <li key={d.key} data-testid="receipt-dropped">{d.text}</li>)}</ul>
              : <p className="crs-note" data-testid="context-receipt-dropped-none">{model.droppedNone}</p>}
            <p className="crs-note" data-testid="context-receipt-disclaimer">{t("disclaimer")}</p>
          </>
        )}
        {(answer?.kind === "receipt" || answer?.kind === "missing") && (
          <>
            <div className="crs-actions">
              {answer.kind === "receipt" && (
                <button type="button" data-testid="receipt-show-in-graph" disabled={graph.state !== "yes"}
                  data-graph={graph.state} aria-describedby="receipt-show-in-graph-reason"
                  onClick={() => { if (graph.state === "yes") onShowInGraph?.(`msg:${requestId}`, [SENT_PERSPECTIVE]); }}>{t("action.show_in_graph")}</button>
              )}
            </div>
            {answer.kind === "receipt" && graph.state !== "yes" && (
              <p className="crs-note" id="receipt-show-in-graph-reason" data-testid="receipt-show-in-graph-reason" data-kernel-answer={graph.state === "no" ? graph.answer : undefined}>
                {graph.state === "no" ? t("action.show_in_graph.disabled") : t("sheet.loading")}</p>
            )}
            <p className="crs-note" data-testid="receipt-next-time-title"><strong>{t("action.next_time")}</strong> — {t("action.next_time.help")}</p>
            <div className="crs-actions" data-testid="receipt-steps">
              {steps.map(id => <button type="button" key={id} disabled={busy} data-testid={`receipt-step-${id}`} onClick={() => void run({ step: id })}>{t(`action.${id}`)}</button>)}
              <button type="button" aria-expanded={profilesOpen} data-testid="receipt-change-profile" onClick={() => void openProfiles()}>{t("action.change_profile")}</button>
              {waitingWords && <button type="button" disabled={busy} data-testid="receipt-reset" onClick={() => void run({ reset: true })}>{t("action.reset")}</button>}
            </div>
            {profilesOpen && (
              <div data-testid="receipt-profile-list">
                <p className="crs-note">{t("action.profile_help")}</p>
                {profiles === null && <p className="crs-note">{t("sheet.loading")}</p>}
                {profiles?.map(p => (
                  <button type="button" key={p.id} className="crs-profile-row" disabled={busy} data-testid={`receipt-profile-${p.id}`} data-status={p.status}
                    aria-current={p.id === current || undefined} onClick={() => void run({ profile: p.id })}>
                    <span>{p.label}{p.id === current ? ` (${t("action.profile_current")})` : ""}</span>
                    <span className="crs-chip" data-testid={`receipt-profile-status-${p.id}`}>{p.status ? t(`status.${p.status}`) : ""}</span>
                  </button>
                ))}
              </div>
            )}
            {busy && <p className="crs-note" data-testid="receipt-modify-busy">{t("modify.busy")}</p>}
            {modify?.kind === "applied" && <p className="crs-result" data-kind="applied" data-testid="receipt-modify-result">{modifySentence(modify, pendingParts(modify.waiting, t, id => profiles?.find(p => p.id === id)?.label ?? id).join(", "), t)}</p>}
            {modify?.kind === "declined" && <p className="crs-result" data-kind="declined" data-testid="receipt-modify-result">{t("modify.declined")}</p>}
            {modify?.kind === "failed" && <p className="crs-result" data-kind="failed" role="alert" data-testid="receipt-modify-result">{t("modify.failed", { reason: modify.message })}</p>}
            {modify?.kind === "question" && (
              <UsagePendingBlock testId="receipt-usage-pending" feature={usageFeature} busy={busy}
                receipt={{ status: (modify.usage.status as string | undefined) ?? "requires_confirmation", receipt_id: modify.receiptId, operation_id: modify.operationId,
                  baseline_key: (modify.usage.baseline_key as string | null | undefined) ?? null, projected: modify.usage.projected as UsagePendingReceipt["projected"] }}
                onConfirm={() => void decide(true)} onReject={() => void decide(false)} />
            )}
          </>
        )}
      </section>
    </>
  );
}
