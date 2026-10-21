// M1-W1a: the pure state of the sheet "Co wysłaliśmy" (ContextReceipt.tsx) and of the line under an answer (ChatView.tsx). Everything shown is
// computed from the receipt the kernel recorded (loom.context_receipt/1) and the words of presentation.context_receipt; nothing here lists a
// section, a source, a reason or a profile (a word that is missing shows its id, never a guess), and nothing is invented: a price the receipt
// does not carry is "unknown", a guard value the kernel could not compute is "unknown".
import type { ContextReceipt, ReceiptSection } from "../api/context-receipt";
import type { ShellT } from "../shell/text";

export interface SheetItem { key: string; text: string; chip: string; why: string; tokens: number | null }
export interface SheetSection { id: string; title: string; items: SheetItem[]; omitted: number; tokens: number }
export interface SheetDropped { key: string; text: string }
export interface SheetModel {
  profileLine: string;
  modifiersLine: string;
  /** null when the receipt names no order / no flow. */
  orderLine: string | null;
  flowLine: string | null;
  prefixLine: string;
  prefixDetail: string | null;
  /** The guard's sentence and its state, for a data attribute: ok (asked: false), asked, unknown (the kernel could not compute a ratio). */
  guardLine: string;
  guardState: "ok" | "asked" | "unknown";
  tokensLine: string;
  costLine: string;
  truncated: string | null;
  /** Sections with at least one item (or items the receipt omitted for size), in the order the receipt gives them. */
  sections: SheetSection[];
  /** Sections that held nothing: named in one line, never hidden. */
  emptyNames: string[];
  emptyLine: string | null;
  droppedTitle: string;
  dropped: SheetDropped[];
  droppedNone: string;
  totalItems: number;
}

const numberText = (n: number, digits = 1): string => {
  const fixed = n.toFixed(digits);
  return fixed.replace(/\.0+$/, "");
};

/** A label that is an {en, pl} table, or a string: in the language, else English, else the fallback (the id). */
export function labelOf(label: unknown, language: string, fallback: string): string {
  if (typeof label === "string" && label) return label;
  if (label && typeof label === "object") {
    const table = label as Record<string, unknown>;
    const text = table[language] ?? table.en;
    if (typeof text === "string" && text) return text;
  }
  return fallback;
}

/** A word that exists, else the fallback (a missing word must not show as its own id where a better text is known). */
function wordOr(t: ShellT, id: string, fallback: string, params?: Record<string, string>): string {
  const said = t(id, params);
  return said === id ? fallback : said;
}

export function sectionTitle(cls: string, t: ShellT): string { return wordOr(t, `section.${cls}`, cls); }

export function itemsOf(section: ReceiptSection): number { return Array.isArray(section.items) ? section.items.length : 0; }
export function totalItems(receipt: ContextReceipt): number { return receipt.sections.reduce((n, s) => n + itemsOf(s), 0); }

/** The text of one item: the word for its fixed ref (the instruction, the question ...), the role of a history message, else the kernel's label. */
export function itemText(item: { ref: string; label?: string; source_kind?: string }, t: ShellT): string {
  const byRef = t(`ref.${item.ref}`);
  if (byRef !== `ref.${item.ref}`) return byRef;
  if (item.source_kind && item.source_kind.startsWith("message:")) {
    const role = t(`role.${item.label ?? ""}`);
    if (item.label && role !== `role.${item.label}`) return role;
  }
  return item.label || item.ref;
}

export function itemChip(kind: string | undefined, t: ShellT): string {
  if (!kind) return "";
  const word = t(`chip.${kind.replace(":", ".")}`);
  return word === `chip.${kind.replace(":", ".")}` ? kind : word;
}

export function profileName(receipt: ContextReceipt, language: string): string | null {
  const p = receipt.profile;
  if (!p || p.id === null || p.id === undefined) return null;
  return labelOf(p.label, language, p.id);
}

