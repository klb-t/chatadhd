// M1-G1b (2): the attachment chips under a message. They were drawn only for messages of the role user and only from `message.attachments`, so an
// imported Claude.ai conversation (attachments kept as names and descriptions in metadata.export.attachments, `message.attachments` empty) showed none, and an
// assistant message with a file showed none either. A chip is now drawn for every role and from both places; its state comes only from data the message
// carries: the export says whether the bytes of the file were found in the archive (`resolved`). A chip with no such data claims nothing (state "plain").
// The chip never previews the file: no content is invented here.
import type { Message } from "../api/types";
import { attachmentName } from "../shell/composer-menu";
import { projectImportedMessage } from "./imported-message";

export type AttachmentState = "saved" | "names_only" | "plain";
export interface AttachmentChip { name: string; state: AttachmentState }

export function attachmentChips(message: Pick<Message, "attachments" | "metadata">): AttachmentChip[] {
  const chips: AttachmentChip[] = [];
  const refs = Array.isArray(message.attachments) ? message.attachments.filter((x): x is string => typeof x === "string") : [];
  const imported = projectImportedMessage(message)?.references.filter(r => r.kind === "attachment") ?? [];
  const stateByName = new Map<string, AttachmentState>();
  for (const ref of imported) {
    const name = ref.label || "?";
    const state: AttachmentState = ref.resolved === true ? "saved" : ref.resolved === false ? "names_only" : "plain";
    if (!stateByName.has(name) || state === "saved") stateByName.set(name, state);
  }
  const named = new Set<string>();
  for (const ref of refs) { const name = attachmentName(ref); named.add(name); chips.push({ name, state: stateByName.get(name) ?? "plain" }); }
  // every attachment the export lists is a chip, the ones counted twice by the export (the same file under /files and /files_v2) too: the number of chips is the number the kernel counted
  for (const ref of imported) {
    const name = ref.label || "?";
    if (named.has(name) && refs.length > 0) continue;   // already drawn from message.attachments with the export's state
    chips.push({ name, state: ref.resolved === true ? "saved" : ref.resolved === false ? "names_only" : "plain" });
  }
  return chips;
}

/** The words of a chip's state ("plik zapisany w aplikacji" | "tylko nazwa, bez pliku"); null when the message says nothing or the screen has no catalogue. */
export function chipStateText(chip: AttachmentChip, t: (id: string) => string): string | null {
  const id = chip.state === "saved" ? "att_saved" : chip.state === "names_only" ? "att_names_only" : null;
  if (!id) return null;
  const text = t(id);
  return text === id ? null : text;
}
