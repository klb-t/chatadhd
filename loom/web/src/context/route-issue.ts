import { featureMessage, type PresentationFeature } from "../onboarding/presentation.mjs";
import type { JsonValue } from "../onboarding/types";

/** Presentation feature holding the texts (`presentation.route_issue`). */
export const ROUTE_ISSUE_FEATURE = "route_issue";
/** Stream chunk type: routing the input had a problem it worked around. The input went on as it would have; nothing was lost. */
export const ROUTE_ISSUE_CHUNK = "route_issue";
/** The one problem the kernel names today (a model route that failed and the local rules decided); other codes get the general note. */
const MODEL_ROUTE_FAILED = "model_route_failed";

/** Index signature keeps the module lexically scannable by the literal guard. */
interface Row { [key: string]: JsonValue }
export interface RouteIssue { code: string; message: string; fallback: string }

function text(value: unknown): string {
  return typeof value === "string" ? value : "";
}

/** Issues as the stream carries them; anything that is not an object with a code is dropped, the rest is kept in order. */
export function routeIssues(raw: unknown): RouteIssue[] {
  if (!Array.isArray(raw)) return [];
  const issues: RouteIssue[] = [];
  for (const item of raw) {
    if (item === null || typeof item !== "object") continue;
    const row = item as { [key: string]: unknown };
    const code = text(row.code);
    if (code === "") continue;
    issues.push({ code, message: text(row.message), fallback: text(row.fallback) });
  }
  return issues;
}

/** One quiet line per issue, in the page's language (words from the catalog; without one the machine names are shown). */
export function routeIssueNotes(feature: PresentationFeature | null, issues: RouteIssue[]): string[] {
  return issues.map((issue) => {
    const id = issue.code === MODEL_ROUTE_FAILED ? MODEL_ROUTE_FAILED : "other";
    const parameters: Row = { code: issue.code, message: issue.message };
    if (feature) {
      try { return featureMessage(feature, id, parameters); } catch { /* the catalog lacks the words: the names below */ }
    }
    return issue.message === "" ? issue.code : issue.code + ": " + issue.message;
  });
}