/** "szerzej ×2, głębiej" from the steps the receipt recorded (the words are action.<step>), plus the profile of the turn. */
export function modifierParts(receipt: ContextReceipt, t: ShellT, language: string): string[] {
  const parts: string[] = [];
  const m = receipt.modifiers;
  if (!m) return parts;
  for (const steps of [m.breadth_delta, m.depth_delta]) {
    if (!Array.isArray(steps)) continue;
    const counted = new Map<string, number>();
    for (const s of steps) if (typeof s === "string") counted.set(s, (counted.get(s) ?? 0) + 1);
    for (const [id, n] of counted) {
      const w = wordOr(t, `action.${id}`, id);
      parts.push(n > 1 ? `${w} ×${n}` : w);
    }
  }
  if (typeof m.profile === "string" && m.profile) parts.push(t("modifier.profile", { profile: profileName(receipt, language) ?? m.profile }));
  return parts;
}

export function guardView(receipt: ContextReceipt, t: ShellT): { line: string; state: "ok" | "asked" | "unknown" } {
  const g = receipt.guard;
  if (!g || typeof g.ratio !== "number") return { line: t("guard.unknown"), state: "unknown" };
  const ratio = numberText(g.ratio);
  return g.asked ? { line: t("guard.asked", { ratio }), state: "asked" } : { line: t("guard.ok", { ratio }), state: "ok" };
}

/** The prefix is stable when the order puts the profile (and the rules) before everything that changes from turn to turn and the profile block
 *  was built by the kernel (profile_prefix has a level): then the start of the request is the same in every turn. */
export function prefixStable(receipt: ContextReceipt): boolean {
  const classes = receipt.order?.classes ?? [];
  const at = classes.indexOf("profile");
  if (at < 0) return false;
  const prefix = receipt.profile_prefix;
  if (!prefix || typeof prefix.level !== "string") return false;
  return ["history", "graph", "memory", "project"].every(c => { const i = classes.indexOf(c); return i < 0 || i > at; });
}

export function costLine(receipt: ContextReceipt, t: ShellT): string {
  const usd = receipt.cost_usd;
  if (typeof usd === "number" && Number.isFinite(usd) && usd > 0) return t("cost.amount", { amount: `$${usd.toFixed(usd < 0.01 ? 4 : 2)}` });
  return t("cost.unknown");   // the receipt carries no price (or a zero is not a price): never "0.0000"
}

export function sheetModel(receipt: ContextReceipt, t: ShellT, language: string): SheetModel {
  const name = profileName(receipt, language);
  const p = receipt.profile;
  const profileLine = name !== null && p
    ? t("header.profile_line", { profile: name, status: p.status ? wordOr(t, `status.${p.status}`, p.status) : "", source: p.source ? wordOr(t, `source.${p.source}`, p.source) : "" })
    : t("header.profile_none");
  const mods = modifierParts(receipt, t, language);
  const scope = receipt.modifiers?.scope;
  const modifiersLine = mods.length
    ? t("header.modifiers", { modifiers: mods.join(", ") + (scope ? ` (${wordOr(t, `scope.${scope}`, scope)})` : "") })
    : t("header.modifiers_none");
  const prefix = receipt.profile_prefix;
  let prefixDetail: string | null = null;
  if (prefix && typeof prefix === "object") {
    if (prefix.state) prefixDetail = wordOr(t, `prefix.state.${prefix.state}`, prefix.state);
    else if (prefix.unavailable) prefixDetail = wordOr(t, `prefix.unavailable.${prefix.unavailable}`, prefix.unavailable);
    else if (typeof prefix.slots === "number") prefixDetail = t("header.prefix_detail", { slots: String(prefix.slots), level: prefix.level ?? "" });
  }
  const guard = guardView(receipt, t);
  const filled: SheetSection[] = [];
  const emptyNames: string[] = [];
  for (const s of receipt.sections) {
    const items = (s.items ?? []).map((it, i) => ({
      key: `${it.ref}:${i}`, text: itemText(it, t), chip: itemChip(it.source_kind, t), why: it.why ?? "", tokens: typeof it.tokens_est === "number" ? it.tokens_est : null,
    }));
    const omitted = typeof s.items_omitted === "number" ? s.items_omitted : 0;
    if (items.length || omitted) filled.push({ id: s.class, title: sectionTitle(s.class, t), items, omitted, tokens: s.tokens_est ?? 0 });
    else emptyNames.push(sectionTitle(s.class, t));
  }
  const dropped = (receipt.dropped ?? []).map((d, i) => {
    const count = String(d.count ?? (d.refs?.length ?? 0));
    const known = t(`dropped.${d.reason}`, { count, level: "", reason: d.reason });
    const reason = known === `dropped.${d.reason}` ? t("dropped.other", { reason: d.reason, count }) : known;
    return { key: `${d.ref ?? ""}:${d.reason}:${i}`, text: d.ref ? t("dropped.row", { section: sectionTitle(d.ref, t), reason }) : reason };
  });
  return {
    profileLine, modifiersLine,
    orderLine: receipt.order?.name ? t("header.order", { order: receipt.order.name }) : null,
    flowLine: receipt.flow?.flow ? t("header.flow", { flow: receipt.flow.flow }) : null,
    prefixLine: t(prefixStable(receipt) ? "prefix.stable.yes" : "prefix.stable.no"), prefixDetail,
    guardLine: guard.line, guardState: guard.state,
    tokensLine: typeof receipt.tokens_est === "number" ? t("tokens", { n: String(receipt.tokens_est) }) : "",
    costLine: costLine(receipt, t),
    truncated: receipt.truncated ? t("truncated") : null,
    sections: filled, emptyNames, emptyLine: emptyNames.length ? t("sections.empty", { names: emptyNames.join(", ") }) : null,
    droppedTitle: t("dropped.title"), dropped, droppedNone: t("dropped.none"),
    totalItems: totalItems(receipt),
  };
}

