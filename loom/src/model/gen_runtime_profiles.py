#!/usr/bin/env python3
"""Embed canonical runtime .pack data; --check verifies generated source."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "src/model/runtime_profiles_embedded.inc"


def generate():
    lines = ["// Generated from loom/data/runtime/*.pack; do not edit.",
             "// Regenerate: python3 loom/src/model/gen_runtime_profiles.py",
             "constexpr std::pair<std::string_view, std::string_view> kRuntimeProfiles[] = {"]
    for path in sorted((ROOT / "data/runtime").glob("*.pack")):
        def unique_object(pairs):
            out = {}
            for key, value in pairs:
                if key in out:
                    raise ValueError(f"duplicate key {key}")
                out[key] = value
            return out

        def invalid_constant(value):
            raise ValueError(f"nonfinite JSON number {value}")

        doc = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object,
                         parse_constant=invalid_constant)
        if not re.fullmatch(r"[a-z0-9_-]+", path.stem):
            raise SystemExit(f"invalid domain filename: {path}")
        if doc.get("domain") != path.stem or doc.get("schema") != "loom.runtime_profile/1":
            raise SystemExit(f"invalid profile identity: {path}")
        text = json.dumps(doc, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
        # Each string chunk stays below the C++ translation limit. Adjacent
        # literals concatenate; split on Unicode characters, never raw bytes.
        chunks = [text[i:i + 4000] for i in range(0, len(text), 4000)]
        lines.append('  {"' + path.stem + '",')
        for chunk in chunks:
            if ')LPROFILE"' in chunk:
                raise SystemExit(f"raw-string delimiter in {path}")
            lines.append('    R"LPROFILE(' + chunk + ')LPROFILE"')
        lines[-1] += "},"
    lines.append("};")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    result = generate()
    if args.check:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != result:
            raise SystemExit("runtime profile embedding is stale")
    else:
        OUT.write_text(result, encoding="utf-8")
