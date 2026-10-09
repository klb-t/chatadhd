"""Composable syntax parsers; recognition never implies domain interpretation.

Parsing is invoked on demand by the resource layer. It does not create graph
nodes, import into a database, resolve external links, or modify source bytes.
Standard parsers consume the requested member/document, not its enclosing archive.
"""
from __future__ import annotations

import csv
import io
import json
import math
from pathlib import Path
from typing import Any, Callable


class ParseError(ValueError):
    """Input is corrupt, ambiguous, or exceeds the caller's syntax budget."""


class UnsupportedFormat(ParseError):
    """No registered parser (or its optional dependency) is available."""


_PROFILE = Path(__file__).resolve().parents[2] / "data/resource_graph/parsers.json"
_REGISTRY: dict[str, tuple[Callable, dict]] = {}


def _profile() -> dict:
    return json.loads(_PROFILE.read_text(encoding="utf-8"))


def register_parser(format_id: str, parser: Callable, *, descriptor: dict | None = None,
                    replace: bool = False) -> None:
    """Register already trusted executable code; this never loads discovered code."""
    if not isinstance(format_id, str) or not format_id or not callable(parser):
        raise ValueError("invalid_parser_registration")
    if format_id in _REGISTRY and not replace:
        raise ValueError("parser_already_registered")
    desc = dict(descriptor or {})
    desc.setdefault("version", "caller-declared/1")
    desc.setdefault("semantics", "unrecognized")
    _REGISTRY[format_id] = (parser, desc)


def parser_descriptor(format_id: str) -> dict:
    if format_id not in _REGISTRY:
        raise UnsupportedFormat("unsupported_syntax:" + format_id)
    return {"id": format_id, **_REGISTRY[format_id][1]}


def available_parsers() -> dict:
    return {key: parser_descriptor(key) for key in _REGISTRY}


def recognize(data: bytes, candidates=None, *, options: dict | None = None) -> list[dict]:
    """Return syntax alternatives without selecting semantics or exposing values.

    Successful parsing is deliberately weak evidence: arbitrary text is often
    legal CSV and YAML. Callers select a candidate explicitly or under their
    own declared policy. Registry extensions participate without core changes.
    """
    results = []
    for format_id in (list(_REGISTRY) if candidates is None else candidates):
        result = {"format_id": format_id, "recognized": False}
        try:
            result["parser_descriptor"] = parser_descriptor(format_id)
            parse(data, format_id, options=options)
            result.update(recognized=True, evidence="syntax_parse_succeeded",
                          semantic_recognition=False)
        except (ParseError, ImportError) as exc:
            result["diagnostic"] = type(exc).__name__
        results.append(result)
    return results


def _budget(options: dict) -> dict:
    result = {**_profile()["defaults"], **options}
    for key in ("max_bytes", "max_depth", "max_nodes", "max_aliases"):
        if type(result[key]) is not int or result[key] <= 0:
            raise ValueError("invalid_parser_budget:" + key)
    return result


def _check_tree(value: Any, options: dict) -> Any:
    stack, count = [(value, 0)], 0
    while stack:
        item, depth = stack.pop()
        count += 1
        if count > options["max_nodes"] or depth > options["max_depth"]:
            raise ParseError("syntax_tree_budget_exceeded")
        if isinstance(item, dict):
            if any(not isinstance(key, str) for key in item):
                raise ParseError("syntax_tree_non_string_key")
            stack.extend((child, depth + 1) for child in item.values())
        elif isinstance(item, list):
            stack.extend((child, depth + 1) for child in item)
        elif isinstance(item, float) and not math.isfinite(item):
            raise ParseError("non_finite_syntax_number")
        elif item is not None and type(item) not in (str, int, float, bool):
            raise ParseError("unsupported_syntax_value_type")
    return value


def parse(data: bytes, format_id: str, *, options: dict | None = None) -> Any:
    """Parse a single document to an addressable, JSON-compatible syntax tree.

Budgets here are configurable caller policy, independent of transport/container
environment safety ceilings. Source bytes remain authoritative for lexical loss.
"""
    if not isinstance(data, bytes):
        raise TypeError("parser_requires_bytes")
    policy = _budget(options or {})
    if len(data) > policy["max_bytes"]:
        raise ParseError("syntax_byte_budget_exceeded")
    if format_id not in _REGISTRY:
        raise UnsupportedFormat("unsupported_syntax:" + format_id)
    try:
        result = _REGISTRY[format_id][0](data, policy)
        return _check_tree(result, policy)
    except (ParseError, ImportError):
        raise
    except Exception as exc:
        # Parser exception text may contain source content; retain only type.
        raise ParseError("syntax_parse_failed:" + type(exc).__name__) from None


def _json(data: bytes, options: dict) -> Any:
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ParseError("duplicate_json_member")
            result[key] = value
        return result

    def constant(_):
        raise ParseError("non_json_numeric_constant")

    return json.loads(data.decode(options["encoding"]), object_pairs_hook=pairs,
                      parse_constant=constant)


