// M1-W2b-1: the screen "Ty" - the person's own page (ON05: what the app knows about me, with the origin of every piece, correcting and forgetting).
// It is built from the PARTS the kernel answers for party:self (graph.navigate; a part is a node of kind person_subgraph with its count, its mix
// of evidence and its actions) and from the entries of each part (graph.navigate of the part, read when the section is opened). Every row shows
// what the kernel says: the label, the value in its words, the EVIDENCE CHIP (the class of the evidence in words), the PROVENANCE LINE (where it
// comes from and when), and the actions the kernel offers on it (actions[] of the node) as buttons. A button runs the action on the application;
// a run that the rules gate waits for the person's Tak/Nie in the proposal block of the chat window (ActionProposalBlock), then the page reloads.
// Which parts exist, which actions a row has and what they are called are the kernel's answers and the pack's words (presentation.graph_screen
// keys me.*) - nothing of that is written in this file. The inferred rows have a distinct look (a PRESET, me_inferred_opacity changes it).
import { useCallback, useEffect, useMemo, useState, type CSSProperties } from "react";
import { api } from "../api";
import type { GraphCardResult, GraphNavigateResult, GraphNodeRow } from "../api/graph-projection";
import { readInterviewStart } from "../api/profile-interview";
import type { PendingAnswer } from "../context/action-proposal";
import Disclosure from "../shell/Disclosure";
import type { ShellT } from "../shell/text";
import type { PresentationFeature } from "../onboarding/presentation.mjs";
import ActionProposalBlock from "./ActionProposalBlock";
import "./me-view.css";
import { chainRows } from "./node-card-state";
import {
  INTERVIEW_ACTION, OPEN_SECTIONS_SETTING, actionButtons, detailLines, inferredOpacityOf, inheritanceOf, inputFields, shownValue, interpretRun, looseRows, openSectionsOf, rowView, runArgs,
  runRequest, sections, serializeSections, withSection, type ActionButton, type RowView,
} from "./me-view-state";

interface Props {
  /** The kernel's answer for party:self. */
  result: GraphNavigateResult;
  t: ShellT;
  language: string;
  convId: string | null;
  /** presentation.action_proposal: the words of the proposal block (the window of the chat uses the same). */
  proposalFeature: PresentationFeature | null;
  /** The setting me_open_sections as loaded (a string of part addresses); the page follows it until the person opens or closes a section. */
  openSetting: unknown;
  /** The setting me_inferred_opacity as loaded (0 to 1); anything else is the preset. */
  inferredOpacity: unknown;
  onSetting?: (key: string, value: string) => void;
  /** Something was written (a value corrected, a note forgotten): the navigator reads the counts again. */
  onChanged: () => void;
  /** The interview opened a conversation: the app shows it. */
  onOpenConversation?: (convId: string) => void;
  /** Bumps when the navigator reloaded: the open sections read their entries again. */
  reloadKey: number;
}

type Note = { kind: "ok" | "error"; text: string } | null;
interface Gate { button: ActionButton; runId: string; receiptId: string; reasons: string[]; what: string }

