/** Browser transport boundary, not a controller. No text, renderer, optimizer or I/O here. */
export type FaceParams = readonly [number, number, number, number, number, number, number, number, number];
export interface FaceParameterSet {
  readonly id: string;
  readonly version: string;
  readonly label: Readonly<{ pl: string; en: string }>;
  readonly params: FaceParams;
}

/** References only: never embed observation quotes, conversation text or model output. */
export type FaceEvidence =
  | { readonly evidenceClass: "user"; readonly parameterSetRef: string; readonly libraryVersion: string }
  | { readonly evidenceClass: "observed"; readonly observationRef: string }
  | { readonly evidenceClass: "inferred"; readonly methodRef: string; readonly premiseRefs: readonly string[];
      readonly expectedPropertyRef: string; readonly confidence: number | null };

export interface FaceContextEvent {
  readonly kind: "context";
  readonly contextId: string;
  readonly version: number;
  readonly explicit9Params: FaceParams | null;
  readonly evidence: FaceEvidence | null;
}
export interface FaceDisplayEvent {
  readonly kind: "display";
  readonly contextId: string;
  readonly version: number;
  readonly explicit9Params: FaceParams;
  readonly evidence: FaceEvidence;
}

/** Inject an existing asynchronous host. post/close must enqueue and return; never render/join here.
 * The host owns cancellation, evaluation and rendering. This port owns one connection and one
 * replaceable microtask slot only. There is deliberately no default URL or network implementation. */
export interface FaceConnection {
  post(event: FaceContextEvent): void;
  close(): void;
}
export type FaceConnector = (onDisplay: (event: unknown) => void) => FaceConnection;
export interface FaceSnapshot {
  readonly contextId: string | null;
  readonly version: number;
  readonly explicit: FaceParameterSet | null;
  readonly backend: "disabled" | "connected" | "failed" | "closed";
  readonly run: "NOTRUN" | "pending" | "received" | "error";
  /** An attributed backend report, not independently verified rendering or model quality. */
  readonly display: FaceDisplayEvent | null;
}
export const EMPTY_FACE_SNAPSHOT: FaceSnapshot = Object.freeze({ contextId: null, version: 0,
  explicit: null, backend: "disabled", run: "NOTRUN", display: null });

function record(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw Error("face_object_required");
  return value as Record<string, unknown>;
}
function ref(value: unknown): string {
  if (typeof value !== "string" || !value.trim()) throw Error("face_reference_required");
  return value;
}
/** Domain/order pinned to async_controller.vector; no coercion, clamping or imputed coordinates. */
export function faceParams(value: unknown): FaceParams {
  if (!Array.isArray(value) || value.length !== 9) throw Error("face_nine_parameters_required");
  for (let i = 0; i < 9; i++) {
    const n: unknown = value[i];
    const low = i === 4 || i === 5 || i === 8 ? 0 : -1;
    if (typeof n !== "number" || !Number.isFinite(n) || n < low || n > 1) throw Error("face_parameter_domain");
  }
  return Object.freeze([...value]) as unknown as FaceParams;
}
function evidence(value: unknown): FaceEvidence {
  const row = record(value);
  switch (row.evidenceClass) {
    case "user": return Object.freeze({ evidenceClass: "user", parameterSetRef: ref(row.parameterSetRef), libraryVersion: ref(row.libraryVersion) });
    case "observed": return Object.freeze({ evidenceClass: "observed", observationRef: ref(row.observationRef) });
    case "inferred": {
      if (!Array.isArray(row.premiseRefs) || row.premiseRefs.length === 0) throw Error("face_premises_required");
      const confidence = row.confidence;
      if (confidence !== null && (typeof confidence !== "number" || !Number.isFinite(confidence) || confidence < 0 || confidence > 1)) throw Error("face_confidence_domain");
      return Object.freeze({ evidenceClass: "inferred", methodRef: ref(row.methodRef),
        premiseRefs: Object.freeze(row.premiseRefs.map(ref)), expectedPropertyRef: ref(row.expectedPropertyRef), confidence });
    }
    default: throw Error("face_evidence_class");
  }
}
export function faceParameterSet(value: unknown): FaceParameterSet {
  const row = record(value), label = record(row.label);
  return Object.freeze({ id: ref(row.id), version: ref(row.version),
    label: Object.freeze({ pl: ref(label.pl), en: ref(label.en) }), params: faceParams(row.params) });
}

