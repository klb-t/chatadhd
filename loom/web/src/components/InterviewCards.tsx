import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { api } from "../api";
import { readInterviewStart, readInterviewStatus, type InterviewLevel, type InterviewStatus } from "../api/profile-interview";
import { cardSettingOf, cardVisible, chipSpecs, isInterviewConversation, progressWord, settingAtLaunch, type CardSetting } from "../context/interview";
import { shellTranslator, type ShellT } from "../shell/text";
import { usePresentationFeature } from "./ConversationSourceView";
import type { UserProfileHost } from "../api/onboarding-host";
import "./interview-cards.css";

/** The words of presentation.interview in the language of the screen (a missing word shows its id). */
export function useInterviewWords(profileHost: UserProfileHost | undefined, language: string): ShellT {
  const words = usePresentationFeature(profileHost, "interview");
  return useMemo(() => shellTranslator(words.feature, language), [words.feature, language]);
}

/** Whether the conversation is the interview: the kernel marked it with metadata.flow when it opened it. Read from the conversation itself
 *  (the conversation view of a native host does not carry the record). */
export function useIsInterviewConversation(convId: string | null | undefined): boolean {
  const [flow, setFlow] = useState<{ convId: string; interview: boolean } | null>(null);
  useEffect(() => {
    let current = true;
    if (!convId) return;
    api.getConversation(convId).then(c => { if (current) setFlow({ convId, interview: isInterviewConversation(c.metadata) }); }, () => { if (current) setFlow(null); });
    return () => { current = false; };
  }, [convId]);
  return !!convId && flow?.convId === convId && flow.interview;
}

// "Później" lasts until the next launch: the first look of a launch (this page load) turns a stored `later` back into `show`.
let launchSettled = false;

/** The card of the empty chat: its state (from the setting onboarding_card and the kernel's status of the interview) and the three taps.
 *  The setting is read and written through the settings path of the kernel (api.getConfig / api.setConfigKey), never browser storage. */
export function useInterviewCard(language: string, onOpen: (convId: string) => void, enabled: boolean) {
  const [setting, setSetting] = useState<CardSetting | null>(null);
  const [status, setStatus] = useState<InterviewStatus | null>(null);
  const [hiddenNow, setHiddenNow] = useState(false);
  const [busy, setBusy] = useState(false);
  const [failure, setFailure] = useState<string | null>(null);
  const [note, setNote] = useState<"later" | "never" | null>(null);
  const live = useRef(true);
  useEffect(() => { live.current = true; return () => { live.current = false; }; }, []);
  useEffect(() => {
    let current = true;
    if (!enabled) return;
    void (async () => {
      try {
        let value = cardSettingOf((await api.getConfig()).onboarding_card);
        if (!launchSettled) {
          launchSettled = true;
          const settled = settingAtLaunch(value);
          if (settled !== value) { await api.setConfigKey("onboarding_card", settled); value = settled; }
        }
        if (current) setSetting(value);
      } catch { if (current) setSetting(null); }
      const actions = api.actions;
      if (!actions) return;
      try { const run = await actions.profileInterview({ op: "status", lang: language }); if (current) setStatus(readInterviewStatus(run)); }
      catch (e) { if (current) setStatus({ kind: "unavailable", message: e instanceof Error ? e.message : String(e) }); }
    })();
    return () => { current = false; };
  }, [language, enabled]);
  const start = useCallback(async () => {
    const actions = api.actions;
    if (!actions) return;
    setBusy(true); setFailure(null);
    try {
      const answer = readInterviewStart(await actions.profileInterview({ op: "start", lang: language }));
      if (!live.current) return;
      if (answer.kind === "opened") { setStatus({ kind: "active", remaining: null, levelChoicePending: true, level: null }); onOpen(answer.convId); }
      else setFailure(answer.message);
    } catch (e) { if (live.current) setFailure(e instanceof Error ? e.message : String(e)); }
    finally { if (live.current) setBusy(false); }
  }, [language, onOpen]);
  const choose = useCallback(async (value: "later" | "never") => {
    setFailure(null);
    try { await api.setConfigKey("onboarding_card", value); if (live.current) { setSetting(value); setHiddenNow(true); setNote(value); } }
    catch (e) { if (live.current) setFailure(e instanceof Error ? e.message : String(e)); }
  }, []);
  // checking = the setting or the status is not known yet; shown | hidden = decided (the e2e waits for this, never for time)
  const visible = cardVisible({ setting, status, hiddenNow });
  const state: "checking" | "shown" | "hidden" = !enabled ? "hidden" : visible ? "shown" : ((setting === null || status === null) && !hiddenNow && !failure ? "checking" : "hidden");
  return { visible, state, busy, failure, note, start, later: () => choose("later"), never: () => choose("never") };
}

