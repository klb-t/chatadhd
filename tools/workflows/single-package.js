export const meta = {
  name: 'single-package',
  description: 'One small checkpointed work package in a git worktree (about 50 tool calls, commit + resume card), optionally pushed and checked by a CI workflow; reusable: the brief, scope and acceptance come in args',
  phases: [
    { title: 'Package', detail: 'one agent, one coherent piece, one resume card; a continuation agent if it stopped early' },
  ],
}
// args: { wt, branch, base_note?, remote_branch?, ci_workflow?, id, scope, accept, rules_extra?, model? }
//  wt            git worktree the agent works in (it must exist and be on a local branch)
//  branch        the local branch name in that worktree (informational)
//  remote_branch where the agent may push (git push origin HEAD:<remote_branch>), omit = "do not push"
//  ci_workflow   workflow file whose run for the pushed commit the agent must read (e.g. xport-apk.yml)
//  id, scope, accept   the package: what to do, how it is accepted
//  rules_extra   repo/area specific rules appended to the common ones
//  model         optional model override for the agent ('fable' for hard design/judgement packages)
//  run_id        optional run record (IW-1, loom.routine_run/1) the coordinator opened BEFORE the workflow with
//                `python3 tools/run_record.py start --routine package.run --param id=<id> ...`; the agent appends
//                steps/events to it and returns it in the card; after the workflow the coordinator runs
//                `python3 tools/run_record.py import-card <card.json> --run-id <run_id>` (0 tokens)
//  runs_root     directory under which docs/reports/runs lives (the documents-branch checkout); omit = the agent's worktree
//  context_pack, context_prefix, context_pack_hash   (IW-2) shared prefix of the brief: the coordinator runs
//                `python3 tools/context_pack.py fill-brief <brief.json> -o <filled.json>` (0 tokens) which renders the pack
//                named in context_pack into context_prefix + its sha256; the prompt STARTS with the prefix byte for byte
//                (same pack = same bytes = cacheable prefix), the per-package part follows. Check first (advisory):
//                `python3 tools/context_pack.py check-brief <filled.json>`
//  spec          (IW-2) list of spec ids the package implements (spec:<id>, read with `python3 tools/spec.py show <id>`)
const A = args
const MODEL_NAMES = { fable: 'Claude Fable 5.1', opus: 'Claude Opus 5.5', sonnet: 'Claude Sonnet 5.5', haiku: 'Claude Haiku 5.5' }
// The trailer names the model the AGENT runs on (the model override, else the session default Sonnet), not the coordinator.
const TRAILER = `Co-Authored-By: ${MODEL_NAMES[A.model] || MODEL_NAMES.sonnet} <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_015yMVYJqXDd8sNSZdYq3gu5`
const PUSH = A.remote_branch
  ? `Push with: git push origin HEAD:${A.remote_branch} (never force; if it is not a fast-forward, fetch and merge first).${A.ci_workflow ? ` After a push read the CI run for your commit (workflow ${A.ci_workflow}; GitHub MCP tools via ToolSearch select:mcp__github__actions_list,mcp__github__actions_get,mcp__github__get_job_logs); if it fails, fix and push again, at most 3 pushes.` : ''}`
  : 'Do not push.'
const RULES = `Repository rules that apply (CLAUDE.md is injected): everything configurable, defaults are presets, no invented limits, nothing hidden silently, EXAMPLES ARE EXAMPLES (owner 2026-10-23: a concrete assignment such as which visual channel carries which measure is a labelled example or a default preset in data, never a requirement and never attributed to the owner; the engine handles every configuration including useless ones; a little-useful or degenerate configuration raises a non-blocking advisory that asks whether it is intended and offers 'do not ask next time'; THIS BRIEF IS A LIST OF EXAMPLES TOO: everything concrete in it - values, lists, names, options, orderings - is an example of what the engine and the abstraction must support, so build the general mechanism with the brief's case as one instance and one test; examples are necessary but insufficient, cover the possibility space (generative/property tests where feasible) and report what stays uncovered as a gap, not as out of scope; information from the web: official documentation first, then reliable forums, and only without those the data analysis supported by weaker sources and testing, test outcomes updating the reliability of the source and author, time-dependent), Polish plain labels for the owner (a non-engineer on a phone), texts tell what the code does TODAY. Work ONLY in the git worktree ${A.wt} (local branch ${A.branch}); commit after every meaningful piece, commit messages end with:\n${TRAILER}\nNever touch /home/user/chatadhd or other worktrees. Keep context small (grep, sed -n ranges, never dump big files). CACHE RULE (measured 2026-10-21: 17 % of agent spend was whole contexts rewritten after a pause longer than 5 minutes): never let one tool call block for more than about 4 minutes - start builds, test suites and CI waits with run_in_background (or poll with short calls of at most 3 minutes), so the next model turn follows within 5 minutes. ${PUSH} ${A.rules_extra || ''} Report facts: what you ran, results, what you did not do.`
const RUNREC = A.run_id
  ? `RUN RECORD (IW-1, 0 tokens): this package has the run record ${A.run_id}. Use python3 tools/run_record.py${A.runs_root ? ` --root ${A.runs_root}` : ''} with: step ${A.run_id} --id <piece> --state done (one per meaningful piece, after its commit); event ${A.run_id} --kind building|failed|original_ideas --text "<one line>" (original_ideas only for genuinely new ideas); decision ${A.run_id} --text ... --reversal .... Do NOT call finish: the coordinator imports your card. Return run_id in the card.`
  : ''
