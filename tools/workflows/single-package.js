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
const A = args
const TRAILER = 'Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_015yMVYJqXDd8sNSZdYq3gu5'
const PUSH = A.remote_branch
  ? `Push with: git push origin HEAD:${A.remote_branch} (never force; if it is not a fast-forward, fetch and merge first).${A.ci_workflow ? ` After a push read the CI run for your commit (workflow ${A.ci_workflow}; GitHub MCP tools via ToolSearch select:mcp__github__actions_list,mcp__github__actions_get,mcp__github__get_job_logs); if it fails, fix and push again, at most 3 pushes.` : ''}`
  : 'Do not push.'
const RULES = `Repository rules that apply (CLAUDE.md is injected): everything configurable, defaults are presets, no invented limits, nothing hidden silently, EXAMPLES ARE EXAMPLES (owner 2026-10-23: a concrete assignment such as which visual channel carries which measure is a labelled example or a default preset in data, never a requirement and never attributed to the owner; the engine handles every configuration including useless ones; a little-useful or degenerate configuration raises a non-blocking advisory that asks whether it is intended and offers 'do not ask next time'), Polish plain labels for the owner (a non-engineer on a phone), texts tell what the code does TODAY. Work ONLY in the git worktree ${A.wt} (local branch ${A.branch}); commit after every meaningful piece, commit messages end with:\n${TRAILER}\nNever touch /home/user/chatadhd or other worktrees. Keep context small (grep, sed -n ranges, never dump big files). CACHE RULE (measured 2026-10-21: 17 % of agent spend was whole contexts rewritten after a pause longer than 5 minutes): never let one tool call block for more than about 4 minutes - start builds, test suites and CI waits with run_in_background (or poll with short calls of at most 3 minutes), so the next model turn follows within 5 minutes. ${PUSH} ${A.rules_extra || ''} Report facts: what you ran, results, what you did not do.`
const DISCIPLINE = `PACKAGE DISCIPLINE (owner's waste-control rule, 2026-10-21; cost grows with the square of an agent's length): this is ONE small work package. Do only the scope below; read only what it needs; commit after every piece; stop after about 50 tool calls or when the acceptance holds, whichever comes first, and return the resume card (done=false plus exactly what is left, if you stop early). Anything that matters is in a commit or in the card.`
const CARD = { type: 'object', properties: {
  done: { type: 'boolean' }, commits: { type: 'array', items: { type: 'string' } },
  ci: { type: 'string', description: 'CI run id and conclusion for the last pushed commit, or "not pushed" / "no CI configured"' },
  summary: { type: 'string' }, decisions: { type: 'array', items: { type: 'string' }, description: 'decisions and how to reverse them' },
  not_done: { type: 'array', items: { type: 'string' } },
}, required: ['done', 'commits', 'ci', 'summary', 'not_done'] }
phase('Package')
const cards = []
for (let attempt = 1; attempt <= 2; attempt++) {
  const earlier = cards.map(c => `done=${c.done}; commits ${c.commits.join(', ') || '-'}; CI ${c.ci}; ${String(c.summary).slice(0, 500)} | NOT DONE: ${c.not_done.join('; ').slice(0, 500)}`).join('\n')
  const card = await agent(`You implement one small work package. ${RULES}\n\n${DISCIPLINE}\n\nPACKAGE ${A.id}${attempt > 1 ? ' — continuation: the previous agent stopped early; read its card below and git log, finish only what is NOT DONE' : ''}\nScope: ${A.scope}\nAcceptance: ${A.accept}\n${earlier ? `\nPrevious card:\n${earlier}\n` : ''}\nReturn the resume card.`, { label: `${A.id}${attempt > 1 ? ':c1' : ''}`, phase: 'Package', schema: CARD, ...(A.model ? { model: A.model } : {}) })
  if (!card) { log(`${A.id}: agent returned nothing (attempt ${attempt})`); continue }
  cards.push(card)
  log(`${A.id} attempt ${attempt}: done=${card.done}, CI ${card.ci}`)
  if (card.done) break
}
return { id: A.id, done: cards.some(c => c.done), cards }
