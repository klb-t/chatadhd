// M1-W2a: the pure rules of the interview about the person in the conversation window (kernel M1-K2b, form mode, no model). Nothing here is a
// word of the screen: the words are presentation.interview of conversation_view.pack, the shapes come from the kernel's messages
// (metadata.interview = {kind, question, mode}) and from the answers of profile.interview.
import type { InterviewLevel, InterviewMeta, InterviewQuestion, InterviewStatus } from "../api/profile-interview";

type Meta = Record<string, unknown> | undefined | null;

/** Whether the conversation is the interview: every message the kernel writes in it (the first question, the answers, the replies, the proposal)
 *  carries metadata.interview, and the first one exists from the moment the conversation opens. So the messages the window has already loaded
 *  say it; no extra request for the conversation record is made (an ordinary conversation costs nothing for this feature). */
export function isInterviewMessages(convId: string | null | undefined, messages: readonly { conv_id?: string; metadata?: Meta }[]): boolean {
  return !!convId && messages.some(m => (m.conv_id === undefined || m.conv_id === convId) && interviewMeta(m.metadata) !== null);
}

/** True when part of the conversation went to a model provider: an assistant message with a model that is not a form (interview) message. */
export function reachedProvider(messages: readonly { role?: string; model?: string | null; metadata?: Meta }[]): boolean {
  return messages.some(m => m.role === "assistant" && typeof m.model === "string" && m.model !== "" && interviewMeta(m.metadata) === null);
}

/** The values of the setting onboarding_card. */
export type CardSetting = "show" | "later" | "never";
export const cardSettingOf = (value: unknown): CardSetting | null => (value === "show" || value === "later" || value === "never" ? value : null);

function levelOf(value: unknown): InterviewLevel | null {
  if (!value || typeof value !== "object") return null;
  const o = value as Record<string, unknown>;
  const preview = o.preview as Record<string, unknown> | undefined;
  if (typeof o.id !== "string" || typeof o.label !== "string" || o.label === "" || !preview || typeof preview !== "object") return null;
  if (typeof preview.asks !== "string" || typeof preview.stores !== "string" || typeof preview.model_sees !== "string") return null;
  return { id: o.id, label: o.label, preview: { asks: preview.asks, stores: preview.stores, model_sees: preview.model_sees } };
}

/** metadata.interview of a message, or null when the message is not a message of the interview (or the shape is not the kernel's). */
export function interviewMeta(metadata: Meta): InterviewMeta | null {
  const raw = metadata && typeof metadata === "object" ? (metadata as Record<string, unknown>).interview : null;
  if (!raw || typeof raw !== "object") return null;
  const o = raw as Record<string, unknown>;
  if (typeof o.kind !== "string") return null;
  let question: InterviewQuestion | null = null;
  const q = o.question;
  if (q && typeof q === "object") {
    const r = q as Record<string, unknown>;
    question = {
      field: typeof r.field === "string" ? r.field : undefined, topic: typeof r.topic === "string" ? r.topic : undefined,
      text: typeof r.text === "string" ? r.text : "", is_level_question: r.is_level_question === true,
      levels: Array.isArray(r.levels) ? r.levels.flatMap(l => { const x = levelOf(l); return x ? [x] : []; }) : [],
    };
  }
  return { kind: o.kind, question, mode: typeof o.mode === "string" ? o.mode : "form" };
}

/** The three level cards of a message: only when the message asks the level question AND carries every level with its three previews;
 *  anything less is no cards (the message is then drawn as the plain text the kernel wrote). */
export function levelCards(metadata: Meta): InterviewLevel[] {
  const meta = interviewMeta(metadata);
  if (!meta?.question?.is_level_question) return [];
  return meta.question.levels.length >= 2 ? meta.question.levels : [];
}

interface RowLike { id: string; role: string; metadata?: Meta; status?: string }

/** The question that waits for an answer: the LAST message of the conversation must be an assistant message of the interview that carries one
 *  (a person's answer after it, or a proposal, means nothing waits here). */
export function waitingQuestion(messages: readonly RowLike[]): { messageId: string; question: InterviewQuestion } | null {
  const active = messages.filter(m => m.status === undefined || m.status === "active");
  const last = active[active.length - 1];
  if (!last || last.role !== "assistant") return null;
  const meta = interviewMeta(last.metadata);
  return meta?.question ? { messageId: last.id, question: meta.question } : null;
}

/** The card "Poznajmy się" of the empty chat: the setting says show, the interview was never started (the kernel's status says not_found)
 *  and nothing was tapped away in this launch. While the setting or the status is not known yet, nothing is drawn. */
export function cardVisible(input: { setting: CardSetting | null; status: InterviewStatus | null; hiddenNow: boolean }): boolean {
  return input.setting === "show" && input.status?.kind === "not_started" && !input.hiddenNow;
}

/** "Później" lasts until the next launch: at the first look of a launch a stored `later` is turned back into `show` (and the card is shown). */
export const settingAtLaunch = (setting: CardSetting | null): CardSetting | null => (setting === "later" ? "show" : setting);

export interface ChipSpec { id: string; label: string; say: string }
/** The quick replies under a message of the interview, from the words of the pack: the ids in chips.order (a question waits) or chips.idle (the
 *  message is an answer to "how many are left" or "summary": nothing waits, the person goes on or asks again), each with its label and the control
 *  word it sends. */
export function chipSpecs(t: (id: string) => string, idle = false): ChipSpec[] {
  const key = idle ? "chips.idle" : "chips.order";
  const order = t(key);
  if (order === key) return [];
  return order.split(/\s+/).filter(Boolean).map(id => ({ id, label: t(`chip.${id}.label`), say: t(`chip.${id}.say`) }))
    .filter(c => c.label !== `chip.${c.id}.label` && c.say !== `chip.${c.id}.say`);
}

/** The message of the interview the quick replies stand under: the LAST message when it is an assistant message of the interview that asks something
 *  (question | reply); `idle` when it carries no question (the answer to a status or a summary). A proposal waits for yes or no: no replies under it. */
export function repliesTarget(messages: readonly RowLike[]): { messageId: string; idle: boolean } | null {
  const active = messages.filter(m => m.status === undefined || m.status === "active");
  const last = active[active.length - 1];
  if (!last || last.role !== "assistant") return null;
  const meta = interviewMeta(last.metadata);
  if (!meta || (meta.kind !== "question" && meta.kind !== "reply")) return null;
  return { messageId: last.id, idle: meta.question === null };
}

/** The line of the progress: how many questions are left; the level choice or the end have their own words. */
export function progressWord(status: InterviewStatus | null, t: (id: string, params?: Record<string, string>) => string): string | null {
  if (!status || status.kind !== "active") return null;
  if (status.levelChoicePending) return t("info.level_pending");
  if (status.remaining === null) return null;
  return status.remaining === 0 ? t("info.finished") : t("info.progress", { n: String(status.remaining) });
}
