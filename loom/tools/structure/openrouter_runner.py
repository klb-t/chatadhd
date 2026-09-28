#!/usr/bin/env python3
"""Bounded, resumable OpenRouter experiments; no canonical graph writes."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import stat
import sys
import tempfile
import time
import urllib.error
import urllib.request

VERSION = "openrouter-runner/1"
MANIFEST_SCHEMA = "loom.openrouter_manifest/1"
API_ROOT = "https://openrouter.ai/api/v1"
MAX_INPUT_BYTES = 16 * 1024 * 1024
MAX_RESPONSE_BYTES = 2 * 1024 * 1024
MAX_BODY_BYTES = 256 * 1024
TIMEOUT_SECONDS = 60
_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,95}\Z")
_BODY_KEYS = {"model", "messages", "max_tokens", "temperature", "top_p", "seed",
              "response_format", "provider", "stream", "reasoning"}


class RunnerError(ValueError):
    """Messages are fixed codes; never include request, credential, or remote text."""


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
                      allow_nan=False).encode("utf-8")


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise RunnerError("duplicate_json_key")
        result[key] = value
    return result


def parse_json(raw):
    try:
        return json.loads(raw, object_pairs_hook=_pairs,
                          parse_constant=lambda _: (_ for _ in ()).throw(RunnerError("non_finite_json")))
    except (UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        if isinstance(exc, RunnerError):
            raise
        raise RunnerError("invalid_json") from None


def _money(value):
    if type(value) not in (int, float, str) or isinstance(value, bool):
        raise RunnerError("invalid_money")
    if len(str(value)) > 80:
        raise RunnerError("invalid_money")
    try:
        number = Decimal(str(value))
    except InvalidOperation:
        raise RunnerError("invalid_money") from None
    if (not number.is_finite() or number < 0 or number > Decimal("1000000")
            or number.as_tuple().exponent < -18):
        raise RunnerError("invalid_money")
    return number


def _amount(value):
    return format(value, "f")


def _bounded(value):
    stack, count, size = [(value, 0)], 0, 0
    while stack:
        item, depth = stack.pop()
        count += 1
        if count > 200000 or depth > 64:
            raise RunnerError("manifest_complexity_limit")
        if isinstance(item, dict):
            if len(item) > 200000 or any(not isinstance(k, str) for k in item):
                raise RunnerError("invalid_json_object")
            stack.extend((k, depth + 1) for k in item)
            stack.extend((v, depth + 1) for v in item.values())
        elif isinstance(item, list):
            if len(item) > 200000:
                raise RunnerError("manifest_complexity_limit")
            stack.extend((v, depth + 1) for v in item)
        elif isinstance(item, str):
            try:
                size += len(item.encode("utf-8"))
            except UnicodeError:
                raise RunnerError("invalid_utf8") from None
            if size > MAX_INPUT_BYTES:
                raise RunnerError("manifest_size_limit")
        elif type(item) in (int, float):
            if (type(item) is float and not math.isfinite(item)) or (type(item) is int and item.bit_length() > 128):
                raise RunnerError("invalid_json_number")
        elif item is not None and type(item) is not bool:
            raise RunnerError("invalid_json_value")


def _keys(value, required, optional=()):
    if not isinstance(value, dict) or not set(required) <= set(value) or set(value) - set(required) - set(optional):
        raise RunnerError("invalid_object_fields")


def _timestamp(value):
    try:
        result = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if result.tzinfo is None:
            raise ValueError
        return result.timestamp()
    except (ValueError, TypeError, AttributeError, OverflowError):
        raise RunnerError("invalid_timestamp") from None


def _utc():
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def estimate_reservation(body):
    """USD allowance, not a tokenizer/billing guarantee. Prices are USD/million."""
    prices = body["provider"]["max_price"]
    allowance = len(canonical(body)) + 1024 + 32 * len(body["messages"])
    amount = (Decimal(allowance) * _money(prices["prompt"]) +
              Decimal(body["max_tokens"]) * _money(prices["completion"])) / Decimal(1000000)
    return {"input_token_allowance": allowance, "minimum_reservation_usd": _amount(amount)}


def plan_manifest(manifest):
    """Validate an exact text-only experiment. This function does no network I/O."""
    _bounded(manifest)
    _keys(manifest, {"schema", "experiment_id", "budget_usd", "max_requests", "requests", "pricing_evidence"}, {"metadata"})
    if manifest["schema"] != MANIFEST_SCHEMA or not isinstance(manifest["experiment_id"], str) or not _ID.fullmatch(manifest["experiment_id"]):
        raise RunnerError("invalid_manifest_identity")
    budget = _money(manifest["budget_usd"])
    if budget <= 0:
        raise RunnerError("budget_must_be_positive")
    maximum = manifest["max_requests"]
    if type(maximum) is not int or not 1 <= maximum <= 256:
        raise RunnerError("invalid_request_limit")
    requests = manifest["requests"]
    if not isinstance(requests, list) or not 1 <= len(requests) <= maximum:
        raise RunnerError("request_limit_exceeded")
    evidence = manifest["pricing_evidence"]
    if not isinstance(evidence, list) or not 1 <= len(evidence) <= 256:
        raise RunnerError("missing_pricing_evidence")
    price_index = {}
    for entry in evidence:
        _keys(entry, {"model", "provider", "pricing", "source_url", "retrieved_at"})
        if not isinstance(entry["model"], str) or not isinstance(entry["provider"], str):
            raise RunnerError("invalid_pricing_identity")
        url = entry["source_url"]
        if not isinstance(url, str) or url != API_ROOT + "/models/" + entry["model"] + "/endpoints":
            raise RunnerError("endpoint_pricing_evidence_required")
        _timestamp(entry["retrieved_at"])
        rates = entry["pricing"]
        if not isinstance(rates, dict) or not {"prompt", "completion"} <= rates.keys():
            raise RunnerError("missing_token_prices")
        rates = {key: _money(value) for key, value in rates.items()}
        for name, rate in rates.items():
            if name in ("prompt", "completion"):
                continue
            if name == "input_cache_read" and rate <= rates["prompt"]:
                continue
            # Text-only requests cannot enable search; preserve its catalog price.
            if name == "web_search":
                continue
            if name == "discount" and rate <= 1:
                continue
            if rate != 0:
                raise RunnerError("unaccounted_pricing_charge")
        pair = (entry["model"], entry["provider"])
        if pair in price_index:
            raise RunnerError("duplicate_pricing_evidence")
        price_index[pair] = rates
    rows, ids, total = [], set(), Decimal(0)
    for request in requests:
        _keys(request, {"id", "body", "reservation_usd"}, {"metadata"})
        request_id = request["id"]
        if not isinstance(request_id, str) or not _ID.fullmatch(request_id) or request_id in ids:
            raise RunnerError("invalid_request_id")
        ids.add(request_id)
        body = request["body"]
        _keys(body, {"model", "messages", "max_tokens", "provider", "stream"}, _BODY_KEYS)
        model = body["model"]
        if not isinstance(model, str) or "/" not in model or ":" in model or model.startswith("openrouter/") or len(model) > 200:
            raise RunnerError("explicit_model_required")
        if body["stream"] is not False:
            raise RunnerError("streaming_not_supported")
        if type(body["max_tokens"]) is not int or not 1 <= body["max_tokens"] <= 16384:
            raise RunnerError("invalid_output_token_limit")
        messages = body["messages"]
        if not isinstance(messages, list) or not 1 <= len(messages) <= 64:
            raise RunnerError("invalid_messages")
        for message in messages:
            _keys(message, {"role", "content"})
            if message["role"] not in ("system", "user", "assistant") or not isinstance(message["content"], str):
                raise RunnerError("text_only_messages_required")
        if "temperature" in body and (type(body["temperature"]) not in (int, float) or not 0 <= body["temperature"] <= 2):
            raise RunnerError("invalid_temperature")
        if "top_p" in body and (type(body["top_p"]) not in (int, float) or not 0 < body["top_p"] <= 1):
            raise RunnerError("invalid_top_p")
        if "seed" in body and type(body["seed"]) is not int:
            raise RunnerError("invalid_seed")
        if "reasoning" in body:
            # Enabled reasoning may have provider-dependent budgets/charges.
            if body["reasoning"] != {"enabled": False}:
                raise RunnerError("reasoning_must_be_explicitly_disabled")
        if "response_format" in body:
            fmt = body["response_format"]
            if not isinstance(fmt, dict) or fmt.get("type") not in ("json_object", "json_schema"):
                raise RunnerError("invalid_response_format")
            if fmt["type"] == "json_object":
                _keys(fmt, {"type"})
            else:
                _keys(fmt, {"type", "json_schema"})
                _keys(fmt["json_schema"], {"name", "strict", "schema"})
                if fmt["json_schema"]["strict"] is not True or not isinstance(fmt["json_schema"]["schema"], dict):
                    raise RunnerError("strict_json_schema_required")
        provider = body["provider"]
        _keys(provider, {"only", "allow_fallbacks", "require_parameters", "max_price"}, {"data_collection", "zdr"})
        only = provider["only"]
        if not isinstance(only, list) or len(only) != 1 or not isinstance(only[0], str) or not only[0]:
            raise RunnerError("one_explicit_provider_required")
        if provider["allow_fallbacks"] is not False or provider["require_parameters"] is not True:
            raise RunnerError("fallback_or_unsupported_parameters_forbidden")
        if "data_collection" in provider and provider["data_collection"] not in ("allow", "deny"):
            raise RunnerError("invalid_data_collection")
        if "zdr" in provider and type(provider["zdr"]) is not bool:
            raise RunnerError("invalid_zdr")
        _keys(provider["max_price"], {"prompt", "completion"}, {"request", "image"})
        caps = {key: _money(value) for key, value in provider["max_price"].items()}
        if any(caps.get(key, Decimal(0)) != 0 for key in ("request", "image")):
            raise RunnerError("non_token_price_caps_forbidden")
        rates = price_index.get((model, only[0]))
        if rates is None:
            raise RunnerError("matching_endpoint_pricing_required")
        if any(rates[k] * Decimal(1000000) > caps[k] for k in ("prompt", "completion")):
            raise RunnerError("endpoint_price_exceeds_request_cap")
        if len(canonical(body)) > MAX_BODY_BYTES:
            raise RunnerError("request_body_limit")
        estimate = estimate_reservation(body)
        reserved = _money(request["reservation_usd"])
        if reserved < _money(estimate["minimum_reservation_usd"]):
            raise RunnerError("reservation_below_allowance")
        total += reserved
        rows.append({"id": request_id, "request_hash": digest(body), "model": model,
                     "provider": only[0], "reservation_usd": _amount(reserved), **estimate})
    if total > budget:
        raise RunnerError("planned_reservations_exceed_budget")
    if len(canonical(manifest)) > MAX_INPUT_BYTES:
        raise RunnerError("manifest_size_limit")
    return {"schema": "loom.openrouter_plan/1", "runner_version": VERSION,
            "manifest_hash": digest(manifest), "experiment_id": manifest["experiment_id"],
            "budget_usd": _amount(budget), "total_reservation_usd": _amount(total),
            "request_count": len(rows), "requests": rows,
            "billing_bound_guaranteed": False, "no_graph_promotion": True}


def _atomic(path, raw):
    """Persist bytes and directory entry before proceeding to a paid request."""
    fd, name = tempfile.mkstemp(prefix=".pending-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as out:
            out.write(raw)
            out.flush()
            os.fsync(out.fileno())
        os.replace(name, path)
        dirfd = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(dirfd)
        finally:
            os.close(dirfd)
    finally:
        if os.path.exists(name):
            os.unlink(name)


@contextmanager
def _lock(directory):
    try:
        import fcntl
    except ImportError:
        raise RunnerError("runner_requires_posix_file_lock") from None
    with open(directory / "run.lock", "a+b") as handle:
        os.chmod(directory / "run.lock", 0o600)
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RunnerError("run_already_locked") from None
        try:
            yield
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


def load_key():
    """Read only the explicitly configured credential; never discover app secrets."""
    direct, filename = os.environ.get("OPENROUTER_API_KEY"), os.environ.get("OPENROUTER_API_KEY_FILE")
    if direct and filename:
        raise RunnerError("configure_exactly_one_key_source")
    if filename:
        path = Path(filename).expanduser().resolve()
        repo = Path(__file__).resolve().parents[3]
        if path == repo or repo in path.parents:
            raise RunnerError("credential_file_must_be_outside_repository")
        try:
            info = path.stat()
            if not stat.S_ISREG(info.st_mode) or info.st_mode & 0o077 or info.st_size > 4096:
                raise RunnerError("credential_file_requires_private_regular_file")
            direct = path.read_text("utf-8").strip()
        except OSError:
            raise RunnerError("credential_file_unavailable") from None
    if not direct or len(direct) > 4096 or any(c.isspace() for c in direct) or not direct.isascii():
        raise RunnerError("credential_not_configured_or_invalid")
    return direct


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise RunnerError("redirect_refused")


@contextmanager
def _deadline():
    # A socket inactivity timeout alone permits an indefinitely trickling body.
    if not hasattr(signal, "setitimer"):
        raise RunnerError("transport_requires_posix_deadline")
    previous = signal.getsignal(signal.SIGALRM)
    if signal.getitimer(signal.ITIMER_REAL)[0]:
        raise RunnerError("transport_conflicts_with_existing_alarm")
    def expired(signum, frame):
        raise RunnerError("request_deadline_exceeded")
    try:
        signal.signal(signal.SIGALRM, expired)
    except ValueError:
        raise RunnerError("transport_requires_main_thread") from None
    signal.setitimer(signal.ITIMER_REAL, TIMEOUT_SECONDS)
    try:
        yield
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous)


def transport(method, path, body, key):
    with _deadline():
        return _transport_once(method, path, body, key)


def _transport_once(method, path, body, key):
    """Fixed origin, one request, no redirect/retry. Errors contain no remote text."""
    if (method, path) not in (("GET", "/key"), ("POST", "/chat/completions")):
        raise RunnerError("invalid_transport_route")
    headers = {"Authorization": "Bearer " + key, "Content-Type": "application/json",
               "X-OpenRouter-Title": "Loom bounded structure pilot"}
    request = urllib.request.Request(API_ROOT + path, data=body, headers=headers, method=method)
    opener = urllib.request.build_opener(_NoRedirect())
    try:
        response = opener.open(request, timeout=TIMEOUT_SECONDS)
    except urllib.error.HTTPError as exc:
        response = exc
    except RunnerError:
        raise
    except Exception:
        raise RunnerError("transport_uncertain") from None
    try:
        with response:
            raw = response.read(MAX_RESPONSE_BYTES + 1)
            if len(raw) > MAX_RESPONSE_BYTES:
                raise RunnerError("response_size_limit")
            return int(response.status), raw
    except RunnerError:
        raise
    except Exception:
        raise RunnerError("response_read_uncertain") from None


def _key_gate(raw, budget, needed):
    envelope = parse_json(raw)
    data = envelope.get("data") if isinstance(envelope, dict) else None
    if not isinstance(data, dict):
        raise RunnerError("invalid_key_metadata")
    limit, remaining = _money(data.get("limit")), _money(data.get("limit_remaining"))
    if not 0 < limit <= budget or remaining > limit or remaining < needed:
        raise RunnerError("dedicated_key_cap_or_remaining_budget_invalid")
    if data.get("limit_reset") is not None or data.get("is_management_key") is not False or data.get("is_provisioning_key", False) is not False:
        raise RunnerError("dedicated_nonresetting_inference_key_required")
    if data.get("include_byok_in_limit") is not True:
        raise RunnerError("key_must_include_byok_usage_in_limit")
    return {"checked_at": _utc(), "limit_usd": _amount(limit), "remaining_usd": _amount(remaining),
            "nonresetting": True, "includes_byok": True}


def _response_result(raw):
    try:
        response = parse_json(raw)
    except RunnerError:
        return {"state": "rejected", "reason": "invalid_response_json"}
    if not isinstance(response, dict) or response.get("error") is not None:
        return {"state": "rejected", "reason": "provider_error_envelope"}
    choices = response.get("choices")
    if not isinstance(choices, list) or len(choices) != 1 or not isinstance(choices[0], dict):
        return {"state": "rejected", "reason": "invalid_choices"}
    choice = choices[0]
    if choice.get("finish_reason") != "stop":
        return {"state": "rejected", "reason": "nonstop_finish_reason"}
    message = choice.get("message")
    if not isinstance(message, dict) or not isinstance(message.get("content"), str) or message.get("refusal") or message.get("tool_calls"):
        return {"state": "rejected", "reason": "nontext_or_refusal_response"}
    result = {"state": "completed", "reason": "transport_complete_semantics_unchecked"}
    usage = response.get("usage")
    if isinstance(usage, dict) and "cost" in usage:
        try:
            result["reported_cost_usd"] = _amount(_money(usage["cost"]))
        except RunnerError:
            result["cost_status"] = "invalid_reported_cost"
    return result


def _validate_ledger(ledger, plan, directory):
    if not isinstance(ledger, dict) or ledger.get("schema") != "loom.openrouter_ledger/1" or ledger.get("manifest_hash") != plan["manifest_hash"]:
        raise RunnerError("ledger_manifest_mismatch")
    attempts = ledger.get("attempts")
    if not isinstance(attempts, list) or len(attempts) > len(plan["requests"]):
        raise RunnerError("invalid_ledger_attempts")
    for index, row in enumerate(attempts):
        planned = plan["requests"][index]
        if not isinstance(row, dict) or any(row.get(k) != planned[k] for k in ("id", "request_hash", "reservation_usd")):
            raise RunnerError("ledger_request_mismatch")
        if row.get("state") not in ("started", "completed", "rejected", "http_error", "uncertain"):
            raise RunnerError("invalid_ledger_state")
        if "response_file" in row:
            expected = row["id"] + ".response.bin"
            if row["response_file"] != expected:
                raise RunnerError("invalid_response_path")
            try:
                with (directory / expected).open("rb") as handle:
                    raw = handle.read(MAX_RESPONSE_BYTES + 1)
                if len(raw) > MAX_RESPONSE_BYTES:
                    raise RunnerError("response_artifact_size_limit")
            except OSError:
                raise RunnerError("response_artifact_missing") from None
            if hashlib.sha256(raw).hexdigest() != row.get("response_sha256"):
                raise RunnerError("response_artifact_hash_mismatch")
    return attempts


def run_manifest(manifest, run_dir, *, transport_fn=None, key_loader=None):
    """Run/resume exactly this manifest; terminal and uncertain rows never retry."""
    plan = plan_manifest(manifest)
    directory = Path(run_dir)
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    send, get_key = transport_fn or transport, key_loader or load_key
    ledger_path = directory / "ledger.json"
    key = None
    with _lock(directory):
        if ledger_path.exists():
            ledger = parse_json(ledger_path.read_bytes())
            attempts = _validate_ledger(ledger, plan, directory)
            for row in attempts:
                if row["state"] == "started":
                    row.update(state="uncertain", reason="interrupted_after_writeahead_no_retry")
            _atomic(ledger_path, canonical(ledger))
        else:
            key = get_key()
            if not isinstance(key, str) or not key or key.encode() in canonical(manifest):
                raise RunnerError("credential_invalid_or_present_in_manifest")
            ledger = {"schema": "loom.openrouter_ledger/1", "runner_version": VERSION,
                      "manifest_hash": plan["manifest_hash"], "experiment_id": plan["experiment_id"],
                      "created_at": _utc(), "attempts": [], "no_graph_promotion": True}
            attempts = ledger["attempts"]
            # A stranded response means an earlier run's ledger was removed. Do not restart it.
            if any(directory.glob("*.response.bin")) or (directory / "manifest.json").exists():
                raise RunnerError("run_directory_contains_prior_artifacts")
            _atomic(directory / "manifest.json", canonical(manifest))
            _atomic(directory / "plan.json", canonical(plan))
            _atomic(ledger_path, canonical(ledger))
        if len(attempts) == len(plan["requests"]) or ledger.get("stopped_reason"):
            return ledger
        now = time.time()
        for item in manifest["pricing_evidence"]:
            age = now - _timestamp(item["retrieved_at"])
            if not -300 <= age <= 86400:
                raise RunnerError("pricing_evidence_stale_refresh_before_new_run")
        key = key or get_key()
        # Avoid storing/sending accidentally pasted credentials in prompt/metadata.
        if key.encode() in canonical(manifest):
            raise RunnerError("credential_present_in_manifest")
        needed = sum((_money(row["reservation_usd"]) for row in plan["requests"][len(attempts):]), Decimal(0))
        try:
            status, raw = send("GET", "/key", None, key)
            if status != 200:
                raise RunnerError("key_metadata_http_error")
            ledger["key_check"] = _key_gate(raw, _money(plan["budget_usd"]), needed)
        except RunnerError:
            raise
        except Exception:
            raise RunnerError("key_metadata_unavailable") from None
        _atomic(ledger_path, canonical(ledger))
        for index in range(len(attempts), len(plan["requests"])):
            planned, request = plan["requests"][index], manifest["requests"][index]
            row = {"id": planned["id"], "request_hash": planned["request_hash"],
                   "reservation_usd": planned["reservation_usd"], "state": "started", "started_at": _utc()}
            attempts.append(row)
            _atomic(ledger_path, canonical(ledger))  # Durable attempt BEFORE a potentially billed POST.
            try:
                started = time.monotonic()
                status, raw = send("POST", "/chat/completions", canonical(request["body"]), key)
                if not isinstance(raw, bytes) or len(raw) > MAX_RESPONSE_BYTES:
                    raise RunnerError("response_size_or_type_invalid")
                if key.encode() in raw:
                    raise RunnerError("credential_in_response_not_persisted")
                name = row["id"] + ".response.bin"
                _atomic(directory / name, raw)
                row.update(response_file=name, response_sha256=hashlib.sha256(raw).hexdigest(),
                           http_status=status, elapsed_seconds=round(time.monotonic() - started, 6))
                row.update(_response_result(raw) if status == 200 else {"state": "http_error", "reason": "http_error_no_retry"})
                if "reported_cost_usd" in row and _money(row["reported_cost_usd"]) > _money(row["reservation_usd"]):
                    row["cost_status"] = "reservation_exceeded_stop"
                row["finished_at"] = _utc()
                if status in (401, 402, 403, 429):
                    ledger["stopped_reason"] = "terminal_http_" + str(status)
                elif row.get("cost_status") == "reservation_exceeded_stop":
                    ledger["stopped_reason"] = "reported_cost_exceeded_reservation"
                # Record terminal status and stop reason together, before any next POST.
                _atomic(ledger_path, canonical(ledger))
                if ledger.get("stopped_reason"):
                    break
            except Exception:
                row.update(state="uncertain", reason="attempt_outcome_uncertain_no_retry", finished_at=_utc())
                _atomic(ledger_path, canonical(ledger))
                # Stop this invocation after ambiguity; resume may run untouched requests only.
                break
        return ledger


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("plan", "run"))
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--run-dir", type=Path)
    args = parser.parse_args(argv)
    try:
        with args.manifest.open("rb") as handle:
            raw = handle.read(MAX_INPUT_BYTES + 1)
        if len(raw) > MAX_INPUT_BYTES:
            raise RunnerError("manifest_size_limit")
        manifest = parse_json(raw)
        if args.command == "plan":
            result = plan_manifest(manifest)
        else:
            if args.run_dir is None:
                raise RunnerError("run_directory_required")
            result = run_manifest(manifest, args.run_dir)
        print(canonical(result).decode())
        return 0
    except RunnerError as exc:
        print(json.dumps({"error": str(exc)}), file=sys.stderr)
        return 2
    except (OSError, ValueError, TypeError, KeyError, RecursionError):
        print('{"error":"local_input_or_storage_failure"}', file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
