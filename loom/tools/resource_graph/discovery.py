"""Composable, source-preserving discovery of declarative structural adapters.

This is an adapter producer, not a workflow engine or graph/configuration store.
JSON Schema, inspected structure, and trusted registry mappings are independent
interpretations. A sample establishes structural compatibility, never domain
semantics or dataset-wide uniqueness. No sample is sent to a remote service.
"""
from __future__ import annotations

from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit

from jsonschema import Draft202012Validator
from jsonschema.exceptions import SchemaError
from referencing import Registry
from referencing.exceptions import Unresolvable

from loom.tools.seeding.method_graph import canonical, pointer, walk
from loom.tools.structure.agentic_graph_v1 import packet as codec
from loom.tools.structure import method_graph_export_v1 as graph

DEFAULT_REGISTRY = Path(__file__).resolve().parents[2] / "data/resource_graph/discovery.json"


def load_registry(path=DEFAULT_REGISTRY):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if data.get("schema") != "loom.resource_discovery_registry/1":
        raise ValueError("resource_discovery_registry_schema_invalid")
    return data


def _hash(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def _path(tokens):
    return "".join("/" + str(token).replace("~", "~0").replace("/", "~1") for token in tokens)


def _selector_key(key):
    # A literal object key "*" must not become a wildcard traversal.
    return {"key": key} if key == "*" else key


def _selected(value, selectors, prefix=""):
    """Compose the existing walker with literal wildcard-key addressing."""
    literal = next((i for i, token in enumerate(selectors) if isinstance(token, dict)), None)
    if literal is None:
        yield from walk(value, selectors, prefix)
        return
    for location, parent in walk(value, selectors[:literal], prefix):
        key = selectors[literal]["key"]
        if isinstance(parent, dict) and key in parent:
            yield from _selected(parent[key], selectors[literal + 1:], location + _path([key]))


def _schema_check(value, schema):
    """Validate with an offline registry: external $refs are never downloaded."""
    try:
        Draft202012Validator.check_schema(schema)
        errors = list(Draft202012Validator(schema, registry=Registry()).iter_errors(value))
        return {"status": "failed" if errors else "passed", "errors": [
            {"selector": _path(error.absolute_path), "schema_selector": _path(error.absolute_schema_path),
             "validator": str(error.validator)} for error in errors]}
    except SchemaError:
        return {"status": "invalid_schema", "errors": []}
    except Unresolvable:
        return {"status": "unresolved_schema_reference", "errors": []}


def _mapping(rules, *, basis, schema=None):
    result = {"schema": "loom.resource_mapping/1", "version": "1", "rules": rules,
              "preserve_unknown": True, "basis": basis, "domain_semantics": "unrecognized"}
    if schema is not None:
        result["declared_schema"] = deepcopy(schema)
    result["id"] = "resource-mapping:" + _hash(result)
    return result


def _rule(selector, fields, kind, *, identity_candidates=()):
    return {"selector": selector, "kind": kind,
            "fields": [{"name": name, "selector": [name]} for name in fields],
            "identity": {"mode": "source_selector", "scope": "source_version"},
            "identity_hypotheses": [{"selector": [name], "status": "hypothesis",
                                      "evidence": "unique_nonnull_scalar_in_inspected_sample",
                                      "logical_identity_established": False}
                                     for name in identity_candidates]}


def validate_mapping(mapping):
    if (not isinstance(mapping, dict) or mapping.get("schema") != "loom.resource_mapping/1"
            or mapping.get("preserve_unknown") is not True or not isinstance(mapping.get("rules"), list)):
        raise ValueError("resource_mapping_contract_invalid")
    for rule in mapping["rules"]:
        if (not isinstance(rule.get("kind"), str) or not rule["kind"]
                or not isinstance(rule.get("selector"), list)
                or any(not isinstance(x, str) and not (isinstance(x, dict) and set(x) == {"key"} and isinstance(x["key"], str)) for x in rule["selector"])
                or not isinstance(rule.get("fields"), list)
                or rule.get("identity") != {"mode": "source_selector", "scope": "source_version"}):
            raise ValueError("resource_mapping_rule_invalid")
        names = set()
        for field in rule["fields"]:
            if (not isinstance(field, dict) or not isinstance(field.get("name"), str)
                    or field["name"] in names or not isinstance(field.get("selector"), list)
                    or any(not isinstance(x, str) for x in field["selector"])):
                raise ValueError("resource_mapping_field_invalid")
            names.add(field["name"])
    codec.validate_json_resources(mapping)
    return mapping


def iter_mapping(value, mapping, *, source=None, inline=False):
    """Yield selected records without copying/materializing the whole source.

    Records identify physical source selectors. Candidate logical identities stay
    hypotheses. Unknown fields carry references, including when values are omitted.
    The caller chooses whether to embed each selected field with ``inline=True``.
    """
    validate_mapping(mapping)
    source = {} if source is None else source
    for rule_number, rule in enumerate(mapping["rules"]):
        for location, record in _selected(value, rule["selector"]):
            fields, covered = [], set()
            for spec in rule["fields"]:
                field_location = _path(spec["selector"])
                try:
                    item = pointer(record, field_location)
                except (KeyError, IndexError, TypeError, ValueError):
                    fields.append({"name": spec["name"], "status": "unavailable",
                                   "source_reference": {**source, "selector": location + field_location}})
                    continue
                entry = {"name": spec["name"], "status": "available",
                         "source_reference": {**source, "selector": location + field_location}}
                if inline:
                    entry["value"] = deepcopy(item)
                fields.append(entry)
                if spec["selector"]:
                    covered.add(spec["selector"][0])
            unknown = []
            if isinstance(record, dict):
                for key, item in record.items():
                    if key in covered:
                        continue
                    entry = {"name": key, "source_reference": {**source, "selector": location + _path([key])}}
                    if inline:
                        entry["value"] = deepcopy(item)
                    unknown.append(entry)
            yield {"kind": rule["kind"], "source_reference": {**source, "selector": location},
                   "identity": {"mode": "source_selector", "scope": "source_version"},
                   "identity_hypotheses": deepcopy(rule.get("identity_hypotheses", [])),
                   "mapping": {"id": mapping.get("id"), "version": mapping["version"], "rule": rule_number},
                   "fields": fields, "unknown_fields": unknown,
                   "domain_semantics": mapping.get("domain_semantics", "unrecognized")}


def apply_mapping(value, mapping, *, source=None, inline=False):
    """Explicit eager convenience; ``iter_mapping`` is the reference/lazy API."""
    return list(iter_mapping(value, mapping, source=source, inline=inline))


class Discovery:
    """Strategy registry whose entries compose operators supplied by the caller.

    The data registry does not import Python entrypoints or execute documentation.
    Adding a declarative adapter or composing registered operators needs no change
    to graph schemas, renderer code, or this class's dispatch mechanism.
    """
    def __init__(self, registry=None, *, strategy_operators=None, document_fetch=None, policy=None):
        self.registry = load_registry() if registry is None else deepcopy(registry)
        if self.registry.get("schema") != "loom.resource_discovery_registry/1":
            raise ValueError("resource_discovery_registry_schema_invalid")
        self.policy = {**self.registry["policy"], **(policy or {})}
        for key, value in self.registry["inspection"].items():
            if type(value) is not int or value <= 0:
                raise ValueError("resource_discovery_inspection_budget_invalid:" + key)
        self.document_fetch = document_fetch
        self.operators = {"local_registry": self._local, "declared_schema": self._schema,
                          "sample_structure": self._sample, "online_documentation": self._documentation,
                          "optional_model": self._model, "compose": self._compose}
        self.operators.update(strategy_operators or {})

    def discover(self, value, declared_schema=None, *, documentation=None, source=None):
        context = {"value": value, "declared_schema": declared_schema,
                   "documentation": documentation or [], "source": deepcopy(source or {})}
        alternatives, trace = [], []
        for strategy in self.registry["strategies"]:
            produced, event = self._execute(strategy, context, ())
            alternatives.extend(produced)
            trace.append(event)
        return {"schema": "loom.resource_discovery/1", "version": "1",
                "source": context["source"], "source_preserved": True,
                "registry_sha256": _hash(self.registry), "policy": deepcopy(self.policy),
                "alternatives": alternatives, "trace": trace, "selected_adapter": None,
                "model_calls": 0, "private_samples_sent_online": 0,
                "interpretation_scope": "inspected_structure_and_declared_schemas_only"}

    def _execute(self, strategy, context, stack):
        ident = strategy["id"]
        if ident in stack:
            raise ValueError("resource_discovery_strategy_cycle")
        operation = strategy["operator"]
        operator = self.operators.get(operation)
        if operator is None:
            return [], {"strategy": ident, "operator": operation, "status": "unavailable"}
        alternatives, details = operator(context, strategy, stack + (ident,))
        return alternatives, {"strategy": ident, "operator": operation, **details}

    def _candidate(self, mapping, value, strategy, *, recognition, provenance=None, schema_result=None):
        descriptor = {"id": mapping.get("id", "resource-mapping:" + _hash(mapping)),
                      "adapter_type": "declarative_mapping", "mapping": deepcopy(mapping),
                      "recognition": recognition,
                      "availability": {"status": "available", "operator": "iter_mapping"},
                      "validation": {"status": "not_run"},
                      "permission": {"status": "allowed" if self.policy["allow_declarative"] else "denied",
                                     "basis": "allow_declarative"},
                      "provenance": {"strategy": strategy["id"], **(provenance or {})},
                      "structures": [{"kind": rule["kind"], "selector": rule["selector"],
                                      "fields": deepcopy(rule["fields"]), "addressing": "source_selector"}
                                     for rule in mapping.get("rules", [])]}
        try:
            validate_mapping(mapping)
            count = 0
            complete = True
            for _ in iter_mapping(value, mapping):
                if count == self.registry["inspection"]["max_records_per_validation"]:
                    complete = False
                    break
                count += 1
            descriptor["validation"] = {"status": "passed" if complete else "partial", "records_checked": count,
                                         "scope": "structural_mapping_of_available_sample", "complete": complete,
                                         "domain_semantics_validated": False}
        except (ValueError, KeyError, TypeError, IndexError) as error:
            descriptor["validation"] = {"status": "failed", "error_type": type(error).__name__}
        if schema_result is not None:
            descriptor["validation"]["schema_validation"] = schema_result
            if schema_result["status"] != "passed":
                descriptor["validation"]["status"] = "failed" if schema_result["status"] in ("failed", "invalid_schema") else "partial"
        return descriptor

    def _local(self, context, strategy, stack):
        results = []
        for adapter in self.registry["adapters"]:
            recognized = _schema_check(context["value"], adapter.get("matches_schema", True))
            if recognized["status"] != "passed":
                continue
            if "mapping" not in adapter:
                results.append({"id": adapter["id"], "recognition": {"status": "declared", "capabilities": adapter.get("capabilities", [])},
                                "availability": {"status": "unavailable"}, "validation": {"status": "not_run"},
                                "permission": {"status": "not_evaluated"}, "provenance": {"strategy": strategy["id"]}})
                continue
            results.append(self._candidate(adapter["mapping"], context["value"], strategy,
                recognition={"status": "declared", "domain_semantics": adapter.get("domain_semantics", "unrecognized")},
                provenance={"registry_adapter": adapter["id"]}, schema_result=recognized))
        return results, {"status": "complete", "candidates": len(results)}

    def _schema(self, context, strategy, stack):
        schema = context["declared_schema"]
        if schema is None:
            return [], {"status": "unavailable", "reason": "schema_not_declared"}
        validation = _schema_check(context["value"], schema)
        rules, pending, count = [], [(schema, [], 0)] if validation["status"] != "invalid_schema" else [], 0
        limits = self.registry["inspection"]
        incomplete = validation["status"] == "invalid_schema"
        while pending:
            current, location, depth = pending.pop()
            count += 1
            if count > limits["max_nodes"] or depth > limits["max_depth"]:
                incomplete = True
                continue
            if not isinstance(current, dict):
                continue
            if current.get("type") == "object" or "properties" in current:
                properties = current.get("properties", {})
                if not isinstance(properties, dict):
                    incomplete = True
                    continue
                rules.append(_rule(location, properties, self.registry["vocabulary"]["record_kind"]))
                pending.extend((child, location + [_selector_key(name)], depth + 1) for name, child in properties.items())
            if current.get("type") == "array" and isinstance(current.get("items"), dict):
                pending.append((current["items"], location + ["*"], depth + 1))
            if any(key in current for key in ("$ref", "$dynamicRef", "oneOf", "anyOf", "allOf", "patternProperties", "prefixItems")):
                incomplete = True
        mapping = _mapping(rules, basis="declared_schema", schema=schema)
        result = self._candidate(mapping, context["value"], strategy,
            recognition={"status": "declared", "domain_semantics": "unrecognized", "projection_complete": not incomplete},
            provenance={"schema_sha256": _hash(schema)}, schema_result=validation)
        return [result], {"status": "partial" if incomplete else "complete", "candidates": 1}

    def _sample(self, context, strategy, stack):
        limits = self.registry["inspection"]
        pending, rules, inspected, incomplete = [(context["value"], [], 0)], [], 0, False
        while pending:
            current, location, depth = pending.pop()
            inspected += 1
            if inspected > limits["max_nodes"] or depth > limits["max_depth"]:
                incomplete = True
                continue
            if isinstance(current, dict):
                rules.append(_rule(location, current, self.registry["vocabulary"]["record_kind"]))
                pending.extend((child, location + [_selector_key(str(key))], depth + 1) for key, child in current.items()
                               if isinstance(child, (dict, list)))
            elif isinstance(current, list):
                remaining = max(0, limits["max_nodes"] - inspected)
                sampled = current[:remaining]
                if len(sampled) < len(current):
                    incomplete = True
                inspected += len(sampled)
                objects = [row for row in sampled if isinstance(row, dict)]
                if objects and len(objects) == len(sampled):
                    fields = sorted({key for row in objects for key in row})
                    candidates = []
                    for name in fields:
                        values = [row.get(name) for row in objects]
                        if (all(type(item) in (str, int, float, bool) for item in values)
                                and len({_hash(item) for item in values}) == len(values)):
                            candidates.append(name)
                    rules.append(_rule(location + ["*"], fields, self.registry["vocabulary"]["record_kind"], identity_candidates=candidates))
                    # Nested containers are still addressable by references. Avoid
                    # duplicating a collection's rows as separate mapping rules.
                else:
                    rule = _rule(location + ["*"], [], self.registry["vocabulary"]["value_kind"])
                    rule["fields"] = [{"name": "value", "selector": []}]
                    rules.append(rule)
                    pending.extend((child, location + [str(index)], depth + 1) for index, child in enumerate(sampled)
                                   if isinstance(child, (dict, list)))
            elif current is None or type(current) in (str, int, float, bool):
                rule = _rule(location, [], self.registry["vocabulary"]["value_kind"])
                rule["fields"] = [{"name": "value", "selector": []}]
                rules.append(rule)
            else:
                incomplete = True
        mapping = _mapping(rules, basis="sample_structure")
        candidate = self._candidate(mapping, context["value"], strategy,
            recognition={"status": "observed_structure" if rules else "unrecognized", "domain_semantics": "unrecognized",
                         "projection_complete": not incomplete, "sample_is_population": False},
            provenance={"inspected_containers": min(inspected, limits["max_nodes"])})
        return [candidate], {"status": "partial" if incomplete else "complete", "candidates": 1}

    def _documentation(self, context, strategy, stack):
        if not self.policy["allow_online_documentation"]:
            return [], {"status": "denied", "reason": "online_documentation_policy"}
        if self.document_fetch is None:
            return [], {"status": "unavailable", "reason": "controlled_document_transport_missing"}
        results, receipts = [], []
        for spec in context["documentation"]:
            parsed = urlsplit(spec["url"])
            if (parsed.scheme not in ("https", "http") or parsed.username or parsed.password
                    or parsed.query or parsed.fragment or not parsed.hostname):
                receipts.append({"status": "denied", "reason": "documentation_locator_contains_credentials_or_unsupported_components"})
                continue
            try:
                fetched = self.document_fetch(spec["url"])
                raw = fetched["body"]
                raw = raw.encode("utf-8") if isinstance(raw, str) else raw
                provenance = {"url": spec["url"], "version": fetched.get("version"),
                              "acquired_at": fetched.get("acquired_at") or datetime.now(timezone.utc).isoformat(),
                              "sha256": hashlib.sha256(raw).hexdigest(), "interpretation": "data_only"}
                decoded = json.loads(raw)
            except (OSError, ValueError, TypeError, KeyError):
                receipts.append({"url": spec["url"], "status": "unavailable_or_unsupported_document"})
                continue
            # The path and interpretation are caller-owned instructions. Text
            # inside the document is never executable or an agent instruction.
            try:
                schema = pointer(decoded, spec.get("schema_pointer", ""))
                if not isinstance(schema, (dict, bool)):
                    raise ValueError("documentation_schema_invalid")
                candidates, _ = self._schema({**context, "declared_schema": schema}, strategy, stack)
                for candidate in candidates:
                    candidate["provenance"]["documentation"] = provenance
                results.extend(candidates)
                receipts.append({**provenance, "status": "retrieved"})
            except (KeyError, IndexError, TypeError, ValueError):
                receipts.append({**provenance, "status": "unsupported_schema_location"})
        return results, {"status": "complete", "documents": receipts}

    def _model(self, context, strategy, stack):
        return [], {"status": "unavailable", "reason": "model_operator_not_configured",
                    "permission": "allowed" if self.policy["allow_model"] else "denied", "model_calls": 0}

    def _compose(self, context, strategy, stack):
        by_id = {item["id"]: item for item in self.registry["strategies"]}
        alternatives, trace = [], []
        for ident in strategy.get("strategies", []):
            if ident not in by_id:
                trace.append({"strategy": ident, "status": "unavailable"})
                continue
            found, event = self._execute(by_id[ident], context, stack)
            alternatives.extend(found)
            trace.append(event)
        return alternatives, {"status": "complete", "composition": trace}


class ExecutableAdapterGate:
    """Explicitly trusted code only, tested in a fresh resource-limited process.

    Process separation is NOT a hostile-code security sandbox. It does not deny
    filesystem/network syscalls. Never test an unknown fetched program here:
    its exact digest must already be trusted by caller policy. A genuine hostile
    code sandbox can replace this gate without changing discovery or GraphPacket.
    """
    def __init__(self, *, policy=None, resources=None):
        config = load_registry()
        self.policy = {**config["policy"], **(policy or {})}
        self.resources = {**config["test_resources"], **(resources or {})}
        self._validated = {}

    def test(self, code, fixture):
        digest = hashlib.sha256(code.encode("utf-8")).hexdigest()
        result = {"code_sha256": digest, "fixture_sha256": _hash(fixture), "status": "denied",
                  "isolation": "fresh_process_sanitized_environment_resource_limits",
                  "hostile_code_sandbox": False, "execution": False}
        if digest not in self.policy["executable_test_sha256"]:
            return result
        try:
            import resource  # POSIX execution boundary; explicit unsupported elsewhere.
        except ImportError:
            return {**result, "status": "unavailable", "reason": "posix_resource_limits_unavailable"}
        limits = self.resources
        if any(type(limits[key]) not in (int, float) or limits[key] <= 0 for key in limits):
            raise ValueError("executable_adapter_test_resource_limits_invalid")
        wrapper = ("import resource\n"
                   f"resource.setrlimit(resource.RLIMIT_CPU, ({int(limits['cpu_seconds'])}, {int(limits['cpu_seconds'])}))\n"
                   f"resource.setrlimit(resource.RLIMIT_AS, ({int(limits['memory_bytes'])}, {int(limits['memory_bytes'])}))\n"
                   f"resource.setrlimit(resource.RLIMIT_FSIZE, ({int(limits['output_bytes'])}, {int(limits['output_bytes'])}))\n"
                   f"exec(compile({code!r}, 'trusted_adapter', 'exec'))\n")
        with tempfile.TemporaryDirectory(prefix="resource-adapter-") as temporary:
            path = Path(temporary)
            script = path / "adapter.py"
            script.write_text(wrapper, encoding="utf-8")
            with (path / "stdout").open("w+b") as stdout, (path / "stderr").open("w+b") as stderr:
                try:
                    process = subprocess.run([sys.executable, "-I", str(script)], input=canonical(fixture),
                        stdout=stdout, stderr=stderr, cwd=path, env={}, timeout=limits["timeout_seconds"], check=False)
                    result.update(execution=True, returncode=process.returncode)
                    stdout.seek(0)
                    raw = stdout.read(int(limits["output_bytes"]) + 1)
                    if process.returncode or len(raw) > limits["output_bytes"]:
                        return {**result, "status": "failed"}
                    output = json.loads(raw)
                    codec.validate_json_resources(output)
                    result.update(status="passed", output=output, output_sha256=_hash(output))
                except subprocess.TimeoutExpired:
                    return {**result, "status": "timeout", "execution": True}
                except (ValueError, UnicodeDecodeError):
                    return {**result, "status": "failed", "execution": True}
        receipt = _hash(result)
        result["receipt"] = receipt
        self._validated[receipt] = deepcopy(result)
        return result

    def activate(self, code, receipt):
        digest = hashlib.sha256(code.encode("utf-8")).hexdigest()
        evidence = self._validated.get(receipt)
        if (digest not in self.policy["executable_activate_sha256"] or not evidence
                or evidence["code_sha256"] != digest or evidence["status"] != "passed"):
            return {"status": "denied", "code_sha256": digest}
        # Return an activation descriptor; importing/executing it belongs to the
        # caller's existing workflow/operator mechanism, not a new execution loop.
        return {"status": "allowed", "code_sha256": digest, "test_receipt": receipt,
                "scope": "trusted_code_policy_plus_fixture_json_protocol", "domain_semantics_validated": False}


def export_discovery_packet(report, *, observed_on, vocabulary=None):
    """Export method, adapters, mappings and evidence via existing native records."""
    vocabulary = load_registry()["vocabulary"] if vocabulary is None else vocabulary
    source = graph.source_record("resource-discovery.json", canonical(report), observed_on)
    method = graph.entity(vocabulary["method_kind"], {"registry_sha256": report["registry_sha256"]},
                          "resource_discovery", observed_on,
                          {"registry_sha256": report["registry_sha256"], "version": report["version"]})
    run = graph.entity(vocabulary["run_kind"], {"report_sha256": _hash(report)}, "resource_discovery_run", observed_on,
                       {"trace": deepcopy(report["trace"]), "policy": deepcopy(report["policy"]),
                        "source": deepcopy(report["source"]), "model_calls": report["model_calls"]})
    entities, claims, seen = [method, run], [], {method["id"], run["id"]}
    def link(subject, predicate, object_id, location):
        claims.append(graph.claim(subject, predicate, object_id=object_id, source=source,
                                  location=location, observed_on=observed_on,
                                  method_version=report["version"], run_id=run["id"],
                                  measurement_kind="resource_discovery_mechanism"))
    link(run["id"], vocabulary["produced_by"], method["id"], "/registry_sha256")
    for index, adapter in enumerate(report["alternatives"]):
        location = "/alternatives/" + str(index)
        item = graph.entity(vocabulary["adapter_kind"], {"run": run["id"], "index": index}, adapter["id"], observed_on, deepcopy(adapter))
        entities.append(item)
        link(item["id"], vocabulary["produced_by"], method["id"], location)
        if "mapping" in adapter:
            mapping = graph.entity(vocabulary["mapping_kind"], adapter["mapping"], adapter["mapping"]["id"], observed_on, adapter["mapping"])
            if mapping["id"] not in seen:
                entities.append(mapping)
                seen.add(mapping["id"])
            link(item["id"], vocabulary["uses_mapping"], mapping["id"], location + "/mapping")
        evidence = graph.entity(vocabulary["evidence_kind"], {"adapter": item["id"], "validation": adapter["validation"]},
                                "resource_adapter_validation", observed_on, adapter["validation"])
        entities.append(evidence)
        link(evidence["id"], vocabulary["evaluates_adapter"], item["id"], location + "/validation")
    return codec.make_packet(entities=entities, claims=claims, sources=[source],
        task={"kind": "resource_discovery", "native_execution": "not_asserted_by_export", "model_calls": 0},
        origin={"kind": "recorded", "actor": "resource_graph.discovery", "model": None,
                "recipe_sha256": report["registry_sha256"], "response_sha256": None})
