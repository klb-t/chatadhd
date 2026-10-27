// The person's own content inside a sentence of the app (IMPORT_JAKO_PLIKI_W_GRAFIE P2, person_content): the kernel names the values that
// came from the person's data (a file name, a title, an e-mail); they are wrapped as content (data-person-content) so that the words
// audit skips exactly them and still reads every word of the app around them.
import { Fragment } from "react";

/** Splits `text` at every occurrence of the person's values (longest first) into [text, isPerson] pieces. */
export function personPieces(text: string, person: readonly string[]): [string, boolean][] {
  const values = [...new Set(person.filter(v => v !== ""))].sort((a, b) => b.length - a.length);
  const out: [string, boolean][] = [];
  let rest = text;
  while (rest) {
    let at = -1, hit = "";
    for (const v of values) {
      const i = rest.indexOf(v);
      if (i >= 0 && (at < 0 || i < at)) { at = i; hit = v; }
    }
    if (at < 0) { out.push([rest, false]); break; }
    if (at > 0) out.push([rest.slice(0, at), false]);
    out.push([hit, true]);
    rest = rest.slice(at + hit.length);
  }
  return out;
}

export default function PersonText({ text, person }: { text: string; person: readonly string[] }) {
  return <>{personPieces(text, person).map(([piece, mine], i) => mine
    ? <span key={i} data-person-content="">{piece}</span>
    : <Fragment key={i}>{piece}</Fragment>)}</>;
}
