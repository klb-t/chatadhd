"""Explicit acceptance and durable replay through Loom's existing native store.

This module is a verification/CLI boundary, not a new storage implementation.
The caller chooses the packet and destination by invoking ``roundtrip``. It
does not fetch source resources or invoke a model/renderer. The reused native
context uses its ordinary data-directory initialization with workers disabled.
"""
from __future__ import annotations

from copy import deepcopy
from pathlib import Path

from loom.tools.coordination.graph_store import NativeGraphStore
from loom.tools.structure.agentic_graph_v1 import packet as codec


def roundtrip(packet: dict, library: str | Path, data_dir: str | Path) -> dict:
    """Accept a closed packet, close/reopen native storage, verify exact replay.

    Uses a packet-version target and compare-and-swap with absent-row
    expectations; another version gets its own existing native run, rather
    than replacing earlier data. Native exceptions remain exceptions, rather
    than being reported as a successful schema-only check. The receipt retains
    the entire original packet while native selected rows remain ordinary
    KnowledgeStore records. Source payload retention is determined by the
    supplied packet, not by this function.
    """
    frozen = deepcopy(packet)
    codec.validate_packet(frozen)
    selection = {name: [codec.record_id(name, row) for row in frozen[name]]
                 for name in ("entities", "claims", "sources")}
    expected = {name: {record_id: None for record_id in ids}
                for name, ids in selection.items()}
    target = "resource_graph:" + frozen["packet_id"]
    with NativeGraphStore(library, data_dir) as store:
        accepted = store.accept(frozen, target=target, selection=selection,
                                expected_rows=expected, explicitly_accepted=True)
        receipt = accepted["receipt"]
        if receipt["packet"] != frozen:
            raise RuntimeError("resource_graph_native_accept_packet_mismatch")
    # A new context is important: a receipt from the original context alone
    # does not demonstrate persistence through a shutdown/reopen boundary.
    with NativeGraphStore(library, data_dir) as reopened:
        read = reopened.read(receipt["id"])
        replay = reopened.replay(receipt["id"])
    for result in (read, replay):
        if result["receipt"] != receipt:
            raise RuntimeError("resource_graph_native_reopen_receipt_mismatch")
        if not result["row_drift"]["matches"]:
            raise RuntimeError("resource_graph_native_reopen_row_drift")
    return {"native_executed": True, "reopened": True,
            "packet_id": frozen["packet_id"], "receipt_id": receipt["id"],
            "packet_equal": True, "row_drift_matches": True,
            "selected_counts": {name: len(ids) for name, ids in selection.items()},
            "acceptance_establishes_content_truth":
                receipt["acceptance_establishes_content_truth"],
            "packet": replay["receipt"]["packet"]}
