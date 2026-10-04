import { useEffect, useMemo, useRef, useState } from "react";
import { operationEvidence, type ArtifactRecord, type ArtifactResult, type MediaStatus, type OperationEvidence, type OperationsApi, type TranscriptionResult } from "../api/operations";
import type { GraphPacketStoreRequest, GraphPacketStoreResult } from "../api/graph-packets";
import type { ApplicationProfile } from "../profiles/runtime";
import { makeCapabilityGraphAcceptance } from "../profiles/capability-graph";
import "./operations-panel.css";

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
    <section aria-label="Artifact browser">
      <h3>Artifacts</h3>
      {!operations && <p className="operations-status">This transport has no artifact adapter.</p>}
      <div className="operations-controls">
        <label>Kind<input aria-label="Artifact kind" value={kind} onChange={event => setKind(event.target.value)} /></label>
        <label>Run<input aria-label="Artifact run" value={run} onChange={event => setRun(event.target.value)} /></label>
        <label>Page size<input aria-label="Artifact page size" type="number" step="1" value={limit} onChange={event => setLimit(event.target.value)} /></label>
        <button disabled={!operations || loading} onClick={() => void listArtifacts()}>List artifacts</button>
      </div>
      <p>Listing reads metadata. Content is loaded only when you choose Read content.</p>
      {artifactError && <p role="alert">{artifactError}</p>}
      {artifacts.length > 0 && <table><thead><tr><th>Title / ID</th><th>Kind / MIME</th><th>Action</th></tr></thead><tbody>
        {artifacts.map(artifact => <tr key={artifact.id} data-testid="artifact-row"><td>{artifact.title || artifact.id}<br /><code>{artifact.id}</code></td>
          <td>{artifact.kind}<br />{artifact.mime}</td><td><button disabled={reading} onClick={() => void readArtifact(artifact.id)}>Read content</button></td></tr>)}
      </tbody></table>}
      {selected && <div className="operations-result" data-testid="artifact-content"><p>{selected.artifact.title || selected.artifact.id}</p>
        <pre>{selected.content ?? "This artifact has no content."}</pre><details><summary>Artifact metadata</summary><pre>{JSON.stringify(selected.artifact, null, 2)}</pre></details></div>}
    </section>
    <section aria-label="File transcription">
      <h3>Voice transcription</h3>
      <p>Select an audio file and request transcription through the configured native provider.</p>
      {mediaError && <p role="alert">Media status: {mediaError}</p>}
      {media && <p data-testid="asr-status">ASR: {media.asr?.configured ? `configured (${media.asr.available.join(", ")})` : "unavailable — no provider configured"}</p>}
      <label>Audio file<input aria-label="Transcription audio file" type="file" accept="audio/*" onChange={event => { setFile(event.target.files?.[0] ?? null); setTranscript(null); }} /></label>
      <div className="operations-controls">
        <label>Provider<input aria-label="Transcription provider" placeholder="Configured default" value={provider} onChange={event => setProvider(event.target.value)} /></label>
        <label>Language<input aria-label="Transcription language" placeholder="Provider default" value={language} onChange={event => setLanguage(event.target.value)} /></label>
        <button disabled={!operations?.transcribe || !file || transcribing || media?.asr?.configured === false} onClick={() => void transcribe()}>Transcribe file</button>
      </div>
      {transcriptionError && <p role="alert">{transcriptionError}</p>}
      {transcript && <div className="operations-result" data-testid="transcription-result"><pre>{transcript.text}</pre>
        {onTranscript && <button onClick={() => void useTranscript()}>Use transcript</button>}
        <details><summary>Transcription result</summary><pre>{JSON.stringify(transcript, null, 2)}</pre></details></div>}
    </section>
    <section aria-label="Operation capabilities">
      <h3>Operation capabilities</h3>
      <p>Statuses describe installed Loom adapters. Matching an original application's workflow requires separate evidence.</p>
      {evidence.map(entry => <div className="operation-evidence" key={JSON.stringify([entry.operation, entry.capability])} data-testid="operation-evidence" data-operation={entry.operation} data-status={entry.status}>
        <code>{entry.operation}</code>: <span>{entry.status}</span><p>{entry.detail}</p><details><summary>Evidence</summary><pre>{JSON.stringify(entry, null, 2)}</pre></details>
      </div>)}
      {selectedProfile && <div className="operations-controls"><label>Profile for capability snapshot<select aria-label="Capability snapshot profile" value={profileKey(selectedProfile)} onChange={event => setProfileSelection(event.target.value)}>
        {profiles.map(profile => <option key={profileKey(profile)} value={profileKey(profile)}>{profile.label} · r{profile.profile_revision}</option>)}
      </select></label><button disabled={!graphPacketStore || savingGraph} onClick={() => void saveEvidence()}>Save capability snapshot to graph</button></div>}
      <p>The snapshot stores individual capability entities linked by their native parent and profile identity. It adds no execution authority.</p>
      {graphError && <p role="alert">{graphError}</p>}
      {graphResult && <p data-testid="capability-graph-receipt">Native receipt: <code>{graphResult.receipt.id}</code> · run <code>{graphResult.receipt.run_id}</code></p>}
    </section>
  </div>;
}
