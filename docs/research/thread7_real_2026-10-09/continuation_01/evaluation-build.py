"""Build a new, allowlisted public preparation artifact; never overwrite a seal."""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from jsonschema import Draft202012Validator
from loom.tools.seeding import method_graph
from loom.tools.structure import experiment_analysis_v1 as analysis


@analysis.public_error_boundary
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--projected-at', default=None)
    args = parser.parse_args()
    base = Path(__file__).parent
    example = method_graph.strict_json((base/'handoff-example.json').read_bytes())
    analysis.approve_public_input('continuation01/handoff-example.json',example)
    contract=method_graph.strict_json((base/'handoff-contract.json').read_bytes())
    analysis.approve_public_input('continuation01/handoff-contract.json',contract)
    Draft202012Validator(contract).validate(example)
    protocol = method_graph.strict_json((base/'evaluation-protocol.json').read_bytes())
    projection = method_graph.load_projection(base/'handoff-projection.json')
    artifact = analysis.build_handoff_artifact(example, protocol, projection,
        projected_at=args.projected_at or datetime.now(timezone.utc).isoformat())
    raw = method_graph.canonical(artifact)+b'\n'
    compressed = gzip.compress(raw, mtime=0)
    with args.output.open('xb') as stream:
        stream.write(compressed)
    receipt = {'schema':'loom.thread7_analysis_contract_receipt/1',
        'artifact_sha256':hashlib.sha256(raw).hexdigest(), 'compressed_sha256':hashlib.sha256(compressed).hexdigest(),
        'protocol_sha256':analysis.digest(protocol), 'presentation_sha256':analysis.digest(example),
        'projection_sha256':analysis.digest(projection), 'producer_file_sha256':hashlib.sha256(Path(analysis.__file__).read_bytes()).hexdigest(),
        'contracts':['loom.method_graph/1','loom.method_run_trace/1'],
        'python_schema_and_codec_verified':True, 'exact_source_and_result_recovery':True,
        'native_execution':False, 'provider_calls':0, 'new_cost_usd':'0', 'quality':None,
        'entities':len(artifact['packet']['entities']), 'claims':len(artifact['packet']['claims']),
        'sources':len(artifact['packet']['sources']), 'result_entities':len(artifact['result_entity_ids'])}
    with args.output.with_suffix('.receipt.json').open('x') as stream:
        json.dump(receipt, stream, indent=2); stream.write('\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
