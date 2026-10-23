export const meta = {
  name: 'check-before-use',
  description: 'Coordinator rule 2026-10-23: every design document and brief gets a cheap read-only check (Haiku, 4 anti-patterns + R60) before it is used; findings are claims to verify; fixes run on Sonnet as an addendum, never as a restart',
  phases: [
    { title: 'Kontrola', detail: 'Haiku, tylko odczyt: 4 anty-wzorce + pytanie R60, znaleziska z cytatem i linią' },
    { title: 'Poprawki', detail: 'Sonnet: zastosować potwierdzone znaleziska, zapisać odrzucone z powodem, commit' },
  ],
}
// args: { docs: [repo-relative paths], wt, remote_branch }
const A = args
const TRAILER = (m) => `Co-Authored-By: ${m} <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_015yMVYJqXDd8sNSZdYq3gu5`
const FINDINGS = { type: 'object', properties: {
  doc: { type: 'string' },
  findings: { type: 'array', items: { type: 'object', properties: {
    line: { type: 'integer' }, quote: { type: 'string', description: 'verbatim fragment, at most 200 chars' },
    pattern: { type: 'string', description: 'A choice-as-requirement | B attributed-to-owner-without-quote | C option-demoted-for-cost-or-usefulness | D present-fact-as-constraint | E unverified-fact-stated-as-fact | F closed-list-or-number-as-the-list (R83/R84) | R60 absence-today-as-forever' },
    severity: { type: 'string', description: 'blocking | wording' },
    why: { type: 'string' }, fix: { type: 'string', description: 'the smallest rewording that repairs it' },
  }, required: ['line', 'quote', 'pattern', 'severity', 'why', 'fix'] } },
  r60_answer: { type: 'string', description: 'Does the document anywhere drop or demote an option because something does not exist today? Quote or "nie".' },
  verdict: { type: 'string', description: 'usable as is | usable after fixes | not usable (why)' },
}, required: ['doc', 'findings', 'r60_answer', 'verdict'] }
const FIXCARD = { type: 'object', properties: { doc: { type: 'string' }, applied: { type: 'integer' }, rejected: { type: 'array', items: { type: 'string' } }, commit: { type: 'string' }, summary: { type: 'string' } }, required: ['doc', 'applied', 'rejected', 'commit', 'summary'] }

phase('Kontrola')
const checks = (await parallel(A.docs.map(d => () => agent(
  `READ-ONLY CHECK (coordinator rule 'Check before use', CLAUDE.md section 'Examples are examples'). Document: ${A.wt}/${d}. Read it whole (sed -n ranges of ~150 lines). Report every place that breaks the owner's rules: A) a concrete choice written as a requirement ('must', 'always', 'the only', fixed values) that is not marked 'przykład' or 'preset'; B) anything attributed to the owner ('jak mówił', 'właściciel chce/wymaga', 'zgodnie z życzeniem') without his verbatim quote or an R-id citation; C) an option dropped, demoted or 'not worth it' because of cost, usefulness or rarity (the philosophy: every configuration is served, little usefulness is an advisory, not a refusal); D) a fact about the present (a tool's feature, a provider's limit, a price, 'nobody uses') used as a constraint instead of a dated observation; E) a fact the document itself calls unverified yet elsewhere states as fact; F) a closed list or a number presented as the list ('two knobs', 'three positions', an enum without a recorded reason and date — R83/R84). Then answer R60: does the document anywhere drop or demote an option because something does not exist today? Severity: blocking = would make a package implement a limit or a false attribution; wording = a sentence to soften. Quote verbatim with the line number; propose the smallest fix. Do not edit anything. Keep it under 40 findings; if more, keep the blocking ones and say how many wording ones you skipped.`,
  { label: `kontrola:${d.split('/').pop()}`, phase: 'Kontrola', model: 'haiku', schema: FINDINGS })))).filter(Boolean)
for (const c of checks) log(`${c.doc}: ${c.findings.filter(f => f.severity === 'blocking').length} blocking, ${c.findings.length} total — ${c.verdict}`)

phase('Poprawki')
const fixes = await parallel(checks.filter(c => c.findings.length > 0).map(c => () => agent(
  `ADDENDUM FIX (Sonnet; never a restart). Document: ${A.wt}/${c.doc} in worktree ${A.wt} (branch pushed to ${A.remote_branch}). A Haiku read-only check produced the findings below — they are CLAIMS to verify, not orders: for each, read the lines, decide if the rule is really broken (CLAUDE.md 'Examples are examples', R58–R60, R83, R84), apply the smallest rewording when it is (mark examples 'przykład', values 'preset', present facts with a date and 'obserwacja', remove owner attributions without a quote or replace them with the R-id, open closed lists), and record the rejected ones with a reason in a short 'Kontrola przed użyciem (${A.wt.split('/').pop()}, data)' section at the end of the document (applied count, rejected with why). Do not change the substance of decisions; do not add scope. Commit only this file (git add that path; retry on .git/index.lock) with message 'kontrola przed użyciem: ${c.doc.split('/').pop()} — n poprawek' ending with:\n${TRAILER('Claude Sonnet 5.5')}\nand push: git push origin HEAD:${A.remote_branch} (never force; on rejection git pull --rebase origin ${A.remote_branch} then push).\nFINDINGS (JSON): ${JSON.stringify(c.findings)}\nR60 answer: ${c.r60_answer}\nVerdict: ${c.verdict}`,
  { label: `poprawki:${c.doc.split('/').pop()}`, phase: 'Poprawki', model: 'sonnet', schema: FIXCARD })))
return { checks: checks.map(c => ({ doc: c.doc, blocking: c.findings.filter(f => f.severity === 'blocking').length, total: c.findings.length, verdict: c.verdict, r60: c.r60_answer })), fixes: fixes.filter(Boolean) }