/** The line under an answer: "Kontekst: Loom (preset) · pozycji: 12 · ~1240 tok. — zobacz"; without a receipt, the missing word in its place. */
export function lineText(receipt: ContextReceipt | null, t: ShellT, language: string): string {
  if (!receipt) return t("line", { summary: t("missing") });
  const profile = profileName(receipt, language) ?? t("line.profile_none");
  return t("line", { summary: t("line.summary", { profile, items: String(totalItems(receipt)), tokens: String(receipt.tokens_est ?? 0) }) });
}

interface MessageLike { id: string; role: string; parent_id?: string | null; metadata?: Record<string, unknown> | null }

/** The receipt of the request that made an answer, read from the messages already loaded (no call): the answer's parent, else the nearest
 *  message of the person before it. A message with no recorded receipt gives null (the sheet then asks the kernel and shows its reason). */
export function requestOf(messages: MessageLike[], answerId: string): MessageLike | null {
  const at = messages.findIndex(m => m.id === answerId);
  if (at < 0) return null;
  const answer = messages[at];
  if (answer.role === "user") return answer;
  if (answer.parent_id) {
    const parent = messages.find(m => m.id === answer.parent_id && m.role === "user");
    if (parent) return parent;
  }
  for (let i = at - 1; i >= 0; i--) if (messages[i].role === "user") return messages[i];
  return null;
}
export function recordedReceipt(message: MessageLike | null): ContextReceipt | null {
  const trace = message?.metadata && typeof message.metadata === "object" ? (message.metadata as Record<string, unknown>).context_trace : null;
  const receipt = trace && typeof trace === "object" ? (trace as Record<string, unknown>).receipt : null;
  return receipt && typeof receipt === "object" && Array.isArray((receipt as ContextReceipt).sections) ? receipt as ContextReceipt : null;
}

/** The steps of "Następnym razem", in the order of the data: the word `action.steps` is a comma list of step ids (labels: action.<id>). */
export function stepIds(t: ShellT): string[] {
  const list = t("action.steps");
  return list === "action.steps" ? [] : list.split(",").map(s => s.trim()).filter(Boolean);
}

export interface ProfileChoice { id: string; label: string; status: string; why: string }
/** The rows of views.effective for selector_profiles (result.data.entries[].entry), skipping what the overlay disabled. */
export function profileChoices(data: unknown, language: string): ProfileChoice[] {
  const entries = data && typeof data === "object" ? (data as { entries?: unknown }).entries : null;
  if (!Array.isArray(entries)) return [];
  const out: ProfileChoice[] = [];
  for (const row of entries as Record<string, unknown>[]) {
    const entry = (row && typeof row === "object" ? row.entry : null) as Record<string, unknown> | null;
    const id = typeof row?.id === "string" ? row.id : entry && typeof entry.id === "string" ? entry.id : "";
    if (!id || !entry || row.disabled === true) continue;
    out.push({ id, label: labelOf(entry.label, language, id), status: typeof entry.status === "string" ? entry.status : "", why: labelOf(entry.why, language, "") });
  }
  return out;
}
