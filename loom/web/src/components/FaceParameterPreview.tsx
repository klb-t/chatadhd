import { useId, useMemo } from "react";
import { faceParameterLibrary, type FaceParameterSet, type FaceSnapshot } from "../face-client/port";
import words from "../content/face-preview.words.json";

export interface FaceParameterPreviewProps {
  snapshot: FaceSnapshot;
  onSelect: (value: FaceParameterSet) => void;
  /** Explicit versioned vectors only. No default expression, text mapping or emotion inference. */
  library?: unknown;
  language?: "pl" | "en";
}

export default function FaceParameterPreview({ snapshot, onSelect, library = words.library, language = "pl" }: FaceParameterPreviewProps) {
  const id = useId();
  const entries = useMemo(() => {
    try { return faceParameterLibrary(library); } catch { return null; }
  }, [library]);
  const explicit = snapshot.explicit;
  const selected = entries?.findIndex(row => explicit !== null && row.id === explicit.id && row.version === explicit.version) ?? -1;
  return <details data-testid="face-parameter-preview">
    <summary>{words.title[language]}</summary>
    <p>{words.scope[language]}</p>
    <p role="status" data-testid="face-backend-status">{words.backend[snapshot.backend][language]} {words.run[snapshot.run][language]}</p>
    {entries === null && <p role="alert">{words.invalidLibrary[language]}</p>}
    {entries?.length === 0 && <p>{words.emptyLibrary[language]}</p>}
    <label htmlFor={id}>{words.choose[language]}</label>{" "}
    <select id={id} value={selected < 0 ? "" : String(selected)} disabled={!entries?.length || snapshot.contextId === null}
      onChange={event => {
        const entry = entries?.[Number(event.target.value)];
        if (event.target.value !== "" && entry) onSelect(entry);
      }}>
      <option value="" disabled>{words.noSelection[language]}</option>
      {entries?.map((entry, index) => <option key={JSON.stringify([entry.id, entry.version])} value={String(index)}>{entry.label[language]}</option>)}
    </select>
    {snapshot.explicit && <>
      <p>{words.explicitEvidence[language]}</p>
      <table data-testid="face-explicit-parameters">
        <caption>{snapshot.explicit.label[language]}</caption>
        <thead><tr><th scope="col">{words.coordinate[language]}</th><th scope="col">{words.value[language]}</th></tr></thead>
        <tbody>{snapshot.explicit.params.map((value, index) => <tr key={index}>
          <th scope="row">{words.coordinates[index][language]}</th><td>{value}</td>
        </tr>)}</tbody>
      </table>
    </>}
    {snapshot.display && <p data-testid="face-display-evidence">{words.backendReport[language]} {words.evidence[snapshot.display.evidence.evidenceClass][language]}</p>}
  </details>;
}
