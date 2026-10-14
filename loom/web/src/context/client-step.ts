import { featureMessage, type PresentationFeature } from "../onboarding/presentation.mjs";
import type { JsonValue } from "../onboarding/types";
import { stepKind, type ClientStepSpec, type StepKind } from "./client-device";

interface Row { [key: string]: JsonValue }

/** The id of the text on the button that starts a one-act step, by the kind of the step. */
const BUTTON_OF: { [kind: string]: string } = {
  photo: "do_capture_image",
  file: "do_pick_file",
  read_clipboard: "do_clipboard_read",
  write_clipboard: "do_clipboard_write",
  notify: "do_notify",
  play: "do_play_audio",
  permission: "do_request_permission",
  unsupported: "do_unsupported",
};

function pick(table: { [lang: string]: string } | undefined, locale: string): string {
  if (!table) return "";
  const own = table[locale];
  if (typeof own === "string" && own !== "") return own;
  const english = table.en;
  return typeof english === "string" ? english : "";
}

/** What the person is told about a step and the words of its controls. Without a catalog the machine names are shown. */
export interface ClientStepTexts {
  kind: StepKind;
  title: string;
  /** What the kernel says this step does (its own prompt, in the language of the catalog). */
  prompt: string;
  permissionHint: string | null;
  waiting: string | null;
  /** The button that starts the step: "Start recording" for a recording, the act itself for any other. */
  start: string;
  stop: string | null;
  recording: (seconds: number) => string;
  decline: string;
  cancel: string;
  sending: string;
  failed: (reason: string) => string;
  sendFailed: (reason: string) => string;
  unsupported: (reason: string) => string;
}

export function clientStepTexts(feature: PresentationFeature | null, spec: ClientStepSpec): ClientStepTexts {
  const say = (id: string, parameters: Row = {}): string | null => {
    if (!feature) return null;
    try { return featureMessage(feature, id, parameters); }
    catch { return null; }
  };
  const kind = stepKind(spec);
  const locale = feature ? feature.locale : "en";
  const named = pick(spec.labels, locale) || (typeof spec.title === "string" ? spec.title : "") || (typeof spec.action === "string" ? spec.action : "");
  const permission = typeof spec.permission === "string" ? spec.permission : "";
  return {
    kind,
    title: say("title") ?? named,
    prompt: pick(spec.prompt, locale) || named,
    permissionHint: permission === "" ? null : say("permission_hint", { permission }),
    waiting: say("waiting"),
    start: (kind === "record" ? say("start_record") : say(BUTTON_OF[kind] ?? "do_unsupported")) ?? named,
    stop: kind === "record" ? say("stop_record") ?? named : null,
    recording: (seconds) => say("recording", { seconds: String(seconds) }) ?? String(seconds),
    decline: say("decline") ?? "-",
    cancel: say("cancel") ?? "-",
    sending: say("sending") ?? "-",
    failed: (reason) => say("failed", { reason }) ?? reason,
    sendFailed: (reason) => say("send_failed", { reason }) ?? reason,
    unsupported: (reason) => say("unsupported", { reason }) ?? reason,
  };
}
