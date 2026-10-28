// W-gaps (OKNA, W8 gap 1): the last request this page sent to the kernel, kept for the window `request_inspector` („Zapytanie do modelu”).
// A tiny store (module state + listeners): ChatView records what it sends (or what the Expert view prepared), the window reads it. Nothing
// is persisted and nothing leaves the page: it is the same object the page already sent. Pure helpers below turn it into words.
export interface SentRequest { request: Record<string, unknown>; at: number; prepared: boolean }

let last: SentRequest | null = null;
const listeners = new Set<() => void>();

export function recordRequest(request: object, prepared = false): void {
  last = { request: JSON.parse(JSON.stringify(request)) as Record<string, unknown>, at: Date.now(), prepared };
  if (!prepared) draft = { text: "", conv: draft.conv };  // the field is emptied by a send
  for (const l of listeners) l();
}

// RI-2: what the field holds now (for „Podgląd następnego”, built by the kernel without sending) and the conversation the last request
// settled in (a message on a blank page learns its conversation only from the kernel). Module state again: nothing persisted, nothing sent.
let draft: { text: string; conv: string | null } = { text: "", conv: null };
let settledConv: string | null = null;
export function recordDraft(text: string, conv: string | null): void { draft = { text, conv }; }
export function currentDraft(): { text: string; conv: string | null } { return draft; }
/** The kernel finished (or refused) the request: the window asks the kernel again for what it sent. */
export function settleRequest(conv: string | null | undefined): void {
  if (conv) settledConv = conv;
  for (const l of listeners) l();
}
export function settledConversation(): string | null { return settledConv; }

/** The kernel's record of a request to the provider (loom.provider_request/1) in words: one line per part with its size, then tools,
 *  parameters and what the kernel masked. Ids are shell texts (ri_k_*); the raw JSON stays for „Szczegóły techniczne”. */
export function kernelLines(v: Record<string, unknown>): { id: string; vars?: Record<string, string> }[] {
  const out: { id: string; vars?: Record<string, string> }[] = [];
  const str = (x: unknown) => (typeof x === "string" ? x : x == null ? "?" : String(x));
  out.push({ id: "ri_k_head", vars: { provider: str(v.provider), endpoint: str(v.endpoint), model: str(v.model), bytes: str(v.body_bytes) } });
  if (v.kind === "sent" && v.dispatched === false) out.push({ id: "ri_k_not_dispatched" });
  const known = new Set(["system_prompt", "memory", "context", "history_user", "history_assistant", "current_message"]);
  for (const p of Array.isArray(v.parts) ? (v.parts as Record<string, unknown>[]) : []) {
    const part = str(p.part);
    out.push({ id: known.has(part) ? `ri_k_part_${part}` : "ri_k_part_other", vars: { chars: str(p.chars), role: str(p.role) } });
  }
  if (typeof v.tools === "number" && v.tools > 0) out.push({ id: "ri_k_tools", vars: { count: String(v.tools) } });
  const params = v.parameters && typeof v.parameters === "object" ? Object.entries(v.parameters as Record<string, unknown>) : [];
  const shown = params.filter(([, x]) => x === null || typeof x !== "object").map(([k, x]) => `${k}=${String(x)}`);
  if (shown.length) out.push({ id: "ri_k_params", vars: { list: shown.join(", ") } });
  const masks = Array.isArray(v.redactions) ? (v.redactions as Record<string, unknown>[]) : [];
  const hidden = masks.filter((m) => m.rule !== "credential_header").reduce((n, m) => n + (typeof m.count === "number" ? m.count : 1), 0);
  out.push(hidden ? { id: "ri_k_redacted", vars: { count: String(hidden) } } : { id: "ri_k_not_redacted" });
  return out;
}
export function lastRequest(): SentRequest | null { return last; }
export function onRequest(listener: () => void): () => void { listeners.add(listener); return () => { listeners.delete(listener); }; }

/** The request in words: one line per fact a person can check (text ids of the shell + their variables), the raw JSON stays for „Szczegóły”. */
export function requestLines(r: Record<string, unknown>): { id: string; vars?: Record<string, string> }[] {
  const out: { id: string; vars?: Record<string, string> }[] = [];
  const msg = typeof r.message === "string" ? r.message : "";
  out.push({ id: "ri_message", vars: { chars: String([...msg].length) } });
  out.push(typeof r.model === "string" && r.model ? { id: "ri_model", vars: { model: r.model } } : { id: "ri_model_default" });
  if (typeof r.conv_id === "string" && r.conv_id) out.push({ id: "ri_conversation", vars: { conv: r.conv_id } });
  else if (r.new_conversation) out.push({ id: "ri_new_conversation" });
  if (Array.isArray(r.attachments) && r.attachments.length) out.push({ id: "ri_attachments", vars: { count: String(r.attachments.length) } });
  const off = (["include_memory", "include_graph_memory", "include_history"] as const).filter((k) => r[k] === false).map((k) => k.replace(/^include_/, ""));
  if (off.length) out.push({ id: "ri_off", vars: { what: off.join(", ") } });
  const k = r.knowledge_context as { budget_tokens?: unknown } | undefined;
  if (k && typeof k === "object") out.push({ id: "ri_knowledge", vars: { budget: String(k.budget_tokens ?? "?") } });
  if (typeof r.action === "string" && r.action) out.push({ id: "ri_action", vars: { action: r.action } });
  return out;
}
