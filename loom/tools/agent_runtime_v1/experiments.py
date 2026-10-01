"""Reproducible synthetic multitrack probes, preserving separate first outputs."""
import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
import json
import hashlib
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import tempfile
import time

from .runtime import AgentRuntime, Environment, Tool, observed
from .adapters import MCPStdio, graph_tool, process_tool
from ..coordination.leases import digest
from ..contracts import analysis_plan_ref as plans

REPO = Path(__file__).resolve().parents[3]
EXAMPLES = REPO / 'loom/tools/coordination/examples'
FIXTURE_APP = Path(__file__).with_name('fixture_app.py')


def source_commit():
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip()


def local_environment(cwd, *, timeout=2, capture=65536, capabilities=None):
    return Environment('user-selected-local', 'local_process',
        tuple(capabilities if capabilities is not None else
              ('python_callback', 'os_process', 'mcp_stdio')),
        {'cwd': str(Path(cwd).resolve()), 'env': {},
         'timeout_seconds': timeout, 'capture_bytes': capture},
        'none_host_process_with_explicit_cwd')


def connect_app(mode='normal', *, timeout=2):
    return MCPStdio([sys.executable, str(FIXTURE_APP), '--mode', mode],
        cwd=REPO, env={}, timeout_seconds=timeout, max_frame_bytes=65536)


def graph_arguments():
    plan = json.loads((EXAMPLES / 'graph-review-plan.json').read_text())
    packet = json.loads((EXAMPLES / 'graph-review-packet.json').read_text())
    config = plan['methods'][0]['config']
    return {'packet': packet, 'diff': config['diff'], 'policy': config['policy'],
            'explicitly_accepted': False}


def run_agent(directory, tools, planner, *, session='probe', environment=None,
              steps=8, commit=None, lease=10):
    environment = environment or local_environment(directory)
    runtime = AgentRuntime(directory, tools=tools, environments=[environment],
                           source_commit=commit or source_commit())
    result = runtime.run(session_id=session, goal={'operation': 'synthetic_mechanism_probe'},
        environment_id=environment.id, planner_id='deterministic-observation-planner/1',
        planner=planner, max_steps=steps, lease_seconds=lease, owner='experiment')
    return runtime, result


def compose(directory):
    app = connect_app()
    try:
        def planner(context):
            observations = context['observations']
            if not observations:
                return {'kind': 'action', 'tool': 'loom.graph.apply_diff', 'arguments': graph_arguments()}
            if len(observations) == 1:
                graph = observations[0]['result']['payload']
                assert graph['application']['accepted'] is False
                return {'kind': 'action', 'tool': 'application.temperature',
                        'arguments': {'sensor': 'fixture'}}
            if len(observations) == 2:
                value = observations[-1]['result']['payload']['structuredContent']
                # Next arguments depend on observed domain data, not a macro.
                return {'kind': 'action', 'tool': 'system.process', 'arguments': {'argv':
                    [sys.executable, '-c', 'import sys;sys.stdout.write(sys.argv[1])',
                     json.dumps(value, ensure_ascii=False)]}}
            payload = observations[-1]['result']['payload']
            value = json.loads(base64.b64decode(payload['stdout']['prefix_base64']))
            return {'kind': 'final', 'value': {'temperature': value,
                'graph_preview_packet': observations[0]['result']['payload']['preview']['candidate_packet'],
                'canonical_store_written': False, 'model_planning_quality_measured': False}}
        runtime, result = run_agent(directory, [graph_tool(), process_tool()] +
                                  app.tools(namespace='application'), planner)
        repeat = runtime.run(session_id='probe', goal={'operation': 'synthetic_mechanism_probe'},
            environment_id='user-selected-local', planner_id='deterministic-observation-planner/1',
            planner=lambda _: (_ for _ in ()).throw(AssertionError('unexpected_replanning')),
            max_steps=8, lease_seconds=10, owner='restart')
        assert repeat == result
        return {'result': result, 'replay_equal': True, 'mcp_frames': deepcopy(app.transcript),
                'paid_provider_calls': 0, 'cloud_provisioning_calls': 0}
    finally:
        app.close()


def process_boundaries(directory):
    results = {}
    cases = {
        'raw_bytes': ('import os;os.write(1,b"\\xff\\x00\\n");os.write(2,b"err")', 2, 32),
        'nonzero': ('import sys;print("partial");sys.exit(7)', 2, 32),
        'timeout': ('import time;print("before",flush=True);time.sleep(30)', .15, 32),
        'truncated': ('import os;os.write(1,b"abcdef")', 2, 3),
        'cwd_not_sandbox': ('import pathlib;print(pathlib.Path("..").resolve().is_dir())', 2, 32)}
    for name, (code, timeout, capture) in cases.items():
        path = Path(directory) / name
        path.mkdir()
        def planner(context, code=code):
            return {'kind': 'action', 'tool': 'system.process',
                    'arguments': {'argv': [sys.executable, '-c', code]}} if not context['observations'] else {
                        'kind': 'final', 'value': context['observations'][-1]['result']}
        _, results[name] = run_agent(path, [process_tool()], planner,
            environment=local_environment(path, timeout=timeout, capture=capture))
    return results


