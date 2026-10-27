// W8 (OKNA §7.1, UI08): a block of an answer in a frame - its format, the code, and the three actions: Kopiuj (clipboard), Otwórz w oknie
// (a window of the kind `artifact`, through OPEN_ARTIFACT_EVENT to the shell), Zapisz (a file download in the browser). The words are
// the conversation view's pack (conversation_view.pack: copy, copied, artifact_open, artifact_save, artifact_frame).
import { useState } from "react";
import "./artifacts.css";
import { useShellText } from "../text";
import { OPEN_ARTIFACT_EVENT, fileNameOf, formatById, type Artifact } from "./artifacts";

/** „Zapisz”: the browser's download of a text file (the same path as the workflow session export: Blob + <a download>). */
export function downloadText(name: string, mime: string, text: string): void {
  const url = URL.createObjectURL(new Blob([text], { type: `${mime};charset=utf-8` }));
  const a = document.createElement("a"); a.href = url; a.download = name; a.rel = "noopener";
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function ArtifactActions({ artifact, open = true }: { artifact: Artifact; open?: boolean }) {
  const t = useShellText();
  const [copied, setCopied] = useState(false);
  const copy = () => { void navigator.clipboard?.writeText(artifact.content).then(() => setCopied(true), (e) => console.debug("artifact: not copied", e)); };
  return (
    <div className="artifact-actions" role="group">
      <button type="button" data-testid="artifact-copy" onClick={copy}>{copied ? t("copied") : t("copy")}</button>
      {open && <button type="button" data-testid="artifact-open" onClick={() => window.dispatchEvent(new CustomEvent(OPEN_ARTIFACT_EVENT, { detail: { artifact } }))}>{t("artifact_open")}</button>}
      <button type="button" data-testid="artifact-save" onClick={() => downloadText(fileNameOf(artifact), formatById(artifact.format)?.mime ?? "text/plain", artifact.content)}>{t("artifact_save")}</button>
    </div>
  );
}

export default function ArtifactFrame({ artifact }: { artifact: Artifact }) {
  const t = useShellText();
  const label = artifact.lang || artifact.format;
  return (
    <figure className="artifact-frame" data-testid="artifact-frame" data-format={artifact.format} data-index={artifact.index}>
      <figcaption className="artifact-head">
        <span className="artifact-format">{t("artifact_frame", { format: label })}</span>
        <ArtifactActions artifact={artifact} />
      </figcaption>
      <pre className="artifact-code"><code data-testid="artifact-code">{artifact.content}</code></pre>
    </figure>
  );
}
