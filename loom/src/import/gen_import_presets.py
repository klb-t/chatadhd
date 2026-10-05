#!/usr/bin/env python3
"""Embed exact JSON bytes of both import presets; --check detects source drift.

Domain validation belongs to native/Python consumers. Exact-byte embedding
requires a UTF-8 JSON object with finite numbers. The derived layer pack also
validates the explicit mapping contract and source identities. Alternate inputs
support isolated checks without changing canonical embedded source identifiers.
Revision lifecycle checks belong to the common layer engine, not this transform.
"""

import argparse
import json
import math
from pathlib import Path


IMPORT_DIR = Path(__file__).resolve().parent
REPO_ROOT = IMPORT_DIR.parents[2]
IMPORT_SOURCE_IDENTIFIER = "loom/data/presets/import.pack"
AUDIT_SOURCE_IDENTIFIER = "loom/data/presets/import_audit.pack"
DEFAULT_IMPORT_SOURCE = REPO_ROOT / IMPORT_SOURCE_IDENTIFIER
DEFAULT_AUDIT_SOURCE = REPO_ROOT / AUDIT_SOURCE_IDENTIFIER
DEFAULT_OUTPUT = IMPORT_DIR / "import_presets.inc"
DEFAULT_LAYERS_SPEC = REPO_ROOT / "loom/data/presets/import_layers.spec.pack"
DEFAULT_LAYERS_OUTPUT = REPO_ROOT / "loom/data/presets/import_layers.pack"


class _JsonInteger(str):
    """A validated JSON integer token, without a conversion/digit-count cap."""


class _JsonFloat(str):
    """Retain a finite JSON decimal token without losing its source precision."""

    def __new__(cls, token):
        if not math.isfinite(float(token)):
            raise ValueError("import preset source contains a nonfinite JSON number")
        return super().__new__(cls, token)


def _reject_constant(value):
    raise ValueError(f"nonfinite JSON constant: {value}")


def _validate_source(raw):
    text = raw.decode("utf-8")
    # The BOM remains in the embedded source; remove it only for JSON parsing.
    # Numeric tokens require no native conversion for embedding/derivation.
    # This avoids the Python integer digit cap and decimal precision loss.
    # Native consumers enforce each field's actual representation separately.
    document = json.loads(text.removeprefix("\ufeff"), parse_constant=_reject_constant,
                          parse_int=_JsonInteger, parse_float=_JsonFloat)
    if not isinstance(document, dict):
        raise ValueError("import preset source must be a JSON object")
    return document


def _positive_integer(value, label):
    if not isinstance(value, _JsonInteger) or value.startswith("-") or value == "0":
        raise ValueError(f"{label} must be a positive JSON integer")


def _nonempty_text(value, label):
    # Numeric token subclasses are not text metadata.
    if type(value) is not str or not value.strip():
        raise ValueError(f"{label} must be a nonempty JSON string")


def _json_text(value, level=0):
    """Pretty JSON retaining every original numeric token as a JSON number."""
    if isinstance(value, (_JsonInteger, _JsonFloat)):
        return str(value)
    indent = "  " * level
    child_indent = "  " * (level + 1)
    if isinstance(value, dict):
        if not value:
            return "{}"
        members = [child_indent + json.dumps(key, ensure_ascii=False) + ": " +
                   _json_text(item, level + 1) for key, item in value.items()]
        return "{\n" + ",\n".join(members) + "\n" + indent + "}"
    if isinstance(value, list):
        if not value:
            return "[]"
        members = [child_indent + _json_text(item, level + 1) for item in value]
        return "[\n" + ",\n".join(members) + "\n" + indent + "]"
    return json.dumps(value, ensure_ascii=False, allow_nan=False)


