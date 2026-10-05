"""Data-defined graph dimensions and projections for offline seeding.

The expression interpreter provides universal operations. Project field names,
dimension identifiers and property bindings live in the profile. No expression
evaluates Python or imports code supplied by a profile.
"""
from __future__ import annotations

from copy import deepcopy
from functools import lru_cache
import json
from pathlib import Path
from string import Formatter
from typing import Any


DEFAULT_PROFILE = Path(__file__).with_name("profiles") / "default.json"
PROFILE_SCHEMA = Path(__file__).with_name("profiles") / "schema.json"


class RecipeUnavailable(ValueError):
    """A preserved declaration requests an operation this runtime cannot run."""


_OPERATIONS = {
    "items": {"value"}, "get": {"value", "key"},
    "collect": {"items", "value"}, "flatten": {"items"}, "union": {"items"},
    "nonempty": {"value"}, "max": {"items"},
    "format": {"template", "values"}, "source_record": {"item", "path"},
    "merge": {"items"},
}
_OPTIONAL_FIELDS = {"get": {"default"}, "max": {"default"}}


def overlay(base: Any, patch: Any) -> Any:
    """Merge objects recursively; arrays/scalars replace, including explicit null.

    No implicit deletion, array merging by guessed identity or field projection.
    The exact base and overlays are retained separately by the experiment.
    """
    if isinstance(base, dict) and isinstance(patch, dict):
        result = deepcopy(base)
        for key, value in patch.items():
            result[key] = overlay(result[key], value) if key in result else deepcopy(value)
        return result
    return deepcopy(patch)


def _check_expression(expression: Any, location: str) -> None:
    if isinstance(expression, list):
        for index, value in enumerate(expression):
            _check_expression(value, f"{location}/{index}")
    elif isinstance(expression, dict):
        special = {key for key in expression if key.startswith("$")}
        if special:
            if special == {"$literal"} and set(expression) == {"$literal"}:
                return
            if special == {"$ref"} and set(expression) == {"$ref"}:
                if not isinstance(expression["$ref"], str) or not expression["$ref"]:
                    raise ValueError(f"invalid profile reference at {location}")
                return
            if special != {"$op"}:
                raise RecipeUnavailable(f"unavailable expression declaration at {location}")
            name = expression["$op"]
            if not isinstance(name, str) or name not in _OPERATIONS:
                raise RecipeUnavailable(f"unavailable projection operation {name!r} at {location}")
            required = _OPERATIONS[name]
            allowed = required | _OPTIONAL_FIELDS.get(name, set()) | {"$op"}
            if not required <= set(expression) or not set(expression) <= allowed:
                raise ValueError(f"invalid {name} expression fields at {location}")
        for key, value in expression.items():
            if key != "$op":
                _check_expression(value, f"{location}/{key}")


def validate_profile(profile: Any) -> dict[str, Any]:
    if not isinstance(profile, dict) or profile.get("schema") != "loom.seeding.recipe/1":
        raise ValueError("invalid seeding profile schema")
    if not isinstance(profile.get("version"), str) or not profile["version"]:
        raise ValueError("profile version must be a nonempty string")
    if not isinstance(profile.get("dimensions"), list):
        raise ValueError("profile dimensions must be an array")
    identifiers: set[str] = set()
    for index, dimension in enumerate(profile["dimensions"]):
        location = f"dimensions/{index}"
        if not isinstance(dimension, dict):
            raise ValueError(f"invalid dimension at {location}")
        required = {"id", "predict", "rows", "label"}
        if not required <= set(dimension):
            raise ValueError(f"missing dimension fields at {location}")
        if set(dimension) - (required | {"select", "bindings", "properties", "eligibility", "extensions"}):
            raise RecipeUnavailable(f"unavailable dimension declaration at {location}")
        identifier = dimension["id"]
        if not isinstance(identifier, str) or not identifier or identifier in identifiers:
            raise ValueError(f"invalid or duplicate dimension id at {location}")
        identifiers.add(identifier)
        if not isinstance(dimension["predict"], bool):
            raise ValueError(f"dimension predict must be boolean at {location}")
        bindings = dimension.get("bindings", {})
        if not isinstance(bindings, dict) or any(not isinstance(k, str) or not k for k in bindings):
            raise ValueError(f"invalid dimension bindings at {location}")
        if set(bindings) & {"project", "operators", "item", "key", "label", "members"}:
            raise ValueError(f"binding shadows a projection input at {location}")
        for field in ("rows", "select", "label", "properties", "bindings"):
            if field in dimension:
                _check_expression(dimension[field], f"{location}/{field}")
        if dimension["predict"] and "properties" not in dimension:
            raise ValueError(f"prediction dimension needs properties at {location}")
        eligibility = dimension.get("eligibility")
        if eligibility is not None:
            fields = {"policy_key", "support_dimension", "union_property", "alternatives_property",
                      "member_property", "application_id_property", "application_source_property",
                      "mapping_relation", "mapping_label_key", "mapping_witness_id_key"}
            if not isinstance(eligibility, dict) or set(eligibility) != fields:
                raise ValueError(f"invalid eligibility bindings at {location}")
            if any(not isinstance(value, str) or not value for value in eligibility.values()):
                raise ValueError(f"eligibility bindings must be nonempty strings at {location}")
    for dimension in profile["dimensions"]:
        if "eligibility" in dimension and dimension["eligibility"]["support_dimension"] not in identifiers:
            raise ValueError("eligibility support dimension is not declared")
    if set(profile) - {"schema", "version", "dimensions", "extensions"}:
        raise RecipeUnavailable("unavailable top-level profile declaration")
    return deepcopy(profile)


