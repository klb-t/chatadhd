// R67-fix: the recorded context of a turn as rows of words (a person reads "Złożone wiadomości: 5"), not as a JSON dump. Only facts the record
// carries become rows; the raw JSON stays behind "Szczegóły techniczne". Words come from the conversation-view pack through `t`.
import type { ChatContextTrace } from "../api/types";

export type RowText = (id: string, params?: Record<string, string>) => string;
export interface TraceRow { id: string; text: string }

/** The first array an object holds under one of the keys the kernel uses for a selection (items, selected, entries); null when none. */
function selectionCount(value: unknown): number | null {
  if (!value || typeof value !== "object" || Array.isArray(value)) return null;
  const record = value as Record<string, unknown>;
  for (const key of ["items", "selected", "entries"]) {
    const list = record[key];
    if (Array.isArray(list)) return list.length;
  }
  return null;
}

export function contextTraceRows(trace: ChatContextTrace, t: RowText): TraceRow[] {
  const rows: TraceRow[] = [{ id: "messages", text: t("ctx_row_messages", { count: String(trace.messages.length) }) }];
  const roles = new Map<string, number>();
  for (const message of trace.messages) roles.set(message.role, (roles.get(message.role) ?? 0) + 1);
  if (roles.size > 0) rows.push({ id: "roles", text: t("ctx_row_roles", { list: [...roles].map(([role, count]) => `${role} ${count}`).join(" · ") }) });
  if (Array.isArray(trace.history_message_ids)) rows.push({ id: "history", text: t("ctx_row_history", { count: String(trace.history_message_ids.length) }) });
  const knowledge = selectionCount(trace.knowledge_context);
  if (knowledge !== null) rows.push({ id: "knowledge", text: t("ctx_row_knowledge", { count: String(knowledge) }) });
  return rows;
}
