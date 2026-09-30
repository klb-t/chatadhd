"""Local graph AnalysisPlan integration, real CLI and original graph invariants."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from loom.tools.coordination import analysis_graph as adapter
from loom.tools.coordination.leases import CoordinationError, LeaseStore
from loom.tools.contracts.test_analysis_plan_ref import plan as base_plan
from loom.tools.structure.agentic_graph_v1.test_packet import fixture, MODEL, AUTO, PREVIEW

COMMIT = 'b118c80e981c08ec6d7f9ab6aacc177979186cf2'
ROOT = Path(__file__).resolve().parents[3]


def inputs(policy=PREVIEW):
    packet = fixture()
    diff = adapter.codec.empty_diff(packet, proposal_id='local-review', origin=MODEL)
    after = deepcopy(packet['claims'][0]); after['assessment']['status'] = 'contested'
    diff['claims']['update'] = [{'id': after['id'],
        'before_sha256': adapter.codec.digest(packet['claims'][0]), 'after': after}]
    plan = base_plan()
    plan['sources'][0]['binding']['snapshot_id'] = packet['packet_id']
    method = plan['methods'][0]
    method.update(method=adapter.CALLBACK[0], runtime={'id': adapter.CALLBACK[1], 'config': {}},
                  acceptance_policy=deepcopy(policy), config={'diff': diff, 'policy': deepcopy(policy)})
    return plan, packet


class AnalysisGraphTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        self.store = LeaseStore(self.path / 'coordination.sqlite3')
        self.plan, self.packet = inputs()

    def run_plan(self, **changes):
        kwargs = dict(task_id='graph', source_commit=COMMIT, owner='operator', plan=self.plan,
            variant_index=0, packet=self.packet, ledger_directory=self.path / 'budget',
            output_root=self.path / 'artifacts', lease_seconds=300)
        kwargs.update(changes)
        return adapter.run_coordinated_analysis_graph(self.store, **kwargs)

    def test_preview_durable_restart_and_inverse(self):
        result = self.run_plan()['execution']['outcome']
        self.assertTrue(result['all_methods_completed'])
        output = result['analysis_result']['results']['one']['result']['output']
        self.assertEqual(output['selected_packet'], self.packet)
        self.assertNotEqual(output['preview']['candidate_packet'], self.packet)
        self.assertFalse(output['application']['accepted'])
        self.assertFalse(output['acceptance_establishes_content_truth'])
        directory = Path(result['artifact_directory'])
        raw = (directory / 'result_first.json').read_bytes()
        second = self.run_plan()['execution']
        self.assertFalse(second['acquired'])
        self.assertEqual((directory / 'result_first.json').read_bytes(), raw)
        self.assertEqual(len(list((self.path / 'budget').glob('attempt-*'))), 1)
        self.assertEqual(adapter.codec.invert_application(output['application'], output['selected_packet']), self.packet)

    def test_auto_and_explicit_preview_keep_epistemic_origin(self):
        for i, policy in enumerate((AUTO, PREVIEW)):
            plan, packet = inputs(policy)
            plan['methods'][0]['config']['explicitly_accepted'] = True
            result = self.run_plan(task_id=str(i), plan=plan)['execution']['outcome']
            output = result['analysis_result']['results']['one']['result']['output']
            self.assertTrue(output['application']['accepted'])
            self.assertEqual(output['selected_packet']['claims'][0]['assessment']['origin'], 'archive')
            self.assertEqual(output['selected_packet']['provenance']['claims']['cl_test']['origin'], MODEL)
            self.assertEqual(adapter.codec.invert_application(output['application'], output['selected_packet']), packet)

    def test_chained_selected_packet_and_symbolic_variants(self):
        self.plan, self.packet = inputs(AUTO)
        first = self.plan['methods'][0]
        selected, _ = adapter.codec.apply_diff(self.packet, first['config']['diff'], AUTO)
        second = deepcopy(first); second['id'] = 'two'; second['depends_on'] = ['one']
        second['config']['input_dependency'] = 'one'
        second['config']['diff'] = adapter.codec.empty_diff(selected, proposal_id='second', origin=MODEL)
        self.plan['methods'].append(second)
        self.plan['variant_axes'] = {'agent': {'range': {'start': 0, 'stop': 10**60, 'step': 1}}}
        result = self.run_plan(variant_index=10**60-1)['execution']['outcome']
        self.assertTrue(result['all_methods_completed'])
        output = result['analysis_result']['results']['two']['result']['output']
        self.assertEqual(len(output['selected_packet']['history']), 2)
        self.assertEqual(len(list((self.path / 'budget').glob('attempt-*'))), 2)

    def test_budget_and_missing_capability_do_not_dispatch(self):
        self.plan['resource_limits']['money_usd']['limit'] = '0'
        result = self.run_plan()['execution']['outcome']
        self.assertFalse(result['all_methods_completed'])
        self.assertEqual(result['analysis_result']['results']['one']['reason'], 'resource_limit_exceeded_money_usd')
        self.assertEqual(len(list((self.path / 'budget').glob('attempt-*'))), 0)
        plan, _ = inputs(); plan['resource_budget_ref'] = 'other'
        plan['methods'][0]['required_capabilities'] = ['remote_provider']
        result = self.run_plan(task_id='missing', plan=plan, ledger_directory=self.path / 'other')['execution']['outcome']
        self.assertEqual(result['analysis_result']['results']['one']['reason'], 'missing_capability')

    def test_stale_patch_is_uncertain_not_success_and_never_retried(self):
        self.plan['methods'][0]['config']['diff']['base_packet_sha256'] = '0'*64
        result = self.run_plan()['execution']['outcome']
        self.assertFalse(result['all_methods_completed']); self.assertEqual(result['uncertain_methods'], ['one'])
        self.assertEqual(result['analysis_result']['resource_usage']['money_usd'], '1')
        self.assertFalse(self.run_plan()['execution']['acquired'])

    def test_source_config_policy_and_code_identity_bound(self):
        self.run_plan()
        changed = deepcopy(self.plan); changed['methods'][0]['reasoning']['depth'] = 'new'
        with self.assertRaises(CoordinationError): self.run_plan(plan=changed)
        changed = deepcopy(self.plan); changed['sources'][0]['binding']['snapshot_id'] = 'wrong'
        with self.assertRaises(CoordinationError): self.run_plan(plan=changed, task_id='wrong')
        changed = deepcopy(self.plan); changed['methods'][0]['acceptance_policy'] = AUTO
        with self.assertRaises(CoordinationError): self.run_plan(plan=changed, task_id='policy')
        fresh = self.run_plan(task_id='new-code', source_commit='a'*40)['execution']['outcome']
        self.assertEqual(fresh['analysis_result']['results']['one']['dispatch'], 'callback_dispatched')
        self.assertEqual(len(list((self.path / 'budget').glob('attempt-*'))), 2)

    def test_real_cli_first_execution_restart_and_changed_file_bytes(self):
        for name, value in [('plan', self.plan), ('packet', self.packet)]:
            (self.path / (name+'.json')).write_text(json.dumps(value))
        command = [sys.executable, '-m', 'loom.tools.coordination', '--database', str(self.store.path),
            'analysis-graph', '--task', 'cli', '--source-commit', COMMIT, '--owner', 'cli-worker',
            '--plan', str(self.path/'plan.json'), '--packet', str(self.path/'packet.json'),
            '--ledger-directory', str(self.path/'budget'), '--output-root', str(self.path/'artifacts')]
        first = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertTrue(json.loads(first.stdout)['execution']['outcome']['all_methods_completed'])
        second = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertFalse(json.loads(second.stdout)['execution']['acquired'])
        with (self.path/'plan.json').open('a') as stream: stream.write('\n')
        third = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(third.returncode, 2)
        self.assertIn('task_identity', third.stderr)


if __name__ == '__main__': unittest.main()
