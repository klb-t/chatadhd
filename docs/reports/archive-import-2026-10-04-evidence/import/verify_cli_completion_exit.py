#!/usr/bin/env python3
"""Offline CLI receipt/exit-status regression; no model or network calls.

Usage: python verify_cli_completion_exit.py /absolute/path/to/loom

Fresh synthetic data directories verify complete exit 0 and partial exit 4,
with and without --audit. Partial JSON receipts retain successfully committed
conversations and their diagnostic errors. Cancellation retains priority 130
in the CLI implementation; this script does not inject an asynchronous signal.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sqlite3
import subprocess
import tempfile


def conversation(identifier: str) -> dict:
    return {
        "uuid": identifier,
        "name": "Synthetic CLI completion verification",
        "chat_messages": [{
            "uuid": "message-" + identifier,
            "sender": "human",
            "text": "Synthetic text retained in the native database.",
        }],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    checked = []
    with tempfile.TemporaryDirectory(prefix="loom-cli-completion-") as temporary:
        root = Path(temporary)
        for partial in (False, True):
            for audit in (False, True):
                label = ("partial" if partial else "complete") + ("-audit" if audit else "")
                source = root / (label + ".json")
                items = [conversation(label)]
                if partial:
                    # A valid JSON element outside the provider structure must
                    # be diagnosed while the following conversation is retained.
                    items.insert(0, {"unknown_nonconversation": True})
                source.write_text(json.dumps(items, ensure_ascii=False), encoding="utf-8")
                data = root / (label + "-data")
                command = [str(binary), "--data-dir", str(data), "--quiet", "--json",
                           "import", str(source), "--export-mode", "on"]
                if audit:
                    command.append("--audit")
                result = subprocess.run(command, capture_output=True, text=True, timeout=60)
                expected_exit = 4 if partial else 0
                assert result.returncode == expected_exit, (
                    label, result.returncode, result.stdout, result.stderr
                )
                receipt = json.loads(result.stdout)
                assert receipt["cancelled"] is False, label
                assert receipt["export_report"]["partial"] is partial, label
                assert len(receipt["conversations"]) == 1, label
                assert receipt["messages"] == 1, label
                assert bool(receipt["export_report"]["errors"]) is partial, label
                assert ("audit" in receipt) is audit, label
                databases = list(data.rglob("*.db"))
                native = []
                for database in databases:
                    with sqlite3.connect(f"file:{database}?mode=ro", uri=True) as connection:
                        if connection.execute(
                            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='conversations'"
                        ).fetchone():
                            native.append(database)
                            assert connection.execute("SELECT COUNT(*) FROM conversations").fetchone()[0] == 1
                            assert connection.execute("SELECT COUNT(*) FROM messages").fetchone()[0] == 1
                assert len(native) == 1, (label, databases)
                checked.append({"case": label, "exit_code": result.returncode,
                                "partial": partial, "conversations": 1, "messages": 1})
    print(json.dumps({"schema": "loom.cli_completion_verification.v1", "cases": checked,
                      "passed": len(checked), "model_calls": 0}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
