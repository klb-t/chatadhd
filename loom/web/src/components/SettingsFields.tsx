import { useEffect, useState } from "react";
import { api } from "../api";
import { SettingsFieldsDoc, defaultLabel, editLabel } from "../settings/field-meta";

// Read-only reference table: default presence and edit rule of every settings key.
export default function SettingsFields() {
  const [doc, setDoc] = useState<SettingsFieldsDoc | null>(null);
  const [error, setError] = useState("");
  useEffect(() => {
    api.getSettingsFields().then(setDoc).catch((cause) => setError(cause instanceof Error ? cause.message : String(cause)));
  }, []);
  if (error) return <p role="alert" data-testid="settings-fields-error">Field metadata unavailable: {error}</p>;
  if (!doc) return null;
  return (
    <details data-testid="settings-fields">
      <summary className="section-title">Field reference (default and edit rule)</summary>
      <table>
        <thead><tr><th>Key</th><th>Default</th><th>Edit rule</th><th>Description</th></tr></thead>
        <tbody>
          {doc.fields.map((f) => (
            <tr key={`${f.scope}:${f.key}`} data-testid={`settings-field-${f.key}`}>
              <td>{f.key}{f.scope === "secret" ? " (secret)" : ""}</td>
              <td data-default={f.default}>{defaultLabel(f)}</td>
              <td data-edit={f.edit}>{editLabel(f, doc)}</td>
              <td>{f.description}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </details>
  );
}
