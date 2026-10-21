// M1-W0 (2), package P3: the NODE CARD of the graph navigator (spec §3.2, §3.5). Everything on it is what graph.card answers for
// the address: value and why, what it changes, the provenance chip with its layers, "Skąd jest wartość" as the whole chain the
// kernel drew (the winning level highlighted, "bez projektu" said, never skipped), the relations grouped by edge kind, the actions
// in words. A settings field can be changed: an editor by type → "Dla kogo?" (the field's own levels, "więcej poziomów" shows
// the whole chain) → settings.set (person) or rules.set (a scoped value); a value can be taken off one layer ("Zdejmij wartość:
// <warstwa> — wróci: <warstwa> (<wartość>)") through rules.disable. A confirmation the rules ask for is shown with its reason and
// answered with actions.confirm. The card and each editor step are history layers (shell/history.ts): Back closes the last one.
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { api } from "../api";
import type { GraphCardResult } from "../api/graph-projection";
import type { ActionRun } from "../api/types";
import Disclosure from "../shell/Disclosure";
import type { ShellT } from "../shell/text";
import { useLayer } from "../shell/use-layer";
import { chipText, evidenceText } from "./navigator-state";
import { actionRows, chainRows, draftOf, editorKind, levelChoices, parseDraft, pendingGate, relationGroups, runOutcome, unsetChoices,
  valueText, writeRequest, type CardStep, type LevelChoice, type PendingGate, type ScopedRule, type UnsetChoice } from "./node-card-state";

interface Props {
  address: string;
  language: string;
  convId: string | null;
  t: ShellT;
  onClose: () => void;
  /** A tap on a related node: the navigator goes there (a path step) and the card closes. */
  onNavigate: (address: string, label: string) => void;
  /** A write finished (a value set or taken off): a screen that shows counts of the same values reloads them. */
  onChanged?: () => void;
}

/** A gate waits for the user's yes, then `after` runs (the card reloads). */
interface GateState extends PendingGate { after: () => void }

