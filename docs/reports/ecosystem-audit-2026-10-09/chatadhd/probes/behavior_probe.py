#!/usr/bin/env python3
"""Offline probes of real legacy product units. No GUI, model or network calls.

Usage: python behavior_probe.py /path/to/chatadhd > receipt.json
Only temporary configuration and synthetic messages are used.
"""
import hashlib
import json
import logging
import socket
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

root = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(root))
logging.disable(logging.CRITICAL)
records = []

def check(name, run):
    run()
    records.append({"name": name, "passed": True})

def eq(actual, expected):
    assert actual == expected, (actual, expected)

with patch.object(socket.socket, "connect", side_effect=AssertionError("network forbidden")), \
     patch.object(socket, "create_connection", side_effect=AssertionError("network forbidden")):
    from engine.config import Config
    from engine.semantic_llm import SemanticLLM, _ANALYSIS_PROMPT
    from engine.semantic_worker import SemanticWorker

    with tempfile.TemporaryDirectory() as temp:
        config_path = Path(temp) / "config.json"
        check("missing_config_uses_compiled_defaults", lambda: eq(Config(config_path).get("max_tokens"), 4096))
        config_path.write_text('{"semantic_model": "anthropic/claude-haiku-4-5"}')
        check("model_choice_is_not_reset_already_fixed", lambda: eq(Config(config_path).get("semantic_model"), "anthropic/claude-haiku-4-5"))
        config_path.write_text('{broken')
        check("corrupt_config_continues_with_compiled_defaults", lambda: eq(Config(config_path).get("max_tokens"), 4096))

        config_path.write_text(json.dumps({"semantic_model": "fixture/model", "base_url": "https://example.invalid/v1", "max_tokens": 7777, "temperature": .77}))
        cfg = Config(config_path)
        llm = SemanticLLM(cfg, {"api_key": "FAKE-OFFLINE-FIXTURE"})
        response = SimpleNamespace(status_code=200, json=lambda: {"choices": [{"message": {"content": "{}"}}]})
        with patch("engine.semantic_llm.requests.post", return_value=response) as transport:
            llm._call_llm("x" * 3000 + "TAIL_SENTINEL")
            body = transport.call_args.kwargs["json"]
            check("semantic_input_is_fixed_3000_prefix", lambda: eq(body["messages"][0]["content"], _ANALYSIS_PROMPT + "x" * 3000))
            check("semantic_generation_ignores_chat_sampling_controls", lambda: eq((body["temperature"], body["max_tokens"]), (.1, 800)))

        db = Mock()
        db.get_unanalysed_msgs.return_value = [{"id": "fixture-message", "conv_id": "fixture-conv", "text": "synthetic message"}]
        db.count_pending_semantic.return_value = 501
        worker = SemanticWorker(db, SimpleNamespace(enabled=True), Mock(), cfg, {"anthropic_batch_key": "FAKE-OFFLINE-FIXTURE"})
        with patch.object(worker, "_submit_batch_api", return_value=0) as submit:
            worker._drain_batch()
            check("queue_501_and_key_automatically_select_batch", lambda: eq(submit.call_count, 1))

        cfg.set("semantic_analysis", False)
        db.count_pending_semantic.return_value = 1
        with patch.object(worker, "_regex_analyse", side_effect=RuntimeError("synthetic failure")):
            worker._drain_batch()
        check("processing_error_is_marked_analysed", lambda: eq(db.mark_analysed.call_args.args, ("fixture-message", {"source": "error"})))

        ent = SimpleNamespace(text="Same", entity_type=SimpleNamespace(value="code_ref"), confidence=1.0)
        merged = SemanticLLM._merge({"entities": [{"name": "same", "kind": "person"}]}, {"entities": [ent]})
        check("entity_merge_name_only_hides_different_kind", lambda: eq(len(merged["entities"]), 1))

        cfg.set("semantic_analysis", True)
        eq(llm.enabled, True)
        generation = llm._config_generation
        for _ in range(5):
            llm._record_result(generation, False)
        check("five_failures_disable_semantic_model", lambda: eq(llm.enabled, False))
        cfg.set("semantic_model", "fixture/model-2")
        check("model_change_resets_failures_already_fixed", lambda: eq(llm.enabled, True))

paths = ["engine/config.py", "engine/semantic_llm.py", "engine/semantic_worker.py", "core/semantic.py", "engine/events.py"]
print(json.dumps({"suite": "chatadhd-offline-audit-behavior/1", "cases": len(records), "passed": len(records), "network_calls": 0,
    "paid_calls": 0, "scope": "Executed actual Python units with stubbed DB/HTTP; not C++ or Kivy E2E", "results": records,
    "source_sha256": {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths}}, indent=2))
