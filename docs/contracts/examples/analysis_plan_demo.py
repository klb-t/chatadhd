"""Offline example: callbacks and a supplied graph packet, zero external calls."""
import argparse
from pathlib import Path
from loom.tools.contracts import analysis_plan_ref as plans


def run(output):
    plan = plans._read(Path(__file__).with_name('analysis_plan_v1.json'))
    plans.validate_plan(plan)
    ledger = plans.ResourceLedger(output, plan['resource_limits'], budget_id=plan['resource_budget_ref'])
    def load_packet(context):
        from loom.tools.structure.agentic_graph_v1 import packet
        return packet.make_packet(origin={'kind': 'system', 'actor': 'scripted-plan-demo',
            'model': None, 'recipe_sha256': None, 'response_sha256': None})
    def inspect(context, value):
        from loom.tools.structure.agentic_graph_v1 import packet
        packet.validate_packet(value)
        measurements = {d: '0' for d in plans.DIMENSIONS}
        measurements['calls'] = '1'  # local callback invocation, not an API call
        for dimension in ('cpu_seconds', 'wall_seconds', 'memory_bytes', 'storage_bytes'):
            measurements[dimension] = None  # not instrumented; reservation stays
        proposal = packet.empty_diff(value, proposal_id=context['method']['id'] + '_' + plans.digest(context['axes'])[:12],
            origin={'kind': 'system', 'actor': 'scripted-plan-demo', 'model': None,
                'recipe_sha256': None, 'response_sha256': None})
        return {'output': {'execution_kind': 'scripted_not_model', 'real_api_calls': 0,
            'proposal': proposal, 'quality_measured': False},
            'measurements': measurements, 'measurement_provenance': {d: {
                'status': 'instrument_measured' if d == 'calls' else 'declared',
                'source_ref': 'local-callback-counter-v1' if d == 'calls' else 'scripted-no-provider-or-worker-v1'}
                for d, quantity in measurements.items() if quantity is not None}}
    callbacks = {(m['method'], m['runtime']['id']): inspect for m in plan['methods']}
    receipts = []
    for index, _ in plans.selected_variants(plan):
        receipts.append(plans.execute_variant(plan, index, ledger, callbacks,
            capabilities={'scripted'}, packet_loader=load_packet))
    first = {'schema': 'loom.analysis_plan_demo_receipt/1', 'execution_kind': 'scripted_not_model',
        'real_api_calls': 0, 'model_quality_measured': False, 'symbolic_variants': plans.cardinality(plan),
        'selected_indices': plan['selection']['indices'], 'allocated_workers': 0,
        'results': receipts, 'resource_usage': ledger.usage(), 'canonical_graph_writes': 0}
    first['resource_usage_kind'] = 'policy_accounting_not_actual_measurement'
    plans._write_new(Path(output) / 'FIRST_DEMO_RECEIPT.json', first)
    return first


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(); result = run(args.output)
    print(plans.encoded({k:v for k,v in result.items() if k != 'results'}).decode())
