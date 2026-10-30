import { useId, useState } from "react";
import { useResourcePresets } from "../context/resource-controls";
import { useShellText } from "../shell/text";

export default function ResourcePresetEditor() {
  const t = useShellText();
  const id = useId();
  const { presets, override, error, saveOverride } = useResourcePresets();
  const [draft, setDraft] = useState(override || "{}");
  const [saveError, setSaveError] = useState("");
  const [notice, setNotice] = useState("");
  const save = () => {
    setSaveError(""); setNotice("");
    try { saveOverride(draft); setNotice("Browser preset override saved. Slider suggestions update now; defaults apply when a form has no saved chosen parameters."); }
    catch (failure) { setSaveError(failure instanceof Error ? failure.message : String(failure)); }
  };
  return <details className="resource-preset-editor"><summary>Resource control presets</summary>
    <p>Override the data preset with JSON in this browser. Object fields merge; arrays and scalar values replace. Slider ranges are suggestions and do not constrain numeric input. Keep credentials outside these settings.</p>
    <details><summary>{t("lit_effective_resource_control_preset")}</summary><pre>{JSON.stringify(presets, null, 2)}</pre></details>
    <label htmlFor={id}>{t("lit_preset_override_json")}</label><textarea id={id} aria-label={t("lit_resource_preset_override_json")} rows={6} value={draft} onChange={event => setDraft(event.target.value)} />
    <button onClick={save}>{t("lit_save_browser_preset_override")}</button>
    {(saveError || error) && <p role="alert">{saveError || error}</p>}{notice && <p role="status">{notice}</p>}
  </details>;
}
