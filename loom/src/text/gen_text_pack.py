#!/usr/bin/env python3
"""Embed loom/data/text/segmentation.pack into src/text/text_pack.inc.

The pack is the source of truth; the .inc is generated. The literal carries an explicit length
(std::string_view(R"...", N)) because clang's constexpr strlen step limit rejects long bare literals
(CLAUDE.md, CI 262). `--check` exits 1 when the .inc is stale (the gate runs it).
"""
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
LOOM = HERE.parent.parent
PACK = LOOM / "data" / "text" / "segmentation.pack"
OUT = HERE / "text_pack.inc"
DELIM = "LOOMTXT"


def render() -> str:
    raw = PACK.read_bytes()
    json.loads(raw.decode("utf-8"))  # must be valid JSON
    text = raw.decode("utf-8")
    if ")" + DELIM + '"' in text:
        raise SystemExit("segmentation.pack contains the raw-string delimiter")
    return (
        "// Generated from loom/data/text/segmentation.pack; do not edit.\n"
        "// Regenerate: python3 loom/src/text/gen_text_pack.py\n"
        'constexpr std::string_view kTextPackSource = "loom/data/text/segmentation.pack";\n'
        f'constexpr std::string_view kTextPack = std::string_view(R"{DELIM}({text}){DELIM}", {len(raw)});\n'
    )


def main() -> int:
    want = render()
    if "--check" in sys.argv[1:]:
        have = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if have != want:
            print(f"{OUT} is stale; run python3 loom/src/text/gen_text_pack.py", file=sys.stderr)
            return 1
        return 0
    OUT.write_text(want, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
