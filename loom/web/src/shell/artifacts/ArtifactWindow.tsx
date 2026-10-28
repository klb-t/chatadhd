// W8 (OKNA §7.1): the content of a window of the kind `artifact`. The block travels in the window's params (params.artifact), so the
// window comes back with the layout. HTML and SVG get a preview in an iframe sandbox (no scripts unless the settings field
// artifact_html_scripts is on - then allow-scripts, never allow-same-origin, with a non-blocking advisory); Markdown a sanitized preview;
// every format its source. The preview/source switch is local to the window.
import { useMemo } from "react";
import { useKeptState } from "../windows/useKeep";
import { marked } from "marked";
import DOMPurify from "dompurify";
import { useShellText } from "../text";
import { ArtifactActions } from "./ArtifactFrame";
import { artifactOf, formatById, sandboxOf } from "./artifacts";

export default function ArtifactWindow({ value, scripts, win }: { value: unknown; scripts: boolean; win?: { id: string; kind: string } }) {
  const t = useShellText();
  const a = artifactOf(value);
  const preview = a ? formatById(a.format)?.preview : undefined;
  const [source, setSource] = useKeptState(win, "view_mode", !preview);   // W10: the switch survives an unmount (state_keys)
  const md = useMemo(() => {
    if (!a || preview !== "markdown") return "";
    try { return DOMPurify.sanitize(marked.parse(a.content, { async: false }) as string, { FORBID_TAGS: ["style"] }); } catch { return ""; }
  }, [a?.content, preview]);   // eslint-disable-line react-hooks/exhaustive-deps
  if (!a) return <div className="artifact-window" data-testid="artifact-window"><p>{t("artifact_empty")}</p></div>;
  return (
    <div className="artifact-window" data-testid="artifact-window" data-format={a.format}>
      <div className="artifact-head">
        {preview && <div className="artifact-tabs" role="group">
          <button type="button" aria-pressed={!source} data-testid="artifact-show-preview" onClick={() => setSource(false)}>{t("artifact_preview")}</button>
          <button type="button" aria-pressed={source} data-testid="artifact-show-source" onClick={() => setSource(true)}>{t("artifact_source")}</button>
        </div>}
        <ArtifactActions artifact={a} open={false} />
      </div>
      {preview === "html_sandbox" && !source && <>
        <p className={scripts ? "artifact-advisory warn" : "artifact-advisory"} role={scripts ? "status" : undefined} data-testid={scripts ? "artifact-scripts-on" : "artifact-scripts-off"}>{t(scripts ? "artifact_scripts_on" : "artifact_scripts_off")}</p>
        <iframe className="artifact-preview" data-testid="artifact-iframe" title={t("artifact_preview")} sandbox={sandboxOf(scripts)} srcDoc={a.content} referrerPolicy="no-referrer" />
      </>}
      {preview === "markdown" && !source && <div className="artifact-preview body" data-testid="artifact-markdown" dangerouslySetInnerHTML={{ __html: md }} />}
      {source && <pre className="artifact-code"><code data-testid="artifact-window-code">{a.content}</code></pre>}
    </div>
  );
}
