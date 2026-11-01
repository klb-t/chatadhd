import { useEffect, useState } from "react";
import { api } from "../api";
import type { ShellLanguageChoice } from "../shell/language";
import { languageName } from "../shell/language-pack";
import { dataLabel } from "../shell/text";
import words from "../content/interface-language.words.json";

/** Writes only an explicit choice or the canonical AUTO value, after user input. */
export default function InterfaceLanguage({ choice, available, onChanged }: {
  choice: ShellLanguageChoice; available: string[]; onChanged?: () => void;
}) {
  const initial = choice.mode === "auto" ? "auto" : choice.language;
  const [value, setValue] = useState(initial), [busy, setBusy] = useState(false), [failed, setFailed] = useState(false);
  useEffect(() => { setValue(initial); }, [initial]);
  const w = (key: keyof typeof words) => dataLabel(words[key], choice.language, words[key].en);
  const save = async (next: string) => {
    if (busy || (next !== "auto" && !available.includes(next))) return;
    setBusy(true); setFailed(false);
    try { await api.setConfigKey("ui_language", next); setValue(next); onChanged?.(); }
    catch { setFailed(true); }
    finally { setBusy(false); }
  };
  return <section data-testid="interface-language" data-source={choice.source}>
    <label>{w("label")} <select data-testid="interface-language-choice" value={value} disabled={busy} onChange={event => void save(event.target.value)}>
      <option value="auto">{w("auto")}</option>
      {available.map(code => <option key={code} value={code}>{languageName(code)}</option>)}
    </select></label>
    <p>{w("about")}</p>
    <p data-testid="interface-language-source">{w(choice.source)}</p>
    {failed && <p role="alert">{w("failed")}</p>}
  </section>;
}
