/// <reference types="vite/client" />
// Examples remain data. Application identifiers and source versions never
// select a backend/model, nor imply tested parity with a vendor's application.
import loom from "./data/loom-default.json";
import chatgpt from "./data/chatgpt-inspired.json";
import claude from "./data/claude-inspired.json";
import gemini from "./data/gemini-inspired.json";
import loom2 from "./data/loom-default-r2.json";
import chatgpt2 from "./data/chatgpt-inspired-r2.json";
import claude2 from "./data/claude-inspired-r2.json";
import gemini2 from "./data/gemini-inspired-r2.json";
import librechat from "./data/librechat-0.8.8.json";
import nextchat from "./data/nextchat-2.16.1.json";

// Original revisions remain available so a stored view/session is not reinterpreted.
export const BUILTIN_PROFILE_DOCUMENTS: unknown[] = [loom2, chatgpt2, claude2, gemini2, librechat, nextchat, loom, chatgpt, claude, gemini];

const sourceFiles = import.meta.glob("./data/*.json", { query: "?raw", import: "default", eager: true });
export function builtinProfileSource(id: string, revision: number): { text: string; sourceRef: string } | undefined {
  for (const [path, text] of Object.entries(sourceFiles)) {
    if (typeof text !== "string") continue;
    const document = JSON.parse(text);
    if (document.id === id && document.profile_revision === revision) {
      return { text, sourceRef: `repository:loom/web/src/profiles/${path.replace(/^\.\//, "")}` };
    }
  }
}
