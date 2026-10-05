import { nativeDispatchAction, normalizeNativeSnapshot } from "../onboarding/native-snapshot";
import type { JsonValue, OnboardingAdapter, OnboardingSnapshot, RequestOptions } from "../onboarding/types";

export interface OnboardingHostApi {
  onboarding?(command: Record<string, unknown>, options?: RequestOptions): Promise<Record<string, unknown>>;
}
export interface IdentityStorage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
  removeItem(key: string): void;
}
export const USER_PROFILE_IDENTITY_KEY = "loom.user-profile.identity.v1";
export interface NativeProfileState {
  revision: string | null;
  reloadRequired: boolean;
  outcomeUnknown: boolean;
  error: string | null;
  active: number;
}
export interface UserProfileHostState {
  userId: string | null;
  storageError: string | null;
  viewEpoch: number;
  session: NativeProfileState | null;
}
export interface UserProfileHostOptions {
  storage?: IdentityStorage | null;
  onActivity?: (delta: number) => void;
}

function object(value: unknown, label: string): Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error(`${label} must be an object.`);
  return value as Record<string, unknown>;
}
function copy<T>(value: T): T {
  return JSON.parse(JSON.stringify(value, (_key, item: unknown) => {
    if (typeof item === "number" && !Number.isFinite(item)) throw new Error("Native profile data requires finite JSON numbers.");
    if (["bigint", "function", "symbol"].includes(typeof item)) throw new Error("Native profile data must be JSON.");
    return item;
  })) as T;
}
function revision(value: unknown): string {
  if (typeof value !== "string" || !/^(0|[1-9][0-9]*)$/.test(value) || BigInt(value) > 9223372036854775807n) {
    throw new Error("Native profile revision must be an exact nonnegative int64 decimal string.");
  }
  return value;
}
function failure(message: string, status?: number): Error & { status?: number } {
  return Object.assign(new Error(message), status === undefined ? {} : { status });
}
function errorText(error: unknown): string { return error instanceof Error ? error.message : String(error); }
function status(error: unknown): number | undefined {
  return error && typeof error === "object" && "status" in error && typeof error.status === "number" ? error.status : undefined;
}
function aborted(signal?: AbortSignal): void {
  if (signal?.aborted) throw new DOMException("Profile operation was interrupted before dispatch.", "AbortError");
}
function string(value: unknown, label: string): void {
  if (typeof value !== "string") throw new Error(`${label} must be a string.`);
}
/** Check the representation required by the supplied views, not native policy or pack rules. */
function checkedProjection(snapshot: OnboardingSnapshot): OnboardingSnapshot {
  for (const field of Object.values(snapshot.fields)) {
    const row = object(field, "Native profile field");
    string(row.status, "Native profile field status"); string(row.category, "Native profile field category");
  }
  for (const candidate of Object.values(snapshot.candidates)) {
    const row = object(candidate, "Native profile candidate");
    for (const key of ["id", "field", "provenance", "review"]) string(row[key], `Native profile candidate ${key}`);
    if (!("value" in row)) throw new Error("Native profile candidate is missing its value.");
  }
  for (const rule of snapshot.privacy) {
    const row = object(rule, "Native profile privacy rule");
    string(row.id, "Native profile rule ID"); string(row.category, "Native profile rule category");
    for (const key of ["store", "infer", "explicit_only"]) {
      if (typeof row[key] !== "boolean") throw new Error(`Native profile rule ${key} must be a boolean.`);
    }
    if (!Array.isArray(row.providers) || row.providers.some(provider => typeof provider !== "string")) {
      throw new Error("Native profile rule providers must be an array of strings.");
    }
    for (const key of ["max_detail", "max_sensitivity", "retention"]) {
      if (!(key in row)) throw new Error(`Native profile rule is missing ${key}.`);
    }
  }
  for (const section of snapshot.scenario.sections) for (const field of section.fields) {
    if (field.options !== undefined && (!Array.isArray(field.options) || field.options.some(option =>
      !option || typeof option !== "object" || typeof option.label !== "string" || !("value" in option)))) {
      throw new Error("Native scenario options must contain labels and JSON values.");
    }
  }
  if (snapshot.latest_reply !== undefined) {
    const reply = object(snapshot.latest_reply, "Native saved model reply");
    string(reply.section, "Native saved reply section"); string(reply.summary, "Native saved reply summary");
    if (!Array.isArray(reply.questions) || !Array.isArray(reply.candidates)) throw new Error("Native saved reply needs questions and candidates arrays.");
    for (const question of reply.questions) {
      const row = object(question, "Native saved reply question"); string(row.field, "Native question field"); string(row.text, "Native question text");
    }
  }
  if (!["ask", "candidate", "automatic"].includes(snapshot.settings.preference_mode)) throw new Error("Native preference mode is unsupported by this view.");
  return snapshot;
}

