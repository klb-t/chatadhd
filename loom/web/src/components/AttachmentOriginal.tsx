import { useState } from "react";
import { api } from "../api";
import { dataLabel, useShellLanguage } from "../shell/text";
import words from "../content/attachment-download.words.json";

/** Download only a content-addressed original, never execute a document path. */
export default function AttachmentOriginal({ reference, name }: { reference?: string; name: string }) {
  const language = useShellLanguage();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<keyof typeof words | null>(null);
  const match = /^blob:([a-f0-9]{64})(?:\/|$)/.exec(reference ?? "");
  if (!match || !api.actions?.clientBlobFetch) return null;
  const hash = match[1];
  const text = (key: keyof typeof words) => dataLabel(words[key], language, words[key].en);
  const download = async () => {
    setBusy(true); setError(null);
    try {
      const blob = await api.actions!.clientBlobFetch!(hash);
      const digest = await crypto.subtle.digest("SHA-256", await blob.arrayBuffer());
      const actual = Array.from(new Uint8Array(digest), b => b.toString(16).padStart(2, "0")).join("");
      if (actual !== hash) { setError("mismatch"); return; }
      const url = URL.createObjectURL(blob);
      try {
        const anchor = document.createElement("a");
        anchor.href = url; anchor.download = name.split(/[\\/]/).pop() || hash; anchor.rel = "noopener";
        document.body.appendChild(anchor); anchor.click(); anchor.remove();
      } finally { setTimeout(() => URL.revokeObjectURL(url), 1000); }
    } catch { setError("unavailable"); }
    finally { setBusy(false); }
  };
  return <>
    <button type="button" data-testid="message-attachment-original" disabled={busy} onClick={() => void download()}>{text(busy ? "busy" : "download")}</button>
    {error && <span role="alert" data-testid="attachment-original-error">{text(error)}</span>}
  </>;
}