def generate_layers(spec_raw, import_raw, audit_raw):
    """Project mapped values into the common layer contract, without state."""
    sources = {"import": _validate_source(import_raw),
               "import_audit": _validate_source(audit_raw)}
    contracts = {"import": ("loom.import_preset/1", "loom.preset.import.default"),
                 "import_audit": ("loom.import_audit_preset/1", "loom.preset.import-audit.default")}
    for resource, source in sources.items():
        schema, identity = contracts[resource]
        if source.get("schema") != schema or source.get("id") != identity:
            raise ValueError(f"invalid source identity/schema for resource {resource}")
        _positive_integer(source.get("version"), f"{resource} source version")
        if not isinstance(source.get("values"), dict):
            raise ValueError(f"{resource} source values must be an object")

    spec = _validate_source(spec_raw)
    if spec.get("schema") != "loom.import_layers_spec/1":
        raise ValueError("unsupported import layer mapping schema")
    target = spec.get("target")
    if not isinstance(target, dict) or target.get("schema") != "loom.default_layers_pack/1":
        raise ValueError("invalid import layer target schema")
    _nonempty_text(target.get("pack_id"), "target pack_id")
    _positive_integer(target.get("revision"), "target revision")
    if not isinstance(target.get("policy"), dict):
        raise ValueError("target policy must be an explicit object")
    if "entries" in target:
        raise ValueError("target metadata cannot contain entries")
    entries = spec.get("entries")
    if not isinstance(entries, list):
        raise ValueError("import layer mapping entries must be an array")
    fields = {"id", "key", "area", "revision", "resource", "field"}
    identities, keys = set(), set()
    projected = []
    for binding in entries:
        if not isinstance(binding, dict) or set(binding) != fields:
            raise ValueError("each layer binding must contain exactly id/key/area/revision/resource/field")
        for field in ("id", "key", "area", "resource", "field"):
            _nonempty_text(binding[field], f"binding {field}")
        _positive_integer(binding["revision"], "entry revision")
        if binding["id"] in identities or binding["key"] in keys:
            raise ValueError("layer binding IDs and keys must be unique")
        identities.add(binding["id"])
        keys.add(binding["key"])
        resource = binding["resource"]
        if resource not in sources or binding["field"] not in sources[resource]["values"]:
            raise ValueError(f"unresolved import layer binding: {resource}.{binding['field']}")
        entry = {field: binding[field] for field in ("id", "key", "area", "revision")}
        entry["value"] = sources[resource]["values"][binding["field"]]
        projected.append(entry)
    result = dict(target)
    result["entries"] = projected
    return (_json_text(result) + "\n").encode("utf-8")


def _resource_lines(name, identifier, raw):
    lines = [f'constexpr std::string_view k{name}PresetSource = "{identifier}";',
             f"constexpr std::string_view k{name}PresetChunks[] = {{"]
    # Independent literals bound compiler token size, not resource size. Fixed
    # three-digit octal escapes preserve bytes across source/execution charsets
    # and physical newline normalization, including hostile delimiter text.
    for start in range(0, len(raw), 2048):
        encoded = "".join(f"\\{byte:03o}" for byte in raw[start:start + 2048])
        lines.append(f'    "{encoded}",')
    lines.append("};")
    return lines


def generate(import_raw, audit_raw):
    """Return deterministic C++ bytes, retaining both full original sources."""
    # Validate both before creating any output: one invalid source cannot leave
    # a partially refreshed compiled pair.
    _validate_source(import_raw)
    _validate_source(audit_raw)
    lines = ["// Generated from loom/data/presets/{import,import_audit}.pack; do not edit.",
             "// Regenerate: python3 loom/src/import/gen_import_presets.py",
             "// Alternate sources retain the canonical resource identifiers."]
    lines.extend(_resource_lines("Import", IMPORT_SOURCE_IDENTIFIER, import_raw))
    lines.extend(_resource_lines("ImportAudit", AUDIT_SOURCE_IDENTIFIER, audit_raw))
    return ("\n".join(lines) + "\n").encode("ascii")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--import-source", type=Path, default=DEFAULT_IMPORT_SOURCE)
    parser.add_argument("--audit-source", type=Path, default=DEFAULT_AUDIT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--layers-spec", type=Path, default=DEFAULT_LAYERS_SPEC)
    parser.add_argument("--layers-output", type=Path, default=DEFAULT_LAYERS_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        import_raw = args.import_source.read_bytes()
        audit_raw = args.audit_source.read_bytes()
        generated = generate(import_raw, audit_raw)
        layers = generate_layers(args.layers_spec.read_bytes(), import_raw, audit_raw)
        if args.output.resolve() == args.layers_output.resolve():
            raise ValueError("embedding and layers output paths must be distinct")
        outputs = ((args.output, generated), (args.layers_output, layers))
        if args.check:
            for path, expected in outputs:
                if not path.exists() or path.read_bytes() != expected:
                    parser.exit(1, f"import preset generated output is stale or missing: {path}\n")
        else:
            # All source/mapping validation completes before either write.
            for path, expected in outputs:
                path.write_bytes(expected)
    except (OSError, UnicodeError, ValueError, RecursionError) as error:
        parser.exit(1, f"import preset generation failed: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
