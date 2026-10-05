/** Native onboarding/layer snapshots are translated here by the W10 adapter.
 * No persisted user data or policy defaults belong in this frontend contract. */
export type JsonValue = null | boolean | number | string | JsonValue[] | { [key: string]: JsonValue };
export type FieldStatus = "unknown" | "known" | "declined" | "never";
export type Provenance = "user_stated" | "model_inferred" | "form";
export type ReviewDecision = "confirmed" | "rejected";

export interface ScenarioField {
  id: string;
  label: string;
  question?: string;
  category: string;
  input?: "text" | "multiline" | "json" | "number" | "boolean" | "select";
  options?: { label: string; value: JsonValue }[];
}
export interface ScenarioSection {
  id: string;
  title: string;
  description?: string;
  fields: ScenarioField[];
}
export interface OnboardingScenario {
  id: string;
  title: string;
  method_ref: string;
  sections: ScenarioSection[];
  /** Exact editable native method document, retained by the host adapter. */
  source: JsonValue;
}
export interface ProfileField {
  status: FieldStatus;
  question_disposition?: "unknown" | "declined" | "never";
  value?: JsonValue;
  category: string;
  provenance?: Provenance | null;
  review?: string | null;
  source_refs?: string[];
}
export interface ProfileCandidate {
  id: string;
  field: string;
  value: JsonValue;
  provenance: Provenance;
  review: "pending" | "confirmed" | "rejected";
  section?: string;
  time?: string;
  source_refs?: string[];
}
export interface PrivacyRule {
  id: string;
  category: string;
  store: boolean;
  max_detail: JsonValue;
  max_sensitivity: JsonValue;
  providers: string[];
  infer: boolean;
  explicit_only: boolean;
  retention: JsonValue;
  [key: string]: JsonValue;
}
export interface HistoryRecord {
  id?: string;
  field?: string;
  op?: string;
  time?: string;
  [key: string]: JsonValue | undefined;
}
export interface EffectiveDefault {
  id: string;
  key: string;
  area: string;
  label?: string;
  value?: JsonValue;
  layer: string;
  reason: string;
  enabled: boolean;
  excluded: boolean;
  status?: string;
  area_mode?: "proposal" | "direct";
  history?: HistoryRecord[];
}
export interface OnboardingSnapshot {
  scenario: OnboardingScenario;
  fields: Record<string, ProfileField>;
  session: {
    status: string;
    section: string;
    sections: Record<string, string>;
    summary?: string;
  };
  candidates: Record<string, ProfileCandidate>;
  privacy: PrivacyRule[];
  history: HistoryRecord[];
  settings: { preference_mode: "ask" | "candidate" | "automatic" };
  defaults?: EffectiveDefault[];
  latest_reply?: ModelReply;
}

export type OnboardingAction =
  | { op: "answer"; field: string; value: JsonValue; provenance: "user_stated" | "form"; id: string; time: string; source_refs: string[] }
  | { op: "status"; field: string; status: "unknown" | "declined" | "never" }
  | { op: "review"; id: string; decision: ReviewDecision; value?: JsonValue }
  | { op: "pause" | "resume" }
  | { op: "skip" | "repeat"; section: string }
  | { op: "confirm_section"; section: string; decision: "confirmed" | "corrected" | "rejected" }
  | { op: "privacy"; rule: PrivacyRule }
  | { op: "delete"; field: string }
  | { op: "settings"; settings: Partial<OnboardingSnapshot["settings"]> };

export type LayerAction =
  | { op: "override"; key: string; value: JsonValue }
  | { op: "disable" | "exclude" | "reenable" | "clear_override" | "accept_proposal"; key: string }
  | { op: "set_area_mode"; area: string; mode: "proposal" | "direct" };

export interface ModelRequest {
  method_ref: JsonValue;
  prompt: JsonValue;
  section: string;
  questions: JsonValue;
  context: JsonValue;
  reply_schema: JsonValue;
  [key: string]: JsonValue;
}
export interface ModelReply {
  section: string;
  summary: string;
  questions: { field: string; text: string }[];
  candidates: { id: string; field: string; value: JsonValue; time?: string; source_refs?: string[] }[];
  provider?: string;
  request_token?: string;
  snapshot_revision?: number;
  graph_run_id?: string;
}
/** Model-facing payload is deliberately closed: host tokens and future native
 * metadata cannot accidentally reach the provider through an object spread. */
export interface ProviderModelRequest {
  prompt: JsonValue;
  section: string;
  questions: JsonValue;
  context: JsonValue;
  reply_schema: JsonValue;
  candidates?: JsonValue;
  policy?: JsonValue;
}
export interface RequestOptions { signal?: AbortSignal }
export interface OnboardingAdapter {
  getSnapshot(options?: RequestOptions): Promise<OnboardingSnapshot>;
  dispatch(action: OnboardingAction, options?: RequestOptions): Promise<OnboardingSnapshot>;
  /** MUST call native model_request(provider), which enforces privacy and field states. */
  modelRequest?(provider: string, options?: RequestOptions): Promise<ModelRequest>;
  /** Only this already-filtered request may reach the chosen provider. No raw field data. */
  completeModelRequest?(request: ProviderModelRequest, options: RequestOptions & { provider: string }): Promise<ModelReply>;
  ingestModelReply?(reply: ModelReply, options?: RequestOptions): Promise<OnboardingSnapshot>;
  /** Store/edit the scenario as a version of its graph method, never browser-only policy. */
  saveScenario?(source: JsonValue, options?: RequestOptions): Promise<OnboardingSnapshot>;
  dispatchLayer?(action: LayerAction, options?: RequestOptions): Promise<OnboardingSnapshot>;
}
