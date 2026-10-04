#!/usr/bin/env python3
"""Offline synthetic provider fixtures; only one conversation is built at a time.

Examples (requires enough disk for input, source blob and resulting database):
  python generate_provider_export.py wrapper.json --shape wrapper --min-bytes 2147483648 --payload-bytes 1048576
  python generate_provider_export.py array.json --shape array --min-bytes 2147483648 --payload-bytes 1048576
  python generate_provider_export.py large.json --shape wrapper --conversations 2 --messages 32 --payload-bytes 1048576
The min-bytes option appends complete conversations until the file reaches the
requested size. No private content, randomness, credentials or model calls.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time


def conversation(index, messages, payload, payload_site):
    mapping = {}
    for message_index in range(messages):
        key = f"node-{message_index}"
        text = f"Synthetic conversation {index}, message {message_index}. Zażółć gęślą jaźń 🧪."
        metadata = {"synthetic": True, "message_index": message_index}
        if payload_site == "text":
            text += "\n" + payload
        elif payload_site == "metadata":
            metadata["synthetic_payload"] = payload
        mapping[key] = {
            "id": key,
            "parent": None if message_index == 0 else f"node-{message_index - 1}",
            "children": [] if message_index + 1 == messages else [f"node-{message_index + 1}"],
            "message": {
                "id": f"message-{index}-{message_index}",
                "author": {"role": "user" if message_index % 2 == 0 else "assistant"},
                "create_time": 1700000000 + message_index,
                "content": {"content_type": "text", "parts": [text]},
                "metadata": metadata,
            },
        }
    return {
        "id": f"synthetic-conversation-{index}", "title": f"Synthetic benchmark {index}",
        "create_time": 1700000000, "update_time": 1700000000 + messages,
        "current_node": f"node-{messages - 1}", "mapping": mapping,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--shape", choices=["array", "wrapper"], default="wrapper")
    parser.add_argument("--min-bytes", type=int, default=0)
    parser.add_argument("--conversations", type=int, default=256)
    parser.add_argument("--messages", type=int, default=2)
    parser.add_argument("--payload-bytes", type=int, default=512 * 1024)
    parser.add_argument("--payload-site", choices=["text", "metadata", "whitespace"], default="metadata")
    args = parser.parse_args()
    if min(args.min_bytes, args.conversations, args.payload_bytes) < 0 or args.messages < 1:
        parser.error("sizes/counts must be nonnegative and messages positive")
    payload = "x" * args.payload_bytes
    prefix = b"["
    if args.shape == "wrapper":
        prefix = b'{"synthetic":true,"before":{"marker":"prefix"},"conversations":['
    suffix = b"]" if args.shape == "array" else b'],"after":{"marker":"suffix","synthetic":true}}'
    digest = hashlib.sha256()
    output_bytes = 0
    total_text_chars = 0
    started = time.monotonic()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb", buffering=1024 * 1024) as stream:
        def write(data):
            nonlocal output_bytes
            stream.write(data)
            digest.update(data)
            output_bytes += len(data)
        write(prefix)
        count = 0
        while count < args.conversations or output_bytes + len(suffix) < args.min_bytes:
            if count:
                write(b",")
            conv = conversation(count, args.messages, payload, args.payload_site)
            total_text_chars += sum(len(node["message"]["content"]["parts"][0]) for node in conv["mapping"].values())
            raw = json.dumps(conv, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            write(raw)
            if args.payload_site == "whitespace":
                write(b" " * args.payload_bytes)
            count += 1
        write(suffix)
    receipt = {
        "schema": "loom.synthetic_import_benchmark/1", "source": "offline synthetic generated fixture",
        "shape": args.shape, "conversations": count, "messages": count * args.messages,
        "normalized_text_characters": total_text_chars, "payload_site": args.payload_site,
        "payload_bytes_per_message": args.payload_bytes if args.payload_site != "whitespace" else 0,
        "whitespace_bytes_per_conversation": args.payload_bytes if args.payload_site == "whitespace" else 0,
        "source_bytes": output_bytes, "sha256": digest.hexdigest(),
        "generator_elapsed_seconds": time.monotonic() - started,
        "caveat": "Whitespace padding measures scanner traversal, not realistic stored-content memory." if args.payload_site == "whitespace" else "Memory remains proportional to the largest conversation and result summaries, not mathematically constant.",
    }
    args.output.with_suffix(args.output.suffix + ".fixture.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