/** The buttons of a node and what happens after a tap: a field to type (when the pack's words declare one), the run, the question of the rules. */
function ActionBar({ buttons, subject, defaultValue, scope, t, language, proposalFeature, onChanged, onOpenConversation }: {
  buttons: ActionButton[]; subject: string; defaultValue: string; scope: string; t: ShellT; language: string; convId: string | null;
  proposalFeature: PresentationFeature | null; onChanged: (done: string) => void; onOpenConversation?: (convId: string) => void;
}) {
  const [form, setForm] = useState<{ button: ActionButton; values: Record<string, string> } | null>(null);
  const [gate, setGate] = useState<Gate | null>(null);
  const [busy, setBusy] = useState(false);
  const [note, setNote] = useState<Note>(null);
  const [text, setText] = useState<string | null>(null);

  const finish = useCallback((button: ActionButton, ungatedText: string | null) => {
    setForm(null); setGate(null);
    setText(ungatedText);
    const done = t("me.confirm.done", { action: button.title });
    setNote({ kind: "ok", text: done });
    onChanged(done);
  }, [t, onChanged]);

  const execute = useCallback(async (button: ActionButton, typed: Record<string, string>) => {
    const actions = api.actions;
    if (!actions) { setNote({ kind: "error", text: t("error.no_actions") }); return; }
    setBusy(true); setNote(null); setText(null);
    try {
      const args = runArgs(button, typed);
      const fieldsOf = inputFields(button, t);
      const what = [subject, ...Object.entries(typed).filter(([, v]) => v.trim() !== "").map(([k, v]) => shownValue(fieldsOf.find(f => f.arg === k), v))].filter(Boolean);
      const run = await actions.run(runRequest(button, args));
      const outcome = interpretRun(run, t, language);
      if (outcome.kind === "gate") {
        setForm(null);
        setGate({ button, runId: outcome.gate.runId, receiptId: outcome.gate.receiptId, reasons: outcome.gate.reasons, what: what.length > 1 ? `${what[0]} → ${what.slice(1).join(", ")}` : what[0] ?? "" });
      } else if (outcome.kind === "error") setNote({ kind: "error", text: outcome.text });
      else {
        finish(button, outcome.text !== "" ? outcome.text : null);
        // An action that started a conversation (the restart of the interview) says which: the page opens it, as "Poznaj mnie" does.
        const opened = outcome.data?.open_conv_id;
        if (typeof opened === "string" && opened !== "") onOpenConversation?.(opened);
      }
    } catch (failure) { setNote({ kind: "error", text: t("me.confirm.failed", { reason: failure instanceof Error ? failure.message : String(failure) }) }); }
    finally { setBusy(false); }
  }, [subject, t, language, finish, onOpenConversation]);

  const startInterview = useCallback(async () => {
    const actions = api.actions;
    if (!actions) return;
    setBusy(true); setNote({ kind: "ok", text: t("me.interview.starting") });
    try {
      const started = readInterviewStart(await actions.profileInterview({ op: "start", lang: language }));
      if (started.kind === "opened") { setNote(null); onOpenConversation?.(started.convId); }
      else setNote({ kind: "error", text: t("me.interview.failed", { reason: started.message }) });
    } catch (failure) { setNote({ kind: "error", text: t("me.interview.failed", { reason: failure instanceof Error ? failure.message : String(failure) }) }); }
    finally { setBusy(false); }
  }, [t, language, onOpenConversation]);

  const press = (button: ActionButton) => {
    setNote(null); setText(null); setGate(null);
    if (button.id === INTERVIEW_ACTION) { void startInterview(); return; }
    const fields = inputFields(button, t);
    if (fields.length === 0) { void execute(button, {}); return; }
    setForm({ button, values: Object.fromEntries(fields.map((f, i) => [f.arg, i === 0 && !f.choices ? defaultValue : ""])) });
  };
  const answer = async (approved: boolean) => {
    const actions = api.actions;
    if (!gate || !actions) return;
    setBusy(true);
    try {
      const run = await actions.confirm({ run_id: gate.runId, approved, receipt_id: gate.receiptId });
      const button = gate.button;
      if (!approved) { setGate(null); setNote({ kind: "ok", text: t("me.confirm.declined") }); return; }
      const outcome = interpretRun(run, t, language);
      if (outcome.kind === "error") { setGate(null); setNote({ kind: "error", text: outcome.text }); }
      else if (outcome.kind === "gate") setGate({ ...gate, runId: outcome.gate.runId, receiptId: outcome.gate.receiptId, reasons: outcome.gate.reasons });
      else {
        finish(button, null);
        const opened = outcome.data?.open_conv_id;
        if (typeof opened === "string" && opened !== "") onOpenConversation?.(opened);
      }
    } catch (failure) { setGate(null); setNote({ kind: "error", text: t("me.confirm.failed", { reason: failure instanceof Error ? failure.message : String(failure) }) }); }
    finally { setBusy(false); }
  };

  const fields = form ? inputFields(form.button, t) : [];
  const gateAnswer: PendingAnswer | null = gate ? { kind: "confirmation", runId: gate.runId, receiptId: gate.receiptId, proposal: null, usage: null, clientStep: null, guardian: null } : null;
  if (buttons.length === 0 && !note && !text) return null;
  return (
    <div className="me-actions" data-testid={`me-actions-${scope}`}>
      {buttons.length > 0 && <div className="me-action-buttons">
        {buttons.map(b => b.planned
          ? <span key={b.id} className="gnav-chip gnav-chip-status" data-testid="me-action-planned" data-action={b.id}>{b.label} · {t("me.action.planned")}</span>
          : <button key={b.id} type="button" className="ncard-btn me-action" data-testid="me-action" data-action={b.id} title={b.title} disabled={busy}
            onClick={() => press(b)}>{b.label}</button>)}
      </div>}
      {form && <form className="me-input-form" data-testid="me-input-form" data-action={form.button.id}
        onSubmit={e => { e.preventDefault(); void execute(form.button, form.values); }}>
        {fields.map(f => <label key={f.arg} className="me-input-label">
          <span>{f.label}</span>
          {f.choices
            ? <select className="ncard-input" data-testid={`me-input-${f.arg}`} value={form.values[f.arg] ?? ""} autoFocus={f === fields[0]}
              onChange={e => setForm(cur => cur ? { ...cur, values: { ...cur.values, [f.arg]: e.target.value } } : cur)}>
              <option value="">{f.none ?? ""}</option>
              {f.choices.map(c => <option key={c.value} value={c.value}>{c.label}</option>)}
            </select>
            : <input className="ncard-input" data-testid={`me-input-${f.arg}`} type="text" value={form.values[f.arg] ?? ""} autoFocus={f === fields[0]}
              onChange={e => setForm(cur => cur ? { ...cur, values: { ...cur.values, [f.arg]: e.target.value } } : cur)} />}
        </label>)}
        <div className="ncard-buttons">
          <button type="submit" className="ncard-btn ncard-btn-primary" data-testid="me-input-next" disabled={busy}>{t("me.input.next")}</button>
          <button type="button" className="ncard-btn" data-testid="me-input-cancel" disabled={busy} onClick={() => setForm(null)}>{t("me.input.cancel")}</button>
        </div>
      </form>}
      {gate && gateAnswer && <div className="me-proposal" data-testid="me-gate">
        <p className="ncard-line" data-testid="me-proposal-ask"><strong>{gate.what !== "" ? t("me.confirm.ask", { action: gate.button.title, what: gate.what }) : t("me.confirm.ask_plain", { action: gate.button.title })}</strong></p>
        {gate.reasons.map((r, i) => <p key={i} className="ncard-line gnav-note" data-testid="me-proposal-reason">{r}</p>)}
        <ActionProposalBlock feature={proposalFeature} answer={gateAnswer} busy={busy} testId="me-proposal" plain={{ confirm: t("me.confirm.yes"), reject: t("me.confirm.no") }}
          onConfirm={() => void answer(true)} onReject={() => void answer(false)} />
      </div>}
      {text && <p className="ncard-line me-result" data-testid="me-action-result"><span className="ncard-key">{t("me.result.title")}</span> {text}</p>}
      {note && <p className={`gnav-note ncard-notice${note.kind === "error" ? " gnav-error" : ""}`} data-testid="me-action-note" role="status">{note.text}</p>}
    </div>
  );
}

