// M1-K1c3b: what the navigator shows of the perspective "Wysłane do modelu" (the record of one request drawn as a graph, K1c-2/K1c-3a). Pure
// functions over the answer of graph.navigate / graph.card, so a unit test drives them without a browser. Nothing here decides what was sent:
// the weights, the reasons and the sentences are the kernel's (edge.label, edge.metadata.reason_human, node.derived_note, receipt.text, receipt.note);
// the pack (presentation.graph_screen, sent.*) only words what the kernel leaves as numbers - tokens as an estimate, the mark "widok zapisu", the
// summary line - and the fallback for a sentence an older kernel does not send. A missing word shows its id, never a guess.
import type { GraphEdgeRow, GraphNavigateResult, GraphNodeRow } from "../api/graph-projection";
import type { ShellT } from "../shell/text";

export const SENT_EDGE_KIND = "sent_to_model";
export const LEFT_OUT_EDGE_KIND = "left_out_of_model";

/** The links of the record that end at this row: what was sent (weight = tokens, an estimate) and what was left out (weight = 1 or the group's size). */
export function edgesOfRow(result: Pick<GraphNavigateResult, "edges">, address: string): { sent: GraphEdgeRow[]; leftOut: GraphEdgeRow[] } {
  const into = result.edges.filter(e => e.dst === address);
  return { sent: into.filter(e => e.kind === SENT_EDGE_KIND), leftOut: into.filter(e => e.kind === LEFT_OUT_EDGE_KIND) };
}

/** "19 tokenów (szacunek)": always said as an estimate - the record counts characters, it does not measure the provider's tokens. */
export function tokensText(n: number, t: ShellT): string { return t("sent.tokens", { n: String(Math.round(n)) }); }

export interface RowMarks {
  /** "widok zapisu": this row is a view of the record, not a stored node. */
  derived: string | null;
  /** "wysłane do modelu: 19 tokenów (szacunek)" (the kernel's name of the link, the tokens as an estimate). */
  sent: string | null;
  /** "pominięte w żądaniu" (the kernel's name of the link). */
  leftOut: string | null;
  /** Why it was left out, in the kernel's words - only when the row's own label does not already say it. */
  reason: string | null;
  /** The summed weight of the links that sent it (tokens), for a data attribute. */
  weight: number | null;
}

export function rowMarks(result: Pick<GraphNavigateResult, "edges">, row: Pick<GraphNodeRow, "address" | "label" | "derived" | "value_human">, t: ShellT): RowMarks {
  const { sent, leftOut } = edgesOfRow(result, row.address);
  const weights = sent.map(e => e.weight).filter((w): w is number => typeof w === "number");
  const weight = weights.length ? weights.reduce((a, b) => a + b, 0) : null;
  // A derived node says its own value in the kernel's words ("19 tokenów (szacunek)"); a real message has the weight on the link only.
  const tokens = sent.length === 1 && row.derived && row.value_human ? row.value_human : weight !== null ? tokensText(weight, t) : null;
  const reasons = leftOut.map(e => e.metadata?.reason_human).filter((r): r is string => typeof r === "string" && r !== "" && !row.label.includes(r));
  return {
    derived: row.derived ? t("sent.derived") : null,
    sent: sent.length ? (tokens ? `${sent[0].label}: ${tokens}` : sent[0].label) : null,
    leftOut: leftOut.length ? leftOut[0].label : null,
    reason: reasons.length ? reasons.join("; ") : null,
    weight,
  };
}

/** The lines under the list for the perspective: where the view came from (an answer opened its request), what the request held (counts and the
 *  total, an estimate), once - not per row - that derived rows are a view of the record, and the kernel's note that a record is not proof. */
export function receiptLines(result: Pick<GraphNavigateResult, "children" | "receipt" | "handed_from">, t: ShellT): { id: string; text: string }[] {
  const receipt = result.receipt;
  if (!receipt) return [];
  const lines: { id: string; text: string }[] = [];
  if (receipt.present && result.handed_from) lines.push({ id: "sent-handed", text: t("sent.handed_from") });
  if (receipt.present && receipt.counts) {
    const total = receipt.receipt?.tokens_est;
    const base = t("sent.summary", { sent: String(receipt.counts.sent ?? 0), left_out: String(receipt.counts.left_out ?? 0) });
    lines.push({ id: "sent-summary", text: typeof total === "number" ? `${base} · ${t("sent.total", { tokens: tokensText(total, t) })}` : base });
  }
  const derived = result.children.filter(r => r.derived);
  if (derived.length) lines.push({ id: "sent-derived-note", text: derived.find(r => r.derived_note)?.derived_note ?? t("sent.derived_note") });
  if (receipt.present && receipt.note) lines.push({ id: "sent-note", text: receipt.note });
  return lines;
}

/** The sentence of an empty list: with a record that does not exist (before M1, a focus that is no message) the kernel's own sentence for it. */
export function emptyText(result: Pick<GraphNavigateResult, "receipt">, perspectivesOn: boolean, t: ShellT): string {
  if (result.receipt && !result.receipt.present) return result.receipt.text || t("perspective.empty");
  return t(perspectivesOn ? "perspective.empty" : "no_children");
}

/** The card of a derived node: the mark and the kernel's sentence (verbatim; the pack's sentence only when the kernel sent none). */
export function derivedCard(card: Pick<GraphNodeRow, "derived" | "derived_note">, t: ShellT): { mark: string; note: string } | null {
  if (!card.derived) return null;
  return { mark: t("sent.derived"), note: card.derived_note || t("sent.derived_note") };
}
