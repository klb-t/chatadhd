// The "Widok" bar (G3b) over a drawn view: the current view's encodings in words and the advisories as non-blocking chips ("Czy to celowe?" with the
// advisory's own choices, preset "Zostaw" / "Zostaw, nie pytaj więcej"). An answer goes to the catalogue action advisory.answer (POST /api/actions/run);
// the kernel remembers "don't ask" under the code + configuration fingerprint (policy and reversal in Settings). Nothing here blocks the view.
import { useMemo, useState } from "react";
import { api } from "../api";
import type { ViewModel } from "../api/view-model";
import { dataLabel, useShellText } from "../shell/text";
import { answerRequest, barState } from "../view-engine/view-bar";
import type { BarChip } from "../view-engine/view-bar";

/** compact: only the advisories (a view whose encodings are the page's own, e.g. the navigator map); nothing is drawn when there is no chip. */
export default function ViewBar({ model, language, compact = false }: { model: ViewModel; language: string; compact?: boolean }) {
  const t = useShellText();
  const [answered, setAnswered] = useState<Set<string>>(() => new Set());
  const [error, setError] = useState("");
  const state = useMemo(() => barState(model, answered), [model, answered]);
  const view = model.declaration.id ?? "";

  const answer = async (chip: BarChip, choice: string) => {
    setAnswered(prev => new Set(prev).add(chip.key));  // the chip goes at once; the view never waits for the answer
    setError("");
    try {
      const actions = api.actions;
      if (!actions) throw new Error("actions_unavailable");
      const run = await actions.run(answerRequest(chip, choice, view, dataLabel(chip.text, language, chip.code)));
      if (run.status !== "completed") throw new Error(run.error?.message ?? run.status);
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause));
    }
  };

  if (compact && state.chips.length === 0 && !error) return <span data-testid="view-bar" data-chips="0" data-hidden={String(state.hidden)} hidden />;
  const words = state.encodings.map(e => t("vbar_encoding", { channel: e.channel, measure: e.measure }));
  return (
    <div data-testid="view-bar" role="region" aria-label={t("vbar_title")} data-chips={String(state.chips.length)} data-hidden={String(state.hidden)}
      style={{ border: "1px solid var(--border)", borderRadius: "var(--radius)", padding: "0.4em 0.6em", margin: "0.4em 0", fontSize: "0.9em" }}>
      {!compact && <><strong>{t("vbar_title")}</strong>{" "}
      <span data-testid="view-bar-encodings">{words.length ? t("vbar_shows", { list: words.join(", ") }) : t("vbar_no_encodings")}</span></>}
      {!compact && state.composed.map(c => (
        <div key={c.channel} data-testid={`view-bar-composed-${c.channel}`} style={{ color: "var(--dim)" }}>
          {t("vbar_composed", { channel: c.channel, encodings: c.encodings.join(", "), how: c.combines.join(", "), count: String(c.elements) })}
        </div>
      ))}
      {state.chips.length > 0 && (
        <div data-testid="view-bar-advisories" style={{ display: "flex", flexDirection: "column", gap: "0.3em", marginTop: "0.3em" }}>
          {state.chips.map(chip => (
            <div key={chip.key} data-testid="view-advisory-chip" data-code={chip.code} data-fingerprint={chip.fingerprint} className="pill"
              style={{ border: "1px solid var(--warn, var(--border))", borderRadius: "var(--radius)", padding: "0.3em 0.5em" }}>
              <span data-testid="view-advisory-text">{dataLabel(chip.text, language, chip.code)}</span>{" "}
              <em data-testid="view-advisory-question">{t("vbar_question")}</em>{" "}
              {chip.choices.map(c => (
                <button key={c.id} type="button" data-testid={`view-advisory-choice-${c.id}`} onClick={() => void answer(chip, c.id)} style={{ marginLeft: "0.3em", minHeight: 44, minWidth: 44 }}>
                  {dataLabel(c.label, language, c.id)}
                </button>
              ))}
            </div>
          ))}
        </div>
      )}
      {error && <div data-testid="view-bar-error" style={{ color: "var(--err)" }}>{t("vbar_answer_failed", { error })}</div>}
    </div>
  );
}
