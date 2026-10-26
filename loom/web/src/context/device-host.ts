// The types of the devices of this client that need generics - kept apart so the logic modules stay free of them (lexically scannable).
import type { ClientDeclareRequest, ClientResultPart } from "../api/loom-api";

/** What this client did with a step, in the kernel's words (state, the parts it produced, why it could not). */
export interface StepOutcome {
  state: "ok" | "failed" | "declined" | "unavailable";
  parts?: ClientResultPart[];
  data?: unknown;
  error?: { message: string };
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
  /** Opens the folder chooser of the page (an input with webkitdirectory); null when the person closed it without a choice. The folder is
   *  its name and its files, each with the path inside the folder. Present only where the browser has a folder chooser. */
  pickFolder?: () => Promise<PickedFolder | null>;
  /** Takes a still picture with the camera; null when the camera is not there. */
  takePicture?: () => Promise<Blob | null>;
  /** The page has a document at all (a file chooser can be opened). */
  hasDocument?: boolean;
  /** Notifications through the native shell instead of the Notification API (the Android WebView has none): the permission state
   *  (granted | prompt), asking for it (Android shows its own dialog; the answer comes later), and showing one (false: refused). */
  nativeNotify?: { state(): string; request(): void; show(title: string, body: string): boolean };
  /** W2 (OKNA §6.1): arranges the windows of the page (window.open / close / focus / next / prev); the shell answers synchronously with
   *  what it did, or null when no shell listens (then the capability is unavailable). */
  arrangeWindows?: (action: string, args: { [name: string]: unknown }) => StepOutcome | null;
}

/** A folder the person chose: its name and its files with their paths inside it (forward slashes, no leading slash). */
export interface PickedFolder { name: string; files: Array<{ path: string; file: File }> }

/** A recording in progress: stop() ends it and answers with the sound; abort() drops it. */
export interface Recording {
  stop(): Promise<StepOutcome>;
  abort(): void;
}

/** Reads a stored recording for `device.play_audio`: from the argument `audio` (a blob hash) or the audio that flowed in. */
export interface BlobLoader { (hash: string): Promise<Blob> }
/** Hands the bytes of a recording, a picture or a file to the kernel; the answer of the step names what was stored by its hash. */
export interface BlobUploader { (blob: Blob, name: string): Promise<{ hash: string }> }


/** Promises of the results of the logic module, named so its signatures need no generics. */
export type PendingOutcome = Promise<StepOutcome>;
export type PendingRecording = Promise<Recording>;
export type PendingText = Promise<string>;
export type PendingPart = Promise<ClientResultPart>;
export type PendingRequest = Promise<ClientDeclareRequest>;
export interface CapabilityState { permission: string; via: string }
export interface CapabilityTable { [capability: string]: CapabilityState }
export type PendingCapabilities = Promise<CapabilityTable>;