export default function NodeCard({ address, language, convId, t, onClose, onNavigate, onChanged }: Props) {
  const [card, setCard] = useState<GraphCardResult | null>(null);
  const [rules, setRules] = useState<ScopedRule[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [step, setStep] = useState<CardStep>("card");
  const [draft, setDraft] = useState("");
  const [draftError, setDraftError] = useState<string | null>(null);
  const [moreLevels, setMoreLevels] = useState(false);
  const [gate, setGate] = useState<GateState | null>(null);
  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState<{ kind: "ok" | "error"; text: string } | null>(null);
  const [generation, setGeneration] = useState(0);

  useLayer("navigator-card", true, onClose);
  useLayer("navigator-card-edit", step !== "card", () => setStep("card"));
  useLayer("navigator-card-level", step === "level", () => setStep("edit"));

  useEffect(() => {
    let alive = true;
    setLoading(true); setError(null);
    const actions = api.actions;
    if (!actions) { setError("actions_api_unavailable"); setLoading(false); return; }
    const request: { address: string; lang: string; conv_id?: string } = { address, lang: language };
    if (convId) request.conv_id = convId;
    Promise.all([actions.graphCard(request), actions.rulesList({ user_only: true }).catch(() => ({ rules: [] }))]).then(
      ([answer, list]) => { if (alive) { setCard(answer); setRules((list.rules ?? []) as ScopedRule[]); setLoading(false); } },
      failure => { if (alive) { setError(failure instanceof Error ? failure.message : String(failure)); setLoading(false); } });
    return () => { alive = false; };
  }, [address, language, convId, generation]);

  const changed = useRef(onChanged);
  changed.current = onChanged;
  const reload = useCallback(() => { setGeneration(g => g + 1); setStep("card"); setGate(null); changed.current?.(); }, []);
  const key = typeof card?.attrs?.key === "string" ? (card.attrs.key as string) : address.replace(/^setting:/, "");
  const kind = card ? editorKind(card) : "text";
  const chain = useMemo(() => card ? chainRows(card.inheritance, t) : [], [card, t]);
  const levels = useMemo(() => card ? levelChoices(card, convId, t) : [], [card, convId, t]);
  const unsets = useMemo(() => card ? unsetChoices(card, key, rules, t) : [], [card, key, rules, t]);
  const relations = useMemo(() => card ? relationGroups(card) : [], [card]);
  const actionList = useMemo(() => card ? actionRows(card, t) : [], [card, t]);
  const editable = !!card && (card.levels?.length ?? 0) > 0 && card.edit_rule !== "read_only";

  /** Runs a write and either finishes it (reload) or shows the confirmation the kernel asks for. */
  const settle = (run: ActionRun, after: () => void) => {
    const pending = pendingGate(run, language);
    if (pending) { setGate({ ...pending, after }); return; }
    const outcome = runOutcome(run, t);
    if (outcome.ok) { setNotice({ kind: "ok", text: t("card.saved") }); after(); } else setNotice({ kind: "error", text: outcome.text });
  };
  const fail = (failure: unknown) => setNotice({ kind: "error", text: t("card.write_error", { reason: failure instanceof Error ? failure.message : String(failure) }) });

  const startEdit = () => { if (!card) return; setDraft(draftOf(kind, card.value)); setDraftError(null); setNotice(null); setStep("edit"); };
  const toLevels = () => {
    const parsed = parseDraft(kind, draft);
    if ("error" in parsed) { setDraftError(t(parsed.error)); return; }
    setDraftError(null); setMoreLevels(false); setStep("level");
  };
  const writeTo = async (choice: LevelChoice) => {
    const parsed = parseDraft(kind, draft);
    if ("error" in parsed) { setDraftError(t(parsed.error)); setStep("edit"); return; }
    const request = writeRequest(choice, key, parsed.value);
    const actions = api.actions;
    if (!request || !actions) return;
    setBusy(true); setNotice(null);
    try {
      const run = request.kind === "settings.set" ? await actions.run(request.run) : await actions.rulesSet(request.rule);
      settle(run, reload);
    } catch (failure) { fail(failure); } finally { setBusy(false); }
  };
  const unset = async (choice: UnsetChoice) => {
    const actions = api.actions;
    if (!choice.ruleId || !actions) return;
    setBusy(true); setNotice(null);
    try { settle(await actions.rulesDisable({ id: choice.ruleId }), reload); } catch (failure) { fail(failure); } finally { setBusy(false); }
  };
  const answerGate = async (approved: boolean) => {
    const actions = api.actions;
    if (!gate || !actions) return;
    setBusy(true);
    try {
      const run = await actions.confirm({ run_id: gate.runId, approved, receipt_id: gate.receiptId });
      const after = gate.after;
      setGate(null);
      if (!approved) { setNotice({ kind: "ok", text: t("card.gate.declined") }); return; }
      settle(run, after);
    } catch (failure) { setGate(null); fail(failure); } finally { setBusy(false); }
  };

  const shownLevels = moreLevels ? levels : levels.filter(l => l.declared);

  return (
    <section className="ncard" data-testid="node-card" data-address={address} data-step={step} aria-busy={busy || loading}>
      <header className="ncard-head">
        <h2 className="ncard-title" data-testid="card-label">{card?.label ?? address}</h2>
        <button className="ncard-close" data-testid="card-close" aria-label={t("card.close")} onClick={onClose}>×</button>
      </header>
      {loading && <p className="gnav-note" data-testid="card-loading">{t("loading")}</p>}
      {error && <p className="gnav-note gnav-error" data-testid="card-error">{t("error", { reason: error })}</p>}
      {card && <>
        <div className="gnav-chips">
          <span className="gnav-chip" data-testid="card-chip">{chipText(card, t)}</span>
          {evidenceText(card, t) && <span className="gnav-chip gnav-chip-evidence" data-testid="card-evidence">{evidenceText(card, t)}</span>}
          {card.status && <span className="gnav-chip gnav-chip-status">{t(`status.${card.status}`)}</span>}
        </div>
        {card.provenance.layers && card.provenance.layers.length > 0 &&
          <p className="ncard-line ncard-layers" data-testid="card-layers">{t("card.layers", { layers: card.provenance.layers.map(l => t(`chip.${l}`)).join(" → ") })}</p>}
        {card.value !== undefined && <p className="ncard-line ncard-value" data-testid="card-value">
          <span className="ncard-key">{t("card.value")}</span> <strong>{card.value_human ?? valueText(card.value, t)}</strong></p>}
        {card.why && <p className="ncard-line" data-testid="card-why"><span className="ncard-key">{t("card.why")}</span> {card.why}</p>}
        {card.effect && <p className="ncard-line" data-testid="card-effect"><span className="ncard-key">{t("card.effect")}</span> {card.effect}</p>}
        {card.edit_rule && <p className="ncard-line" data-testid="card-edit-rule"><span className="ncard-key">{t("card.edit_rule")}</span> {t(`card.edit_rule.${card.edit_rule}`)}</p>}
        {card.children_count > 0 && <p className="ncard-line gnav-note">{t("card.children", { count: String(card.children_count) })}</p>}

        {notice && <p className={`gnav-note ncard-notice${notice.kind === "error" ? " gnav-error" : ""}`} data-testid="card-notice" role="status">{notice.text}</p>}

        {gate && <div className="ncard-gate" data-testid="card-gate" role="alertdialog">
          <p className="ncard-line"><strong>{t("card.gate")}</strong></p>
          {gate.reasons.map((r, i) => <p key={i} className="ncard-line" data-testid="card-gate-reason">{r}</p>)}
          <div className="ncard-buttons">
            <button className="ncard-btn ncard-btn-primary" data-testid="card-gate-confirm" disabled={busy} onClick={() => answerGate(true)}>{t("card.gate.yes")}</button>
            <button className="ncard-btn" data-testid="card-gate-deny" disabled={busy} onClick={() => answerGate(false)}>{t("card.gate.no")}</button>
          </div>
        </div>}

        {step === "edit" && <div className="ncard-edit" data-testid="card-editor">
          <p className="ncard-line"><strong>{t("card.change")}</strong></p>
          {kind === "toggle" && <div className="ncard-buttons" role="radiogroup">
            {(["true", "false"] as const).map(v => <button key={v} className={`ncard-btn${draft === v ? " ncard-btn-primary" : ""}`} role="radio" aria-checked={draft === v}
              data-testid={`card-editor-${v}`} onClick={() => setDraft(v)}>{t(v === "true" ? "card.value.on" : "card.value.off")}</button>)}
          </div>}
          {kind === "number" && <input className="ncard-input" data-testid="card-editor-input" type="text" inputMode="decimal" value={draft} onChange={e => setDraft(e.target.value)} />}
          {kind === "text" && <input className="ncard-input" data-testid="card-editor-input" type="text" value={draft} onChange={e => setDraft(e.target.value)} />}
          {kind === "json" && <textarea className="ncard-input ncard-json" data-testid="card-editor-input" rows={6} value={draft} onChange={e => setDraft(e.target.value)} />}
          {draftError && <p className="gnav-note gnav-error" data-testid="card-editor-error">{draftError}</p>}
          <div className="ncard-buttons">
            <button className="ncard-btn ncard-btn-primary" data-testid="card-next" onClick={toLevels}>{t("card.for_whom")}</button>
            <button className="ncard-btn" data-testid="card-cancel" onClick={() => setStep("card")}>{t("card.cancel")}</button>
          </div>
        </div>}

        {step === "level" && <div className="ncard-levels" data-testid="card-levels">
          <p className="ncard-line"><strong>{t("card.for_whom")}</strong> <span className="gnav-note">{t("card.for_whom.value", { value: draft })}</span></p>
          <ul className="ncard-list">
            {shownLevels.map(l => <li key={l.level}>
              <button className="ncard-row" data-testid={`card-level-${l.level}`} data-writable={l.write.kind !== "unavailable"} disabled={busy || l.write.kind === "unavailable"}
                onClick={() => writeTo(l)}>
                <span className="ncard-row-label">{l.label}</span>
                {l.write.kind === "unavailable" && <span className="gnav-note">{l.write.reasonText}</span>}
              </button>
            </li>)}
          </ul>
          <div className="ncard-buttons">
            {!moreLevels && levels.some(l => !l.declared) && <button className="ncard-btn" data-testid="card-more-levels" onClick={() => setMoreLevels(true)}>{t("card.more_levels")}</button>}
            <button className="ncard-btn" data-testid="card-levels-back" onClick={() => setStep("edit")}>{t("back")}</button>
          </div>
        </div>}

        <Disclosure testId="card-chain" labels={{ show: t("inheritance"), hide: t("inheritance") }} defaultOpen>
          <ul className="ncard-list ncard-chain" data-testid="card-chain-rows">
            {chain.map(row => <li key={row.level} className={`ncard-chain-row${row.winning ? " ncard-winning" : ""}${row.active ? "" : " ncard-inactive"}`}
              data-testid={`card-chain-${row.level}`} data-role={row.role} data-winning={row.winning}>
              <span className="ncard-row-label">{row.label}</span>
              <span className="ncard-chain-role">{row.noUnitText ?? row.roleText}</span>
              {row.valueText && <span className="ncard-chain-value">{row.valueText}</span>}
            </li>)}
          </ul>
        </Disclosure>

        {(card.flows_reading?.length ?? 0) > 0 && <p className="ncard-line" data-testid="card-flows">
          <span className="ncard-key">{t("card.flows_reading")}</span> {card.flows_reading!.map(f => f.title).join(", ")}</p>}

        <Disclosure testId="card-relations" labels={{ show: t("relations"), hide: t("relations") }} defaultOpen>
          {relations.length === 0 && <p className="gnav-note" data-testid="card-relations-none">{t("card.relations.none")}</p>}
          {relations.map(g => <div key={g.kind} className="ncard-group" data-testid={`card-relation-group-${g.kind}`}>
            <p className="ncard-line"><strong>{g.label}</strong>{g.meaning && <span className="gnav-note"> — {g.meaning}</span>}</p>
            <ul className="ncard-list">
              {g.out.map(n => <li key={`out-${n.address}`}><button className="ncard-row" data-testid={`card-relation-${n.address}`} onClick={() => onNavigate(n.address, n.label)}>→ {n.label}</button></li>)}
              {g.in.map(n => <li key={`in-${n.address}`}><button className="ncard-row" data-testid={`card-relation-${n.address}`} onClick={() => onNavigate(n.address, n.label)}>← {n.label}</button></li>)}
            </ul>
          </div>)}
          {(card.relations_unknown ?? []).map(u => <p key={u.source} className="gnav-note gnav-honesty">{t("unknown_source", { source: u.source, reason: u.reason })}</p>)}
        </Disclosure>

        <Disclosure testId="card-actions" labels={{ show: t("actions"), hide: t("actions") }} defaultOpen>
          <ul className="ncard-list" data-testid="card-action-rows">
            {editable && step === "card" && <li><button className="ncard-row ncard-row-action" data-testid="card-change" disabled={busy} onClick={startEdit}>{t("card.change")}</button></li>}
            {unsets.map(u => <li key={u.level}>
              <button className="ncard-row ncard-row-action" data-testid={`card-unset-${u.level}`} disabled={busy || !u.ruleId} onClick={() => unset(u)}>
                <span className="ncard-row-label">{u.text}</span>
                <span className="gnav-note">{u.returnsText}{u.reasonText ? ` · ${u.reasonText}` : ""}</span>
              </button>
            </li>)}
            {actionList.map(a => <li key={a.id} className="ncard-action" data-testid={`card-action-${a.id}`}>
              <span className="ncard-row-label">{a.label}</span>
              {a.planned && <span className="gnav-chip gnav-chip-status">{t("card.action.planned")}</span>}
              {a.decisionText && <span className="gnav-chip gnav-chip-status">{a.decisionText}</span>}
            </li>)}
          </ul>
          {card.actions_unknown_reason && <p className="gnav-note gnav-honesty">{card.actions_unknown_reason}</p>}
        </Disclosure>
      </>}
    </section>
  );
}
