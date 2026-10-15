import { useRef, useState } from "react";
import { api } from "../api";
import { browserHost } from "../context/browser-host";
import { startRecording } from "../context/client-device";
import { useShellText } from "./text";

/** Dictation through the P13 device path: shown only when this client has the microphone API; the text goes into the composer. */
export default function MicButton({ onText }: { onText: (text: string) => void }) {
  const t = useShellText();
  const host = useRef(browserHost()).current;
  const [rec, setRec] = useState<Awaited<ReturnType<typeof startRecording>> | null>(null);
  const [problem, setProblem] = useState("");
  const kept = useRef<Blob | null>(null);
  if (!host.mediaDevices || !host.MediaRecorder || !api.operations?.transcribe) return null;
  const start = async () => {
    setProblem("");
    try { setRec(await startRecording(host, async (blob) => { kept.current = blob; return { hash: "" }; })); }
    catch (e) { setProblem(t("mic_failed", { reason: e instanceof Error ? e.message : String(e) })); }
  };
  const stop = async () => {
    const active = rec; setRec(null);
    if (!active) return;
    const out = await active.stop();
    if (out.state !== "ok" || !kept.current) { setProblem(t("mic_failed", { reason: out.error?.message ?? out.state })); return; }
    try { const r = await api.operations!.transcribe!(new File([kept.current], "dictation.webm", { type: kept.current.type })); onText(String((r as { text?: string }).text ?? "")); }
    catch (e) { setProblem(t("mic_failed", { reason: e instanceof Error ? e.message : String(e) })); }
  };
  return <>
    <button type="button" data-testid="shell-mic" className={rec ? "danger" : undefined} aria-pressed={rec !== null} aria-label={rec ? t("mic_stop") : t("mic_start")} title={problem || undefined} onClick={rec ? stop : start}>🎤</button>
  </>;
}