export function faceParameterLibrary(value: unknown): readonly FaceParameterSet[] {
  if (!Array.isArray(value)) throw Error("face_library_array_required");
  const keys = new Set<string>();
  return Object.freeze(value.map(item => {
    const entry = faceParameterSet(item);
    const key = JSON.stringify([entry.id, entry.version]);
    if (keys.has(key)) throw Error("face_library_duplicate_version");
    keys.add(key);
    return entry;
  }));
}

export interface FaceClientPort {
  getSnapshot(): FaceSnapshot;
  subscribe(listener: () => void): () => void;
  /** Always advances the epoch, including A→B→A and changes without explicit parameters. */
  contextChanged(): void;
  select(value: FaceParameterSet): void;
  dispose(): void;
}

export function createFaceClientPort(connector?: FaceConnector): FaceClientPort {
  const session = crypto.randomUUID();
  let state = EMPTY_FACE_SNAPSHOT;
  let closed = false;
  let connection: FaceConnection | undefined;
  let queued: FaceContextEvent | null = null;
  let scheduled = false;
  let postedVersion: number | null = null;
  const listeners = new Set<() => void>();
  const update = (next: FaceSnapshot) => {
    state = Object.freeze(next);
    for (const listener of [...listeners]) listener();
  };
  const fail = () => {
    postedVersion = null;
    update({ ...state, backend: "failed", run: "error", display: null });
  };
  const nextVersion = () => {
    if (!Number.isSafeInteger(state.version + 1)) throw Error("face_epoch_exhausted");
    return state.version + 1;
  };
  const enqueue = () => {
    if (!connection || state.backend !== "connected") return;
    queued = Object.freeze({ kind: "context", contextId: state.contextId!, version: state.version,
      explicit9Params: state.explicit?.params ?? null,
      evidence: state.explicit ? Object.freeze({ evidenceClass: "user", parameterSetRef: state.explicit.id,
        libraryVersion: state.explicit.version }) : null });
    if (scheduled) return;
    scheduled = true;
    queueMicrotask(() => {
      scheduled = false;
      const event = queued;
      queued = null;
      if (closed || !event || !connection || state.backend !== "connected") return;
      postedVersion = event.version;
      try { connection.post(event); } catch { fail(); }
    });
  };
  if (connector) {
    try {
      connection = connector((raw: unknown) => {
        if (closed || state.backend !== "connected" || !state.explicit) return;
        try {
          const row = record(raw);
          if (row.kind !== "display" || row.version !== state.version || row.version !== postedVersion || row.contextId !== state.contextId) return;
          const display: FaceDisplayEvent = Object.freeze({ kind: "display", contextId: state.contextId!, version: state.version,
            explicit9Params: faceParams(row.explicit9Params), evidence: evidence(row.evidence) });
          update({ ...state, run: "received", display });
        } catch { fail(); }
      });
      if (!connection || typeof connection.post !== "function" || typeof connection.close !== "function") throw Error("face_connection_contract");
      update({ ...state, backend: "connected" });
    } catch { fail(); }
  }
  return {
    getSnapshot: () => state,
    subscribe(listener) { if (closed) return () => {}; listeners.add(listener); return () => { listeners.delete(listener); }; },
    contextChanged() {
      if (closed) return;
      const version = nextVersion();
      postedVersion = null;
      update({ ...state, contextId: `${session}-${version}`, version, explicit: null, display: null, run: "NOTRUN" });
      enqueue();
    },
    select(value) {
      if (closed || state.contextId === null) return;
      const explicit = faceParameterSet(value);
      postedVersion = null;
      update({ ...state, version: nextVersion(), explicit, display: null,
        run: state.backend === "connected" ? "pending" : "NOTRUN" });
      enqueue();
    },
    dispose() {
      if (closed) return;
      closed = true;
      queued = null;
      postedVersion = null;
      try { connection?.close(); } catch { /* Do not propagate a host payload into the chat. */ }
      connection = undefined;
      state = Object.freeze({ ...state, backend: "closed", run: "NOTRUN", explicit: null, display: null });
      listeners.clear();
    },
  };
}
