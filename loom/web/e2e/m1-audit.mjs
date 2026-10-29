// M1-K1c4: an audit of the text a Polish screen shows (used by the m1-*-native e2e scripts at every screenshot). It lists the visible lines (and the
// aria-label / placeholder / title attributes) that look like developer text instead of words: a raw key (dotted or snake_case: "chip.record",
// "context_receipt_store") or an English phrase. Values the kernel marks as the person's own content (person_content: file names, titles, an e-mail;
// rendered with data-person-content) are skipped exactly, the words of the app around them are still read. The person's own messages, the text of messages in the graph ("Ty: ...") and the folds "Szczegóły techniczne" (where the raw
// names of settings may be) are content or technical detail, not the words of the app, and are skipped. A line the kernel writes in English into the record at send time must not reach the screen unchanged (see whyWords).
const STOP = new Set(["the", "of", "and", "to", "in", "a", "an", "for", "was", "is", "are", "from", "by", "that", "this", "with", "on", "it", "as", "or", "be", "not", "no", "your", "you", "what", "when", "which"]);
const KEY = /\b[a-z][a-z0-9]*(?:[._][a-z0-9]+)+\b/;

export function looksLikeDeveloperText(line) {
  const text = line.trim();
  if (text.length < 3) return null;
  if (/^https?:\/\//.test(text)) return null;
  // M1-NAZWA-PRODUKTU: the app is ChatADHD; "Loom" is the library and is said only in About (marked data-audit-skip), documentation and self-knowledge.
  if (/\bLoom\b/.test(text)) return "library name on screen (the app is ChatADHD)";
  if (KEY.test(text) && !/^\d/.test(text) && !/\b\d+\.\d+\b/.test(text.replace(KEY, ""))) {
    const m = text.match(KEY)[0];
    if (!/^[a-z]+\.(?:com|org|io|ai|net)$/.test(m)) return "raw key: " + m;
  }
  const words = text.split(/[^A-Za-z'’-]+/).filter(Boolean);
  if (words.length >= 3 && !/[ąćęłńóśźżĄĆĘŁŃÓŚŹŻ]/.test(text) && words.filter(w => STOP.has(w.toLowerCase())).length >= 2) return "English phrase";
  return null;
}

/** The suspicious lines of what the page shows now: [{ line, where, why }]. */
export async function auditPage(page) {
  const lines = await page.evaluate(() => {
    const skip = "[data-testid='message'], [data-testid='streaming-message'], textarea, input, script, style, [data-kind='message'] .gnav-label, [data-audit-skip], [data-person-content], [data-testid='card-unset-technical'], details:not([open]) > :not(summary)";
    const seen = new Set(), out = [];
    const where = (el, attr) => { const id = el.closest("[data-testid]"); return (attr ? attr + " of " : "") + el.tagName.toLowerCase() + (el.getAttribute("data-testid") ? "[" + el.getAttribute("data-testid") + "]" : id ? " in [" + id.getAttribute("data-testid") + "]" : ""); };
    const add = (s, el, attr) => { const t = (s ?? "").replace(/\s+/g, " ").trim(); if (t && !seen.has(t)) { seen.add(t); out.push({ line: t, where: where(el, attr) }); } };
    const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
    for (let n = walker.nextNode(); n; n = walker.nextNode()) {
      const el = n.parentElement;
      if (!el || el.closest(skip) || !el.getClientRects().length) continue;
      add(n.nodeValue, el);
    }
    for (const el of document.querySelectorAll("[aria-label],[placeholder],[title]")) {
      if (el.closest(skip) || !el.getClientRects().length) continue;
      for (const a of ["aria-label", "placeholder", "title"]) add(el.getAttribute(a), el, a);
    }
    return out;
  });
  // a message of the graph is written by the kernel as "<role>: <its text>" (a breadcrumb, a card title, a row): the person's words, not the app's
  return lines.filter(x => !/^(?:Ty|Model|Asystent|System): /.test(x.line)).map(x => ({ ...x, why: looksLikeDeveloperText(x.line) })).filter(x => x.why);
}