/** One entry of a part: the label and the value, the evidence chip, the provenance line, the buttons, and the details on a tap (graph.card). */
function MeRow({ row, view, t, language, convId, proposalFeature, opacity, onChanged, onOpenConversation, reloadKey }: {
  row: GraphNodeRow; view: RowView; t: ShellT; language: string; convId: string | null; proposalFeature: PresentationFeature | null; opacity: number;
  onChanged: (done: string) => void; onOpenConversation?: (convId: string) => void; reloadKey: number;
}) {
  const [open, setOpen] = useState(false);
  const [card, setCard] = useState<GraphCardResult | { error: string } | null>(null);
  useEffect(() => {
    if (!open) return;
    let alive = true;
    setCard(null);
    const actions = api.actions;
    if (!actions) { setCard({ error: t("error.no_actions") }); return; }
    actions.graphCard({ address: row.address, lang: language, ...(convId ? { conv_id: convId } : {}) }).then(
      answer => { if (alive) setCard(answer); },
      failure => { if (alive) setCard({ error: failure instanceof Error ? failure.message : String(failure) }); });
    return () => { alive = false; };
  }, [open, row.address, language, convId, reloadKey, t]);
  const loaded = card && !("error" in card) ? card : null;
  const lines = useMemo(() => loaded ? detailLines(loaded.attrs, t, language) : [], [loaded, t, language]);
  const chain = useMemo(() => { const levels = inheritanceOf(loaded); return levels.length > 0 && loaded ? chainRows(levels, t, loaded.provenance) : []; }, [loaded, t]);
  const style = view.inferred ? ({ "--me-inferred-opacity": String(opacity) } as CSSProperties) : undefined;
  return (
    <li className="me-row" data-testid={`me-row-${row.address}`} data-address={row.address} data-kind={row.kind} data-class={view.classId}
      data-inferred={view.inferred ? "true" : undefined} data-status={row.status} style={style}>
      <div className="me-row-top">
        <span className="me-row-label" data-testid="me-row-label">{view.label}</span>
        {view.value !== null && <span className="me-row-value" data-testid="me-row-value">{view.value}</span>}
        {view.state !== null && <span className="me-row-state gnav-note" data-testid="me-row-state">{view.state}</span>}
      </div>
      <div className="gnav-chips">
        {view.chip !== null && <span className="gnav-chip gnav-chip-evidence" data-testid="me-row-chip" data-class={view.classId}>{view.chip}</span>}
        {view.stateChip !== null && <span className="gnav-chip" data-testid="me-row-state-chip" data-state={row.state ?? undefined}>{view.stateChip}</span>}
        {view.setBy && <span className="gnav-chip" data-testid="me-row-setby">{view.setBy}</span>}
        {view.inferredNote && <span className="gnav-chip gnav-chip-derived" data-testid="me-row-inferred-note">{view.inferredNote}</span>}
      </div>
      {view.provenance && <p className="me-row-provenance" data-testid="me-row-provenance" data-time={view.provenance.time ?? undefined} data-method={view.provenance.method ?? undefined}>{view.provenance.text}</p>}
      {view.why !== "" && <p className="gnav-why me-row-why" data-testid="me-row-why">{view.why}</p>}
      <ActionBar buttons={view.actions} subject={view.label} defaultValue={typeof row.value === "string" ? row.value : ""} scope={row.address} t={t} language={language} convId={convId}
        proposalFeature={proposalFeature} onChanged={onChanged} onOpenConversation={onOpenConversation} />
      <Disclosure testId={`me-details-${row.address}`} className="me-details" labels={{ show: t("me.details.show"), hide: t("me.details.hide") }} open={open} onToggle={setOpen}>
        {open && !card && <p className="gnav-note" data-testid="me-details-loading">{t("me.details.loading")}</p>}
        {card && "error" in card && <p className="gnav-note gnav-error" data-testid="me-details-error">{t("me.details.error", { reason: card.error })}</p>}
        {lines.map((l, i) => <p key={i} className="ncard-line" data-testid="me-detail-line">{l}</p>)}
        {chain.length > 0 && <>
          <p className="ncard-line"><strong>{t("inheritance")}</strong></p>
          <ul className="ncard-list ncard-chain" data-testid="me-chain">
            {chain.map(c => <li key={c.level} className={`ncard-chain-row${c.winning ? " ncard-winning" : ""}${c.active ? "" : " ncard-inactive"}`} data-testid={`me-chain-${c.level}`} data-role={c.role} data-winning={c.winning}>
              <span className="ncard-row-label">{c.label}</span>
              <span className="ncard-chain-role">{c.noUnitText ?? c.roleText}</span>
              {c.valueText && <span className="ncard-chain-value">{c.valueText}</span>}
              {c.meaningText && <span className="ncard-chain-meaning">{c.meaningText}</span>}
            </li>)}
          </ul>
        </>}
      </Disclosure>
    </li>
  );
}

