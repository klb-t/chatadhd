import { useEffect, useMemo, useRef, useState } from "react";
import { operationEvidence, type ArtifactRecord, type ArtifactResult, type MediaStatus, type OperationEvidence, type OperationsApi, type TranscriptionResult } from "../api/operations";
import type { GraphPacketStoreRequest, GraphPacketStoreResult } from "../api/graph-packets";
import type { ApplicationProfile } from "../profiles/runtime";
import { makeCapabilityGraphAcceptance } from "../profiles/capability-graph";
import "./operations-panel.css";
import { useShellText } from "../shell/text";

export interface OperationsPanelProps {
  operations?: OperationsApi;
  adapterEvidence?: readonly OperationEvidence[];
  profiles?: ApplicationProfile[];
  graphPacketStore?: (request: GraphPacketStoreRequest) => Promise<GraphPacketStoreResult>;
  onTranscript?: (text: string) => void | Promise<void>;
}
const message = (error: unknown) => error instanceof Error ? error.message : String(error);
const profileKey = (profile: ApplicationProfile) => JSON.stringify([profile.id, profile.profile_revision]);

export default function OperationsPanel({ operations, profiles = [], adapterEvidence = [], graphPacketStore, onTranscript }: OperationsPanelProps) {
  const t = useShellText();
  const [artifacts, setArtifacts] = useState<ArtifactRecord[]>([]);
  const [selected, setSelected] = useState<ArtifactResult | null>(null);
  const [media, setMedia] = useState<MediaStatus | null>(null);
  const [artifactError, setArtifactError] = useState("");
  const [mediaError, setMediaError] = useState("");
  const [transcriptionError, setTranscriptionError] = useState("");
  const [graphError, setGraphError] = useState("");
  const [loading, setLoading] = useState(false);
  const [reading, setReading] = useState(false);
  const [transcribing, setTranscribing] = useState(false);
  const [savingGraph, setSavingGraph] = useState(false);
  const [file, setFile] = useState<File | null>(null);
  const [provider, setProvider] = useState("");
  const [language, setLanguage] = useState("");
  const [kind, setKind] = useState("");
  const [run, setRun] = useState("");
  const [limit, setLimit] = useState("100");
  const [transcript, setTranscript] = useState<TranscriptionResult | null>(null);
  const [profileSelection, setProfileSelection] = useState("");
  const [graphResult, setGraphResult] = useState<GraphPacketStoreResult | null>(null);
  const contentIntent = useRef(0);
  const evidence = useMemo(() => operationEvidence(operations, media, profiles.flatMap(profile => profile.actions.map(action => action.operation)), adapterEvidence), [operations, media, profiles, adapterEvidence]);
  const selectedProfile = profiles.find(profile => profileKey(profile) === profileSelection) ?? profiles[0];

  useEffect(() => {
    let current = true;
    setMedia(null); setMediaError(""); setSelected(null); setArtifacts([]); setTranscript(null); contentIntent.current++;
    if (operations) operations.mediaStatus().then(status => { if (current) setMedia(status); }).catch(error => { if (current) setMediaError(message(error)); });
    return () => { current = false; };
  }, [operations]);

  async function listArtifacts() {
    if (!operations) return;
    setLoading(true); setArtifactError("");
    try {
      const parsed = Number(limit);
      if (!Number.isInteger(parsed) || parsed <= 0) throw new Error("Artifact page size must be a positive integer.");
      const result = await operations.listArtifacts({ limit: parsed, ...(kind ? { kind } : {}), ...(run ? { run_id: run } : {}) });
      setArtifacts(result);
    } catch (error) { setArtifactError(message(error)); }
    finally { setLoading(false); }
  }
  async function readArtifact(id: string) {
    if (!operations) return;
    const intent = ++contentIntent.current;
    setReading(true); setArtifactError(""); setSelected(null);
    try {
      const result = await operations.getArtifact(id, true);
      if (intent === contentIntent.current) setSelected(result);
    } catch (error) { if (intent === contentIntent.current) setArtifactError(message(error)); }
    finally { if (intent === contentIntent.current) setReading(false); }
  }
  async function transcribe() {
    if (!operations?.transcribe || !file) return;
    setTranscribing(true); setTranscriptionError(""); setTranscript(null);
    try { setTranscript(await operations.transcribe(file, { ...(provider ? { provider } : {}), ...(language ? { language } : {}) })); }
    catch (error) { setTranscriptionError(message(error)); }
    finally { setTranscribing(false); }
  }
  async function useTranscript() {
    if (!transcript || !onTranscript) return;
    try { await onTranscript(transcript.text); }
    catch (error) { setTranscriptionError(message(error)); }
  }
  async function saveEvidence() {
    if (!selectedProfile || !graphPacketStore) return;
    setSavingGraph(true); setGraphError(""); setGraphResult(null);
    try {
      const request = await makeCapabilityGraphAcceptance(selectedProfile, evidence, { text: JSON.stringify(selectedProfile),
        sourceRef: `derived:browser-capability-snapshot:${profileKey(selectedProfile)}`, actor: "loom.profile.user" });
      const stored = await graphPacketStore(request);
      const read = await graphPacketStore({ operation: "read", receipt_id: stored.receipt.id });
      if (!read.row_drift.matches) throw new Error("Capability rows have drifted; the retained receipt is still available.");
      setGraphResult(read);
    } catch (error) { setGraphError(message(error)); }
    finally { setSavingGraph(false); }
  }

  return <div className="operations-panel" data-testid="operations-panel">
    <section aria-label={t("lit_artifact_browser")}>
      <h3>{t("lit_artifacts")}</h3>
      {!operations && <p className="operations-status">This transport has no artifact adapter.</p>}
      <div className="operations-controls">
        <label>{t("lit_kind")}<input aria-label={t("lit_artifact_kind")} value={kind} onChange={event => setKind(event.target.value)} /></label>
        <label>{t("lit_run")}<input aria-label={t("lit_artifact_run")} value={run} onChange={event => setRun(event.target.value)} /></label>
        <label>{t("lit_page_size")}<input aria-label={t("lit_artifact_page_size")} type="number" step="1" value={limit} onChange={event => setLimit(event.target.value)} /></label>
        <button disabled={!operations || loading} onClick={() => void listArtifacts()}>{t("lit_list_artifacts")}</button>
      </div>
      <p>{t("lit_listing_reads_metadata_content_is_load")}</p>
      {artifactError && <p role="alert">{artifactError}</p>}
      {artifacts.length > 0 && <table><thead><tr><th>{t("lit_title_id")}</th><th>{t("lit_kind_mime")}</th><th>{t("lit_action")}</th></tr></thead><tbody>
        {artifacts.map(artifact => <tr key={artifact.id} data-testid="artifact-row"><td>{artifact.title || artifact.id}<br /><code>{artifact.id}</code></td>
          <td>{artifact.kind}<br />{artifact.mime}</td><td><button disabled={reading} onClick={() => void readArtifact(artifact.id)}>{t("lit_read_content")}</button></td></tr>)}
      </tbody></table>}
      {selected && <div className="operations-result" data-testid="artifact-content"><p>{selected.artifact.title || selected.artifact.id}</p>
        <pre>{selected.content ?? t("lit_this_artifact_has_no_content")}</pre><details><summary>{t("lit_artifact_metadata")}</summary><pre>{JSON.stringify(selected.artifact, null, 2)}</pre></details></div>}
    </section>
    <section aria-label={t("lit_file_transcription")}>
      <h3>{t("lit_voice_transcription")}</h3>
      <p>{t("lit_select_an_audio_file_and_request_trans")}</p>
      {mediaError && <p role="alert">Media status: {mediaError}</p>}
      {media && <p data-testid="asr-status">ASR: {media.asr?.configured ? `configured (${media.asr.available.join(", ")})` : "unavailable — no provider configured"}</p>}
      <label>{t("lit_audio_file")}<input aria-label={t("lit_transcription_audio_file")} type="file" accept="audio/*" onChange={event => { setFile(event.target.files?.[0] ?? null); setTranscript(null); }} /></label>
      <div className="operations-controls">
        <label>{t("lit_provider")}<input aria-label={t("lit_transcription_provider")} placeholder={t("lit_configured_default")} value={provider} onChange={event => setProvider(event.target.value)} /></label>
        <label>{t("lit_language")}<input aria-label={t("lit_transcription_language")} placeholder={t("lit_provider_default")} value={language} onChange={event => setLanguage(event.target.value)} /></label>
        <button disabled={!operations?.transcribe || !file || transcribing || media?.asr?.configured === false} onClick={() => void transcribe()}>{t("lit_transcribe_file")}</button>
      </div>
      {transcriptionError && <p role="alert">{transcriptionError}</p>}
      {transcript && <div className="operations-result" data-testid="transcription-result"><pre>{transcript.text}</pre>
        {onTranscript && <button onClick={() => void useTranscript()}>{t("lit_use_transcript")}</button>}
        <details><summary>{t("lit_transcription_result")}</summary><pre>{JSON.stringify(transcript, null, 2)}</pre></details></div>}
    </section>
    <section aria-label={t("lit_operation_capabilities")}>
      <h3>{t("lit_operation_capabilities")}</h3>
      <p>Statuses describe installed adapters. Matching an original application's workflow requires separate evidence.</p>
      {evidence.map(entry => <div className="operation-evidence" key={JSON.stringify([entry.operation, entry.capability])} data-testid="operation-evidence" data-operation={entry.operation} data-status={entry.status}>
        <code>{entry.operation}</code>: <span>{entry.status}</span><p>{entry.detail}</p><details><summary>{t("lit_evidence")}</summary><pre>{JSON.stringify(entry, null, 2)}</pre></details>
      </div>)}
      {selectedProfile && <div className="operations-controls"><label>Profile for capability snapshot<select aria-label="Capability snapshot profile" value={profileKey(selectedProfile)} onChange={event => setProfileSelection(event.target.value)}>
        {profiles.map(profile => <option key={profileKey(profile)} value={profileKey(profile)}>{profile.label} · r{profile.profile_revision}</option>)}
      </select></label><button disabled={!graphPacketStore || savingGraph} onClick={() => void saveEvidence()}>{t("lit_save_capability_snapshot_to_graph")}</button></div>}
      <p>{t("lit_the_snapshot_stores_individual_capabil")}</p>
      {graphError && <p role="alert">{graphError}</p>}
      {graphResult && <p data-testid="capability-graph-receipt">Native receipt: <code>{graphResult.receipt.id}</code> · run <code>{graphResult.receipt.run_id}</code></p>}
    </section>
  </div>;
}
