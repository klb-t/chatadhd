// M1-W1c / M1-K1c1: the words of the guard / estimate ratio as patterns, for the two M1 e2e scripts. Nothing here is a literal of the screen: every phrase is
// taken from presentation.context_receipt of conversation_view.pack (ratio.times, ratio.low, ratio.unknown, ratio.with_base, base.*, for.*, guard.*,
// modify.ratio), so a changed word changes the expectation with it. A number in a phrase ({{n}}) is a number with at most one decimal.
export const esc = text => text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
const num = word => word.split("{{n}}").map(esc).join("\\d+(?:\\.\\d)?");
/** A template with {{key}} holes: the text around them escaped, the holes replaced by the given regexp sources. */
export const fill = (template, map) => template.split(/(\{\{\w+\}\})/).map(part => { const m = part.match(/^\{\{(\w+)\}\}$/); return m ? map[m[1]] : esc(part); }).join("");

/** What the base may be called: the average of the last N requests, a starting value, a declared value. */
export const baseSource = w => `(?:${["base.measured", "base.floor", "base.declared"].map(k => num(w[k])).join("|")})`;
/** The number with the sign or "much less than the usual request" (one phrase for the estimate and the guard), then with the base named next to it. */
export const sayingSource = w => `(?:${num(w["ratio.times"])}|${esc(w["ratio.low"])})`;
export const withBaseSource = w => fill(w["ratio.with_base"], { ratio: sayingSource(w), base: baseSource(w) });
/** Every form a ratio can be said in, as a regexp source: a number or "much less" with its base, or "unknown - no base". */
export const ratioSource = w => `(?:${withBaseSource(w)}|${esc(w["ratio.unknown"])})`;
export const forSource = w => `(?:${esc(w["for.empty"])}|${esc(w["for.text"])})`;

/** The guard line of the sheet in a state (ok | asked | unknown): the pack's sentence with the phrases that state may carry. */
export const guardLine = (w, state) => new RegExp("^" + fill(w[`guard.${state}`], { ratio: state === "unknown" ? esc(w["ratio.unknown"]) : withBaseSource(w) }) + "$");

/** The whole sentence after a change: "Gotowe: ... — {what}. ..." and the estimate sentence, whatever ratio the kernel measured. */
export const modifiedSentence = (w, what) => new RegExp("^" + esc(w["modify.applied"].replace("{{what}}", what)) + " " + fill(w["modify.ratio"], { for: forSource(w), ratio: ratioSource(w) }) + "$");

/** M1-K1c4: why an item was sent, as the pack says it: the word of the fixed ref (why.<ref>), else of the source kind (why.<kind>, ":" read as "."), else the
 *  kernel's own sentence (what the record keeps). The facts are the kernel's answer (a derived row's `receipt`, an item of the receipt). */
export const whyWord = (w, facts, kernelSentence) => {
  for (const key of [facts?.ref && `why.${facts.ref}`, facts?.source_kind && `why.${String(facts.source_kind).replace(":", ".")}`])
    if (key && typeof w[key] === "string") return w[key];
  return kernelSentence ?? "";
};
