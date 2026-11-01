"""Trusted native test seam: one in-memory synthetic provider response, no HTTP."""
import importlib.util
import json
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location("provider_fixture", Path(__file__).with_name("test_provider_share.py"))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
envelope = json.load(sys.stdin)
transport = fixture.MemoryTransport((fixture.FIXTURES / "chatgpt_turbo_stream.html").read_bytes())
result = fixture.w.snapshot_request(envelope["request"], envelope["policy"], transport)
assert transport.calls == [fixture.URL]
json.dump(result, sys.stdout)
