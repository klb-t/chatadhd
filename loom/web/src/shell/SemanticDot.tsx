import { useEffect, useState } from "react";
import { api } from "../api";
import { useShellText } from "./text";

export function semanticTone(status: { pending?: number | null; executing?: number | null; failed?: number | null; paused?: boolean } | null): "ok" | "busy" | "warn" | "unknown" {
  if (!status) return "unknown";
  if (status.paused || (status.failed ?? 0) > 0) return "warn";
  if ((status.pending ?? 0) > 0 || (status.executing ?? 0) > 0) return "busy";
  return "ok";
}

/** The status of the background analysis as a dot; the diagnostics are a menu entry, never the header. */
export default function SemanticDot() {
  const t = useShellText();
  const [tone, setTone] = useState<ReturnType<typeof semanticTone>>("unknown");
  useEffect(() => {
    let live = true;
    const read = () => api.getSemanticStatus().then(s => { if (live) setTone(semanticTone(s)); }, () => { if (live) setTone("warn"); });
    read(); const timer = setInterval(read, 4000);
    return () => { live = false; clearInterval(timer); };
  }, []);
  return <span className={`shell-dot ${tone}`} role="img" data-testid="shell-status-dot" data-tone={tone} aria-label={t("status_" + (tone === "unknown" ? "unknown" : tone === "ok" ? "ok" : tone === "busy" ? "busy" : "warn"))} />;
}