/** One part of the person as a section; its entries are read from the kernel when it is opened. */
function MeSection({ head, open, onToggle, t, language, convId, proposalFeature, opacity, onChanged, onOpenConversation, reloadKey }: {
  head: ReturnType<typeof sections>[number]; open: boolean; onToggle: (open: boolean) => void; t: ShellT; language: string; convId: string | null;
  proposalFeature: PresentationFeature | null; opacity: number; onChanged: (done: string) => void; onOpenConversation?: (convId: string) => void; reloadKey: number;
}) {
  const [inside, setInside] = useState<GraphNavigateResult | { error: string } | null>(null);
  useEffect(() => {
    if (!open) return;
    let alive = true;
    const actions = api.actions;
    if (!actions) { setInside({ error: t("error.no_actions") }); return; }
    actions.graphNavigate({ focus: head.address, lang: language, ...(convId ? { conv_id: convId } : {}) }).then(
      answer => { if (alive) setInside(answer); },
      failure => { if (alive) setInside({ error: failure instanceof Error ? failure.message : String(failure) }); });
    return () => { alive = false; };
  }, [open, head.address, language, convId, reloadKey, t]);
  const answer = inside && !("error" in inside) ? inside : null;
  return (
    <Disclosure testId={`me-section-${head.address}`} className="me-section" labels={{ show: head.head, hide: head.head }} open={open} onToggle={onToggle}>
      {open && <div data-testid={`me-section-body-${head.address}`} data-count={head.count ?? undefined}>
        {head.why !== "" && <p className="gnav-why me-section-why" data-testid="me-section-why">{head.why}</p>}
        {head.mix && <p className="gnav-note me-section-mix" data-testid="me-section-mix">{head.mix}</p>}
        <ActionBar buttons={head.actions} subject="" defaultValue="" scope={head.address} t={t} language={language} convId={convId} proposalFeature={proposalFeature}
          onChanged={onChanged} onOpenConversation={onOpenConversation} />
        {!inside && <p className="gnav-note" data-testid="me-section-loading">{t("me.section.loading")}</p>}
        {inside && "error" in inside && <p className="gnav-note gnav-error" data-testid="me-section-error">{t("me.section.error", { reason: inside.error })}</p>}
        {answer && answer.children.length === 0 && <p className="gnav-note" data-testid="me-section-empty">{t("me.section.empty")}</p>}
        {answer && answer.children.length > 0 && <ul className="me-rows" data-testid={`me-section-rows-${head.address}`} data-shown={answer.counts.shown} data-total={answer.counts.total}>
          {answer.children.map(row => <MeRow key={row.address} row={row} view={rowView(row, t, language)} t={t} language={language} convId={convId} proposalFeature={proposalFeature}
            opacity={opacity} onChanged={onChanged} onOpenConversation={onOpenConversation} reloadKey={reloadKey} />)}
        </ul>}
        {answer && answer.counts.total > answer.counts.shown && <p className="gnav-note" data-testid="me-section-partial">{t("me.section.partial", { shown: String(answer.counts.shown), total: String(answer.counts.total) })}</p>}
      </div>}
    </Disclosure>
  );
}

