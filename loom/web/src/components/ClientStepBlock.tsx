import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { PresentationFeature } from "../onboarding/presentation.mjs";
import type { ClientResult } from "../api/loom-api";
import { DeviceError, resultOf, runStep, startRecording, type BlobLoader, type BlobUploader, type ClientStepSpec, type DeviceHost, type Recording, type StepOutcome } from "../context/client-device";
import { clientStepTexts } from "../context/client-step";

/** A run paused on a step of this device (P13): what the kernel asks, the control that does it here, and the three ways out - do it
 *  (the answer goes to the kernel and the run continues), decline it on the device, cancel the run. A recording has two acts, start
 *  and stop; every other step is one. The device is only touched by a click of the person: browsers allow it on no other terms. */
export default function ClientStepBlock({ feature, step, runId, receiptId, host, load, upload, clientRef, onAnswer, onCancel, busy, failure, testId }: {
  feature: PresentationFeature | null; step: ClientStepSpec; runId: string; receiptId: string; host: DeviceHost; load: BlobLoader; upload: BlobUploader; clientRef: string;
  onAnswer: (answer: ClientResult) => Promise<void>; onCancel: () => void; busy?: boolean; failure?: string | null; testId: string;
}) {
  const texts = useMemo(() => clientStepTexts(feature, step), [feature, step]);
  const [recording, setRecording] = useState<Recording | null>(null);
  const [seconds, setSeconds] = useState(0);
  const [working, setWorking] = useState(false);
  const [problem, setProblem] = useState<string | null>(null);
  const recordingRef = useRef<Recording | null>(null);
  recordingRef.current = recording;
  const limit = step.with && typeof step.with.seconds === "number" ? step.with.seconds : 0;

  const send = useCallback(async (outcome: StepOutcome) => {
    setWorking(true);
    setProblem(null);
    try {
      await onAnswer(resultOf(runId, receiptId, outcome, clientRef) as ClientResult);
    } finally {
      setWorking(false);
    }
  }, [onAnswer, runId, receiptId, clientRef]);

  const stop = useCallback(async () => {
    const active = recordingRef.current;
    if (!active) return;
    setRecording(null);
    await send(await active.stop());
  }, [send]);

  // The seconds of a recording tick; a step with a length (the argument `seconds`) ends by itself.
  useEffect(() => {
    if (!recording) return undefined;
    const started = Date.now();
    const tick = window.setInterval(() => {
      const elapsed = Math.floor((Date.now() - started) / 1000);
      setSeconds(elapsed);
      if (limit > 0 && elapsed >= limit) { window.clearInterval(tick); void stop(); }
    }, 250);
    return () => window.clearInterval(tick);
  }, [recording, limit, stop]);
  // Leaving the page while recording drops the recording and closes the microphone.
  useEffect(() => () => { recordingRef.current?.abort(); }, []);

  const begin = async () => {
    setProblem(null);
    if (texts.kind === "record") {
      try {
        setSeconds(0);
        setRecording(await startRecording(host, upload));
      } catch (error) {
        const reason = error instanceof DeviceError ? error.message : String(error);
        setProblem(reason);
        if (error instanceof DeviceError && error.state !== "failed") await send({ state: error.state, error: { message: reason } });
      }
      return;
    }
    setWorking(true);
    const outcome = await runStep(host, step, load, upload);
    setWorking(false);
    if (outcome.state === "ok" || outcome.state === "declined" || outcome.state === "unavailable") await send(outcome);
    else setProblem(outcome.error ? outcome.error.message : outcome.state);
  };

  const decline = async () => {
    recordingRef.current?.abort();
    setRecording(null);
    await send({ state: "declined", error: { message: texts.decline } });
  };
  const disabled = Boolean(busy) || working;
  return (
    <div className="pill warn" role="group" data-testid={testId} aria-live="polite"
      data-run-id={runId} data-receipt-id={receiptId} data-kind="client" data-action={step.action ?? ""} data-recording={recording ? "true" : "false"}>
      <strong>{texts.title}</strong>
      <p data-testid={testId + "-prompt"}>{texts.prompt}</p>
      {texts.permissionHint && !recording && <p data-testid={testId + "-permission"}>{texts.permissionHint}</p>}
      {recording
        ? <>
            <p data-testid={testId + "-recording"}>{texts.recording(seconds)}</p>
            <button type="button" className="primary" disabled={disabled} onClick={() => void stop()} data-testid={testId + "-stop"}>{texts.stop}</button>
          </>
        : <button type="button" className="primary" disabled={disabled || texts.kind === "unsupported"} onClick={() => void begin()}
            data-testid={testId + "-start"}>{texts.start}</button>}
      <button type="button" disabled={disabled} onClick={() => void decline()} data-testid={testId + "-decline"}>{texts.decline}</button>
      <button type="button" disabled={disabled} onClick={onCancel} data-testid={testId + "-cancel"}>{texts.cancel}</button>
      {working && <p data-testid={testId + "-sending"}>{texts.sending}</p>}
      {problem && <p role="alert" data-testid={testId + "-problem"}>{texts.failed(problem)}</p>}
      {failure && <p role="alert" data-testid={testId + "-failure"}>{texts.sendFailed(failure)}</p>}
    </div>
  );
}
