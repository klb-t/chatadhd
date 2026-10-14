import type { ClientDeclareRequest, ClientResultPart } from "../api/loom-api";

/** Presentation feature holding the texts (`presentation.client_step`). */
export const CLIENT_STEP_FEATURE = "client_step";

/** What the kernel asks of this client for one step of a run (loom.client_step/1, in the pending question of the run). */
export interface ClientStepInput { kind?: string; text?: string; blob_hash?: string; mime?: string }
export interface ClientStepSpec {
  action?: string;
  capability?: string;
  permission?: string;
  title?: string;
  labels?: { [lang: string]: string };
  prompt?: { [lang: string]: string };
  expects?: string[];
  with?: { [name: string]: unknown };
  inputs?: ClientStepInput[];
}

/** What this client did with the step, in the kernel's words (state, the parts it produced, why it could not). */
export interface StepOutcome {
  state: "ok" | "failed" | "declined" | "unavailable";
  parts?: ClientResultPart[];
  data?: unknown;
  error?: { message: string };
}

/** Why a device could not be used: declined (the person or the system said no), unavailable (there is no such device), failed. */
export class DeviceError extends Error {
  readonly state: "failed" | "declined" | "unavailable";
  constructor(state: "failed" | "declined" | "unavailable", message: string) {
    super(message);
    this.state = state;
  }
}

/** The browser, reduced to what the devices need. Every member is optional: what is missing is not declared. The page builds it
 *  from the real globals (browser-host.ts); the tests build it from fakes. */
export interface StreamLike { getTracks(): Array<{ stop(): void }> }
export interface RecorderLike {
  mimeType: string;
  ondataavailable: ((event: { data: Blob }) => void) | null;
  onstop: (() => void) | null;
  onerror: ((event: unknown) => void) | null;
  start(): void;
  stop(): void;
}
export interface DeviceHost {
  mediaDevices?: { getUserMedia(constraints: { audio?: boolean; video?: boolean }): Promise<StreamLike> };
  MediaRecorder?: new (stream: StreamLike) => RecorderLike;
  permissions?: { query(descriptor: { name: string }): Promise<{ state: string }> };
  notifications?: { permission(): string; request(): Promise<string>; show(title: string, body: string): void };
  clipboard?: { readText?(): Promise<string>; writeText?(text: string): Promise<void> };
  /** Plays a recording (resolves when it has played). */
  play?: (blob: Blob) => Promise<void>;
  /** Opens the file chooser (an input element); null when the person closed it without a choice. */
  pickFile?: (accept: string, capture: string) => Promise<File | null>;
  /** Takes a still picture with the camera; null when the camera is not there. */
  takePicture?: () => Promise<Blob | null>;
  /** The page has a document at all (a file chooser can be opened). */
  hasDocument?: boolean;
  /** Notifications through the native shell instead of the Notification API. */
  nativeNotify?: (title: string, body: string) => void;
}

/** Browser permission names per capability (the Permissions API). */
const PERMISSION_OF: { [capability: string]: string } = {
  captures_audio: "microphone",
  captures_image: "camera",
  clipboard_read: "clipboard-read",
  clipboard_write: "clipboard-write",
  shows_notifications: "notifications",
};

const STATES = ["granted", "prompt", "denied"];

function state(value: string): string {
  return STATES.indexOf(value) >= 0 ? value : "prompt";
}

async function permissionState(host: DeviceHost, capability: string, fallback: string): Promise<string> {
  const name = PERMISSION_OF[capability];
  if (!name || !host.permissions) return fallback;
  try {
    const status = await host.permissions.query({ name });
    return state(status.state);
  } catch {
    return fallback;  // the browser does not know this permission name (Firefox: clipboard-read)
  }
}

/** The capabilities this browser has right now, with their permission state: feature detection and the Permissions API. */
export async function detectCapabilities(host: DeviceHost): Promise<{ [capability: string]: { permission: string; via: string } }> {
  const found: { [capability: string]: { permission: string; via: string } } = {};
  if (host.mediaDevices && host.MediaRecorder) {
    found.captures_audio = { permission: await permissionState(host, "captures_audio", "prompt"), via: "MediaRecorder" };
  }
  if (host.takePicture || host.mediaDevices) {
    found.captures_image = { permission: await permissionState(host, "captures_image", "prompt"), via: host.takePicture ? "camera" : "getUserMedia" };
  } else if (host.pickFile) {
    found.captures_image = { permission: "granted", via: "file chooser" };
  }
  if (host.play) found.plays_audio = { permission: "granted", via: "audio element" };
  if (host.clipboard && host.clipboard.readText) found.clipboard_read = { permission: await permissionState(host, "clipboard_read", "prompt"), via: "clipboard" };
  if (host.clipboard && host.clipboard.writeText) found.clipboard_write = { permission: await permissionState(host, "clipboard_write", "granted"), via: "clipboard" };
  if (host.notifications) {
    const asked = host.notifications.permission();
    found.shows_notifications = { permission: asked === "granted" ? "granted" : asked === "denied" ? "denied" : "prompt", via: "Notification" };
  } else if (host.nativeNotify) {
    found.shows_notifications = { permission: "granted", via: "native shell" };
  }
  if (host.pickFile) found.picks_files = { permission: "granted", via: "file chooser" };
  if (host.mediaDevices || host.notifications) found.requests_permission = { permission: "granted", via: "Permissions API" };
  return found;
}

