// Examples remain data. Application identifiers and source versions never
// select a backend/model, nor imply tested parity with a vendor's application.
import loom from "./data/loom-default.json";
import chatgpt from "./data/chatgpt-inspired.json";
import claude from "./data/claude-inspired.json";
import gemini from "./data/gemini-inspired.json";

export const BUILTIN_PROFILE_DOCUMENTS: unknown[] = [loom, chatgpt, claude, gemini];
