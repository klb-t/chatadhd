"""Isolated transport process used ONLY by the native doctest seam.
The production dispatcher never exposes the path or a transport toggle.
"""
import importlib.util
import json
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location("fixture", Path(__file__).with_name("test_worker.py"))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
envelope = json.load(sys.stdin)
fixture.WorkerTests.setUpClass()
try:
    result = fixture.w.snapshot_request(envelope["request"], envelope["policy"],
                                       fixture.IsolatedTransport(fixture.WorkerTests.server.server_port))
    json.dump(result, sys.stdout)
finally:
    fixture.WorkerTests.tearDownClass()