def _json_source(raw: bytes) -> Any:
    def nonfinite(value: str) -> None:
        raise ValueError(f"nonfinite JSON value {value!r} in profile")
    return json.loads(raw.decode("utf-8-sig"), parse_constant=nonfinite)


def profile_snapshot(path: Path = DEFAULT_PROFILE,
                     overlay_paths: list[Path] | tuple[Path, ...] = ()) -> tuple[dict[str, Any], list[tuple[Path, bytes]]]:
    """Resolve from the same exact input bytes that the run later freezes."""
    path = Path(path)
    raw = path.read_bytes()
    sources = [(path, raw)]
    profile = validate_profile(_json_source(raw))
    for patch_path in overlay_paths:
        patch_path = Path(patch_path)
        patch_raw = patch_path.read_bytes()
        patch = _json_source(patch_raw)
        if not isinstance(patch, dict):
            raise ValueError("profile overlay must be an object")
        profile = validate_profile(overlay(profile, patch))
        sources.append((patch_path, patch_raw))
    return profile, sources


def load_profile(path: Path = DEFAULT_PROFILE, overlay_paths: list[Path] | tuple[Path, ...] = ()) -> dict[str, Any]:
    return profile_snapshot(path, overlay_paths)[0]


@lru_cache(maxsize=1)
def _default_profile() -> dict[str, Any]:
    return load_profile()


def resolved_profile(profile: dict[str, Any] | None = None) -> dict[str, Any]:
    return _default_profile() if profile is None else validate_profile(profile)


def prediction_dimensions(profile: dict[str, Any] | None = None) -> tuple[str, ...]:
    return tuple(d["id"] for d in resolved_profile(profile)["dimensions"] if d["predict"])


def property_key(dimension: str, label: str) -> str:
    """Injective dimension/label key with unchanged default and historical labels.

    Only the dimension prefix escapes percent and colon. Its terminating colon
    can therefore be located unambiguously, while the complete label (including
    colons, percent, whitespace and Unicode) remains exact. A percent sign is
    escaped first so a literal "%3A" cannot impersonate an encoded colon.
    """
    return dimension.replace("%", "%25").replace(":", "%3A") + ":" + label


def source_record(item: dict[str, Any], path: str) -> dict[str, Any]:
    """Located source evidence; source dates never become unknown know-times."""
    units = item.get("units", []) + ([item["unit"]] if "unit" in item else [])
    locators = [{k: unit[k] for k in ("provider", "conv_id", "node_id", "date") if k in unit}
                for unit in units]
    dates = [unit["date"] for unit in locators if unit.get("date")]
    return {"oracle_path": path, "source_locators": locators,
            "known_at": None, "known_at_status": "not_supplied_by_oracle",
            "source_created_at_max": max(dates) if dates else None}


def _reference(path: str, context: dict[str, Any]) -> Any:
    value: Any = context
    for part in path.split("/"):
        part = part.replace("~1", "/").replace("~0", "~")
        try:
            value = value[int(part)] if isinstance(value, list) else value[part]
        except (KeyError, IndexError, ValueError, TypeError) as exc:
            raise ValueError(f"unbound profile reference {path!r}") from exc
    return value


