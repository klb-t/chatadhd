import { useId, useState } from "react";
import { useResourcePresets } from "../context/resource-controls";

export default function ResourcePresetEditor() {
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
    <details><summary>Effective resource control preset</summary><pre>{JSON.stringify(presets, null, 2)}</pre></details>
    <label htmlFor={id}>Preset override JSON</label><textarea id={id} aria-label="Resource preset override JSON" rows={6} value={draft} onChange={event => setDraft(event.target.value)} />
    <button onClick={save}>Save browser preset override</button>
    {(saveError || error) && <p role="alert">{saveError || error}</p>}{notice && <p role="status">{notice}</p>}
  </details>;
}
