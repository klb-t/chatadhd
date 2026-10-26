import type { BlobLoader, BlobUploader, CapabilityTable, DeviceHost, PendingCapabilities, PendingOutcome, PendingPart, PendingRecording, PendingRequest, PendingText, StepOutcome, StreamLike } from "./device-host";

export type { BlobLoader, BlobUploader, DeviceHost, PickedFolder, Recording, StepOutcome, StreamLike, RecorderLike } from "./device-host";

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

/** Why a device could not be used: declined (the person or the system said no), unavailable (there is no such device), failed. */
export class DeviceError extends Error {
  readonly state: "failed" | "declined" | "unavailable";
  constructor(state: "failed" | "declined" | "unavailable", message: string) {
    super(message);
    this.state = state;
  }
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

async function permissionState(host: DeviceHost, capability: string, fallback: string): PendingText {
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
export async function detectCapabilities(host: DeviceHost): PendingCapabilities {
  const found: CapabilityTable = {};
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
    found.shows_notifications = { permission: host.nativeNotify.state() === "granted" ? "granted" : "prompt", via: "native shell" };
  }
  if (host.arrangeWindows) found.arranges_windows = { permission: "granted", via: "window system of the page" };
  if (host.pickFile) found.picks_files = { permission: "granted", via: "file chooser" };
  if (host.pickFolder) found.picks_folders = { permission: "granted", via: "folder chooser" };
  if (host.mediaDevices || host.notifications) found.requests_permission = { permission: "granted", via: "Permissions API" };
  return found;
}

/** The body of client.declare for this browser. */
export async function declaration(host: DeviceHost, clientId: string, kind: string, label: string): PendingRequest {
  const capabilities = await detectCapabilities(host);
  return { client_id: clientId, client: { kind, label }, capabilities };
}

// ── one step on the device ──────────────────────────────────────────

/** The kinds of step a person has to start (or stop) themselves, by the action of the step. */
export type StepKind = "record" | "photo" | "file" | "folder" | "read_clipboard" | "write_clipboard" | "notify" | "play" | "permission" | "window" | "unsupported";

const KIND_OF_ACTION: { [action: string]: StepKind } = {
  "device.record_audio": "record",
  "device.capture_image": "photo",
  "device.pick_file": "file",
  "device.pick_folder": "folder",
  "device.clipboard_read": "read_clipboard",
  "device.clipboard_write": "write_clipboard",
  "device.notify": "notify",
  "device.play_audio": "play",
  "device.request_permission": "permission",
};

export function stepKind(spec: ClientStepSpec): StepKind {
  const action = typeof spec.action === "string" ? spec.action : "";
  if (KIND_OF_ACTION[action]) return KIND_OF_ACTION[action];
  // W2: every action of the catalogue on the windows of the page (window.open, window.close ... and any later one) is one kind: the shell does it
  return spec.capability === "arranges_windows" || action.startsWith("window.") ? "window" : "unsupported";
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

function failure(error: unknown): DeviceError {
  if (error instanceof DeviceError) return error;
  const name = error instanceof Error ? error.name : "";
  const message = error instanceof Error ? error.message : String(error);
  if (name === "NotAllowedError" || name === "SecurityError" || name === "PermissionDeniedError") return new DeviceError("declined", message);
  if (name === "NotFoundError" || name === "DevicesNotFoundError") return new DeviceError("unavailable", message);
  return new DeviceError("failed", message);
}

/** Opens the microphone and starts recording. A refused or missing microphone is a DeviceError (declined / unavailable). */
export async function startRecording(host: DeviceHost, upload: BlobUploader): PendingRecording {
  if (!host.mediaDevices || !host.MediaRecorder) throw new DeviceError("unavailable", "no microphone API");
  let stream: StreamLike;
  try {
    stream = await host.mediaDevices.getUserMedia({ audio: true });
  } catch (error) {
    throw failure(error);
  }
  const chunks: Blob[] = [];
  const recorder = new host.MediaRecorder(stream);
  const ended = new Promise((resolve, reject) => {
    recorder.onstop = () => resolve(true);
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
      // The recording goes to the kernel as bytes first; the answer names it by hash (nothing is base64 in the answer).
      try {
        const stored = await upload(sound, "recording");
        return { state: "ok", parts: [{ kind: "audio", mime, blob_hash: stored.hash }] };
      } catch (error) {
        const problem = failure(error);
        return { state: problem.state, error: { message: problem.message } };
      }
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

async function partOf(blob: Blob, name: string, upload: BlobUploader): PendingPart {
  const mime = blob.type || "application/octet-stream";
  if (mime.startsWith("text/")) return { kind: "text", text: await blob.text(), name };
  const stored = await upload(blob, name);
  return { kind: mediaKind(mime), mime, blob_hash: stored.hash, name };
}

function audioHash(spec: ClientStepSpec): string {
  const named = spec.with ? text(spec.with.audio) : "";
  if (named !== "") return named;
  for (const input of spec.inputs ?? []) if (input.kind === "audio" && text(input.blob_hash) !== "") return text(input.blob_hash);
  return "";
}

/** The answer of device.pick_folder: the manifest of the files that were uploaded (the kernel writes them back as a folder). */
export const FOLDER_MANIFEST_SCHEMA = "loom.folder_manifest/1";

/** Progress of a step that sends many files: how many are sent of how many. */
export type StepProgress = (done: number, total: number) => void;

/** Carries out a step that is a single act (everything but the recording, which has two: start and stop). */
export async function runStep(host: DeviceHost, spec: ClientStepSpec, load: BlobLoader, upload: BlobUploader, progress?: StepProgress): PendingOutcome {
  try {
    switch (stepKind(spec)) {
      case "window": {
        const done = host.arrangeWindows ? host.arrangeWindows(text(spec.action), spec.with ?? {}) : null;
        if (!done) throw new DeviceError("unavailable", "no window system on this page");
        return done;
      }
      case "photo": {
        const taken = host.takePicture ? await host.takePicture() : null;
        const picture = taken ?? (host.pickFile ? await host.pickFile("image/*", "environment") : null);
        if (!picture) return { state: "declined", error: { message: "no picture was taken" } };
        return { state: "ok", parts: [await partOf(picture, "", upload)] };
      }
      case "file": {
        if (!host.pickFile) throw new DeviceError("unavailable", "no file chooser");
        const chosen = await host.pickFile(text(spec.with ? spec.with.accept : ""), "");
        if (!chosen) return { state: "declined", error: { message: "no file was chosen" } };
        return { state: "ok", parts: [await partOf(chosen, chosen.name, upload)] };
      }
      case "folder": {
        if (!host.pickFolder) throw new DeviceError("unavailable", "no folder chooser");
        const chosen = await host.pickFolder();
        if (!chosen || chosen.files.length === 0) return { state: "declined", error: { message: "no folder was chosen" } };
        // Every file goes to the kernel as bytes (the blob upload), one after the other; the answer is only the manifest naming them by hash.
        const files: Array<{ path: string; blob_hash: string; size: number; mime?: string }> = [];
        for (const item of chosen.files) {
          const stored = await upload(item.file, item.path);
          files.push({ path: item.path, blob_hash: stored.hash, size: item.file.size, ...(item.file.type ? { mime: item.file.type } : {}) });
          if (progress) progress(files.length, chosen.files.length);
        }
        return { state: "ok", parts: [{ kind: "json", value: { schema: FOLDER_MANIFEST_SCHEMA, name: chosen.name, files } }] };
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
          if (host.nativeNotify.state() !== "granted") {
            host.nativeNotify.request();
            throw new DeviceError("declined", "allow notifications in the system dialog, then do it again");
          }
          if (!host.nativeNotify.show(text(spec.title), body)) throw new DeviceError("declined", "the system did not show the notification");
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
export async function askPermission(host: DeviceHost, name: string): PendingText {
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
