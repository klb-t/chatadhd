"""OpenRouter byte transport and receipt normalization for research programmes.

No inference policy, model list, retry, redirect, or response-size ceiling lives
here. Routes and timeouts come from caller-owned JSON data. The provider origin
is fixed so editing a route cannot send a bearer credential to another host.
Raw response bytes are always returned, including HTTP errors and partial reads.

Credential text may have one terminal LF or CRLF. Only that terminator is
removed; the fingerprint hashes the exact remaining bytes sent as Bearer token.
Official contracts checked 2026-10-05:
https://openrouter.ai/docs/api/api-reference/api-keys/get-current-api-key
https://openrouter.ai/docs/api/api-reference/generations/get-generation
https://openrouter.ai/blog/insights/what-is-jev/
"""
from __future__ import annotations

from datetime import datetime
from decimal import Decimal, InvalidOperation
import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import re
import stat
import string
import time
import urllib.error
import urllib.parse
import urllib.request


ORIGIN = "https://openrouter.ai"


class TransportError(ValueError):
    """Fixed error codes only; never retain a remote exception or credential."""


def _key_bytes(key):
    try:
        raw = key.encode("ascii") if isinstance(key, str) else key
    except UnicodeError:
        raise TransportError("invalid_credential") from None
    if not isinstance(raw, bytes):
        raise TransportError("invalid_credential")
    if raw.endswith(b"\r\n"):
        raw = raw[:-2]
    elif raw.endswith(b"\n"):
        raw = raw[:-1]
    if not raw or any(byte < 33 or byte > 126 for byte in raw):
        raise TransportError("invalid_credential")
    return raw


def key_fingerprint(key):
    return hashlib.sha256(_key_bytes(key)).hexdigest()


def load_key_file(path, repo_root):
    """Read an owner-only regular credential file outside all Git worktrees.

    Reject symlinks in the path, hard links, group/world access, and a nonprivate
    containing directory. No environment credential lookup is performed.
    """
    descriptor = None
    try:
        candidate = Path(path).absolute()
        resolved = candidate.resolve(strict=True)
        repo = Path(repo_root).resolve(strict=True)
        if candidate != resolved or resolved == repo or repo in resolved.parents:
            raise TransportError("unsafe_credential_path")
        if any((parent / ".git").exists() for parent in resolved.parents):
            raise TransportError("credential_inside_git")
        parent = resolved.parent.stat()
        if (not stat.S_ISDIR(parent.st_mode) or parent.st_uid != os.getuid()
                or stat.S_IMODE(parent.st_mode) & 0o077):
            raise TransportError("unsafe_credential_directory")
        descriptor = os.open(resolved, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        info = os.fstat(descriptor)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
                or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) not in (0o400, 0o600)):
            raise TransportError("unsafe_credential_file")
        with os.fdopen(descriptor, "rb") as source:
            descriptor = None
            return _key_bytes(source.read()).decode("ascii")
    except TransportError:
        raise
    except Exception:
        raise TransportError("credential_load_failed") from None
    finally:
        if descriptor is not None:
            os.close(descriptor)


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class _PreserveBearerAuth:
    """Explicit auth prevents Requests' environment netrc override."""

    def __call__(self, request):
        return request


def _no_session_redirects(*args, **kwargs):
    # Requests normally consumes/decompresses a redirect body to build .next
    # even when allow_redirects=False. Preserve the original byte stream.
    return iter(())


def _requests_module():
    try:
        return importlib.import_module("requests")
    except Exception:
        raise TransportError("requests_backend_unavailable") from None


def _partial_response_bytes(error, raw):
    """Recover a bytes partial from urllib3/http.client exception wrappers."""
    original = getattr(raw, "_original_response", None)
    if (getattr(original, "chunked", False) is True
            and getattr(original, "chunk_left", None) == 0):
        # A read1 error between chunks can contain truncated framing CRLF.
        # Those bytes are not part of the response body.
        return b""
    pending = [error]
    seen = set()
    while pending:
        current = pending.pop()
        if id(current) in seen:
            continue
        seen.add(id(current))
        partial = getattr(current, "partial", None)
        if isinstance(partial, bytes):
            return partial
        for nested in (current.__cause__, current.__context__, *current.args):
            if isinstance(nested, BaseException):
                pending.append(nested)
    return b""