// The shared prefix goes first and is never interpolated with per-package values (its hash proves identical bytes).
const PREFIX = A.context_prefix ? `${A.context_prefix}\n` : ''
const SPEC = (A.spec && A.spec.length)
  ? `Spec items this package implements: ${A.spec.map(s => `spec:${String(s).replace(/^spec:/, '')}`).join(' ')} — read each with python3 tools/spec.py show <id> before deciding; cite them in commits and the card.`
  : ''
const DISCIPLINE = `PACKAGE DISCIPLINE (owner's waste-control rule, 2026-10-21; cost grows with the square of an agent's length): this is ONE small work package. Do only the scope below; read only what it needs; commit after every piece; stop after about 50 tool calls or when the acceptance holds, whichever comes first, and return the resume card (done=false plus exactly what is left, if you stop early). Anything that matters is in a commit or in the card.`
const CARD = { type: 'object', properties: {
  done: { type: 'boolean' }, commits: { type: 'array', items: { type: 'string' } },
  ci: { type: 'string', description: 'CI run id and conclusion for the last pushed commit, or "not pushed" / "no CI configured"' },
  summary: { type: 'string' }, decisions: { type: 'array', items: { type: 'string' }, description: 'decisions and how to reverse them' },
  not_done: { type: 'array', items: { type: 'string' } },
  run_id: { type: 'string', description: 'the run record id from the brief, if any' },
  original_ideas: { type: 'array', items: { type: 'string' }, description: 'genuinely original ideas, if any' },
  context_pack_hash: { type: 'string', description: 'the context_pack_hash from the brief, copied verbatim, if any' },
  spec: { type: 'array', items: { type: 'string' }, description: 'spec ids the work implements' },
}, required: ['done', 'commits', 'ci', 'summary', 'not_done'] }
phase('Package')
const cards = []
for (let attempt = 1; attempt <= 2; attempt++) {
  const earlier = cards.map(c => `done=${c.done}; commits ${c.commits.join(', ') || '-'}; CI ${c.ci}; ${String(c.summary).slice(0, 500)} | NOT DONE: ${c.not_done.join('; ').slice(0, 500)}`).join('\n')
  const card = await agent(`${PREFIX}You implement one small work package. ${RULES}\n\n${DISCIPLINE}${RUNREC ? `\n\n${RUNREC}` : ''}\n\nPACKAGE ${A.id}${attempt > 1 ? ' — continuation: the previous agent stopped early; read its card below and git log, finish only what is NOT DONE' : ''}\nScope: ${A.scope}\nAcceptance: ${A.accept}\n${SPEC ? `${SPEC}\n` : ''}${A.context_pack_hash ? `Context pack hash (copy into the card): ${A.context_pack_hash}\n` : ''}${earlier ? `\nPrevious card:\n${earlier}\n` : ''}\nReturn the resume card.`, { label: `${A.id}${attempt > 1 ? ':c1' : ''}`, phase: 'Package', schema: CARD, ...(A.model ? { model: A.model } : {}) })
  if (!card) { log(`${A.id}: agent returned nothing (attempt ${attempt})`); continue }
  cards.push(card)
  log(`${A.id} attempt ${attempt}: done=${card.done}, CI ${card.ci}`)
  if (card.done) break
}
return { id: A.id, done: cards.some(c => c.done), cards }
