"""Additive coordination seam for the existing graph packet workflow.

Existing acceptance, provenance, accounting and transport-authorization checks
remain in run_workflow. The lease guards invocation across caller directories;
it does not alter AnalysisPlan reservations or create additional paid authority.
"""
from pathlib import Path

from .leases import digest, execute_once


def run_coordinated_workflow(store, *, task_id, source_commit, owner,
                             packet, method, transport, output_root,
                             lease_seconds, explicit_acceptance=None,
                             transport_binding=None):
    from ..structure.agentic_graph_v1 import packet as codec
    from ..structure.agentic_graph_v1.workflow import run_workflow, validate_method

    validate_method(method)
    codec.validate_packet(packet, resource_limits=method["resources"]["packet_limits"])
    spec = {
        "adapter": "loom.graph_packet_workflow/1",
        "packet": packet, "method": method,
        "explicit_acceptance": explicit_acceptance,
        "output_root": str(Path(output_root).resolve()),
    }
    if transport_binding is not None:
        spec["transport_binding"] = transport_binding
    store.register(task_id, source_commit=source_commit, spec=spec)

    def callback(bound, lease):
        # An explicitly reconciled retry gets a new directory; prior artifacts
        # cannot be overwritten. A path-safe digest keeps arbitrary IDs as data.
        directory = Path(bound["output_root"]) / digest(task_id) / ("fence-%d" % lease["fence"])
        result = run_workflow(bound["packet"], bound["method"], transport,
                              output_dir=directory,
                              explicit_acceptance=bound["explicit_acceptance"])
        return {"workflow_result": result, "artifact_directory": str(directory),
                "workflow_result_sha256": digest(result),
                "execution_completed_is_not_semantic_success": True}

    return execute_once(store, task_id, owner=owner, lease_seconds=lease_seconds,
                        callback=callback)
