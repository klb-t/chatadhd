// W-gaps (OKNA, W8 gap 1): the last request this page sent to the kernel, kept for the window `request_inspector` („Zapytanie do modelu”).
// A tiny store (module state + listeners): ChatView records what it sends (or what the Expert view prepared), the window reads it. Nothing
// is persisted and nothing leaves the page: it is the same object the page already sent. Pure helpers below turn it into words.
export interface SentRequest { request: Record<string, unknown>; at: number; prepared: boolean }

let last: SentRequest | null = null;
const listeners = new Set<() => void>();

export function recordRequest(request: object, prepared = false): void {
  last = { request: JSON.parse(JSON.stringify(request)) as Record<string, unknown>, at: Date.now(), prepared };
  for (const l of listeners) l();
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
