// M1-W2a: the calls and the shapes of the interview about the person (kernel M1-K2b). Four actions of the catalogue, no new C ABI function:
//   profile.interview  POST /api/profile/interview  {op: start|answer|review|skip|decline|never|status|say, ...}  -> a run; result.data depends on the op
//                      (start without conv_id opens the conversation "Poznajmy się": data.conv_id, data.conversation, data.question with data.question.levels)
//   profile.show       POST /api/profile/show       {user_id?, locale?}  read only
//   profile.set        POST /api/profile/set        {slot, value}        confirm:profile.set
//   profile.confirm    POST /api/profile/confirm    {kind: remark|section, ...}  confirm:profile.confirm
// The interview is held in the conversation window in form mode: no model is called. A message of the interview carries
// metadata.interview = {kind, question, mode} for the client; only the fields the screen reads are typed here, the rest stays as it came.
import type { ActionRun } from "./types";

export interface ProfileInterviewRequest {
  op: "start" | "answer" | "review" | "skip" | "decline" | "never" | "status" | "say" | string;
  conv_id?: string; user_id?: string; lang?: string; locale?: string; text?: string; slot?: string;
  [key: string]: unknown;
}
export interface ProfileShowRequest { user_id?: string; locale?: string; lang?: string }
export interface ProfileSetRequest { slot: string; value: unknown; user_id?: string; locale?: string }
export interface ProfileConfirmRequest { kind: "remark" | "section" | string; text?: string; section?: string; decision?: string; user_id?: string; locale?: string }

/** One of the three levels of "how exactly should I know you": what I would ask, what I would write down, what the model would see. */
export interface InterviewLevel {
  id: string;
  label: string;
  preview: { asks: string; stores: string; model_sees: string };
}
export interface InterviewQuestion {
  field?: string;
  topic?: string;
  text: string;
  is_level_question: boolean;
  levels: InterviewLevel[];
}
/** metadata.interview of a message of the interview: question | reply | proposal | answer (the person's own), with the open question (or none). */
export interface InterviewMeta { kind: string; question: InterviewQuestion | null; mode: string }

/** What `profile.interview {op: start}` answered: the conversation to open, or the kernel's reason. */
export type InterviewStart =
  | { kind: "opened"; convId: string; text: string }
  | { kind: "failed"; message: string };

export function readInterviewStart(run: ActionRun | null | undefined): InterviewStart {
  if (!run || typeof run !== "object") return { kind: "failed", message: "no_answer" };
  if (run.status !== "completed") return { kind: "failed", message: run.error?.message ?? run.status ?? "no_answer" };
  const data = (run.result?.data ?? null) as Record<string, unknown> | null;
  const convId = data && typeof data.conv_id === "string" ? data.conv_id : "";
  if (convId === "") return { kind: "failed", message: "no_conversation" };
  const part = run.result?.parts?.find(p => typeof p?.text === "string");
  return { kind: "opened", convId, text: typeof part?.text === "string" ? part.text : "" };
}

/** What `profile.interview {op: status}` answers: not started yet (the kernel says not_found), or the numbers of the interview. */
export type InterviewStatus =
  | { kind: "not_started" }
  | { kind: "active"; remaining: number | null; levelChoicePending: boolean; level: string | null }
  | { kind: "unavailable"; message: string };

export function readInterviewStatus(run: ActionRun | null | undefined): InterviewStatus {
  if (!run || typeof run !== "object") return { kind: "unavailable", message: "no_answer" };
  if (run.status === "completed") {
    const data = (run.result?.data ?? {}) as Record<string, unknown>;
    return {
      kind: "active", remaining: typeof data.remaining === "number" && Number.isFinite(data.remaining) ? data.remaining : null,
      levelChoicePending: data.level_choice_pending === true, level: typeof data.level === "string" ? data.level : null,
    };
  }
  if (run.error?.code === "not_found") return { kind: "not_started" };
  return { kind: "unavailable", message: run.error?.message ?? run.status ?? "no_answer" };
}
