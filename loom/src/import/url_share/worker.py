"""Bounded public URL snapshots; stdlib only, no model or credential integration.

Native caller runs this trusted helper in a process with a wall-clock watchdog
(including DNS and JSON parsing). stdin/stdout is a private JSON pipe, never a
log. Only fetch/parser live here; persistence uses the native Loom stores.
"""
from __future__ import annotations

import base64
import hashlib
import http.client
import ipaddress
import json
import os
import re
import socket
import ssl
import sys
import time
from html.parser import HTMLParser
from collections import deque
from urllib.parse import quote, urljoin, urlsplit, urlunsplit


class Rejected(Exception):
    def __init__(self, reason, status="denied"):
        self.reason, self.status = reason, status


def normalize_url(value):
    if not isinstance(value, str) or not value or any(ord(c) < 33 or ord(c) == 127 for c in value):
        raise Rejected("invalid_url")
    if "\\" in value:
        raise Rejected("invalid_url")
    try:
        u = urlsplit(value)
        if u.scheme.lower() not in ("http", "https"):
            raise Rejected("url_scheme")
        if u.username is not None or u.password is not None:
            raise Rejected("url_credentials")
        host = (u.hostname or "").rstrip(".").encode("idna").decode("ascii").lower()
        if not host or "%" in host:
            raise Rejected("invalid_host")
        port = u.port if u.port is not None else (443 if u.scheme.lower() == "https" else 80)
        if not 1 <= port <= 65535:
            raise Rejected("invalid_port")
        authority = "[" + host + "]" if ":" in host else host
        if port != (443 if u.scheme.lower() == "https" else 80):
            authority += ":" + str(port)
        # RFC redirect resolution is performed by urljoin before this function.
        return urlunsplit((u.scheme.lower(), authority, quote(u.path or "/", safe="/%:@!$&'()*+,;=-._~"),
                           quote(u.query, safe="%/?@:!$&'()*+,;=-._~"), ""))
    except (ValueError, UnicodeError):
        raise Rejected("invalid_url") from None


def public_address(value):
    try:
        a = ipaddress.ip_address(value)
        # Reject transition forms too: policy must not depend on a v4 tunnel.
        if isinstance(a, ipaddress.IPv6Address) and (a.ipv4_mapped or a.sixtofour or a.teredo):
            return False
        if str(a) == "168.63.129.16":  # platform virtual service, not an Internet document
            return False
        if a.version == 6 and (a in ipaddress.ip_network("64:ff9b::/96") or a in ipaddress.ip_network("64:ff9b:1::/48")):
            return False
        return a.is_global and not (a.is_multicast or a.is_unspecified or a.is_reserved)
    except ValueError:
        return False


def remaining(deadline):
    seconds = deadline - time.monotonic()
    if seconds <= 0:
        raise TimeoutError()
    return seconds


