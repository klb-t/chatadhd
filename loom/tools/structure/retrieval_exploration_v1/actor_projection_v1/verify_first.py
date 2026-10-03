"""Recount both actor receipts without rewriting first failures or native sources."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parent))
import actor_projection as first
import actor_projection_v2 as second
import evaluate_controls as controls_first
import evaluate_controls_v2 as controls_second
import first_archive


def recount(function, destination, *args):
    captured = []
    with patch.object(first.panel, 'write_new', side_effect=lambda path, value: captured.append((path, value))), redirect_stdout(io.StringIO()):
        function(*args)
    if len(captured) != 1 or Path(captured[0][0]) != first.HERE / destination:
        raise ValueError('unexpected_actor_recount_output')
    stored, fresh = json.loads((first.HERE / destination).read_text()), captured[0][1]
    stored.pop('runtime', None)
    fresh.pop('runtime', None)
    if fresh != stored:
        raise ValueError('actor_first_recount_drift:' + destination)


def run():
    archive = first_archive.verify()
    fixture = first.panel.ROOT / 'docs/research/native_actor_control_audit_v1/controls.json'
    recount(first.run, 'first_projection.json')
    recount(second.run, 'second_projection.json')
    recount(controls_first.run, 'first_controls.json', fixture)
    recount(controls_second.run, 'second_controls.json', fixture)
    print(json.dumps({'archive': archive, 'exact_semantic_receipts_recounted': 4,
        'runtime_receipts_preserved_excluded_from_clock_equality': True,
        'native_runs': 0, 'gold_read': False, 'validation_read': False, 'paid_calls': 0}))


if __name__ == '__main__':
    run()