/** The body of client.declare for this browser. */
export async function declaration(host: DeviceHost, clientId: string, kind: string, label: string): Promise<ClientDeclareRequest> {
  const capabilities = await detectCapabilities(host);
  return { client_id: clientId, client: { kind, label }, capabilities };
}

// ── one step on the device ──────────────────────────────────────────

/** The kinds of step a person has to start (or stop) themselves, by the action of the step. */
export type StepKind = "record" | "photo" | "file" | "read_clipboard" | "write_clipboard" | "notify" | "play" | "permission" | "unsupported";

const KIND_OF_ACTION: { [action: string]: StepKind } = {
  "device.record_audio": "record",
  "device.capture_image": "photo",
  "device.pick_file": "file",
  "device.clipboard_read": "read_clipboard",
  "device.clipboard_write": "write_clipboard",
  "device.notify": "notify",
  "device.play_audio": "play",
  "device.request_permission": "permission",
};

export function stepKind(spec: ClientStepSpec): StepKind {
  const action = typeof spec.action === "string" ? spec.action : "";
  return KIND_OF_ACTION[action] ?? "unsupported";
}

function text(value: unknown): string {
  return typeof value === "string" ? value : "";
}

/** The text a step works on: its own `text` / `content` argument, else the text parts that flowed into it. */
export function stepText(spec: ClientStepSpec): string {
  const own = spec.with ? text(spec.with.text) || text(spec.with.content) : "";
  if (own !== "") return own;
  const parts: string[] = [];
  for (const input of spec.inputs ?? []) if (input.kind === "text" && text(input.text) !== "") parts.push(text(input.text));
  return parts.join("\n\n");
}

/** A binary string of the bytes in slices, so a long recording does not overflow the argument limit of the call. */
const SLICE = 0x8000;

export async function toBase64(blob: Blob): Promise<string> {
  const bytes = new Uint8Array(await blob.arrayBuffer());
  let binary = "";
  for (let at = 0; at < bytes.length; at += SLICE) binary += String.fromCharCode(...bytes.subarray(at, at + SLICE));
  return btoa(binary);
}

function failure(error: unknown): DeviceError {
  if (error instanceof DeviceError) return error;
  const name = error instanceof Error ? error.name : "";
  const message = error instanceof Error ? error.message : String(error);
  if (name === "NotAllowedError" || name === "SecurityError" || name === "PermissionDeniedError") return new DeviceError("declined", message);
  if (name === "NotFoundError" || name === "DevicesNotFoundError") return new DeviceError("unavailable", message);
  return new DeviceError("failed", message);
}

/** A recording in progress: stop() ends it and answers with the sound; abort() drops it. */
export interface Recording {
  stop(): Promise<StepOutcome>;
  abort(): void;
}

/** Opens the microphone and starts recording. A refused or missing microphone is a DeviceError (declined / unavailable). */
export async function startRecording(host: DeviceHost): Promise<Recording> {
  if (!host.mediaDevices || !host.MediaRecorder) throw new DeviceError("unavailable", "no microphone API");
  let stream: StreamLike;
  try {
    stream = await host.mediaDevices.getUserMedia({ audio: true });
  } catch (error) {
    throw failure(error);
  }
  const chunks: Blob[] = [];
  const recorder = new host.MediaRecorder(stream);
  const ended = new Promise<void>((resolve, reject) => {
    recorder.onstop = () => resolve();
    recorder.onerror = (event) => reject(failure(event));
  });
  recorder.ondataavailable = (event) => { if (event.data && event.data.size > 0) chunks.push(event.data); };
  const release = () => { for (const track of stream.getTracks()) track.stop(); };
  recorder.start();
  let finished = false;
  return {
    async stop() {
      if (finished) return { state: "failed", error: { message: "the recording was already ended" } };
      finished = true;
      try {
        recorder.stop();
        await ended;
      } catch (error) {
        release();
        const problem = failure(error);
        return { state: problem.state, error: { message: problem.message } };
      }
      release();
      const mime = recorder.mimeType || (chunks[0] ? chunks[0].type : "") || "audio/webm";
      const sound = new Blob(chunks, { type: mime });
      if (sound.size === 0) return { state: "failed", error: { message: "nothing was recorded" } };
      return { state: "ok", parts: [{ kind: "audio", mime, data_base64: await toBase64(sound) }] };
    },
    abort() {
      if (finished) return;
      finished = true;
      recorder.onstop = null;
      try { recorder.stop(); } catch { /* the recorder had not started */ }
      release();
    },
  };
}

function mediaKind(mime: string): string {
  if (mime.startsWith("audio/")) return "audio";
  if (mime.startsWith("image/")) return "image";
  if (mime.startsWith("video/")) return "video";
  return "file_ref";
}