def _method_headers(config):
    configured = config.get("headers_by_method", {})
    if not isinstance(configured, dict):
        raise TransportError("invalid_transport_headers")
    protected = {"authorization", "host", "content-length", "content-type", "accept-encoding"}
    result = {}
    for method, headers in configured.items():
        if (not isinstance(method, str) or not re.fullmatch(r"[A-Z]+", method)
                or not isinstance(headers, dict)):
            raise TransportError("invalid_transport_headers")
        copied = {}
        names = set()
        for name, value in headers.items():
            if (not isinstance(name, str)
                    or not re.fullmatch(r"[!#$%&'*+\-.^_`|~0-9A-Za-z]+", name)
                    or name.lower() in protected or name.lower() in names
                    or not isinstance(value, str)
                    or any(ord(char) < 32 and char != "\t" or ord(char) == 127
                           or ord(char) > 255 for char in value)
                    or value.startswith((" ", "\t"))):
                raise TransportError("invalid_transport_headers")
            names.add(name.lower())
            copied[name] = value
        result[method] = copied
    return result


class OpenRouterTransport:
    """One request per invocation; urllib by default, no automatic retry.

    Config: {base_url, timeout_seconds, backend, proxy_mode,
             routes: {route_id: {method, path}}}.
    backend may be urllib (default) or requests_session. The pooled backend
    requires explicit pool_connections, pool_maxsize and pool_block values.
    Optional headers_by_method maps HTTP method names to additional headers;
    credential, origin, body length/type and identity encoding stay protected.
    proxy_mode defaults to disabled; environment explicitly enables the native
    environment proxy needed by some execution environments. Proxy values are
    never logged or included in returned metadata.
    Path templates use named params, such as {model_id}; unused params become
    URL-encoded query parameters. A route may name allowed query keys in query.
    """
    def __init__(self, config, key):
        if not isinstance(config, dict) or config.get("base_url", ORIGIN) != ORIGIN:
            raise TransportError("invalid_openrouter_origin")
        routes = config.get("routes")
        timeout = config.get("timeout_seconds")
        backend = config.get("backend", "urllib")
        proxy_mode = config.get("proxy_mode", "disabled")
        if backend not in ("urllib", "requests_session"):
            raise TransportError("invalid_transport_backend")
        if proxy_mode not in ("disabled", "environment"):
            raise TransportError("invalid_transport_proxy_mode")
        if not isinstance(routes, dict) or not routes:
            raise TransportError("invalid_transport_routes")
        if type(timeout) not in (int, float) or not math.isfinite(timeout) or timeout <= 0:
            raise TransportError("invalid_transport_timeout")
        self._headers_by_method = _method_headers(config)
        self._routes = json.loads(json.dumps(routes))
        self._timeout = timeout
        self._key = _key_bytes(key).decode("ascii")
        self.key_fingerprint = key_fingerprint(key)
        self._backend = backend
        self._session = None
        if backend == "requests_session":
            for name in ("pool_connections", "pool_maxsize"):
                if type(config.get(name)) is not int or config[name] <= 0:
                    raise TransportError("invalid_transport_" + name)
            if type(config.get("pool_block")) is not bool:
                raise TransportError("invalid_transport_pool_block")
            requests = _requests_module()
            try:
                read1 = requests.packages.urllib3.response.HTTPResponse.read1
            except Exception:
                raise TransportError("requests_raw_read1_unavailable") from None
            if not callable(read1):
                raise TransportError("requests_raw_read1_unavailable")
            try:
                self._session = requests.Session()
                self._session.trust_env = proxy_mode == "environment"
                self._session.resolve_redirects = _no_session_redirects
                self._session.mount("https://", requests.adapters.HTTPAdapter(
                    pool_connections=config["pool_connections"],
                    pool_maxsize=config["pool_maxsize"],
                    pool_block=config["pool_block"], max_retries=0))
                self._session.mount("http://", requests.adapters.HTTPAdapter(
                    pool_connections=config["pool_connections"],
                    pool_maxsize=config["pool_maxsize"],
                    pool_block=config["pool_block"], max_retries=0))
            except Exception:
                self.close()
                raise TransportError("requests_backend_initialization_failed") from None
            return
        proxy_handler = (urllib.request.ProxyHandler() if proxy_mode == "environment"
                         else urllib.request.ProxyHandler({}))
        self._opener = urllib.request.build_opener(proxy_handler, _NoRedirect())

    def close(self):
        """Release pooled connections; the urllib backend has no session."""
        if self._session is not None:
            try:
                self._session.close()
            except Exception:
                pass

    def _headers(self, method):
        headers = {"Authorization": "Bearer " + self._key,
                   "Content-Type": "application/json", "Accept-Encoding": "identity"}
        headers.update(self._headers_by_method.get(method, {}))
        return headers

    def _url(self, method, route_id, params):
        try:
            route = self._routes[route_id]
            if not isinstance(route, dict) or route["method"] != method:
                raise TransportError("route_method_mismatch")
            path = route["path"]
            if (not isinstance(path, str) or not path.startswith("/") or path.startswith("//")
                    or "\\" in path or any(ord(char) < 33 for char in path)):
                raise TransportError("invalid_route_path")
            parts = list(string.Formatter().parse(path))
            if any(spec or conversion for _, field, spec, conversion in parts if field is not None):
                raise TransportError("invalid_route_template")
            fields = [field for _, field, spec, conversion in parts if field is not None]
            if any(not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", field) for field in fields):
                raise TransportError("invalid_route_template")
            if params is None:
                params = {}
            if not isinstance(params, dict) or any(not isinstance(k, str) for k in params):
                raise TransportError("invalid_route_params")
            substitutions = {}
            for field in fields:
                value = params[field]
                if not isinstance(value, str) or not value:
                    raise TransportError("invalid_route_params")
                # A model identifier has a publisher/model separator. All other
                # delimiters are encoded, including ? and # and any URL origin.
                substitutions[field] = urllib.parse.quote(value, safe="/")
            path = path.format_map(substitutions)
            parsed = urllib.parse.urlsplit(ORIGIN + path)
            if parsed.scheme != "https" or parsed.netloc != "openrouter.ai" or parsed.fragment:
                raise TransportError("invalid_route_path")
            query = {k: v for k, v in params.items() if k not in fields}
            if "query" in route and (not isinstance(route["query"], list)
                                      or set(query) != set(route["query"])):
                raise TransportError("invalid_route_query")
            if any(not isinstance(v, str) or not v for v in query.values()):
                raise TransportError("invalid_route_query")
            encoded = urllib.parse.urlencode(query)
            return ORIGIN + path + (("&" if parsed.query else "?") + encoded if encoded else "")
        except TransportError:
            raise
        except Exception:
            raise TransportError("invalid_transport_route") from None

    def request(self, method, route_id, body=None, params=None):
        if not isinstance(method, str) or not re.fullmatch(r"[A-Z]+", method):
            raise TransportError("invalid_http_method")
        if body is not None and not isinstance(body, bytes):
            raise TransportError("request_body_must_be_bytes")
        url = self._url(method, route_id, params)
        if self._backend == "requests_session":
            return self._request_session(method, url, body)
        started = time.monotonic()
        response = None
        chunks = []
        try:
            request = urllib.request.Request(url, data=body, method=method,
                headers=self._headers(method))
            try:
                response = self._opener.open(request, timeout=self._timeout)
            except urllib.error.HTTPError as error:
                response = error
            status = response.getcode()
            if type(status) is not int:
                raise TransportError("invalid_http_status")
            result = {"http_status": status}
            try:
                while True:
                    chunk = response.read(64 * 1024)
                    if not chunk:
                        break
                    if not isinstance(chunk, bytes):
                        raise TransportError("invalid_response_bytes")
                    chunks.append(chunk)
            except Exception as error:
                partial = getattr(error, "partial", None)
                if isinstance(partial, bytes):
                    chunks.append(partial)
                result["transport_error"] = "response_read_failed"
            result.update(raw=b"".join(chunks), latency_seconds=time.monotonic() - started)
            return result
        except Exception:
            raise TransportError("transport_failed") from None
        finally:
            if response is not None:
                try:
                    response.close()
                except Exception:
                    pass

    def _request_session(self, method, url, body):
        started = time.monotonic()
        response = None
        chunks = []
        try:
            response = self._session.request(method, url, data=body,
                headers=self._headers(method),
                auth=_PreserveBearerAuth(), timeout=self._timeout,
                allow_redirects=False, verify=True, stream=True)
            status = response.status_code
            if type(status) is not int:
                raise TransportError("invalid_http_status")
            result = {"http_status": status}
            try:
                while True:
                    chunk = response.raw.read1(64 * 1024, decode_content=False)
                    if not chunk:
                        break
                    if not isinstance(chunk, bytes):
                        raise TransportError("invalid_response_bytes")
                    chunks.append(chunk)
            except Exception as error:
                chunks.append(_partial_response_bytes(error, response.raw))
                result["transport_error"] = "response_read_failed"
            result.update(raw=b"".join(chunks), latency_seconds=time.monotonic() - started)
            return result
        except Exception:
            raise TransportError("transport_failed") from None
        finally:
            if response is not None:
                try:
                    response.close()
                except Exception:
                    pass


def _json(response, *, require_success=False):
    raw = response
    if isinstance(response, dict) and "raw" in response:
        if require_success and (response.get("http_status") != 200 or response.get("transport_error")):
            raise TransportError("metadata_response_unavailable")
        raw = response["raw"]
    if not isinstance(raw, bytes):
        raise TransportError("response_must_be_bytes")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise TransportError("duplicate_json_field")
            result[key] = value
        return result
    def constant(_):
        raise TransportError("invalid_json_number")
    try:
        value = json.loads(raw, object_pairs_hook=pairs, parse_float=Decimal, parse_constant=constant)
    except TransportError:
        raise
    except Exception:
        raise TransportError("invalid_response_json") from None
    if not isinstance(value, dict):
        raise TransportError("response_not_object")
    return value, hashlib.sha256(raw).hexdigest()


def _money(value):
    if type(value) not in (str, int, float, Decimal):
        raise TransportError("invalid_money")
    try:
        number = Decimal(str(value))
    except (InvalidOperation, ValueError):
        raise TransportError("invalid_money") from None
    if not number.is_finite() or number < 0:
        raise TransportError("invalid_money")
    return format(number, "f")


def _timestamp(value):
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            raise ValueError
    except Exception:
        raise TransportError("invalid_checked_at") from None
    return value


def normalize_key_metadata(response, fingerprint, checked_at):
    value, raw_hash = _json(response, require_success=True)
    data = value.get("data")
    required = {"usage", "limit", "limit_remaining", "byok_usage", "include_byok_in_limit",
                "is_management_key", "limit_reset"}
    if not isinstance(data, dict) or not required <= set(data):
        raise TransportError("key_metadata_fields_missing")
    if not isinstance(fingerprint, str) or not re.fullmatch(r"[a-f0-9]{64}", fingerprint):
        raise TransportError("invalid_key_fingerprint")
    for name in ("include_byok_in_limit", "is_management_key"):
        if type(data[name]) is not bool:
            raise TransportError("key_metadata_flag_invalid")
    provisioning = data.get("is_provisioning_key")
    if "is_provisioning_key" in data and type(provisioning) is not bool:
        raise TransportError("key_metadata_flag_invalid")
    reset = data["limit_reset"]
    if reset is not None and (type(reset) is not str or reset not in ("daily", "weekly", "monthly")):
        raise TransportError("key_metadata_reset_unknown")
    usage = _money(data["usage"])
    byok = _money(data["byok_usage"])
    limit = None if data["limit"] is None else _money(data["limit"])
    remaining = None if data["limit_remaining"] is None else _money(data["limit_remaining"])
    if (limit is None) != (remaining is None):
        raise TransportError("key_metadata_limit_inconsistent")
    if limit is not None and Decimal(remaining) > Decimal(limit):
        raise TransportError("key_metadata_limit_inconsistent")
    # For non-resetting keys, remaining cannot exceed the cap less the
    # corresponding spend. A lower remaining amount can reflect reservations.
    if limit is not None and reset is None:
        spent = Decimal(usage) + (Decimal(byok) if data["include_byok_in_limit"] else 0)
        if Decimal(remaining) > max(Decimal(0), Decimal(limit) - spent):
            raise TransportError("key_metadata_usage_inconsistent")
    return {"key_fingerprint_sha256": fingerprint, "key_fingerprint": fingerprint,
            "http_status": 200, "checked_at": _timestamp(checked_at),
            "response_sha256": raw_hash, "usage_usd": usage, "byok_usage_usd": byok,
            "limit_usd": limit, "remaining_usd": remaining, "limit_reset": reset,
            "include_byok_in_limit": data["include_byok_in_limit"],
            "is_management_key": data["is_management_key"], "is_provisioning_key": provisioning}


def _operation(operation):
    if isinstance(operation, str):
        operation = {"protocol": operation}
    if not isinstance(operation, dict):
        raise TransportError("invalid_receipt_operation")
    protocol = operation.get("protocol", operation.get("route_id", operation.get("api_type")))
    if protocol in ("jev", "decisions"):
        return operation, "decisions", "input_tokens", "output_tokens"
    if protocol in ("chat", "chat_completions", "completions"):
        return operation, "completions", "prompt_tokens", "completion_tokens"
    raise TransportError("invalid_receipt_operation")


def _text(value):
    if not isinstance(value, str) or not value:
        raise TransportError("receipt_identity_missing")
    return value


def extract_receipt(response, operation):
    """Extract billing independently of choices, answers, and HTTP status.

    Missing/invalid optional evidence stays unknown. No upstream cost or zero
    historical BYOK usage is treated as proof of credit billing.
    """
    operation, api_type, input_name, output_name = _operation(operation)
    value, raw_hash = _json(response)
    usage = value.get("usage")
    if not isinstance(usage, dict):
        raise TransportError("receipt_usage_missing")
    cost = _money(usage.get("cost"))
    receipt = {"response_sha256": raw_hash, "reported_cost_usd": cost,
               "generation_id": value.get("id"), "model": value.get("model"),
               "provider": value.get("provider"), "api_type": api_type,
               "is_byok": usage.get("is_byok") if type(usage.get("is_byok")) is bool else None,
               "billing_verified": False}
    for output, name in (("input_tokens", input_name), ("output_tokens", output_name)):
        token_count = usage.get(name)
        receipt[output] = token_count if type(token_count) is int and token_count >= 0 else None
    receipt["billing_mode"] = ("byok" if receipt["is_byok"] is True else
                               "credits" if receipt["is_byok"] is False else "unknown")
    return receipt


def verify_generation_receipt(response, receipt, operation=None):
    """Require matching generation, exact cost, selected pair, and credit proof."""
    value, raw_hash = _json(response, require_success=True)
    data = value.get("data")
    if not isinstance(data, dict) or not isinstance(receipt, dict):
        raise TransportError("generation_metadata_missing")
    op, default_api, _, _ = _operation(operation or receipt.get("api_type"))
    generation_id = _text(receipt.get("generation_id"))
    model = _text(data.get("model"))
    provider = _text(data.get("provider_name"))
    models = op.get("model_aliases", [])
    providers = op.get("provider_aliases", [])
    if (not isinstance(models, list) or not isinstance(providers, list)
            or any(not isinstance(value, str) or not value for value in models + providers)):
        raise TransportError("invalid_receipt_aliases")
    selected_model = op.get("model_id", receipt.get("model"))
    selected_provider = op.get("provider_id", receipt.get("provider"))
    permitted_models = set(models + ([_text(selected_model)] if selected_model is not None else []))
    permitted_providers = set(providers + ([_text(selected_provider)] if selected_provider is not None else []))
    if not permitted_models or not permitted_providers:
        raise TransportError("generation_selected_pair_missing")
    if (data.get("id") != generation_id or model not in permitted_models or provider not in permitted_providers
            or data.get("api_type") != op.get("api_type", default_api)):
        raise TransportError("generation_identity_mismatch")
    if receipt.get("model") is not None and receipt["model"] not in permitted_models:
        raise TransportError("response_model_mismatch")
    if receipt.get("provider") is not None and receipt["provider"] not in permitted_providers:
        raise TransportError("response_provider_mismatch")
    cost = _money(data.get("total_cost"))
    if Decimal(cost) != Decimal(_money(receipt.get("reported_cost_usd"))):
        raise TransportError("generation_cost_mismatch")
    if data.get("is_byok") is not False or receipt.get("is_byok") is True:
        raise TransportError("generation_credit_billing_unproven")
    return {**receipt, "model": model, "provider": provider, "is_byok": False,
            "billing_mode": "credits", "billing_verified": True,
            "generation_response_sha256": raw_hash, "generation_total_cost_usd": cost,
            "actual_cost_usd": cost}
