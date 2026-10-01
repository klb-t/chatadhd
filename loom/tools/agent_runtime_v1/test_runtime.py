import base64
import fcntl
from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
import hashlib
import json
import os
import os
from pathlib import Path
import sys
import signal
import tempfile
import time
import unittest

from .adapters import graph_tool, process_tool
from .experiments import (compose, fault_probe, budget_probe, connect_app,
                         graph_arguments, local_environment, run_agent)
from .runtime import AgentError, AgentRuntime, Environment, Tool, observed
from ..coordination.leases import digest


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.effects = []

    def tool(self, invoke=None, *, output_schema=None, capabilities=()):
        def default(environment, args):
            self.effects.append(deepcopy(args))
            return observed({'value': args['value']})
        return Tool('fixture.tool', '1', {'type': 'object', 'additionalProperties': False,
            'required': ['value'], 'properties': {'value': {'type': 'integer'}}},
            output_schema or {'type': 'object'}, capabilities,
            {'kind': 'synthetic_effect'}, invoke or default, {})

    def planner(self, context):
        return {'kind': 'action', 'tool': 'fixture.tool', 'arguments': {'value': 7}} \
            if not context['observations'] else {'kind': 'final', 'value': context['observations'][0]['result']}

    def test_three_adapters_compose_observed_data_and_replay_without_dispatch(self):
        result = compose(self.path)
        self.assertEqual(result['result']['state'], 'completed')
        self.assertEqual(len(result['result']['observations']), 3)
        self.assertEqual(result['result']['value']['temperature']['unit'], 'degC')
        self.assertTrue(result['replay_equal'])
        self.assertFalse(result['result']['value']['canonical_store_written'])

    def test_graph_preview_retains_source_bytes_and_domain_truth_status(self):
        args = graph_arguments()
        output = graph_tool().invoke(local_environment(self.path), args)['payload']
        self.assertEqual(output['selected_packet'], args['packet'])
        self.assertEqual(output['preview']['candidate_packet']['sources'], args['packet']['sources'])
        self.assertFalse(output['application']['acceptance_establishes_content_truth'])

    def test_pinned_unavailable_environment_does_not_fallback_or_plan(self):
        runtime = AgentRuntime(self.path, tools=[self.tool()],
            environments=[local_environment(self.path)], source_commit='a' * 40)
        result = runtime.run(session_id='missing', goal={}, environment_id='chosen-vm',
            planner_id='fixture', planner=lambda _: self.fail('planned'), max_steps=2,
            lease_seconds=10, owner='test')
        self.assertEqual(result['state'], 'unavailable')
        self.assertEqual(result['dispatch_count'], 0)
        self.assertEqual(self.effects, [])

    def test_physical_device_capability_is_unavailable_without_fake_action(self):
        _, result = run_agent(self.path, [self.tool(capabilities=('physical_device',))], self.planner)
        self.assertEqual(result['state'], 'unavailable')
        self.assertEqual(self.effects, [])

    def test_invalid_arguments_do_not_dispatch_or_coerce_units(self):
        _, result = run_agent(self.path, [self.tool()], lambda _: {
            'kind': 'action', 'tool': 'fixture.tool', 'arguments': {'value': '7'}})
        self.assertEqual(result['state'], 'unavailable')
        self.assertEqual(self.effects, [])
        self.assertTrue(list(self.path.glob('*/step-*/planner_first.json')))

    def test_bad_planner_schema_is_retained_and_not_replanned(self):
        planner = lambda _: {'kind': 'action', 'tool': 'fixture.tool', 'arguments': {}, 'unbound': True}
        runtime, first = run_agent(self.path, [self.tool()], planner)
        _, again = run_agent(self.path, [self.tool()], lambda _: self.fail('replanned'))
        self.assertEqual(first, again)
        self.assertEqual(first['state'], 'unavailable')

    def test_tool_catalog_revision_change_rejects_existing_session(self):
        run_agent(self.path, [self.tool()], self.planner)
        tool = self.tool()
        changed = Tool(tool.id, '2', tool.input_schema, tool.output_schema,
                       tool.required_capabilities, tool.effects, tool.invoke, tool.metadata)
        with self.assertRaisesRegex(AgentError, 'session_binding_changed'):
            run_agent(self.path, [changed], self.planner)
        self.assertEqual(len(self.effects), 1)

    def test_environment_config_change_rejects_existing_session(self):
        run_agent(self.path, [self.tool()], self.planner)
        with self.assertRaisesRegex(AgentError, 'session_binding_changed'):
            run_agent(self.path, [self.tool()], self.planner,
                      environment=local_environment(self.path, capture=12))

    def test_new_application_tool_does_not_require_loop_change(self):
        tools = [self.tool()]
        extra = Tool('other-app.echo', '1', {'type': 'object'}, {'type': 'object'}, (),
                     {'kind': 'read'}, lambda env, args: observed(args), {'domain': 'arbitrary'})
        _, result = run_agent(self.path, tools + [extra], lambda ctx: {
            'kind': 'action', 'tool': extra.id, 'arguments': {'label': 'żaba', 'unit': 'K'}}
            if not ctx['observations'] else {'kind': 'final', 'value': ctx['observations'][0]['result']})
        self.assertEqual(result['value']['payload'], {'label': 'żaba', 'unit': 'K'})

    def test_step_limit_stops_effects(self):
        _, result = run_agent(self.path, [self.tool()], lambda _: {
            'kind': 'action', 'tool': 'fixture.tool', 'arguments': {'value': 1}}, steps=2)
        self.assertEqual(result['state'], 'step_limit_reached')
        self.assertEqual(len(self.effects), 2)

    def test_concurrent_same_session_dispatches_once(self):
        def call():
            return run_agent(self.path, [self.tool()], self.planner)[1]
        # Initialize the durable store before concurrency (construction is not
        # itself the scheduling interface).
        AgentRuntime(self.path, tools=[], environments=[], source_commit='a' * 40)
        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(lambda _: call(), range(2)))
        self.assertEqual(results[0], results[1])
        self.assertEqual(len(self.effects), 1)

    def test_invalid_first_tool_output_is_retained_blocks_retry(self):
        def bad(environment, args):
            self.effects.append(args)
            return observed({'unit': 'K'})
        tool = self.tool(bad, output_schema={'type': 'object', 'required': ['unit'],
            'properties': {'unit': {'const': 'degC'}}})
        _, first = run_agent(self.path, [tool], self.planner)
        _, again = run_agent(self.path, [tool], self.planner)
        self.assertEqual(first['reason'], 'outcome_unknown')
        self.assertEqual(again['reason'], first['reason'])
        self.assertEqual(len(self.effects), 1)
        returned = json.loads(next(self.path.glob('*/step-*/tool_return_first.json')).read_text())
        self.assertEqual(returned['payload']['unit'], 'K')

    def test_unknown_tool_effect_blocks_next_action(self):
        tool = self.tool(lambda env, args: observed({'partial': True}, 'outcome_unknown'))
        _, result = run_agent(self.path, [tool], self.planner)
        self.assertEqual(result['reason'], 'tool_outcome_unknown')
        self.assertEqual(len(result['observations']), 1)

    def test_tool_error_is_an_observation_planner_can_handle(self):
        tool = self.tool(lambda env, args: observed({'failure': 'declared'}, 'tool_error'))
        _, result = run_agent(self.path, [tool], self.planner)
        self.assertEqual(result['state'], 'completed')
        self.assertEqual(result['value']['execution_status'], 'tool_error')

    def test_real_kill_before_effect_does_not_retry(self):
        self.assertEqual(fault_probe(self.path, phase='before_effect')['effect_count'], 0)

    def test_real_kill_after_effect_does_not_duplicate(self):
        self.assertEqual(fault_probe(self.path, phase='after_effect')['effect_count'], 1)

    def test_parent_budget_denial_prevents_agent_dispatch(self):
        result = budget_probe(self.path)
        self.assertEqual(result['callback_dispatches'], 1)
        self.assertFalse(result['nested_budget_transfer'])

    def test_process_preserves_non_utf8_stdout_and_stderr(self):
        result = process_tool().invoke(local_environment(self.path), {'argv': [sys.executable,
            '-c', 'import os;os.write(1,b"\\xff\\x00");os.write(2,b"err")']})['payload']
        self.assertEqual(base64.b64decode(result['stdout']['prefix_base64']), b'\xff\x00')
        self.assertEqual(result['stdout']['sha256'], hashlib.sha256(b'\xff\x00').hexdigest())
        self.assertEqual(base64.b64decode(result['stderr']['prefix_base64']), b'err')

    def test_process_truncation_declares_loss_and_full_hash(self):
        result = process_tool().invoke(local_environment(self.path, capture=3), {'argv':
            [sys.executable, '-c', 'print("abcdef",end="")']})['payload']['stdout']
        self.assertEqual(base64.b64decode(result['prefix_base64']), b'abc')
        self.assertTrue(result['truncated'])
        self.assertFalse(result['reversible'])
        self.assertEqual(result['byte_count'], 6)
        self.assertEqual(result['sha256'], hashlib.sha256(b'abcdef').hexdigest())

    def test_process_nonzero_retains_output_as_tool_error(self):
        result = process_tool().invoke(local_environment(self.path), {'argv':
            [sys.executable, '-c', 'import sys;print("partial");sys.exit(7)']})
        self.assertEqual(result['execution_status'], 'tool_error')
        self.assertEqual(result['payload']['returncode'], 7)

    def test_process_timeout_cannot_claim_external_effect_rollback(self):
        result = process_tool().invoke(local_environment(self.path, timeout=.1), {'argv':
            [sys.executable, '-c', 'import time;print("partial",flush=True);time.sleep(30)']})
        self.assertEqual(result['execution_status'], 'outcome_unknown')
        self.assertTrue(result['payload']['timed_out'])

    def test_process_group_cleanup_includes_child_after_leader_exit(self):
        result = process_tool().invoke(local_environment(self.path), {'argv': [sys.executable,
            '-c', 'import subprocess,sys; child=subprocess.Popen([sys.executable,"-c",'
                  '"import time;time.sleep(30)"]); print(child.pid,flush=True)']})
        pid = int(base64.b64decode(result['payload']['stdout']['prefix_base64']))
        status = Path('/proc') / str(pid) / 'stat'
        deadline = time.monotonic() + 1
        while status.exists() and status.read_text().split()[2] != 'Z':
            if time.monotonic() > deadline:
                self.fail('child_survived_group_cleanup')
            time.sleep(.01)

    def test_cwd_does_not_supply_security_isolation(self):
        result = process_tool().invoke(local_environment(self.path), {'argv': [sys.executable,
            '-c', 'import pathlib;print(pathlib.Path("..").resolve().is_dir())']})
        self.assertEqual(base64.b64decode(result['payload']['stdout']['prefix_base64']), b'True\n')
        self.assertEqual(result['payload']['isolation'], 'none_host_process_with_explicit_cwd')


class MCPTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)

    def run_mcp(self, mode):
        app = connect_app(mode)
        self.addCleanup(app.close)
        def planner(ctx):
            return {'kind': 'action', 'tool': 'app.temperature', 'arguments': {'sensor': 'fixture'}} \
                if not ctx['observations'] else {'kind': 'final', 'value': ctx['observations'][0]['result']}
        return app, run_agent(self.path, app.tools(namespace='app'), planner)[1]

    def test_mcp_schema_custom_domain_fields_and_unit_are_preserved(self):
        app, result = self.run_mcp('normal')
        self.assertEqual(result['value']['payload']['structuredContent']['unit'], 'degC')
        self.assertEqual(app.tools(namespace='app')[0].metadata['mcp_descriptor']['x-domain'],
                         {'quantity': 'temperature'})
        self.assertTrue(any(json.loads(base64.b64decode(f['base64'])).get('method') ==
            'notifications/initialized' for f in app.transcript))

    def test_mcp_unit_mismatch_retains_first_output_and_blocks(self):
        app, result = self.run_mcp('bad-output')
        self.assertEqual(result['reason'], 'outcome_unknown')
        returned = json.loads(next(self.path.glob('*/step-*/tool_return_first.json')).read_text())
        self.assertEqual(returned['payload']['structuredContent']['unit'], 'K')

    def test_mcp_declared_tool_error_is_distinct_from_protocol_error(self):
        _, result = self.run_mcp('tool-error')
        self.assertEqual(result['value']['execution_status'], 'tool_error')

    def test_mcp_protocol_error_does_not_trigger_retry(self):
        app, result = self.run_mcp('protocol-error')
        self.assertEqual(result['state'], 'blocked')
        calls = [f for f in app.transcript if f['direction'] == 'sent' and
            json.loads(base64.b64decode(f['base64'])).get('method') == 'tools/call']
        self.assertEqual(len(calls), 1)

    def test_mcp_changed_descriptor_prevents_tool_call(self):
        app, result = self.run_mcp('change-catalog')
        self.assertEqual(result['state'], 'blocked')
        methods = [json.loads(base64.b64decode(f['base64'])).get('method')
                   for f in app.transcript if f['direction'] == 'sent']
        self.assertNotIn('tools/call', methods)

    def test_mcp_unsupported_version_is_visible(self):
        with self.assertRaisesRegex(AgentError, 'mcp_version_or_tools_capability_unavailable'):
            connect_app('bad-version')

    def test_mcp_missing_capability_is_visible(self):
        with self.assertRaisesRegex(AgentError, 'mcp_version_or_tools_capability_unavailable'):
            connect_app('no-tools')

    def test_mcp_silent_server_times_out_and_is_closed(self):
        with self.assertRaisesRegex(AgentError, 'mcp_timeout_effect_unknown'):
            connect_app('silent', timeout=.1)

    def test_mcp_outbound_frame_limit_is_explicit(self):
        app = connect_app()
        self.addCleanup(app.close)
        with self.assertRaisesRegex(AgentError, 'mcp_outbound_frame_limit'):
            app.request('tools/call', {'large': 'x' * 65536})

    def test_mcp_server_not_reading_cannot_block_outbound_forever(self):
        app = connect_app()
        self.addCleanup(app.close)
        app.timeout = .1
        os.kill(app.process.pid, signal.SIGSTOP)
        capacity = fcntl.fcntl(app.process.stdin.fileno(), fcntl.F_GETPIPE_SZ)
        app.max_frame = capacity * 4
        with self.assertRaisesRegex(AgentError, 'mcp_send_timeout_effect_unknown'):
            app.request('tools/call', {'large': 'x' * (capacity * 2)})


if __name__ == '__main__':
    unittest.main()
