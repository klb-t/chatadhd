#!/usr/bin/env python3
"""Embed the canonical usage preset's exact UTF-8 bytes; --check detects drift.

The input is a JSON object. Policy-specific validation belongs to the native
usage validator. --source and --output support isolated source-change checks;
the generated source identifier always names the canonical repository resource.
"""

import argparse
import json
import math
from pathlib import Path


POLICY_DIR = Path(__file__).resolve().parent
REPO_ROOT = POLICY_DIR.parents[2]
SOURCE_IDENTIFIER = "loom/data/policy/usage_policy.pack"
DEFAULT_SOURCE = REPO_ROOT / SOURCE_IDENTIFIER
DEFAULT_OUTPUT = POLICY_DIR / "usage_policy_preset.inc"


def _reject_constant(value):
    raise ValueError(f"nonfinite JSON constant: {value}")


def _validate_source(raw):
    text = raw.decode("utf-8")
    # A leading UTF-8 BOM remains in the embedded bytes. Remove it only for
    # Python's JSON parser, matching the native parser's BOM acceptance.
    document = json.loads(text.removeprefix("\ufeff"), parse_constant=_reject_constant)
    if not isinstance(document, dict):
        raise ValueError("usage preset source must be a JSON object")
    pending = [document]
    while pending:
        value = pending.pop()
        if isinstance(value, float) and not math.isfinite(value):
            raise ValueError("usage preset source contains a nonfinite JSON number")
        if isinstance(value, dict):
            pending.extend(value.values())
        elif isinstance(value, list):
            pending.extend(value)
    return text


def generate(raw):
    """Return deterministic generated C++ bytes without changing the input."""
    _validate_source(raw)
    lines = [
        "// Generated from loom/data/policy/usage_policy.pack; do not edit.",
        "// Regenerate: python3 loom/src/policy/gen_usage_policy.py",
        "// --source may provide an isolated comparison input for this resource.",
        f'constexpr std::string_view kUsagePolicyPresetSource = "{SOURCE_IDENTIFIER}";',
        "constexpr std::string_view kUsagePolicyPresetChunks[] = {",
    ]
    # Each independent literal contains at most 2048 bytes. Array elements do
    # not concatenate into one oversized C++ literal, and total input is not
    # capped. Fixed three-digit octal escapes encode original bytes regardless
    # of compiler source/execution charset or physical newline normalization.
    for start in range(0, len(raw), 2048):
        chunk = raw[start:start + 2048]
        encoded = "".join(f"\\{byte:03o}" for byte in chunk)
        lines.append(f'    "{encoded}",')
    lines.append("};")
    return ("\n".join(lines) + "\n").encode("ascii")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        # Validate before inspecting or writing output. Invalid source leaves an
        # existing generated artifact untouched.
        generated = generate(args.source.read_bytes())
        if args.check:
            if not args.output.exists() or args.output.read_bytes() != generated:
                parser.exit(1, "usage preset embedding is stale or missing\n")
        else:
            args.output.write_bytes(generated)
    except (OSError, UnicodeError, ValueError, RecursionError) as error:
        parser.exit(1, f"usage preset generation failed: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