export default function MeView({ result, t, language, convId, proposalFeature, openSetting, inferredOpacity, onSetting, onChanged, onOpenConversation, reloadKey }: Props) {
  const [openList, setOpenList] = useState<string[]>(() => openSectionsOf(openSetting));
  const [saveNote, setSaveNote] = useState<string | null>(null);
  const [done, setDone] = useState<string | null>(null);
  // The header is the kernel's card of the person (graph.card): its words and the actions it offers.
  const [card, setCard] = useState<GraphCardResult | null>(null);
  useEffect(() => {
    let alive = true;
    const actions = api.actions;
    if (!actions) return;
    actions.graphCard({ address: result.focus.address, lang: language, ...(convId ? { conv_id: convId } : {}) }).then(
      answer => { if (alive) setCard(answer); }, () => { if (alive) setCard(null); });
    return () => { alive = false; };
  }, [result.focus.address, language, convId, reloadKey]);
  // The setting arrives after the config loads: follow it until the person opens or closes a section here.
  const [touched, setTouched] = useState(false);
  useEffect(() => { if (!touched) setOpenList(openSectionsOf(openSetting)); }, [openSetting, touched]);
  const heads = useMemo(() => sections(result, t), [result, t]);
  const loose = useMemo(() => looseRows(result), [result]);
  const opacity = inferredOpacityOf(inferredOpacity);
  const toggle = (address: string, isOpen: boolean) => {
    const next = withSection(openList, address, isOpen);
    setTouched(true); setOpenList(next); setSaveNote(null);
    onSetting?.(OPEN_SECTIONS_SETTING, serializeSections(next));
    api.setConfigKey(OPEN_SECTIONS_SETTING, serializeSections(next)).catch(failure => setSaveNote(failure instanceof Error ? failure.message : String(failure)));
  };
  // The fixed button "Poznaj mnie" is the action of the interview; the card's own offer of it would be the same button twice.
  const headerButtons = useMemo(() => actionButtons(card?.actions, t).filter(b => b.id !== INTERVIEW_ACTION), [card, t]);
  const changed = useCallback((text: string) => { setDone(text); onChanged(); }, [onChanged]);
  const interviewButton: ActionButton = { id: INTERVIEW_ACTION, label: t("me.interview.start"), title: t("me.interview.start"), planned: false, args: { op: "start" }, params: [] };
  const common = { t, language, convId, proposalFeature, onChanged: changed, onOpenConversation };
  return (
    <section className="me-view" data-testid="me-view" aria-label={t("me.title")} data-parts={heads.length}>
      <header className="me-header">
        <h2 className="me-title" data-testid="me-title">{card?.label ?? result.focus.label}</h2>
        <p className="gnav-why me-lead" data-testid="me-lead">{card?.why ?? result.focus.why ?? t("me.lead.hint")}</p>
        <p className="gnav-note me-hint" data-testid="me-hint">{t("me.lead.hint")}</p>
        <div className="me-interview">
          <ActionBar buttons={[interviewButton]} subject="" defaultValue="" scope="interview" {...common} />
          <p className="gnav-note" data-testid="me-interview-hint">{t("me.interview.hint")}</p>
        </div>
        {headerButtons.length > 0 && <ActionBar buttons={headerButtons} subject="" defaultValue="" scope="header" {...common} />}
      </header>
      {done && <p className="gnav-note ncard-notice me-done" data-testid="me-done" role="status">{done}</p>}
      <h3 className="me-sections-title" data-testid="me-sections-title">{t("me.sections.title")}</h3>
      {heads.map(head => <MeSection key={head.address} head={head} open={openList.includes(head.address)} onToggle={isOpen => toggle(head.address, isOpen)} opacity={opacity} reloadKey={reloadKey} {...common} />)}
      {loose.length > 0 && <ul className="me-rows" data-testid="me-loose-rows">
        {loose.map(row => <MeRow key={row.address} row={row} view={rowView(row, t, language)} opacity={opacity} reloadKey={reloadKey} {...common} />)}
      </ul>}
      {saveNote && <p className="gnav-note gnav-error" data-testid="me-save-error">{t("view.save_error", { reason: saveNote })}</p>}
    </section>
  );
}