def _yaml(data: bytes, options: dict) -> Any:
    try:
        import yaml
    except ImportError:
        raise UnsupportedFormat("dependency_unavailable:PyYAML") from None
    text = data.decode(options["encoding"])
    depth = count = aliases = 0
    # Events are streamed before composition; aliases are never exponentially
    # expanded by construction. Tags never instantiate application/Python objects.
    for event in yaml.parse(text, Loader=yaml.SafeLoader):
        count += 1
        if isinstance(event, (yaml.events.MappingStartEvent, yaml.events.SequenceStartEvent)):
            depth += 1
        elif isinstance(event, (yaml.events.MappingEndEvent, yaml.events.SequenceEndEvent)):
            depth -= 1
        elif isinstance(event, yaml.events.AliasEvent):
            aliases += 1
        if count > options["max_nodes"] or depth > options["max_depth"] or aliases > options["max_aliases"]:
            raise ParseError("yaml_syntax_budget_exceeded")
    loader = yaml.SafeLoader(text)
    seen = {}

    def build(node, pointer=""):
        if id(node) in seen:
            return {"$yaml_alias": seen[id(node)]}
        seen[id(node)] = pointer
        if isinstance(node, yaml.ScalarNode):
            if node.tag == "tag:yaml.org,2002:merge":
                return node.value
            if node.tag in loader.yaml_constructors and node.tag not in (
                    "tag:yaml.org,2002:binary", "tag:yaml.org,2002:timestamp"):
                return loader.construct_object(node)
            return {"$yaml_tag": node.tag, "$yaml_value": node.value}
        if isinstance(node, yaml.SequenceNode):
            child_pointer = pointer if node.tag == "tag:yaml.org,2002:seq" else pointer + "/$yaml_value"
            result = [build(child, child_pointer + "/" + str(i)) for i, child in enumerate(node.value)]
            if node.tag == "tag:yaml.org,2002:seq":
                return result
            return {"$yaml_tag": node.tag, "$yaml_value": result}
        # Keep every mapping key, including non-string and tagged keys.
        child_pointer = pointer if node.tag == "tag:yaml.org,2002:map" else pointer + "/$yaml_value"
        if all(isinstance(k, yaml.ScalarNode) and k.tag in ("tag:yaml.org,2002:str", "tag:yaml.org,2002:merge")
               for k, _ in node.value):
            result = {}
            for key, child in node.value:
                name = key.value
                if name in result:
                    raise ParseError("duplicate_yaml_member")
                escaped = name.replace("~", "~0").replace("/", "~1")
                result[name] = build(child, child_pointer + "/" + escaped)
        else:
            result = {"$yaml_mapping": [
                [build(key, child_pointer + "/$yaml_mapping/" + str(i) + "/0"),
                 build(child, child_pointer + "/$yaml_mapping/" + str(i) + "/1")]
                for i, (key, child) in enumerate(node.value)]}
        if node.tag == "tag:yaml.org,2002:map":
            return result
        return {"$yaml_tag": node.tag, "$yaml_value": result}

    try:
        node = loader.get_single_node()
        return None if node is None else build(node)
    finally:
        loader.dispose()


def _xml(data: bytes, options: dict) -> Any:
    try:
        from defusedxml.ElementTree import DefusedXMLParser
    except ImportError:
        raise UnsupportedFormat("dependency_unavailable:defusedxml") from None
    from xml.etree import ElementTree as ET
    parser = DefusedXMLParser(target=ET.TreeBuilder(insert_comments=True, insert_pis=True),
                             forbid_dtd=True, forbid_entities=True, forbid_external=True)
    root = ET.fromstring(data, parser=parser)

    def project(element, depth=0):
        if depth > options["max_depth"]:
            raise ParseError("xml_depth_budget_exceeded")
        kind = "element"
        if element.tag is ET.Comment:
            kind = "comment"
        elif element.tag is ET.ProcessingInstruction:
            kind = "processing_instruction"
        return {"kind": kind, "tag": element.tag if isinstance(element.tag, str) else None,
                "attributes": dict(element.attrib), "text": element.text, "tail": element.tail,
                "children": [project(child, depth + 1) for child in element]}
    return project(root)


def _csv(data: bytes, options: dict) -> Any:
    reader = csv.reader(io.StringIO(data.decode(options["encoding"]), newline=""),
                        delimiter=options["csv_delimiter"], strict=True)
    result, cells = [], 0
    for row in reader:
        cells += len(row) + 1
        if cells > options["max_nodes"]:
            raise ParseError("csv_syntax_budget_exceeded")
        result.append(row)
    return result


for _format, _handler in (("json", _json), ("yaml", _yaml), ("xml", _xml), ("csv", _csv)):
    register_parser(_format, _handler, descriptor=_profile()["formats"][_format])
