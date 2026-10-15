/** An agent run that stopped on proposals (agent.run, tool_loop_on_proposal = stop): what the page shows and how the user's answers
 *  resume the run. The kernel keeps the transcript; the answers go back to agent.run as `resume` (src/agent/agent.cpp). */
type Meta = Record<string, unknown>;

export interface AgentProposalItem { runId: string; action: string; arguments: string; status: string }
export interface AgentLoopView {
  loopId: string;
  proposals: AgentProposalItem[];
  messages: unknown[];
  /** run_id -> true (accepted) | false (rejected), as recorded after the run was resumed. */
  answered: Record<string, boolean>;
}
export interface AgentDecision { run_id: string; approved: boolean }

const str = (value: unknown): string => (typeof value === "string" ? value : "");

/** The proposals of a message that carries `agent_loop` (set by the router for an agent run that needs the user), else null. */
export function agentLoopView(metadata: Meta | undefined): AgentLoopView | null {
  const loop = metadata?.agent_loop;
  if (!loop || typeof loop !== "object") return null;
  const record = loop as Meta;
  if (!Array.isArray(record.proposals) || record.proposals.length === 0) return null;
  const proposals: AgentProposalItem[] = [];
  for (const row of record.proposals) {
    if (!row || typeof row !== "object") continue;
    const item = row as Meta;
    const runId = str(item.run_id);
    if (runId === "") continue;
    let args = "";
    try { args = item.arguments === undefined ? "" : JSON.stringify(item.arguments); } catch { args = ""; }
    proposals.push({ runId, action: str(item.action), arguments: args === "{}" ? "" : args, status: str(item.status) });
  }
  if (proposals.length === 0) return null;
  const answered: Record<string, boolean> = {};
  if (record.answered && typeof record.answered === "object")
    for (const [key, value] of Object.entries(record.answered as Meta)) if (typeof value === "boolean") answered[key] = value;
  return { loopId: str(record.loop_id), proposals, messages: Array.isArray(record.messages) ? record.messages : [], answered };
}

/** Every proposal has an answer: the run can continue. */
export function allAnswered(view: AgentLoopView, decided: Record<string, boolean>): boolean {
  return view.proposals.every(p => p.runId in decided);
}

/** The request that resumes the stopped run: its transcript and the answers (nothing else is added; the model and the settings are the kernel's). */
export function resumeRequest(view: AgentLoopView, decided: Record<string, boolean>, convId?: string | null) {
  const resume: AgentDecision[] = view.proposals.filter(p => p.runId in decided).map(p => ({ run_id: p.runId, approved: decided[p.runId] }));
  const request: { messages: unknown[]; resume: AgentDecision[]; conversation_id?: string } = { messages: view.messages, resume };
  if (convId) request.conversation_id = convId;
  return request;
}