/** One native identity and one serial queue. No profile data is persisted by this adapter. */
export class NativeProfileSession {
  private raw: Record<string, unknown> | null = null;
  private queue: Promise<unknown> = Promise.resolve();
  private opened = false;
  private closed = false;
  private current: NativeProfileState = { revision: null, reloadRequired: false, outcomeUnknown: false, error: null, active: 0 };
  readonly adapter: OnboardingAdapter;

  constructor(readonly userId: string, private readonly api: OnboardingHostApi,
    private readonly notify: (state: NativeProfileState) => void = () => undefined,
    private readonly activity?: (delta: number) => void) {
    if (!userId.trim()) throw new Error("Choose a nonempty native profile ID.");
    if (!api.onboarding) throw new Error("This host has no native onboarding adapter.");
    this.adapter = {
      getSnapshot: options => this.load(options),
      dispatch: (action, options) => this.mutate("apply", {
        action: nativeDispatchAction(copy(action), { id: crypto.randomUUID(), time: new Date().toISOString() }),
      }, options),
      dispatchLayer: (action, options) => this.mutate("apply", {
        action: nativeDispatchAction(copy(action), { id: crypto.randomUUID(), time: new Date().toISOString() }),
      }, options),
      saveScenario: (scenario, options) => {
        if (!this.raw) return Promise.reject(failure("Reload the native profile before editing its method."));
        return this.mutate("update_pack", { pack: copy(this.raw.pack), scenario: copy(scenario) }, options);
      },
    };
  }
  state(): NativeProfileState { return this.current; }
  close(): void { this.closed = true; this.raw = null; }
  private publish(patch: Partial<NativeProfileState>): void {
    if (this.closed) return;
    this.current = { ...this.current, ...patch };
    this.notify(this.current);
  }
  private serial<T>(operation: () => Promise<T>): Promise<T> {
    const run = this.queue.then(() => {
      if (this.closed) throw new DOMException("The selected native profile changed.", "AbortError");
      return operation();
    });
    this.queue = run.catch(() => undefined);
    return run;
  }
  private async request(command: Record<string, unknown>, options?: RequestOptions): Promise<Record<string, unknown>> {
    aborted(options?.signal);
    this.activity?.(1);
    this.publish({ active: this.current.active + 1 });
    try { return await this.api.onboarding!(command, options); }
    finally {
      this.activity?.(-1);
      this.publish({ active: this.current.active - 1 });
    }
  }
  private install(response: Record<string, unknown>, expected?: string): OnboardingSnapshot {
    const exactRevision = revision(response.revision);
    const raw = object(response.snapshot, "Native profile snapshot");
    if (raw.schema !== "loom.onboarding_store/1" || raw.user_id !== this.userId) {
      throw new Error("Native profile snapshot has a different schema or identity.");
    }
    object(raw.pack, "Native profile pack");
    if (typeof raw.revision !== "number" || !Number.isInteger(raw.revision) || raw.revision < 0) {
      throw new Error("Native profile snapshot is missing its nonnegative revision.");
    }
    if (Number.isSafeInteger(raw.revision) && String(raw.revision) !== exactRevision) {
      throw new Error("Native profile revision envelope disagrees with the snapshot.");
    }
    if (expected !== undefined && BigInt(exactRevision) !== BigInt(expected) + 1n) {
      throw new Error("Native profile write returned an unexpected revision.");
    }
    const captured = copy(raw);
    const normalized = checkedProjection(normalizeNativeSnapshot(captured as JsonValue));
    if (this.closed) throw new DOMException("The selected native profile changed.", "AbortError");
    this.raw = captured;
    this.opened = true;
    this.publish({ revision: exactRevision, reloadRequired: false, error: null });
    return normalized;
  }
  private load(options?: RequestOptions): Promise<OnboardingSnapshot> {
    return this.serial(async () => {
      const operation = this.opened ? "read" : "open";
      let dispatched = false;
      try {
        aborted(options?.signal);
        dispatched = true;
        const response = await this.request({ operation, user_id: this.userId }, options);
        return this.install(response);
      } catch (error) {
        const code = status(error);
        const uncertainInitialization = operation === "open" && dispatched && !(code !== undefined && code >= 400 && code < 500);
        this.publish({ revision: null, reloadRequired: true,
          outcomeUnknown: this.current.outcomeUnknown || uncertainInitialization, error: errorText(error) });
        throw error;
      }
    });
  }
  private mutate(operation: "apply" | "update_pack", payload: Record<string, unknown>, options?: RequestOptions): Promise<OnboardingSnapshot> {
    const expected = this.current.revision;
    if (expected === null || this.current.reloadRequired) {
      return Promise.reject(failure("Reload and inspect the native profile before another change."));
    }
    const command = copy({ operation, user_id: this.userId, expected_revision: expected, ...payload });
    return this.serial(async () => {
      aborted(options?.signal);
      if (this.current.reloadRequired || this.current.revision !== expected) {
        this.publish({ revision: null, reloadRequired: true, error: "The profile changed while this edit was waiting. Reload before editing." });
        throw failure("The profile changed while this edit was waiting. Reload before editing.", 409);
      }
      let dispatched = false;
      try {
        // Closing a view or aborting after dispatch cannot undo a native commit.
        // Let its response settle into this identity's queue; do not forward the signal.
        dispatched = true;
        const response = await this.request(command);
        return this.install(response, expected);
      } catch (error) {
        if (this.closed) throw error;
        const code = status(error);
        if (code === 409) {
          // A browser may transparently resend a POST when its response is lost.
          // CAS prevents a second commit, but a conflict does not identify its winner.
          this.publish({ revision: null, reloadRequired: true, outcomeUnknown: true,
            error: "Native profile revision changed. Reload and inspect; the outcome of this edit is unverified." });
        } else if (dispatched && !(code !== undefined && code >= 400 && code < 500)) {
          const message = "Native write outcome is unknown. Reload and inspect before repeating the action.";
          this.publish({ revision: null, reloadRequired: true, outcomeUnknown: true, error: message });
          throw failure(`${message} ${errorText(error)}`);
        } else this.publish({ error: errorText(error) });
        throw error;
      }
    });
  }
}

