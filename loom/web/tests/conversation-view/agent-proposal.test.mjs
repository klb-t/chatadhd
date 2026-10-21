// R38: the proposals of an agent run reach the page in the shape the server stores them.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const ap = await loader.load('src/context/agent-proposal.ts');

const proposals = [{ action: 'usage.compact', arguments: { dry_run: true }, run_id: 'ar_49473dc81acd', status: 'proposed', tool_call_id: 'call_smoke_1', element: '' }];
const loop = { status: 'needs_confirmation', loop_id: 'al_1', proposals, messages: [{ role: 'user', content: 'x' }] };

test('an agent run started from the chat: the router stores the loop under action_run (route.cpp report_of), and the page finds it', () => {
  // the metadata of the stored message as the server writes it (checked against loom-server: keys action_run.{run_id,status,lang,agent_loop}, routed)
  const metadata = { action_run: { run_id: 'ar_loop', status: 'completed', lang: 'pl', agent_loop: loop }, routed: 'result' };
  const view = ap.agentLoopView(metadata);
  assert.ok(view, 'the proposals block must show for a run stopped on proposals');
  assert.deepEqual(view.proposals.map(p => [p.runId, p.action, p.arguments]), [['ar_49473dc81acd', 'usage.compact', '{"dry_run":true}']]);
  assert.equal(ap.agentLoopOf(metadata), loop);
});

test('once answered, the copy the page writes at the top wins over the router copy', () => {
  const answered = { ...loop, status: 'answered', proposals: [] };
  const metadata = { agent_loop: answered, action_run: { status: 'completed', agent_loop: loop } };
  assert.equal(ap.agentLoopOf(metadata), answered);
  assert.equal(ap.agentLoopView(metadata), null, 'nothing left to answer');
  assert.equal(ap.agentLoopView({ action_run: { status: 'completed' } }), null);
  assert.equal(ap.agentLoopView(undefined), null);
});