def evaluate(expression: Any, context: dict[str, Any]) -> Any:
    if isinstance(expression, list):
        return [evaluate(value, context) for value in expression]
    if not isinstance(expression, dict):
        return expression
    if "$literal" in expression:
        return deepcopy(expression["$literal"])
    if "$ref" in expression:
        return _reference(expression["$ref"], context)
    if "$op" not in expression:
        return {key: evaluate(value, context) for key, value in expression.items()}
    op = expression["$op"]
    if op == "get":
        value = evaluate(expression["value"], context)
        key = evaluate(expression["key"], context)
        if not isinstance(value, dict) or not isinstance(key, str):
            raise ValueError("get requires an object and string key")
        if key in value:
            return value[key]
        if "default" in expression:
            return evaluate(expression["default"], context)
        raise ValueError(f"projection field {key!r} is missing")
    if op == "items":
        value = evaluate(expression["value"], context)
        if isinstance(value, dict):
            return [{"key": key, "value": item} for key, item in value.items()]
        if isinstance(value, list):
            return [{"key": str(index), "value": item} for index, item in enumerate(value)]
        raise ValueError("items requires an object or array")
    if op == "collect":
        items = evaluate(expression["items"], context)
        if not isinstance(items, list):
            raise ValueError("collect requires an array")
        return [evaluate(expression["value"], context | {"item": item, "key": str(index)})
                for index, item in enumerate(items)]
    if op == "nonempty":
        return bool(evaluate(expression["value"], context))
    if op in ("flatten", "union", "max"):
        items = evaluate(expression["items"], context)
        if not isinstance(items, list):
            raise ValueError(f"{op} requires an array")
        if op == "max":
            present = [item for item in items if item]
            return max(present) if present else evaluate(expression.get("default"), context)
        if any(not isinstance(item, (list, tuple, set, frozenset)) for item in items):
            raise ValueError(f"{op} requires arrays of members")
        flattened = [member for item in items for member in item]
        return sorted(set(flattened)) if op == "union" else flattened
    if op == "format":
        values = evaluate(expression["values"], context)
        template = evaluate(expression["template"], context)
        if not isinstance(template, str) or not isinstance(values, dict):
            raise ValueError("format requires a string template and object values")
        for _, name, spec, conversion in Formatter().parse(template):
            if name is not None and (name not in values or spec or conversion):
                raise ValueError("format placeholders must directly name supplied values")
        return template.format_map(values)
    if op == "merge":
        items = evaluate(expression["items"], context)
        if not isinstance(items, list) or any(not isinstance(item, dict) for item in items):
            raise ValueError("merge requires an array of objects")
        merged = {}
        for item in items:
            merged.update(item)
        return merged
    if op == "source_record":
        return source_record(evaluate(expression["item"], context), evaluate(expression["path"], context))
    raise RecipeUnavailable(f"unavailable projection operation {op!r}")


def project_graph(project: dict[str, Any], operators: dict[str, dict[str, Any]],
                  profile: dict[str, Any] | None = None) -> tuple[dict[str, frozenset[str]], dict[str, dict[str, Any]]]:
    profile = resolved_profile(profile)
    edges: dict[str, frozenset[str]] = {}
    properties: dict[str, dict[str, Any]] = {}
    for dimension in profile["dimensions"]:
        context = {"project": project, "operators": operators}
        rows = evaluate(dimension["rows"], context)
        if not isinstance(rows, list):
            raise ValueError("dimension rows expression must return an array")
        groups: dict[str, list[Any]] = {}
        contexts: dict[str, dict[str, Any]] = {}
        for row in rows:
            if not isinstance(row, dict) or set(row) != {"key", "value"}:
                raise ValueError("dimension rows must be items operation records")
            row_context = context | {"item": row["value"], "key": row["key"]}
            if "select" in dimension and not evaluate(dimension["select"], row_context):
                continue
            label = evaluate(dimension["label"], row_context)
            if not isinstance(label, str):
                raise ValueError("dimension labels must be strings")
            groups.setdefault(label, []).append(row["value"])
            contexts[label] = row_context  # ungrouped duplicate labels preserve last source, as before
        edges[dimension["id"]] = frozenset(groups)
        if "properties" not in dimension:
            continue
        for label, members in groups.items():
            item_context = contexts[label] | {"label": label, "members": members}
            for name, expression in dimension.get("bindings", {}).items():
                item_context[name] = evaluate(expression, item_context)
            prop = evaluate(dimension["properties"], item_context)
            if not isinstance(prop, dict):
                raise ValueError("dimension properties must return an object")
            if dimension["predict"] and not isinstance(prop.get("expected_properties"), list):
                raise ValueError("prediction properties need expected_properties array")
            key = property_key(dimension["id"], label)
            if key in properties:
                raise ValueError("dimension/label property key collision")
            properties[key] = prop
    return edges, properties
