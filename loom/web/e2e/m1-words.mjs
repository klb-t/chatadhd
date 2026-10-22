// M1-W1c: the words of the guard / estimate ratio as patterns, for the two M1 e2e scripts. Nothing here is a literal of the screen: every phrase is
// taken from presentation.context_receipt of conversation_view.pack (ratio.times_base, ratio.times_usual, ratio.low, ratio.unknown, guard.*,
// modify.ratio), so a changed word changes the expectation with it. A number in a phrase ({{n}}) is a number with at most one decimal.
export const esc = text => text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
const wordPattern = word => word.split("{{n}}").map(esc).join("\\d+(?:\\.\\d)?");
const around = (template, key, inner) => template.split(`{{${key}}}`).map(esc).join(inner);

/** Every form a ratio can be said in, as a regexp source: the number with the sign (unit "base" or "usual"), "much less than usual", "unknown - no base". */
export const ratioSource = (w, unit) => `(?:${[wordPattern(w[`ratio.times_${unit}`]), esc(w["ratio.low"]), esc(w["ratio.unknown"])].join("|")})`;

/** The guard line of the sheet in a state (ok | asked | unknown): the pack's sentence with the phrases that state may carry. */
export const guardLine = (w, state) => new RegExp("^" + around(w[`guard.${state}`], "ratio",
  state === "unknown" ? esc(w["ratio.unknown"]) : `(?:${wordPattern(w["ratio.times_base"])}|${esc(w["ratio.low"])})`) + "$");

/** The whole sentence after a change: "Gotowe: ... — {what}. ..." and the estimate sentence, whatever ratio the kernel measured. */
export const modifiedSentence = (w, what) => new RegExp("^" + esc(w["modify.applied"].replace("{{what}}", what)) + " " + around(w["modify.ratio"], "ratio", ratioSource(w, "usual")) + "$");
