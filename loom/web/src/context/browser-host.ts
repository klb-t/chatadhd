import type { DeviceHost, PickedFolder, RecorderLike, StreamLike } from "./client-device";

/** The Android shell's JavaScript interface, when the page runs inside it (notifications are the shell's, not the WebView's). */
interface ShellBridge {
  notify?: (title: string, body: string) => boolean;
  notificationState?: () => string;
  requestNotifications?: () => void;
}

/** Opens the file chooser of the page and waits for the choice: null when it is closed without one. */
function chooseFile(accept: string, capture: string): Promise<File | null> {
  return new Promise((resolve) => {
    const input = document.createElement("input");
    input.type = "file";
    if (accept !== "") input.accept = accept;
    if (capture !== "") input.setAttribute("capture", capture);
    input.style.display = "none";
    const done = (file: File | null) => { input.remove(); resolve(file); };
    input.addEventListener("change", () => done(input.files && input.files.length > 0 ? input.files[0] : null));
    input.addEventListener("cancel", () => done(null));
    document.body.appendChild(input);
    input.click();
  });
}

/** Whether this browser has a folder chooser: an input with webkitdirectory (Chromium, Firefox, Safari; the Android WebView has none). */
function hasFolderChooser(): boolean {
  if (typeof document === "undefined") return false;
  return "webkitdirectory" in document.createElement("input");
}

/** Opens the folder chooser of the page and waits for the choice: null when it is closed without one. Paths are relative to the folder. */
function chooseFolder(): Promise<PickedFolder | null> {
  return new Promise((resolve) => {
    const input = document.createElement("input");
    input.type = "file";
    input.multiple = true;
    input.setAttribute("webkitdirectory", "");
    input.style.display = "none";
    const done = (value: PickedFolder | null) => { input.remove(); resolve(value); };
    input.addEventListener("change", () => {
      const list = input.files ? Array.from(input.files) : [];
      if (list.length === 0) { done(null); return; }
      const relative = (file: File) => (file as File & { webkitRelativePath?: string }).webkitRelativePath || file.name;
      const name = relative(list[0]).split("/")[0] || "folder";
      done({ name, files: list.map((file) => {
        const full = relative(file);
        return { path: full.startsWith(name + "/") ? full.slice(name.length + 1) : full, file };
      }) });
    });
    input.addEventListener("cancel", () => done(null));
    document.body.appendChild(input);
    input.click();
  });
}

/** A still picture from the camera of the device: a frame of the video stream, as a JPEG. */
async function snapshot(): Promise<Blob | null> {
  if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) return null;
  const stream = await navigator.mediaDevices.getUserMedia({ video: true });
  try {
    const video = document.createElement("video");
    video.muted = true;
    video.playsInline = true;
    video.srcObject = stream;
    await video.play();
    if (video.videoWidth === 0) await new Promise<void>((resolve) => video.addEventListener("loadeddata", () => resolve(), { once: true }));
    const canvas = document.createElement("canvas");
    canvas.width = video.videoWidth;
    canvas.height = video.videoHeight;
    const context = canvas.getContext("2d");
    if (!context) return null;
    context.drawImage(video, 0, 0);
    return await new Promise<Blob | null>((resolve) => canvas.toBlob((blob) => resolve(blob), "image/jpeg"));
  } finally {
    for (const track of stream.getTracks()) track.stop();
  }
}

function playBlob(blob: Blob): Promise<void> {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(blob);
    const audio = new Audio(url);
    const release = () => URL.revokeObjectURL(url);
    audio.addEventListener("ended", () => { release(); resolve(); });
    audio.addEventListener("error", () => { release(); reject(new Error("the recording could not be played")); });
    audio.play().catch((error) => { release(); reject(error); });
  });
}

/** The real browser as a DeviceHost: only what the page has is present, so only that is declared. */
export function browserHost(): DeviceHost {
  const host: DeviceHost = {};
  const media = typeof navigator !== "undefined" ? navigator.mediaDevices : undefined;
  if (media && typeof media.getUserMedia === "function") {
    host.mediaDevices = { getUserMedia: (constraints) => media.getUserMedia(constraints) as Promise<StreamLike> };
    host.takePicture = snapshot;
  }
  if (typeof MediaRecorder !== "undefined") {
    host.MediaRecorder = MediaRecorder as unknown as new (stream: StreamLike) => RecorderLike;
  }
  if (typeof navigator !== "undefined" && navigator.permissions && typeof navigator.permissions.query === "function") {
    host.permissions = { query: (descriptor) => navigator.permissions.query(descriptor as unknown as PermissionDescriptor) };
  }
  if (typeof navigator !== "undefined" && navigator.clipboard) {
    host.clipboard = {
      readText: () => navigator.clipboard.readText(),
      writeText: (value) => navigator.clipboard.writeText(value),
    };
  }
  if (typeof Notification !== "undefined") {
    host.notifications = {
      permission: () => Notification.permission === "default" ? "prompt" : Notification.permission,
      request: async () => { const answer = await Notification.requestPermission(); return answer === "default" ? "prompt" : answer; },
      show: (title, body) => { new Notification(title, { body }); },
    };
  }
  const shell = (window as unknown as { LoomBridge?: ShellBridge }).LoomBridge;
  if (shell && typeof shell.notify === "function") {
    const notify = shell.notify.bind(shell);
    const state = typeof shell.notificationState === "function" ? shell.notificationState.bind(shell) : () => "granted";
    const ask = typeof shell.requestNotifications === "function" ? shell.requestNotifications.bind(shell) : () => {};
    host.nativeNotify = {
      state: () => state(),
      request: () => ask(),
      show: (title, body) => notify(title, body),
    };
  }
  if (typeof Audio !== "undefined") host.play = playBlob;
  if (typeof document !== "undefined") {
    host.hasDocument = true;
    host.pickFile = chooseFile;
    // The Android shell's WebView has the attribute but its file chooser picks files only: there the folder is not declared (the action stays
    // planned for that client, with its reason, and the sentence asks for a path or an archive file instead).
    if (hasFolderChooser() && !shell) host.pickFolder = chooseFolder;
  }
  return host;
}
