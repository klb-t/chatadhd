#!/usr/bin/env python3
"""Embed only W3's canonical descriptors; no presets or layering rules here."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOMAINS = ("chat_reasoning", "context_goal_cues")


def generate():
    pieces = ["// Generated from canonical W3 .pack files by context/gen_runtime_presets.py.\n",
              "#pragma once\n",
              "// Include inside namespace loom::context. Do not edit.\n"]
    bindings = json.loads((ROOT / "data/context/runtime_preset_layers.pack").read_text(encoding="utf-8"))
    if bindings["schema"] != "loom.runtime_preset_layer_bindings/1":
        raise ValueError("layer binding schema mismatch")
    layer_entries = []
    layer_bindings = {}
    for domain in DOMAINS:
        source = ROOT / "data" / "runtime" / f"{domain}.pack"
        definition = json.loads(source.read_text(encoding="utf-8"))
        if definition["schema"] != "loom.runtime_profile/1" or definition["domain"] != domain:
            raise ValueError(f"descriptor identity mismatch: {source}")
        payload = json.dumps(definition, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        # C++ raw delimiter is representational, not an execution limit.
        delimiter = "W3_PRESET"
        if f'){delimiter}\"' in payload:
            raise ValueError("descriptor contains raw-string delimiter")
        pieces.append(f"inline const Json& builtin_{domain}_definition() {{\n"
                      f"  static const Json definition = Json::parse(R\"{delimiter}({payload}){delimiter}\");\n"
                      "  return definition;\n}\n")
        consumed = set()
        layer_bindings[domain] = {}
        for entry in bindings["domains"][domain]:
            # This contribution binds whole consumed settings, never overlapping
            # subpaths. Values are derived from the one canonical descriptor.
            pointer = entry["pointer"]
            field = pointer[1:].replace("~1", "/").replace("~0", "~")
            if not pointer.startswith("/") or field not in definition["defaults"] or field in consumed:
                raise ValueError(f"invalid or duplicate binding: {domain}:{pointer}")
            consumed.add(field)
            layer_bindings[domain][pointer] = entry["key"]
            layer_entries.append({k: v for k, v in entry.items() if k != "pointer"} |
                                 {"value": definition["defaults"][field]})
        if consumed != set(definition["defaults"]):
            raise ValueError(f"bindings omit a setting: {domain}")
    for name, value in (("runtime_preset_layer_entries", layer_entries),
                        ("runtime_preset_layer_bindings", layer_bindings)):
        payload = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        pieces.append(f"inline const Json& builtin_{name}() {{\n"
                      f"  static const Json value = Json::parse(R\"W3_PRESET({payload})W3_PRESET\");\n"
                      "  return value;\n}\n")
    return "".join(pieces)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    output = ROOT / "src" / "context" / "runtime_presets.inc"
    generated = generate()
    if args.check:
        if not output.exists() or output.read_text(encoding="utf-8") != generated:
            raise SystemExit("runtime_presets.inc differs from canonical data; regenerate it")
    else:
        output.write_text(generated, encoding="utf-8")


if __name__ == "__main__":
    main()
