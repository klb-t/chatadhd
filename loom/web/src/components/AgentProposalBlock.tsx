import { useState } from "react";
import { allAnswered, type AgentLoopView } from "../context/agent-proposal";
import type { ShellT } from "../shell/text";

/** The agent stopped because some of its calls need the user (proposals[] with their run ids): each can be accepted or rejected, and
 *  when all are answered the run resumes. Nothing has been executed for a proposal until it is accepted. */
export default function AgentProposalBlock({ view, t, busy, failure, onResume, testId }: {
  view: AgentLoopView; t: ShellT; busy?: boolean; failure?: string | null;
  onResume: (decided: Record<string, boolean>) => Promise<boolean>; testId: string;
}) {
  const [decided, setDecided] = useState<Record<string, boolean>>({});
  const answer = (runId: string, approved: boolean) => {
    const next = { ...decided, [runId]: approved };
    setDecided(next);
    // A resume that failed leaves the proposals open again: the answers can be given once more.
    if (allAnswered(view, next)) void onResume(next).then(ok => { if (!ok) setDecided({}); });
  };
  return (
    <div className="pill warn" role="group" data-testid={testId} aria-live="polite" data-loop-id={view.loopId}>
      <strong>{t("agent_title")}</strong>
      <p>{t("agent_sends_note")}</p>
      <ul>
        {view.proposals.map(p => {
          const known = p.runId in decided ? decided[p.runId] : p.runId in view.answered ? view.answered[p.runId] : null;
          return (
            <li key={p.runId} data-testid={testId + "-item"} data-run-id={p.runId}>
              <code data-testid={testId + "-action"}>{p.action}</code>
              {p.arguments !== "" && <code data-testid={testId + "-args"}> {p.arguments}</code>}
              <small data-testid={testId + "-run"}> {t("agent_run", { run: p.runId })}</small>
              {known === null
                ? (<>
                    <button type="button" className="primary" disabled={busy} onClick={() => answer(p.runId, true)} data-testid={testId + "-accept"}>{t("agent_accept")}</button>
                    <button type="button" disabled={busy} onClick={() => answer(p.runId, false)} data-testid={testId + "-reject"}>{t("agent_reject")}</button>
                  </>)
                : <span data-testid={testId + "-decided"} data-approved={known ? "true" : "false"}>{t(known ? "agent_decided_accepted" : "agent_decided_rejected")}</span>}
            </li>
          );
        })}
      </ul>
      {busy && <p data-testid={testId + "-resuming"}>{t("agent_resuming")}</p>}
      {failure && <p role="alert" data-testid={testId + "-failure"}>{t("agent_failed", { reason: failure })}</p>}
    </div>
  );
}
