import { message, PresentationError, resolvePresentation } from "./presentation.mjs";
import type { Presentation } from "./presentation.mjs";
import type { JsonValue, LayerAction, ModelReply, OnboardingAction, OnboardingAdapter, OnboardingSnapshot, ProviderModelRequest } from "./types";

/** A transport that ignores AbortSignal must not keep the view busy forever. */
function abortable<T>(operation: Promise<T>, signal: AbortSignal): Promise<T> {
  return new Promise((resolve, reject) => {
    const abort = () => reject(new DOMException(message(resolvePresentation(), "error.interrupted"), "AbortError"));
    signal.addEventListener("abort", abort, { once: true });
    operation.then(resolve, reject).finally(() => signal.removeEventListener("abort", abort));
    if (signal.aborted) abort();
  });
}

export interface ControllerState {
  snapshot: OnboardingSnapshot | null;
  /** Validated reply for the current open view; never persisted or cached. */
  reply: ModelReply | null;
  busy: boolean;
  error: string | null;
  errorCause?: unknown;
}

/** Serial native mutations; invalidate an in-flight model reply whenever policy
 * or answers change. A host should additionally enforce native revision checks. */
export class OnboardingController {
  private current: ControllerState = { snapshot: null, reply: null, busy: false, error: null };
  private listeners = new Set<(state: ControllerState) => void>();
  private queue: Promise<unknown> = Promise.resolve();
  private pending = 0;
  private revision = 0;
  private stopped = false;
  private modelAbort: AbortController | null = null;
  private readonly adapter: OnboardingAdapter;
  constructor(adapter: OnboardingAdapter) { this.adapter = adapter; }
  state(): ControllerState { return this.current; }
  subscribe(listener: (state: ControllerState) => void): () => void {
    this.listeners.add(listener);
    listener(this.current);
    return () => { this.listeners.delete(listener); };
  }
  private publish(patch: Partial<ControllerState>): void {
    if (this.stopped) return;
    this.current = { ...this.current, ...patch };
    for (const listener of this.listeners) listener(this.current);
  }
  private invalidateModel(): void {
    this.revision += 1;
    this.modelAbort?.abort();
    this.modelAbort = null;
    this.publish({ reply: null });
  }
  private async track<T>(operation: () => Promise<T>): Promise<T | undefined> {
    if (this.stopped) return undefined;
    this.pending += 1;
    this.publish({ busy: true, error: null, errorCause: null });
    try { return await operation(); }
    catch (error) {
      if (!(error instanceof Error && error.name === "AbortError")) {
        this.publish({ error: error instanceof Error ? error.message : String(error), errorCause: error });
      }
      return undefined;
    } finally {
      this.pending -= 1;
      this.publish({ busy: this.pending > 0 });
    }
  }
  private serial<T>(operation: () => Promise<T>): Promise<T> {
    const run = this.queue.then(() => {
      if (this.stopped) throw new DOMException(message(resolvePresentation(), "error.closed"), "AbortError");
      return operation();
    });
    this.queue = run.catch(() => undefined);
    return run;
  }
  private install(snapshot: OnboardingSnapshot): OnboardingSnapshot {
    this.publish({ snapshot });
    return snapshot;
  }
  load(): Promise<OnboardingSnapshot | undefined> {
    this.invalidateModel();
    return this.track(() => this.serial(async () => this.install(await this.adapter.getSnapshot())));
  }
  dispatch(action: OnboardingAction): Promise<OnboardingSnapshot | undefined> {
    this.invalidateModel();
    return this.track(() => this.serial(async () => this.install(await this.adapter.dispatch(action))));
  }
  dispatchLayer(action: LayerAction): Promise<OnboardingSnapshot | undefined> {
    this.invalidateModel();
    return this.track(() => this.serial(async () => {
      if (!this.adapter.dispatchLayer) throw new PresentationError("error.layers_unavailable");
      return this.install(await this.adapter.dispatchLayer(action));
    }));
  }
  saveScenario(source: JsonValue): Promise<OnboardingSnapshot | undefined> {
    this.invalidateModel();
    return this.track(() => this.serial(async () => {
      if (!this.adapter.saveScenario) throw new PresentationError("error.scenario_unavailable");
      return this.install(await this.adapter.saveScenario(source));
    }));
  }
  requestModel(provider: string): Promise<OnboardingSnapshot | undefined> {
    this.invalidateModel();
    const revision = this.revision;
    const cancellation = new AbortController();
    this.modelAbort = cancellation;
    return this.track(async () => {
      const { modelRequest, completeModelRequest, ingestModelReply } = this.adapter;
      if (!modelRequest || !completeModelRequest || !ingestModelReply) {
        throw new PresentationError("error.model_unavailable");
      }
      const options = { signal: cancellation.signal };
      const request = await this.serial(() => modelRequest.call(this.adapter, provider, options));
      if (this.stopped || revision !== this.revision || cancellation.signal.aborted) return undefined;
      // Control metadata remains local. The model must never manufacture a CAS
      // token/provider binding, or receive a fingerprint of hidden profile data.
      const { request_token: token, snapshot_revision: storeRevision, graph_run_id: graphRun } = request;
      const providerRequest: ProviderModelRequest = { prompt: request.prompt, section: request.section,
        questions: request.questions, context: request.context, reply_schema: request.reply_schema,
        ...(request.candidates !== undefined ? { candidates: request.candidates } : {}),
        ...(request.policy !== undefined ? { policy: request.policy } : {}) };
      const modelReply: ModelReply = await abortable(completeModelRequest.call(this.adapter, providerRequest, { ...options, provider }), cancellation.signal);
      const reply: ModelReply = { ...modelReply, provider,
        ...(typeof token === "string" ? { request_token: token } : {}),
        ...(typeof storeRevision === "number" ? { snapshot_revision: storeRevision } : {}),
        ...(typeof graphRun === "string" ? { graph_run_id: graphRun } : {}) };
      return this.serial(async () => {
        if (this.stopped || revision !== this.revision || cancellation.signal.aborted) return undefined;
        const snapshot = await ingestModelReply.call(this.adapter, reply, options);
        this.publish({ snapshot, reply: revision === this.revision && !cancellation.signal.aborted ?
          { section: reply.section, summary: reply.summary, questions: reply.questions, candidates: reply.candidates } : null });
        return snapshot;
      });
    });
  }
  dispose(): void {
    this.stopped = true;
    this.invalidateModel();
    this.listeners.clear();
  }
}

export function mayAsk(snapshot: OnboardingSnapshot, field: string): boolean {
  const profile = snapshot.fields[field];
  const disposition = profile?.question_disposition;
  if (disposition === "declined" || disposition === "never") return false;
  const status = profile?.status;
  return status === "unknown" || status === "known";
}

export function draftValue(value: JsonValue | undefined, kind?: string, presentation: Presentation = resolvePresentation()): string {
  if (value === undefined) return "";
  if (kind === "json" || typeof value !== "string") return JSON.stringify(value, null, presentation.defaults.json_indent);
  return value;
}

export function parseDraft(value: string, kind?: string): JsonValue {
  if (kind === "json" || kind === "number" || kind === "boolean" || kind === "select") {
    const parsed: unknown = JSON.parse(value);
    if (kind === "number" && typeof parsed !== "number") throw new PresentationError("error.number");
    if (kind === "boolean" && typeof parsed !== "boolean") throw new PresentationError("error.boolean");
    return parsed as JsonValue;
  }
  return value;
}
