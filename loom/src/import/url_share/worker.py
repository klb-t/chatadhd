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
import socket
import ssl
import sys
import time
from html.parser import HTMLParser
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


def parse_content(snapshot, mode, page_role):
    """Only the existing Loom JSON export contract is accepted as a share.

    No title-only heuristic, scraping provider endpoints or hidden turns.
    Original JSON bytes remain the authority; JSON pointers locate each row.
    """
    out = {"messages": [], "title": "", "status": snapshot["status"], "reason": snapshot["reason"],
           "scope": "provided_messages_only" if mode == "share" else "fetched_resource"}
    if snapshot["status"] != "complete":
        return out
    try:
        text = base64.b64decode(snapshot["body_base64"]).decode("utf-8-sig")
    except UnicodeError:
        return out | {"status": "unsupported", "reason": "encoding_not_utf8"}
    if mode == "share":
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
    parsed = parse_content(resource, request["mode"], policy["page_role"])
    attempted = 0
    for message in parsed["messages"]:
        resolved = []
        for index, attachment in enumerate(message["attachments"]):
            record = {"pointer": message["pointer"] + "/attachments/" + str(index), "original": attachment}
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