def fault_probe(directory, *, phase):
    path, commit = Path(directory), source_commit()
    path.mkdir(parents=True, exist_ok=True)
    counter = path / 'effect.txt'
    def effect(environment, arguments):
        raise AssertionError('uncertain_action_was_repeated')
    tool = Tool('fixture.effect', '1', {'type': 'object'}, {'type': 'object'}, (),
                {'kind': 'synthetic_file_append'}, effect, {})
    planner = lambda context: {'kind': 'action', 'tool': tool.id, 'arguments': {}}
    process = subprocess.Popen([sys.executable, '-m',
        'loom.tools.agent_runtime_v1.fixture_worker', str(path), phase, commit],
        cwd=REPO, env=dict(os.environ), start_new_session=True)
    # Wait on the concrete marker rather than a random crash delay.
    deadline = time.monotonic() + 5
    while not (path / 'ready').exists():
        if time.monotonic() > deadline or process.poll() is not None:
            process.kill(); process.wait()
            raise RuntimeError('fault_worker_did_not_reach_dispatch')
        time.sleep(.01)
    os.kill(process.pid, signal.SIGKILL)
    process.wait(timeout=5)
    time.sleep(.25)  # Lease expiry; finite subsecond observation, not retry.
    runtime, result = run_agent(path, [tool], planner, commit=commit, lease=.2)
    action_id = 'agent:' + digest(['probe', 0])
    receipts = runtime.store.receipts(action_id)
    expected = 1 if phase == 'after_effect' else 0
    effects = len(counter.read_bytes()) if counter.exists() else 0
    assert effects == expected and result['state'] == 'blocked'
    assert result['reason'] == 'outcome_unknown'
    return {'phase': phase, 'worker_exitcode': process.returncode, 'effect_count': effects,
        'result': result, 'receipts': receipts, 'automatic_repeat_count': 0}


def budget_probe(directory):
    directory = Path(directory)
    directory.mkdir(exist_ok=True)
    source_plan = json.loads((EXAMPLES / 'graph-review-plan.json').read_text())
    source_plan['id'] = 'shared-agent-probe'
    method = source_plan['methods'][0]
    method.update({'method': 'agent:execute', 'runtime': {'id': 'reference:ecosystem', 'config': {}},
                   'required_capabilities': ['local'], 'config': {}})
    called = []
    def callback(context, packet):
        called.append(context['method']['id'])
        started = time.perf_counter()
        _, result = run_agent(directory / 'agent', [graph_tool()], lambda context:
            {'kind': 'action', 'tool': 'loom.graph.apply_diff', 'arguments': graph_arguments()}
            if not context['observations'] else {'kind': 'final', 'value': {'graph_complete': True}})
        measurements = {'money_usd': '0', 'calls': '0', 'input_tokens': '0',
                        'output_tokens': '0', 'wall_seconds': str(time.perf_counter() - started)}
        return {'output': result, 'measurements': measurements,
            'measurement_provenance': {key: {'status': 'instrument_measured',
                'source_ref': 'synthetic-agent-probe/1', 'scope': 'callback_only'} for key in measurements}}
    callbacks = {('agent:execute', 'reference:ecosystem'): callback}
    admitted = plans.execute_variant(source_plan, 0, plans.ResourceLedger(
        directory / 'admitted', source_plan['resource_limits'], budget_id='shared'),
        callbacks, capabilities=['local'])
    denied_plan = deepcopy(source_plan)
    denied_plan['resource_limits']['calls']['limit'] = '0'
    denied = plans.execute_variant(denied_plan, 0, plans.ResourceLedger(
        directory / 'denied', denied_plan['resource_limits'], budget_id='shared'),
        callbacks, capabilities=['local'])
    assert len(called) == 1 and admitted['results']['one']['state'] == 'completed'
    assert denied['results']['one']['state'] == 'unavailable'
    return {'admitted': admitted, 'denied': denied, 'callback_dispatches': len(called),
        'nested_budget_transfer': False, 'paid_provider_calls': 0}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    options = parser.parse_args()
    output = Path(options.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    metadata = {'source_commit': source_commit(),
        'working_tree_dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=REPO)),
        'instrument_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in Path(__file__).parent.glob('*.py')},
        'fixture_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in EXAMPLES.glob('graph-review-*.json')},
        'python': platform.python_version(),
        'platform': platform.platform(), 'docker_available': shutil.which('docker') is not None,
        'bwrap_available': shutil.which('bwrap') is not None,
        'inputs': 'public_synthetic_fixtures', 'model': 'deterministic_injected_planner',
        'paid_provider_calls': 0, 'cloud_provisioning_calls': 0,
        'native_runtime_integration': False}
    plans._write_new(output / 'metadata_first.json', metadata)
    tracks = {'composition': compose, 'process': process_boundaries,
        'fault_before': lambda d: fault_probe(d, phase='before_effect'),
        'fault_after': lambda d: fault_probe(d, phase='after_effect'), 'budget': budget_probe}
    def track(name, probe):
        with tempfile.TemporaryDirectory(prefix='ecosystem-agent-' + name + '-') as work:
            try:
                result = probe(Path(work))
                plans._write_new(output / (name + '_first.json'), result)
                return {'track': name, 'state': 'completed', 'result_sha256': digest(result)}
            except Exception as exc:
                failure = {'track': name, 'state': 'failed', 'exception_type': type(exc).__name__,
                           'message': str(exc)}
                plans._write_new(output / (name + '_failure_first.json'), failure)
                return failure
    with ThreadPoolExecutor(max_workers=len(tracks)) as pool:
        futures = [pool.submit(track, name, probe) for name, probe in tracks.items()]
        summary = [future.result() for future in futures]
    plans._write_new(output / 'summary_first.json', summary)
    print(json.dumps({'output': str(output), 'tracks': summary}))
    return int(any(item['state'] != 'completed' for item in summary))


if __name__ == '__main__':
    sys.exit(main())