async function partOf(blob: Blob, name: string): Promise<ClientResultPart> {
  const mime = blob.type || "application/octet-stream";
  if (mime.startsWith("text/")) return { kind: "text", text: await blob.text(), name };
  return { kind: mediaKind(mime), mime, data_base64: await toBase64(blob), name };
}

/** Reads a stored recording for `device.play_audio`: from the argument `audio` (a blob hash) or the audio that flowed in. */
export interface BlobLoader { (hash: string): Promise<Blob> }

function audioHash(spec: ClientStepSpec): string {
  const named = spec.with ? text(spec.with.audio) : "";
  if (named !== "") return named;
  for (const input of spec.inputs ?? []) if (input.kind === "audio" && text(input.blob_hash) !== "") return text(input.blob_hash);
  return "";
}

/** Carries out a step that is a single act (everything but the recording, which has two: start and stop). */
export async function runStep(host: DeviceHost, spec: ClientStepSpec, load: BlobLoader): Promise<StepOutcome> {
  try {
    switch (stepKind(spec)) {
      case "photo": {
        const taken = host.takePicture ? await host.takePicture() : null;
        const picture = taken ?? (host.pickFile ? await host.pickFile("image/*", "environment") : null);
        if (!picture) return { state: "declined", error: { message: "no picture was taken" } };
        return { state: "ok", parts: [await partOf(picture, "")] };
      }
      case "file": {
        if (!host.pickFile) throw new DeviceError("unavailable", "no file chooser");
        const chosen = await host.pickFile(text(spec.with ? spec.with.accept : ""), "");
        if (!chosen) return { state: "declined", error: { message: "no file was chosen" } };
        return { state: "ok", parts: [await partOf(chosen, chosen.name)] };
      }
      case "read_clipboard": {
        if (!host.clipboard || !host.clipboard.readText) throw new DeviceError("unavailable", "no clipboard");
        const copied = await host.clipboard.readText();
        return { state: "ok", parts: [{ kind: "text", text: copied }] };
      }
      case "write_clipboard": {
        if (!host.clipboard || !host.clipboard.writeText) throw new DeviceError("unavailable", "no clipboard");
        await host.clipboard.writeText(stepText(spec));
        return { state: "ok" };
      }
      case "notify": {
        const body = stepText(spec);
        if (host.notifications) {
          let granted = host.notifications.permission();
          if (granted !== "granted" && granted !== "denied") granted = await host.notifications.request();
          if (granted !== "granted") throw new DeviceError("declined", "notifications are not allowed");
          host.notifications.show(text(spec.title), body);
        } else if (host.nativeNotify) {
          host.nativeNotify(text(spec.title), body);
        } else {
          throw new DeviceError("unavailable", "no notifications");
        }
        return { state: "ok" };
      }
      case "play": {
        if (!host.play) throw new DeviceError("unavailable", "no audio output");
        const hash = audioHash(spec);
        if (hash === "") return { state: "failed", error: { message: "no recording to play" } };
        await host.play(await load(hash));
        return { state: "ok" };
      }
      case "permission": {
        const asked = spec.with ? text(spec.with.permission) : "";
        return { state: "ok", data: { permission: asked, state: await askPermission(host, asked) } };
      }
      default:
        throw new DeviceError("unavailable", "this client has no executor for " + text(spec.action));
    }
  } catch (error) {
    const problem = failure(error);
    return { state: problem.state, error: { message: problem.message } };
  }
}

/** Asks the system for a permission by its browser name; the answer is its state afterwards. */
export async function askPermission(host: DeviceHost, name: string): Promise<string> {
  if (name === "microphone" || name === "camera") {
    if (!host.mediaDevices) throw new DeviceError("unavailable", "no media devices");
    try {
      const stream = await host.mediaDevices.getUserMedia(name === "microphone" ? { audio: true } : { video: true });
      for (const track of stream.getTracks()) track.stop();
      return "granted";
    } catch (error) {
      const problem = failure(error);
      if (problem.state === "declined") return "denied";
      throw problem;
    }
  }
  if (name === "notifications") {
    if (!host.notifications) throw new DeviceError("unavailable", "no notifications");
    return state(await host.notifications.request());
  }
  if (name === "clipboard-read" || name === "clipboard-write") {
    if (!host.permissions) throw new DeviceError("unavailable", "no Permissions API");
    return state((await host.permissions.query({ name })).state);
  }
  throw new DeviceError("failed", "unknown permission " + name);
}

/** The answer to send to actions.client_result for an outcome. */
export function resultOf(runId: string, receiptId: string, outcome: StepOutcome, ref: string) {
  return {
    run_id: runId, receipt_id: receiptId, state: outcome.state, ref,
    ...(outcome.parts && outcome.parts.length > 0 ? { parts: outcome.parts } : {}),
    ...(outcome.data !== undefined ? { data: outcome.data } : {}),
    ...(outcome.error ? { error: outcome.error } : {}),
  };
}
