// M1-W1b: the calls of context.modify and their confirmation, shared by the sheet "Co wysłaliśmy" and the + menu (one implementation: the
// kernel's answer is read by readModifyRun, the x10 question is answered with actions.confirm). What waits for the next request is also kept
// in turn-pending.ts so the context strip can say it.
import { useCallback, useState } from "react";
import { api } from "../api";
import { readModifyRun, type ContextModifyRequest, type ModifyAnswer } from "../api/context-receipt";
import { setPendingTurn } from "../shell/turn-pending";

export interface ContextModify {
  answer: ModifyAnswer | null;
  busy: boolean;
  run: (request: ContextModifyRequest) => Promise<void>;
  decide: (approved: boolean) => Promise<void>;
  clear: () => void;
}

/** What an answer of the kernel leaves in the carrier: applied = `waiting` as sent (empty after a reset); a question or a refusal changes nothing. */
function remember(convId: string | null, answer: ModifyAnswer): void {
  if (answer.kind === "applied") setPendingTurn(convId, answer.waiting);
}

/** `pendingNext` reads the text typed for the next message and the options the send will pass (model, the + menu's switches ...) at the moment of the
 *  call: the estimate in the answer is compiled with them. Without them the estimate is for an empty message with the options of the conversation, and
 *  the answer says so. */
export function useContextModify(convId: string | null, noActions: string, pendingNext?: () => { text: string; options: Record<string, unknown> }): ContextModify {
  const [answer, setAnswer] = useState<ModifyAnswer | null>(null);
  const [busy, setBusy] = useState(false);
  const run = useCallback(async (request: ContextModifyRequest) => {
    const actions = api.actions;
    if (!actions) { setAnswer({ kind: "failed", message: noActions }); return; }
    setBusy(true);
    try {
      const pending = pendingNext && !request.reset ? pendingNext() : null;
      const typed = pending ? pending.text.trim() : "";
      const read = readModifyRun(await actions.contextModify({ ...request, ...(convId ? { conversation_id: convId } : {}),
        ...(typed ? { pending_text: typed } : {}), ...(pending && Object.keys(pending.options).length > 0 ? { pending_options: pending.options } : {}) }));
      remember(convId, read);
      setAnswer(read);
    } catch (failure) { setAnswer({ kind: "failed", message: failure instanceof Error ? failure.message : String(failure) }); }
    finally { setBusy(false); }
  }, [convId, noActions, pendingNext]);
  const decide = useCallback(async (approved: boolean) => {
    const actions = api.actions;
    if (!actions || !answer || answer.kind !== "question") return;
    setBusy(true);
    try {
      const read = readModifyRun(await actions.confirm({ run_id: answer.runId, approved, receipt_id: answer.receiptId, ref: "web" }));
      remember(convId, read);
      setAnswer(read);
    } catch (failure) { setAnswer({ kind: "failed", message: failure instanceof Error ? failure.message : String(failure) }); }
    finally { setBusy(false); }
  }, [answer, convId]);
  const clear = useCallback(() => setAnswer(null), []);
  return { answer, busy, run, decide, clear };
}

/** The rows of views.effective for selector_profiles (the same call the sheet makes). */
export async function loadSelectorProfiles(): Promise<unknown> {
  const actions = api.actions;
  if (!actions) return null;
  const view = await actions.run({ action: "views.effective", element: { class: "application" }, args: { kind: "selector_profiles" } });
  return view.result?.data;
}