/** The card "Poznajmy się" of the empty chat. */
export function InterviewStartCard({ t, busy, failure, onStart, onLater, onNever }: {
  t: ShellT; busy: boolean; failure: string | null; onStart: () => void; onLater: () => void; onNever: () => void;
}) {
  return (
    <div className="shell-card interview-card" data-testid="interview-card" role="group" aria-labelledby="interview-card-title">
      <strong id="interview-card-title" data-testid="interview-card-title">{t("card.title")}</strong>
      <span data-testid="interview-card-body">{t("card.body")}</span>
      <span className="interview-card-privacy" data-testid="interview-card-privacy">{t("card.privacy")}</span>
      <div className="interview-card-buttons">
        <button type="button" className="primary" disabled={busy} onClick={onStart} data-testid="interview-card-start">{busy ? t("card.starting") : t("card.start")}</button>
        <button type="button" disabled={busy} onClick={onLater} data-testid="interview-card-later">{t("card.later")}</button>
        <button type="button" disabled={busy} onClick={onNever} data-testid="interview-card-never">{t("card.never")}</button>
      </div>
      <span className="interview-card-note" data-testid="interview-card-note">{t("card.later_note")} {t("card.never_note")}</span>
      {failure && <span role="alert" data-testid="interview-card-failure">{t("card.failed", { reason: failure })}</span>}
    </div>
  );
}

/** What the person decided on the card, said in one line for as long as the empty chat is on the screen. */
export function InterviewCardDecision({ t, note }: { t: ShellT; note: "later" | "never" }) {
  return <p className="interview-card-decision" role="status" data-testid="interview-card-decision" data-decision={note}>{note === "later" ? t("card.later_note") : t("card.never_note")}</p>;
}

/** The three levels as cards, each with the preview: what I will ask, what I will write down, what the model will see. A tap answers. */
export function InterviewLevelCards({ t, levels, disabled, onPick }: { t: ShellT; levels: InterviewLevel[]; disabled: boolean; onPick: (level: InterviewLevel) => void }) {
  return (
    <div className="interview-levels" role="group" data-testid="interview-levels">
      {levels.map(level => (
        <button key={level.id} type="button" className="interview-level" disabled={disabled} onClick={() => onPick(level)}
          data-testid="interview-level" data-level={level.id} aria-label={t("level.aria", { label: level.label })}>
          <strong data-testid="interview-level-label">{level.label}</strong>
          <dl>
            <dt>{t("level.asks")}</dt><dd data-testid="interview-level-asks">{level.preview.asks}</dd>
            <dt>{t("level.stores")}</dt><dd data-testid="interview-level-stores">{level.preview.stores}</dd>
            <dt>{t("level.model_sees")}</dt><dd data-testid="interview-level-model-sees">{level.preview.model_sees}</dd>
          </dl>
        </button>
      ))}
      <p className="interview-hint" data-testid="interview-level-hint">{t("level.hint")}</p>
    </div>
  );
}

/** The quick replies under a question: each sends its control word as the person's message. */
export function InterviewChips({ t, disabled, onSay }: { t: ShellT; disabled: boolean; onSay: (word: string) => void }) {
  const chips = useMemo(() => chipSpecs(t), [t]);
  if (chips.length === 0) return null;
  return (
    <div className="interview-chips" role="group" aria-label={t("chips.aria")} data-testid="interview-chips">
      {chips.map(chip => <button key={chip.id} type="button" disabled={disabled} onClick={() => onSay(chip.say)} data-testid="interview-chip" data-chip={chip.id}>{chip.label}</button>)}
    </div>
  );
}

/** The line at the top of an interview conversation: no model, a form only, and how many questions are left (from the kernel's status). */
export function InterviewInfoLine({ t, convId, refreshKey, language, variant }: { t: ShellT; convId: string; refreshKey: number; language: string; variant: "chat" | "workshop" }) {
  const [status, setStatus] = useState<InterviewStatus | null>(null);
  useEffect(() => {
    let current = true;
    const actions = api.actions;
    if (!actions) return;
    actions.profileInterview({ op: "status", lang: language }).then(run => { if (current) setStatus(readInterviewStatus(run)); }, () => { if (current) setStatus(null); });
    return () => { current = false; };
  }, [convId, refreshKey, language]);
  const progress = progressWord(status, t);
  return (
    <div className="interview-info" role="status" data-testid="interview-info" data-variant={variant} data-progress={status?.kind === "active" ? String(status.remaining ?? "") : undefined}>
      <strong data-testid="interview-info-line">{t("info.line")}</strong>
      <span data-testid="interview-info-why">{t("info.why")}</span>
      {progress && <span data-testid="interview-info-progress">{progress}</span>}
    </div>
  );
}