/** Only the selected identity is stored locally. Snapshot, history and policy remain native. */
export class UserProfileHost {
  private current: UserProfileHostState = { userId: null, storageError: null, viewEpoch: 0, session: null };
  private session: NativeProfileSession | null = null;
  private listeners = new Set<() => void>();
  private storage: IdentityStorage | null;
  readonly available: boolean;
  constructor(private readonly api: OnboardingHostApi, private readonly options: UserProfileHostOptions = {}) {
    this.available = Boolean(api.onboarding);
    try { this.storage = options.storage === undefined ? (typeof localStorage === "undefined" ? null : localStorage) : options.storage; }
    catch { this.storage = null; this.current.storageError = "The selected profile ID cannot be read from browser settings."; }
    try {
      const raw = this.storage?.getItem(USER_PROFILE_IDENTITY_KEY);
      if (raw !== null && raw !== undefined) {
        const saved = object(JSON.parse(raw), "Saved profile identity");
        if (saved.schema !== "loom.user_profile_identity/1" || typeof saved.user_id !== "string" || !saved.user_id.trim()
          || Object.keys(saved).some(key => key !== "schema" && key !== "user_id")) throw new Error("Invalid saved profile identity.");
        this.installIdentity(saved.user_id);
      }
    } catch { this.current.storageError = "Saved profile identity is invalid or unavailable; its original bytes were preserved. Choose an ID explicitly."; }
  }
  readonly getState = (): UserProfileHostState => this.current;
  readonly subscribe = (listener: () => void): (() => void) => { this.listeners.add(listener); return () => { this.listeners.delete(listener); }; };
  getAdapter(): OnboardingAdapter | null { return this.session?.adapter ?? null; }
  private publish(patch: Partial<UserProfileHostState>): void {
    this.current = { ...this.current, ...patch };
    for (const listener of this.listeners) listener();
  }
  private installIdentity(userId: string | null): void {
    this.session?.close();
    this.session = userId && this.available ? new NativeProfileSession(userId, this.api, state => {
      if (this.current.userId === userId && this.session === next) this.publish({ session: state });
    }, this.options.onActivity) : null;
    const next = this.session;
    this.current = { ...this.current, userId, session: next?.state() ?? null, viewEpoch: this.current.viewEpoch + 1 };
  }
  selectUser(userId: string | null): void {
    if (userId !== null && !userId.trim()) throw new Error("Enter a nonempty native profile ID.");
    if (userId === this.current.userId) this.reload();
    else this.installIdentity(userId);
    let storageError: string | null = null;
    try {
      if (userId === null) this.storage?.removeItem(USER_PROFILE_IDENTITY_KEY);
      else this.storage?.setItem(USER_PROFILE_IDENTITY_KEY, JSON.stringify({ schema: "loom.user_profile_identity/1", user_id: userId }));
      if (!this.storage) storageError = "The profile ID is selected for this session; browser settings are unavailable.";
    } catch { storageError = "The profile ID is selected for this session but could not be saved; previous settings were preserved."; }
    this.publish({ storageError });
  }
  reload(): void { this.publish({ viewEpoch: this.current.viewEpoch + 1 }); }
}
export function createUserProfileHost(api: OnboardingHostApi, options?: UserProfileHostOptions): UserProfileHost {
  return new UserProfileHost(api, options);
}
