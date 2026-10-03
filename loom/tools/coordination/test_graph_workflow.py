"""Exercise the real existing graph workflow through the opt-in lease seam."""
from pathlib import Path
import tempfile
import unittest

from loom.tools.coordination.graph_workflow import run_coordinated_workflow
from loom.tools.coordination.leases import CoordinationError, LeaseStore
from loom.tools.coordination.test_leases import COMMIT
from loom.tools.structure.agentic_graph_v1.test_packet import fixture, PREVIEW
from loom.tools.structure.agentic_graph_v1.test_workflow import method, response


class GraphWorkflowTests(unittest.TestCase):
    def test_existing_workflow_preserves_graph_semantics_and_first_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            store = LeaseStore(Path(temporary) / "coordination.sqlite3")
            calls = []
            def transport(request):
                calls.append(request); return response(request)
            kwargs = dict(task_id="graph/task with arbitrary id", source_commit=COMMIT,
                          packet=fixture(), method=method(2, policy=PREVIEW), transport=transport,
                          output_root=Path(temporary) / "artifacts", lease_seconds=10)
            result = run_coordinated_workflow(store, owner="first-thread", **kwargs)
            retry = run_coordinated_workflow(LeaseStore(store.path), owner="second-thread", **kwargs)
            self.assertFalse(retry["acquired"])
            self.assertEqual(len(calls), 2)
            workflow = result["outcome"]["workflow_result"]
            self.assertEqual(workflow["completed_stages"], 2)
            self.assertEqual(workflow["selected_packet"], fixture())
            self.assertFalse(workflow["canonical_store_written"])
            directory = Path(result["outcome"]["artifact_directory"])
            self.assertTrue((directory / "0001.first_transport_response.json").is_file())
            self.assertTrue((directory / "result_first.json").is_file())
            kwargs["method"] = method(3)
            with self.assertRaises(CoordinationError):
                run_coordinated_workflow(store, owner="third-thread", **kwargs)
            self.assertEqual(len(calls), 2)

    def test_semantic_failure_is_preserved_and_not_presented_as_graph_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            store = LeaseStore(Path(temporary) / "coordination.sqlite3")
            result = run_coordinated_workflow(store, task_id="invalid-output", source_commit=COMMIT,
                owner="one", packet=fixture(), method=method(1), transport=lambda request: {"broken": True},
                output_root=Path(temporary) / "artifacts", lease_seconds=10)
            self.assertEqual(result["outcome"]["workflow_result"]["completed_stages"], 0)
            self.assertTrue(result["outcome"]["execution_completed_is_not_semantic_success"])
            self.assertEqual(result["outcome"]["workflow_result"]["stages"][0]["first_transport_response"], {"broken": True})


if __name__ == "__main__": unittest.main()