class PublicTransport:
    """One hop, DNS validated and pinned at connect; no redirects/cookies/auth.

    A proxy cannot provide the required end-to-end IP pin here. Report missing
    capability instead of silently bypassing the configured network boundary.
    Test transport lives only in tests and is injected as an object, never as a
    request/config option.
    """
    def open(self, url, deadline):
        u = urlsplit(url)
        if u.hostname == "localhost" or u.hostname.endswith(".localhost"):
            raise Rejected("address_not_public")
        try:
            address = ipaddress.ip_address(u.hostname)
        except ValueError:
            address = None
        if address is not None and not public_address(str(address)):
            raise Rejected("address_not_public")
        if any(os.environ.get(k) for k in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy")):
            raise Rejected("proxy_destination_pinning_unavailable", "unsupported")
        u = urlsplit(url)
        port = u.port or (443 if u.scheme == "https" else 80)
        infos = socket.getaddrinfo(u.hostname, port, type=socket.SOCK_STREAM)
        if not infos or any(not public_address(info[4][0]) for info in infos):
            raise Rejected("address_not_public")
        remaining(deadline)
        family, socktype, proto, _, address = infos[0]
        sock = socket.socket(family, socktype, proto)
        try:
            sock.settimeout(remaining(deadline))
            sock.connect(address)  # numeric sockaddr, no second DNS resolution
            if u.scheme == "https":
                sock = ssl.create_default_context().wrap_socket(sock, server_hostname=u.hostname)
            conn = http.client.HTTPConnection(u.hostname, port, timeout=remaining(deadline))
            conn.sock = sock
            target = urlunsplit(("", "", u.path or "/", u.query, ""))
            conn.request("GET", target, headers={"Accept-Encoding": "identity", "Connection": "close"})
            response = conn.getresponse()
            return conn, response
        except BaseException:
            sock.close()
            raise


def encoded(raw):
    return {"body_base64": base64.b64encode(raw).decode("ascii"),
            "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw)}


class Fetcher:
    def __init__(self, limits, transport=None):
        self.limits = limits
        self.transport = transport if transport is not None else PublicTransport()
        self.deadline = time.monotonic() + limits["timeout_ms"] / 1000
        self.used = 0

    def fetch(self, source):
        result = {"source_url": source, "final_url": "", "redirects": [], "http_status": None,
                  "status": "unsupported", "reason": "unread", "mime": "", **encoded(b"")}
        raw = bytearray()
        try:
            current = normalize_url(source)
            result["normalized_url"] = current
            seen = set()
            while True:
                remaining(self.deadline)
                if current in seen:
                    raise Rejected("redirect_loop", "partial")
                seen.add(current)
                result["attempted_url"] = current
                conn, response = self.transport.open(current, self.deadline)
                result["final_url"] = current  # last actual HTTP response, not a denied destination
                try:
                    status = response.status
                    result["http_status"] = status
                    result["mime"] = response.getheader("Content-Type", "application/octet-stream").split(";", 1)[0].lower().strip()
                    location = response.getheader("Location")
                    if status in (301, 302, 303, 307, 308):
                        if not location:
                            raise Rejected("redirect_without_location", "partial")
                        if len(result["redirects"]) >= self.limits["max_redirects"]:
                            raise Rejected("redirect_limit", "partial")
                        target = normalize_url(urljoin(current, location))
                        if urlsplit(current).scheme == "https" and urlsplit(target).scheme != "https":
                            raise Rejected("redirect_tls_downgrade")
                        result["redirects"].append({"from": current, "to": target, "http_status": status})
                        current = target
                        continue
                    available = min(self.limits["max_bytes"], self.limits["max_total_bytes"] - self.used)
                    if available <= 0:
                        raise Rejected("oversize", "partial")
                    # Read at most remaining+1, preserve the admitted prefix on failure.
                    while True:
                        remaining(self.deadline)
                        if conn.sock is not None:
                            conn.sock.settimeout(remaining(self.deadline))
                        block = response.read1(min(65536, available - len(raw) + 1))
                        if not block:
                            break
                        room = available - len(raw)
                        raw.extend(block[:room])
                        if len(block) > room:
                            raise Rejected("oversize", "partial")
                    if status in (401, 403):
                        raise Rejected("http_" + str(status))
                    if status in (404, 410):
                        raise Rejected("http_" + str(status), "unsupported")
                    if not 200 <= status < 300:
                        raise Rejected("http_" + str(status), "partial")
                    if status == 206 or response.getheader("Content-Range"):
                        raise Rejected("incomplete_http", "partial")
                    length = response.getheader("Content-Length")
                    if length is not None and (not length.isdigit() or int(length) != len(raw)):
                        raise Rejected("incomplete_http", "partial")
                    if response.getheader("Content-Encoding", "identity").lower() != "identity":
                        raise Rejected("content_encoding", "unsupported")
                    result.update(status="complete", reason="fetched")
                    return result | encoded(bytes(raw))
                finally:
                    conn.close()
        except Rejected as e:
            result.update(status=e.status, reason=e.reason)
        except (TimeoutError, socket.timeout):
            result.update(status="partial", reason="timeout")
        except (OSError, http.client.HTTPException, ValueError):
            # Never expose exception strings: they may contain URL credentials or body.
            result.update(status="partial", reason="network_error")
        finally:
            self.used += len(raw)
        return result | encoded(bytes(raw))


class PageText(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.skip = []
        self.text = []
        self.closed = False
        self.login = False

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "style", "head", "template"):
            self.skip.append(tag)
        if tag == "input" and dict(attrs).get("type", "").lower() == "password":
            self.login = True
        if not self.skip and tag in ("p", "div", "br", "li", "h1", "h2", "h3", "pre"):
            self.text.append("\n")

    def handle_endtag(self, tag):
        if self.skip and tag == self.skip[-1]:
            self.skip.pop()
        if tag == "html":
            self.closed = True

    def handle_data(self, data):
        if not self.skip:
            self.text.append(data)


class ShareHTML(PageText):
    """Collect inert script bytes, never execute markup or JavaScript."""
    def __init__(self):
        super().__init__()
        self.scripts = []
        self.script = None

    def handle_starttag(self, tag, attrs):
        super().handle_starttag(tag, attrs)
        if tag == "script":
            self.script = [dict(attrs), []]

    def handle_data(self, data):
        super().handle_data(data)
        if self.script is not None:
            self.script[1].append(data)

    def handle_endtag(self, tag):
        if tag == "script" and self.script is not None:
            self.scripts.append((self.script[0], "".join(self.script[1])))
            self.script = None
        super().handle_endtag(tag)


def pointer_token(value):
    return value.replace("~", "~0").replace("/", "~1")


def turbo_json(pool, budget):
    """JSON subset of turbo-stream v2: indexed keys/values, no JS evaluation.

    Cache shared values; reject cycles and expansion beyond the existing worker
    output budget. Unsupported tagged values remain explicit, never text.
    Numbers stored in pool slots are literals, numbers in containers are refs.
    """
    if not isinstance(pool, list) or not pool:
        raise ValueError("pool")
    cache, sizes, visiting = {}, {}, set()
    def hydrate(index):
        if type(index) is not int:
            raise ValueError("reference")
        if index in (-5, -7):  # null / undefined: neither is a message
            return None, 4
        if index < 0 or index >= len(pool) or index in visiting:
            raise ValueError("reference")
        if index in cache:
            return cache[index], sizes[index]
        visiting.add(index)
        raw, size = pool[index], 2
        if isinstance(raw, dict):
            value = {}
            for key, ref in raw.items():
                if not re.fullmatch(r"_\d+", key):
                    raise ValueError("key")
                name, _ = hydrate(int(key[1:]))
                if not isinstance(name, str) or name in value:
                    raise ValueError("key")
                item, item_size = hydrate(ref)
                size += len(name.encode("utf-8")) + item_size + 4
                if size > budget:
                    raise ValueError("expansion")
                value[name] = item
        elif isinstance(raw, list):
            if raw and isinstance(raw[0], str):
                value = {"unsupported_turbo_tag": raw[0]}
                size = len(raw[0]) + 32
            else:
                value = []
                for ref in raw:
                    item, item_size = hydrate(ref)
                    size += item_size + 1
                    if size > budget:
                        raise ValueError("expansion")
                    value.append(item)
        else:
            value = raw
            size = len(json.dumps(raw, ensure_ascii=False).encode("utf-8"))
        if size > budget:
            raise ValueError("expansion")
        visiting.remove(index)
        cache[index], sizes[index] = value, size
        return value, size
    return hydrate(0)[0]


def chatgpt_data(page, budget):
    candidates = []
    for index, (attrs, script) in enumerate(page.scripts):
        if attrs.get("id") == "__NEXT_DATA__" and attrs.get("type") == "application/json":
            data = json.loads(script)["props"]["pageProps"]["serverResponse"]["data"]
            candidates.append((data, {"format": "next_data", "script_index": index,
                                      "json_pointer": "/props/pageProps/serverResponse/data"}))
    chunks, indices = [], []
    decoder = json.JSONDecoder()
    for index, (_, script) in enumerate(page.scripts):
        for match in re.finditer(r"window\.__reactRouterContext\.streamController\.enqueue\(\s*", script):
            chunk, end = decoder.raw_decode(script, match.end())
            if not isinstance(chunk, str) or not script[end:].lstrip().startswith(")"):
                raise ValueError("enqueue")
            chunks.append(chunk)
            indices.append(index)
    if chunks:
        # Deferred/non-JSON frames are unsupported, not silently discarded.
        root = turbo_json(json.loads("".join(chunks)), budget)
        route_key = "routes/share.$shareId.($action)"
        data = root["loaderData"][route_key]["serverResponse"]["data"]
        candidates.append((data, {"format": "turbo_stream_v2", "script_indices": indices,
                                  "json_pointer": "/loaderData/" + pointer_token(route_key) + "/serverResponse/data"}))
    if len(candidates) != 1 or not isinstance(candidates[0][0], dict):
        raise ValueError("share data")
    return candidates[0]


def chatgpt_messages(data, locator):
    """Preserve the supplied tree; select only an explicitly supplied path.

    With only linear_conversation, its order is the shared branch. Ambiguous
    mapping-only trees are refused instead of selecting an invented leaf.
    """
    mapping, linear = data.get("mapping"), data.get("linear_conversation")
    prefix = locator["json_pointer"]
    if isinstance(mapping, dict) and mapping:
        nodes = mapping
        paths = {key: prefix + "/mapping/" + pointer_token(key) for key in nodes}
    elif isinstance(linear, list) and linear:
        nodes, paths = {}, {}
        previous = None
        for index, entry in enumerate(linear):
            if not isinstance(entry, dict) or not isinstance(entry.get("id"), str) or entry["id"] in nodes:
                raise ValueError("linear node")
            key = entry["id"]
            nodes[key] = entry if "parent" in entry else entry | {"parent": previous}
            paths[key] = prefix + "/linear_conversation/" + str(index)
            previous = key
    else:
        raise ValueError("messages")
    children = {key: [] for key in nodes}
    roots = []
    for key, node in nodes.items():
        if not isinstance(node, dict) or "parent" not in node or not isinstance(node.get("id"), str) or node["id"] != key:
            raise ValueError("node")
        parent = node.get("parent")
        if parent is None:
            roots.append(key)
        elif not isinstance(parent, str) or parent not in nodes:
            raise ValueError("parent")
        else:
            children[parent].append(key)
    selected = data.get("current_node")
    selection = "current_node"
    if selected is None and isinstance(linear, list) and linear:
        selected = linear[-1].get("id") if isinstance(linear[-1], dict) else None
        selection = "linear_conversation"
    if not isinstance(selected, str) or selected not in nodes:
        raise ValueError("branch unavailable")
    active, current = set(), selected
    while current is not None:
        if current in active:
            raise ValueError("cycle")
        active.add(current)
        current = nodes[current].get("parent")
    queue, order = deque(roots), []
    while queue:
        key = queue.popleft()
        order.append(key)
        queue.extend(children[key])
    if len(order) != len(nodes):
        raise ValueError("cycle")
    messages, nearest, partial = [], {}, False
    for key in order:
        node = nodes[key]
        parent = nearest.get(node.get("parent"))
        nearest[key] = parent
        if "message" not in node:
            raise ValueError("message unavailable")
        message = node["message"]
        if message is None:
            continue
        if not isinstance(message, dict) or not isinstance(message.get("author"), dict):
            raise ValueError("message")
        role, content = message["author"].get("role"), message.get("content")
        if not isinstance(role, str) or not role or not isinstance(content, dict):
            raise ValueError("content")
        pointer = paths[key] + "/message"
        texts, references, reference_pointers = [], [], []
        kind = content.get("content_type")
        if kind in ("text", "multimodal_text") and isinstance(content.get("parts"), list):
            for part_index, part in enumerate(content["parts"]):
                if isinstance(part, str):
                    texts.append(part)
                elif isinstance(part, dict) and part.get("content_type") == "image_asset_pointer" and isinstance(part.get("asset_pointer"), str):
                    references.append(part)
                    reference_pointers.append(pointer + "/content/parts/" + str(part_index))
                else:
                    partial = True
        elif kind == "code" and isinstance(content.get("text"), str):
            texts.append(content["text"])
        else:
            partial = True
        metadata = message.get("metadata", {})
        if not isinstance(metadata, dict):
            partial = True
            metadata = {}
        assets = metadata.get("attachments", [])
        if isinstance(assets, list):
            references.extend(assets)
            reference_pointers.extend(pointer + "/metadata/attachments/" + str(i) for i in range(len(assets)))
            if any(not isinstance(asset, (dict, str)) for asset in assets):
                partial = True
        else:
            partial = True
        messages.append({"role": role, "text": "".join(texts), "pointer": pointer,
                         "parent_pointer": parent, "active": key in active,
                         "provider_node_id": key, "provider_message_id": message.get("id"),
                         "attachments": references, "attachment_pointers": reference_pointers})
        nearest[key] = pointer
    return {"messages": messages, "title": data.get("title") if isinstance(data.get("title"), str) else "",
            "adapter": "chatgpt.share_html/1", "scope": "shared_snapshot_only", "locator": locator,
            "conversation_pointer": prefix, "branch": {"selection": selection, "selected_node": selected,
            "selected_path": [key for key in order if key in active], "tree_preserved": True},
            "attachments_mode": "references", "status": "partial" if partial or not messages else "complete",
            "reason": "share_content_partial" if partial else "provided_messages" if messages else "empty_share"}


def parse_content(snapshot, mode, page_role, output_budget=None):
    """Parse a conversation snapshot, never a page summary or a model reply."""
    out = {"messages": [], "title": "", "status": snapshot["status"], "reason": snapshot["reason"],
           "scope": "provided_messages_only" if mode == "share" else "fetched_resource"}
    if snapshot["status"] != "complete":
        return out
    try:
        text = base64.b64decode(snapshot["body_base64"]).decode("utf-8-sig")
    except UnicodeError:
        return out | {"status": "unsupported", "reason": "encoding_not_utf8"}
    if mode == "share":
        if snapshot["mime"] not in ("text/html", "application/json"):
            return out | {"status": "unsupported", "reason": "share_media_type"}
        if snapshot["mime"] == "text/html":
            page = ShareHTML()
            page.feed(text)
            if page.login:
                return out | {"status": "denied", "reason": "login_required"}
            if not page.closed:
                return out | {"status": "partial", "reason": "incomplete_html"}
            final = urlsplit(snapshot.get("final_url", ""))
            if final.path.startswith(("/auth/", "/login")):
                return out | {"status": "denied", "reason": "login_required"}
            if final.scheme != "https" or final.hostname not in ("chatgpt.com", "chat.openai.com") or not re.fullmatch(r"/share/[A-Za-z0-9-]+/?", final.path):
                return out | {"status": "unsupported", "reason": "share_contract_unknown"}
            try:
                # Direct parser tests may omit the caller budget; production
                # always supplies the existing configured IPC output limit.
                data, locator = chatgpt_data(page, output_budget if output_budget is not None else float("inf"))
                return out | chatgpt_messages(data, locator)
            except (ValueError, KeyError, TypeError, RecursionError):
                return out | {"status": "unsupported", "reason": "share_structure_unavailable"}
        try:
            data = json.loads(text)
        except (ValueError, RecursionError):
            page = PageText()
            page.feed(text)
            if page.login:
                return out | {"status": "denied", "reason": "login_required"}
            return out | {"status": "partial" if "<html" in text.lower() and not page.closed else "unsupported",
                          "reason": "share_structure_unavailable"}
        if not isinstance(data, dict) or not isinstance(data.get("conversation"), dict) or not isinstance(data.get("messages"), list):
            return out | {"status": "unsupported", "reason": "share_contract_unknown"}
        out.update(adapter="loom.conversation_export/1", reason="provided_messages", title=data["conversation"].get("title", ""))
        if not isinstance(out["title"], str):
            out["title"] = ""
        for index, message in enumerate(data["messages"]):
            pointer = "/messages/" + str(index)
            if not isinstance(message, dict) or not isinstance(message.get("role"), str) or not message["role"] or not isinstance(message.get("text"), str):
                out.update(status="partial", reason="invalid_message")
                continue
            attachments = message.get("attachments", [])
            if not isinstance(attachments, list):
                attachments = [attachments]
                out.update(status="partial", reason="invalid_attachments")
            out["messages"].append({"role": message["role"], "text": message["text"], "pointer": pointer,
                                    "attachments": attachments})
        if not data["messages"]:
            out.update(status="partial", reason="empty_share")
        return out
    mime = snapshot["mime"]
    if mime == "text/html":
        page = PageText()
        page.feed(text)
        if page.login:
            return out | {"status": "denied", "reason": "login_required"}
        text = "".join(page.text).strip()
        if not page.closed:
            out.update(status="partial", reason="incomplete_html")
    elif mime not in ("text/plain", "text/markdown", "application/json"):
        return out | {"status": "unsupported", "reason": "page_media_type"}
    if not text.strip():
        return out | {"status": "partial", "reason": "empty_page"}
    out["messages"] = [{"role": page_role, "text": text, "pointer": "", "attachments": []}]
    return out


def validate_policy(policy):
    if not isinstance(policy, dict) or policy.get("schema") != "loom.url_import_policy/1":
        raise ValueError("policy")
    limits = policy["limits"]
    if set(limits) != {"timeout_ms", "worker_timeout_ms", "max_bytes", "max_total_bytes", "max_redirects", "max_attachments", "worker_output_bytes"}:
        raise ValueError("policy")
    for key in ("timeout_ms", "worker_timeout_ms", "max_bytes", "max_total_bytes", "max_redirects", "max_attachments", "worker_output_bytes"):
        v = limits[key]
        if type(v) is not int or v < (0 if key in ("max_redirects", "max_attachments") else 1):
            raise ValueError("policy")
    if not isinstance(policy["page_role"], str) or not policy["page_role"]:
        raise ValueError("policy")
    return policy


def snapshot_request(request, policy, transport=None):
    validate_policy(policy)
    if request.get("schema") != "loom.url_import_request/1" or request.get("mode") not in ("page", "share") or not isinstance(request.get("url"), str):
        raise ValueError("request")
    # Unknown fields cannot smuggle transport/credentials/executables into the worker.
    if set(request) - {"schema", "mode", "url"}:
        raise ValueError("request")
    fetcher = Fetcher(policy["limits"], transport)
    resource = fetcher.fetch(request["url"])
    parsed = parse_content(resource, request["mode"], policy["page_role"], policy["limits"]["worker_output_bytes"])
    attempted = 0
    for message in parsed["messages"]:
        resolved = []
        for index, attachment in enumerate(message["attachments"]):
            pointer = message.get("attachment_pointers", [])[index] if "attachment_pointers" in message else message["pointer"] + "/attachments/" + str(index)
            record = {"pointer": pointer, "original": attachment}
            if parsed.get("attachments_mode") == "references":
                record.update(status="reference", reason="not_fetched")
                resolved.append(record)
                continue
            target = attachment if isinstance(attachment, str) else attachment.get("url") if isinstance(attachment, dict) else None
            if not isinstance(target, str) or not target.startswith(("http://", "https://")):
                record.update(status="unsupported", reason="attachment_not_public_url")
            elif attempted >= policy["limits"]["max_attachments"]:
                record.update(status="partial", reason="attachment_limit")
            else:
                attempted += 1
                record.update(fetcher.fetch(target))
                if record["http_status"] in (404, 410):
                    record["status"] = "missing"
            if record["status"] != "complete":
                parsed.update(status="partial", reason="attachments_incomplete")
            resolved.append(record)
        message["attachments"] = resolved
    return {"schema": "loom.url_snapshot/1", "mode": request["mode"], "resource": resource,
            "parsed": parsed, "llm_calls": 0}


def main():
    try:
        envelope = json.load(sys.stdin)
        result = snapshot_request(envelope["request"], envelope["policy"])
        json.dump(result, sys.stdout, ensure_ascii=True, separators=(",", ":"))
    except Exception:
        # stdout is an IPC channel; stderr stays empty, including on malformed input.
        json.dump({"error": "url_worker_failed"}, sys.stdout)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
