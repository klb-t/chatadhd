// R67-fix: "Diagnostyka" (menu ⋮): the developer's view of a conversation - identifiers, checksums and raw traces - off the chat screen.
// It reads the messages of the open conversation from the kernel and lists the ones that carry a recorded context or a route trace.
import { useEffect, useState } from "react";
import { api } from "../api";
import type { ChatContextTrace, Message } from "../api/types";
import { useShellText } from "../shell/text";

interface Row { id: string; role: string; model: string; sha: string | null; context: ChatContextTrace | null; route: unknown }

function recorded(value: unknown): ChatContextTrace | null {
  if (!value || typeof value !== "object" || Array.isArray(value)) return null;
  const record = value as Record<string, unknown>;
  return record.kind === "compiled_messages" && Array.isArray(record.messages) ? record as ChatContextTrace : null;
}

/** The rows of the sheet: messages with a recorded context or a route trace, in the order of the conversation. */
export function diagnosticRows(messages: readonly Message[]): Row[] {
  const rows: Row[] = [];
  for (const m of messages) {
    const context = recorded(m.metadata?.context_trace);
    const route = m.metadata?.route_trace ?? null;
    if (context === null && route === null) continue;
    rows.push({ id: m.id, role: m.role, model: m.model ?? "", sha: typeof context?.messages_sha256 === "string" ? context.messages_sha256 : null, context, route });
  }
  return rows;
}

export default function DiagnosticsSheet({ convId }: { convId: string | null }) {
  const t = useShellText();
  const [state, setState] = useState<{ kind: "loading" } | { kind: "failed"; reason: string } | { kind: "ready"; rows: Row[] }>({ kind: "loading" });
  useEffect(() => {
    if (!convId) return;
    let live = true;
    setState({ kind: "loading" });
    api.getMessages(convId, true).then(
      messages => { if (live) setState({ kind: "ready", rows: diagnosticRows(messages) }); },
      err => { if (live) setState({ kind: "failed", reason: err instanceof Error ? err.message : String(err) }); });
    return () => { live = false; };
  }, [convId]);
  if (!convId) return <p className="gnav-why" data-testid="diag-no-conv">{t("diag_no_conv")}</p>;
  if (state.kind === "loading") return <p className="gnav-why" role="status" data-testid="diag-loading">{t("diag_loading")}</p>;
  if (state.kind === "failed") return <p className="gnav-why" role="alert" data-testid="diag-failed">{t("diag_failed", { reason: state.reason })}</p>;
  return (
    <div data-testid="diagnostics">
      <p className="gnav-why">{t("diag_why")}</p>
      {state.rows.length === 0 && <p className="gnav-why" data-testid="diag-empty">{t("diag_empty")}</p>}
      <ul className="diag-list">
        {state.rows.map(r => (
          <li key={r.id} data-testid="diag-row">
            <p data-audit-skip="1">{t("diag_row", { role: r.role, model: r.model || "-", id: r.id })}</p>
            {r.sha !== null && <p data-testid="diag-sha" data-audit-skip="1">{t("diag_sha", { sha: r.sha })}</p>}
            <details data-audit-skip="1"><summary>{t("diag_details")}</summary>
              <pre>{JSON.stringify({ context_trace: r.context, route_trace: r.route }, null, 2)}</pre>
            </details>
          </li>))}
      </ul>
    </div>
  );
}
