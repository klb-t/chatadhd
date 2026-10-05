/** Optional operation adapters, independent of a particular application profile. */
export type OperationEvidenceStatus = "native" | "equivalent" | "limited" | "unavailable";
export interface OperationEvidence {
  operation: string;
  capability: string;
  status: OperationEvidenceStatus;
  detail: string;
  evidence: string[];
}
export interface ArtifactRecord extends Record<string, unknown> {
  id: string; kind: string; title: string; mime: string; task_id: string; blob_hash: string; created: string;
}
export interface ArtifactResult { artifact: ArtifactRecord; content?: string }
export interface ArtifactFilter { kind?: string; run_id?: string; limit?: number }
export interface MediaStatus extends Record<string, unknown> {
  asr: { available: string[]; configured: boolean };
  ocr?: { available: string[]; configured: boolean };
}
export interface TranscriptionOptions { provider?: string; language?: string }
export interface TranscriptionResult extends Record<string, unknown> { text: string }
export interface OperationsApi {
  readonly evidence: readonly OperationEvidence[];
  listArtifacts(filter?: ArtifactFilter): Promise<ArtifactRecord[]>;
  getArtifact(id: string, includeContent?: boolean): Promise<ArtifactResult>;
  mediaStatus(): Promise<MediaStatus>;
  transcribe?(file: File, options?: TranscriptionOptions): Promise<TranscriptionResult>;
}
export type OperationsRequest = <T>(method: string, path: string, body?: unknown) => Promise<T>;
/** Host owns authentication and transport errors, including multipart boundaries. */
export type OperationsUpload = <T>(path: string, body: FormData) => Promise<T>;

export function createOperationsApi(request: OperationsRequest, upload?: OperationsUpload): OperationsApi {
  const evidence: OperationEvidence[] = [
    { operation: "artifact.list", capability: "artifact.list", status: "native", detail: "Read native artifact metadata; original-service workflow equivalence is unverified.", evidence: ["loom_list_artifacts", "/api/artifacts"] },
    { operation: "artifact.read", capability: "artifact.read", status: "native", detail: "Read retained native content on explicit request.", evidence: ["loom_get_artifact", "/api/artifacts/:id"] },
    { operation: "voice.transcribe", capability: "voice.transcribe", status: upload ? "limited" : "unavailable", detail: upload ? "File transcription adapter installed; provider configuration must be checked. Live microphone and realtime voice are unavailable." : "This transport has no audio upload adapter.", evidence: upload ? ["loom_transcribe", "/api/media/transcribe"] : [] },
  ];
  return {
    evidence,
    listArtifacts: (filter = {}) => {
      const query = new URLSearchParams();
      for (const [key, value] of Object.entries(filter)) if (value !== undefined) query.set(key, String(value));
      return request("GET", `/api/artifacts${query.size ? `?${query}` : ""}`);
    },
    getArtifact: (id, includeContent = false) => request("GET", `/api/artifacts/${encodeURIComponent(id)}${includeContent ? "?content=1" : ""}`),
    mediaStatus: () => request("GET", "/api/media/status"),
    ...(upload ? { transcribe: (file: File, options: TranscriptionOptions = {}) => {
      const body = new FormData(); body.append("file", file, file.name); body.append("options", JSON.stringify(options));
      return upload<TranscriptionResult>("/api/media/transcribe", body);
    } } : {}),
  };
}

/** Runtime capability evidence is a snapshot; availability is not parity evidence. */
export function operationEvidence(api?: OperationsApi, media?: MediaStatus | null, declaredOperations: readonly string[] = [], additionalEvidence: readonly OperationEvidence[] = []): OperationEvidence[] {
  const entries = new Map<string, OperationEvidence>();
  const identity = (operation: string, capability: string) => JSON.stringify([operation, capability]);
  for (const entry of [...(api?.evidence ?? []), ...additionalEvidence]) entries.set(identity(entry.operation, entry.capability), { ...entry, evidence: [...entry.evidence] });
  for (const operation of ["artifact.list", "artifact.read", "artifact.edit", "project.execute", "browser.execute", "voice.transcribe", "voice.realtime", "workflow.task.execute", ...declaredOperations]) {
    if (![...entries.values()].some(entry => entry.operation === operation)) entries.set(identity(operation, operation), { operation, capability: operation, status: "unavailable", detail: "This registry snapshot has no execution adapter evidence for this operation. Other adapter registries are not inferred.", evidence: [] });
  }
  const voice = entries.get(identity("voice.transcribe", "voice.transcribe"));
  if (voice) {
    if (!api?.transcribe) { voice.status = "unavailable"; voice.detail = "This transport has no audio upload adapter."; }
    else if (media) {
      voice.status = media.asr?.configured ? "native" : "unavailable";
      voice.detail = media.asr?.configured ? "Native file transcription through a configured provider. Realtime voice and microphone capture are separate capabilities." : "No ASR provider is configured. File transcription cannot execute.";
      voice.evidence = [...voice.evidence, "loom_media_status"];
    }
  }
  const task = entries.get(identity("workflow.task.execute", "workflow.task.execute"));
  if (task?.status === "unavailable") task.detail = "The public task API can inspect/recover/cancel existing tasks; it cannot submit or checkpoint a generic workflow.";
  return [...entries.values()];
}
