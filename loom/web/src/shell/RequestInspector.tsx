// W-gaps (OKNA, W8 gap 1): the window kind `request_inspector` - the Expert view lifted out of the Workshop. It shows the last request this
// page sent (or prepared in the Expert view) in words, the raw JSON folded under „Szczegóły techniczne”. Openable from the chat menu
// („Pokaż zapytanie”) and by the sentence „pokaż zapytanie” / „show the request” (window.open, target request_inspector).
import { useEffect, useState } from "react";
import { lastRequest, onRequest, requestLines } from "./last-request";
import { useShellText } from "./text";

export default function RequestInspector() {
  const t = useShellText();
  const [sent, setSent] = useState(lastRequest);
  useEffect(() => onRequest(() => setSent(lastRequest())), []);
  if (!sent) return <section className="request-inspector" data-testid="request-inspector" data-state="empty"><h3>{t("ri_title")}</h3><p>{t("ri_empty")}</p></section>;
  return (
    <section className="request-inspector" data-testid="request-inspector" data-state={sent.prepared ? "prepared" : "sent"}>
      <h3>{t("ri_title")}</h3>
      <p>{t("ri_intro", { when: new Date(sent.at).toLocaleTimeString() })}</p>
      {sent.prepared && <p data-testid="request-inspector-prepared">{t("ri_preview")}</p>}
      <ul data-testid="request-inspector-words">{requestLines(sent.request).map((l) => <li key={l.id} data-line={l.id}>{t(l.id, l.vars)}</li>)}</ul>
      <details data-testid="request-inspector-details"><summary>{t("ri_details")}</summary>
        <pre data-testid="request-inspector-json">{JSON.stringify(sent.request, null, 2)}</pre>
      </details>
    </section>
  );
}
