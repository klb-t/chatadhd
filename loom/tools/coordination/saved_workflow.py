"""Exact local replay of preserved graph-workflow request/response pairs."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from .leases import CoordinationError, digest


class ReplayValidationError(CoordinationError):
    def __init__(self, reason, evidence):
        super().__init__(reason)
        self.evidence = evidence


def decode_json(raw):
    def pairs(items):
        value = {}
        for key, item in items:
            if key in value:
                raise CoordinationError("duplicate_json_key")
            value[key] = item
        return value

    def constant(_):
        raise CoordinationError("finite_json_required")

    value = json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)
    digest(value)
    return value


class SavedWorkflow:
    """Load immutable bytes once; never synthesize a missing stage response."""
    paid_live = False

    def __init__(self, directory, packet, method):
        from ..structure.agentic_graph_v1.workflow import validate_method
        validate_method(method)
        self.directory = Path(directory).resolve()
        self.manifest = {"schema": "loom.saved_workflow_transcript/1",
                         "directory": str(self.directory), "files": []}
        self.rows = []
        self.consumed = 0
        self.failures = []

        def load(name):
            path = self.directory / name
            try:
                raw = path.read_bytes()
            except FileNotFoundError as exc:
                raise ReplayValidationError("saved_workflow_file_missing",
                                            {"file": str(path), "network_calls": 0}) from exc
            value = decode_json(raw)
            self.manifest["files"].append({"file": name, "bytes": len(raw),
                                            "sha256": hashlib.sha256(raw).hexdigest()})
            return value

        original_packet = load("input_packet.json")
        original_method = load("method.json")
        for kind, expected, actual in (("packet", original_packet, packet),
                                       ("method", original_method, method)):
            if digest(expected) != digest(actual):
                raise ReplayValidationError("saved_workflow_" + kind + "_mismatch",
                    {"expected_sha256": digest(expected), "actual_sha256": digest(actual),
                     "network_calls": 0})
        for ordinal, stage in enumerate(method["stages"], 1):
            request = load(f"{ordinal:04d}.request.json")
            response = load(f"{ordinal:04d}.first_transport_response.json")
            if isinstance(response, dict) and "transport_failure_class" in response:
                raise ReplayValidationError("saved_transport_failure_is_not_a_response",
                    {"ordinal": ordinal, "failure_class": response["transport_failure_class"],
                     "network_calls": 0})
            if (not isinstance(request, dict) or type(request.get("ordinal")) is not int
                    or request["ordinal"] != ordinal
                    or digest(request.get("stage")) != digest(stage)):
                raise ReplayValidationError("saved_workflow_stage_identity_mismatch",
                                            {"ordinal": ordinal, "network_calls": 0})
            self.rows.append((request, response))
        self.manifest["transcript_sha256"] = digest(self.manifest)

    def __call__(self, request):
        ordinal = self.consumed + 1
        if self.consumed >= len(self.rows):
            failure = {"reason": "unexpected_extra_request", "ordinal": ordinal}
        else:
            expected, response = self.rows[self.consumed]
            # Python equality conflates True, 1 and 1.0 recursively. Our JSON
            # contract preserves those values/types; only object key order is
            # canonicalized, with no numeric or boolean normalization.
            if digest(request) == digest(expected):
                self.consumed += 1
                return deepcopy(response)
            failure = {"reason": "saved_request_mismatch", "ordinal": ordinal,
                       "expected_sha256": digest(expected), "actual_sha256": digest(request)}
        self.failures.append(failure)
        raise ReplayValidationError("saved_workflow_request_mismatch", failure)

    def preflight(self, packet, method, *, explicit_acceptance=None):
        """Read-only execution verifies dependencies and every generated request."""
        from ..structure.agentic_graph_v1.workflow import run_workflow
        result = run_workflow(packet, method, self, explicit_acceptance=explicit_acceptance)
        if self.failures or self.consumed != len(self.rows):
            raise ReplayValidationError("saved_workflow_replay_not_exact", {
                "failures": deepcopy(self.failures), "planned_stages": len(self.rows),
                "matched_stages": self.consumed, "network_calls": 0,
                "canonical_store_written": False})
        self.consumed = 0
        self.failures = []
        return {"exact_request_matches": len(self.rows), "workflow_result_sha256": digest(result),
                "network_calls": 0, "model_quality_measured": False}
