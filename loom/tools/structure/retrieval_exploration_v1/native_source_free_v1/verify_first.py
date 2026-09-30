"""Recount archived first results without replacing receipts or invoking native CLI."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_panel_v2 as panel
import evaluate_native_v3 as scorer
import audit_source_bindings2 as bindings
import first_archive


def recompute(function, destination):
    captured = []
    with patch.object(panel, 'write_new', side_effect=lambda path, value: captured.append((path, value))), redirect_stdout(io.StringIO()):
        function()
    if len(captured) != 1 or Path(captured[0][0]) != panel.HERE / destination:
        raise ValueError('unexpected_recount_output')
    stored = json.loads((panel.HERE / destination).read_text())
    fresh = captured[0][1]
    # Measurement timestamps are receipts, not a new generation's clock value.
    stored.pop('created_at', None)
    fresh.pop('created_at', None)
    if fresh != stored:
        raise ValueError('first_result_recount_mismatch:' + destination)


def run():
    archive = first_archive.verify()
    recompute(scorer.run, 'first_results3.json')
    recompute(bindings.run, 'SOURCE_BINDING_RECOUNT.json')
    print(json.dumps({'archive': archive, 'inventory_recount_exact': True,
        'source_binding_recount_exact': True, 'native_processes_run': 0,
        'gold_read': False, 'validation_read': False, 'paid_calls': 0}))


if __name__ == '__main__':
    run()
