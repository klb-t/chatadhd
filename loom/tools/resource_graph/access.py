"""Read-only, demand-driven transport and ZIP container composition.

No graph, configuration engine, extraction directory or persistent cache lives
here. ``read_transport`` and ``open_container`` are separately callable stages.
The application chooses caching/snapshot/embedding independently. An injected
transport is trusted infrastructure: it MUST NOT follow redirects and MUST
honour its byte/time budgets. The built-in transport enforces those conditions.
"""
from __future__ import annotations

import copy
import hashlib
import io
import json
import math
import os
import stat
import struct
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath


POLICY_PATH = Path(__file__).resolve().parents[2] / "data/resource_graph/access.json"


class AccessFailure(Exception):
    """A public, credential-free reason; source exception text is never copied."""

    def __init__(self, status, reason, *, limit_source=None):
        self.status = status
        self.reason = reason
        self.limit_source = limit_source
        super().__init__(reason)


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def _now():
    return datetime.now(timezone.utc).isoformat()


def sanitize_locator(locator):
    """Never put URL credentials, query arguments or fragments in evidence."""
    try:
        parsed = urllib.parse.urlsplit(str(locator))
        if parsed.netloc:
            host = parsed.hostname or ""
            if ":" in host:
                host = "[" + host + "]"
            port = ":" + str(parsed.port) if parsed.port else ""
            return urllib.parse.urlunsplit((parsed.scheme.lower(), host + port,
                                           parsed.path, "", ""))
        return str(locator)
    except (ValueError, TypeError):
        return "<invalid-locator>"


def _safe_member(name):
    if not isinstance(name, str) or not name or "\\" in name or ":" in name:
        return False
    if any(ord(character) < 32 or ord(character) == 127 for character in name):
        return False
    parts = name.rstrip("/").split("/")
    return not PurePosixPath(name).is_absolute() and all(p not in {"", ".", ".."} for p in parts)


class Access:
    """Compose transport with zero or more explicitly selected ZIP members.

    ``policy`` overrides user settings (flat dict or ``user_policy`` section).
    Only the separately supplied, trusted ``environment_limits`` argument can
    change deployment limits. A nested environment_limits in user input is
    rejected. Effective budgets are the minimum, with the limiting layer in
    every budget failure. Defaults are data in access.json.
    """

    def __init__(self, policy=None, transport=None, *, environment_limits=None):
        defaults = json.loads(POLICY_PATH.read_text(encoding="utf-8"))
        self.policy = copy.deepcopy(defaults["user_policy"])
        self.environment_limits = copy.deepcopy(defaults["environment_limits"])
        if policy:
            if "environment_limits" in policy:
                raise ValueError("deployment limits require environment_limits argument")
            self.policy.update(copy.deepcopy(policy.get("user_policy", policy)))
        if environment_limits is not None:
            self.environment_limits.update(copy.deepcopy(environment_limits))
        for name in self.environment_limits:
            for value in (self.policy[name], self.environment_limits[name]):
                if isinstance(value, bool) or not isinstance(value, (int, float)) or value <= 0:
                    raise ValueError("access budgets must be positive finite numbers")
                if not math.isfinite(value):
                    raise ValueError("access budgets must be positive finite numbers")
        self.transport = transport or self._http_transport

    def _limit(self, name):
        return min(self.policy[name], self.environment_limits[name])

    def _check(self, name, value):
        if value > self._limit(name):
            source = ("environment_limits" if self.environment_limits[name] <= self.policy[name]
                      else "user_policy")
            raise AccessFailure("partial", "budget_exceeded:" + name, limit_source=source)

    @staticmethod
    def _failure(base, error):
        result = {**base, "status": error.status, "reason": error.reason}
        if error.limit_source is not None:
            result["limit_source"] = error.limit_source
        result.pop("data", None)
        return result

    @staticmethod
    def _base(locator, members=()):
        return {"status": "unloaded", "locator": sanitize_locator(locator),
                "members": list(members), "source_version": None,
                "content_sha256": None, "provenance": []}

    def _origin(self, url):
        parsed = urllib.parse.urlsplit(url)
        if (parsed.scheme not in {"http", "https"} or not parsed.hostname
                or parsed.username is not None or parsed.password is not None
                or any(ord(c) < 33 or ord(c) == 127 for c in url)):
            raise AccessFailure("unavailable", "url_not_authorized")
        host = parsed.hostname.lower()
        port = parsed.port or (443 if parsed.scheme == "https" else 80)
        return parsed.scheme, host, port

    def _authorize_url(self, url):
        origin = self._origin(url)
        allowed = {self._origin(entry) for entry in self.policy["allowed_origins"]}
        if origin not in allowed:
            raise AccessFailure("unavailable", "origin_not_authorized")

    def _http_transport(self, url, timeout, max_bytes):
        # Never inherit proxy credentials, cookie jars, netrc or auth handlers.
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), _NoRedirect())
        request = urllib.request.Request(url, headers={"Accept-Encoding": "identity"})
        try:
            response = opener.open(request, timeout=timeout)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            headers = {key.lower(): value for key, value in response.headers.items()}
            status = response.code
            if 300 <= status < 400 or status >= 400:
                return {"status_code": status, "headers": headers, "data": b""}
            if headers.get("content-encoding", "identity").lower() not in {"identity", ""}:
                raise AccessFailure("unsupported", "http_content_encoding")
            if "content-length" in headers:
                try:
                    length = int(headers["content-length"])
                except ValueError:
                    raise AccessFailure("corrupt", "invalid_content_length") from None
                if length < 0:
                    raise AccessFailure("corrupt", "invalid_content_length")
                self._check("max_source_bytes", length)
            data = response.read(int(max_bytes) + 1)
            self._check("max_source_bytes", len(data))
            if "content-length" in headers and len(data) != int(headers["content-length"]):
                raise AccessFailure("partial", "truncated_http_response")
            return {"status_code": status, "headers": headers, "data": data,
                    "final_url": response.geturl()}

    def _remote(self, locator):
        url = urllib.parse.urldefrag(locator)[0]
        redirects = []
        while True:
            self._authorize_url(url)
            response = self.transport(url, self._limit("timeout_seconds"),
                                      int(self._limit("max_source_bytes")))
            if response.get("final_url", url) != url:
                raise AccessFailure("unavailable", "transport_followed_redirect")
            status = int(response.get("status_code", 200))
            headers = {str(k).lower(): str(v) for k, v in response.get("headers", {}).items()}
            if 300 <= status < 400:
                if not self.policy["allow_redirects"]:
                    raise AccessFailure("unavailable", "redirect_not_authorized")
                self._check("max_redirects", len(redirects) + 1)
                if not headers.get("location"):
                    raise AccessFailure("corrupt", "redirect_without_location")
                target = urllib.parse.urljoin(url, headers["location"])
                self._authorize_url(target)  # Before any request, also on same origin.
                redirects.append({"from": sanitize_locator(url), "to": sanitize_locator(target)})
                url = target
                continue
            if status < 200 or status >= 300:
                raise AccessFailure("unavailable", "http_status:" + str(status))
            if status == 206:
                raise AccessFailure("partial", "unsolicited_partial_http_response")
            data = response.get("data")
            if not isinstance(data, bytes):
                raise AccessFailure("corrupt", "transport_data_not_bytes")
            self._check("max_source_bytes", len(data))
            version = {"kind": "http_validators"}
            for key in ("etag", "last-modified", "content-length"):
                if key in headers:
                    version[key] = headers[key]
            if not any(key in version for key in ("etag", "last-modified")):
                version = {"kind": "http_content_observation",
                           "content_sha256": hashlib.sha256(data).hexdigest()}
            return data, version, {"stage": "transport", "kind": "http",
                                   "observed_at": _now(), "redirects": redirects,
                                   "locator": sanitize_locator(url)}

    def authorize_local(self, locator):
        """Return a permitted canonical regular-file Path, without reading it.

        No file descriptor is retained. Callers must use a safe open and verify
        the descriptor: this authorization alone is not a race-free read.
        """
        if not self.policy["allow_local"]:
            raise AccessFailure("unavailable", "local_access_not_authorized")
        parsed = urllib.parse.urlsplit(str(locator))
        if parsed.scheme == "file":
            if parsed.netloc not in {"", "localhost"} or parsed.query or parsed.fragment:
                raise AccessFailure("unavailable", "file_locator_not_authorized")
            path = Path(urllib.request.url2pathname(parsed.path))
        elif not parsed.scheme:
            path = Path(locator)
        else:
            raise AccessFailure("unsupported", "local_transport_scheme")
        path = path.resolve(strict=True)
        roots = [Path(root).resolve(strict=True) for root in self.policy["local_roots"]]
        if roots and not any(path.is_relative_to(root) for root in roots):
            raise AccessFailure("unavailable", "outside_local_roots")
        if not stat.S_ISREG(path.stat().st_mode):
            raise AccessFailure("unsupported", "local_source_not_regular_file")
        return path

    def _local(self, locator):
        path = self.authorize_local(locator)
        # NONBLOCK means an adversarial FIFO cannot hang before fstat rejects it.
        descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NONBLOCK", 0))
        with os.fdopen(descriptor, "rb") as source:
            before = os.fstat(source.fileno())
            if not stat.S_ISREG(before.st_mode):
                raise AccessFailure("unsupported", "local_source_not_regular_file")
            self._check("max_source_bytes", before.st_size)
            data = source.read(int(self._limit("max_source_bytes")) + 1)
            self._check("max_source_bytes", len(data))
            after = os.fstat(source.fileno())
            fields = ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")
            if any(getattr(before, key) != getattr(after, key) for key in fields):
                raise AccessFailure("partial", "source_changed_during_read")
        version = {"kind": "local_stat",
                   **{key: getattr(after, key) for key in fields}}
        return data, version, {"stage": "transport", "kind": "file", "locator": str(path),
                              "observed_at": _now()}

    def read_transport(self, locator):
        """Acquire one bounded source, without guessing format or opening ZIPs."""
        base = self._base(locator)
        try:
            scheme = urllib.parse.urlsplit(str(locator)).scheme.lower()
            if scheme in {"http", "https"}:
                data, version, provenance = self._remote(str(locator))
            elif scheme in {"", "file"}:
                data, version, provenance = self._local(str(locator))
            else:
                raise AccessFailure("unsupported", "transport_scheme")
            return {**base, "status": "available" if data else "empty", "data": data,
                    "content_sha256": hashlib.sha256(data).hexdigest(),
                    "source_version": version, "provenance": [provenance]}
        except AccessFailure as error:
            return self._failure(base, error)
        except Exception:
            # Includes incomplete HTTP responses and injected transport errors;
            # their messages may contain URL secrets and are not public evidence.
            return self._failure(base, AccessFailure("unavailable", "source_read_failed"))

    def _zip(self, data):
        # Preflight central-directory count before ZipFile allocates ZipInfo rows.
        end = data.rfind(b"PK\x05\x06", max(0, len(data) - 65557))
        if end < 0 or end + 22 > len(data):
            raise AccessFailure("corrupt", "zip_end_record_missing")
        fields = struct.unpack_from("<4s4H2LH", data, end)
        if fields[1] or fields[2]:
            raise AccessFailure("unsupported", "multi_disk_zip")
        count = fields[4]
        directory_size = fields[5]
        directory_end = end
        if count == 65535:
            # ZIP64 EOCD locator immediately precedes the ordinary EOCD.
            if end < 20 or data[end - 20:end - 16] != b"PK\x06\x07":
                raise AccessFailure("corrupt", "zip64_locator_missing")
            offset = struct.unpack_from("<Q", data, end - 12)[0]
            if offset + 56 > len(data) or data[offset:offset + 4] != b"PK\x06\x06":
                raise AccessFailure("corrupt", "zip64_end_record_missing")
            count = struct.unpack_from("<Q", data, offset + 32)[0]
            directory_size = struct.unpack_from("<Q", data, offset + 40)[0]
            directory_end = offset
        self._check("max_archive_entries", count)
        # Do not trust EOCD's declared entry count: scan the variable-length
        # directory records before ZipFile constructs its in-memory objects.
        cursor = directory_end - directory_size
        if cursor < 0:
            raise AccessFailure("corrupt", "zip_directory_outside_source")
        actual_count = 0
        while cursor < directory_end:
            if cursor + 46 > directory_end or data[cursor:cursor + 4] != b"PK\x01\x02":
                raise AccessFailure("corrupt", "invalid_zip_directory")
            lengths = struct.unpack_from("<HHH", data, cursor + 28)
            cursor += 46 + sum(lengths)
            if cursor > directory_end:
                raise AccessFailure("corrupt", "invalid_zip_directory")
            actual_count += 1
            self._check("max_archive_entries", actual_count)
        if actual_count != count:
            raise AccessFailure("corrupt", "zip_directory_count_mismatch")
        archive = zipfile.ZipFile(io.BytesIO(data))
        try:
            self._check("max_archive_entries", len(archive.infolist()))
        except BaseException:
            archive.close()
            raise
        return archive

    @staticmethod
    def _entry_problem(info):
        if not _safe_member(info.filename):
            return "unsafe_member_path"
        if stat.S_IFMT(info.external_attr >> 16) not in {0, stat.S_IFREG, stat.S_IFDIR}:
            return "non_regular_archive_member"
        if info.flag_bits & 1:
            return "encrypted_archive_member"
        return None

    def open_container(self, data, members=()):
        """Select nested ZIP members; no extraction, domain parsing or writes.

        ``max_expanded_bytes`` accounts for every decoded ancestor plus leaf.
        Archive inventory does not decompress unrelated members.
        """
        base = {"status": "unloaded", "members": list(members), "provenance": []}
        try:
            self._check("max_nesting_depth", len(members))
            current = data
            total = 0
            for depth, member in enumerate(members):
                if not _safe_member(member):
                    raise AccessFailure("unavailable", "unsafe_member_path")
                with self._zip(current) as archive:
                    candidates = [entry for entry in archive.infolist() if entry.filename == member]
                    if not candidates:
                        raise AccessFailure("unavailable", "member_not_found")
                    if len(candidates) != 1:
                        raise AccessFailure("corrupt", "ambiguous_duplicate_member")
                    entry = candidates[0]
                    problem = self._entry_problem(entry)
                    if problem:
                        raise AccessFailure("unsupported", problem)
                    if entry.is_dir():
                        raise AccessFailure("unsupported", "member_is_directory")
                    self._check("max_member_bytes", entry.file_size)
                    self._check("max_expanded_bytes", total + entry.file_size)
                    self._check("max_compression_ratio", entry.file_size / max(entry.compress_size, 1))
                    with archive.open(entry) as source:
                        current = source.read(int(self._limit("max_member_bytes")) + 1)
                    self._check("max_member_bytes", len(current))
                    total += len(current)
                    self._check("max_expanded_bytes", total)
                    if len(current) != entry.file_size:
                        raise AccessFailure("corrupt", "zip_member_size_mismatch")
                    base["provenance"].append({"stage": "container", "kind": "zip", "depth": depth,
                        "member": member, "crc32": entry.CRC, "size": len(current),
                        "content_sha256": hashlib.sha256(current).hexdigest()})
            return {**base, "status": "available" if current else "empty", "data": current,
                    "content_sha256": hashlib.sha256(current).hexdigest()}
        except AccessFailure as error:
            return self._failure(base, error)
        except (zipfile.BadZipFile, EOFError, ValueError, OSError, struct.error):
            return self._failure(base, AccessFailure("corrupt", "invalid_zip_container"))
        except (NotImplementedError, RuntimeError):
            return self._failure(base, AccessFailure("unsupported", "zip_compression_or_encryption"))
        except Exception:
            return self._failure(base, AccessFailure("corrupt", "invalid_zip_container"))

    def read(self, locator, members=()):
        """One transport result composed with an explicit nested ZIP selector."""
        source = self.read_transport(locator)
        if source["status"] not in {"available", "empty"} or not members:
            return {**source, "members": list(members)}
        result = self.open_container(source["data"], members)
        return {**{key: value for key, value in source.items() if key != "data"}, **result,
                "outer_content_sha256": source["content_sha256"],
                "provenance": source["provenance"] + result["provenance"]}

    def list_members(self, locator, members=()):
        """List metadata for one ZIP, without expanding any member payloads."""
        result = self.read(locator, members)
        if result["status"] not in {"available", "empty"}:
            return {**result, "entries": []}
        data = result.pop("data")
        inventory = self.inspect_container(data)
        return {**result, **inventory,
                "provenance": result["provenance"] + inventory["provenance"]}

    def inspect_container(self, data):
        """ZIP byte-stage inventory, with no expansion of member contents."""
        result = {"provenance": [], "content_sha256": hashlib.sha256(data).hexdigest()}
        try:
            with self._zip(data) as archive:
                entries = archive.infolist()
                counts = {}
                for entry in entries:
                    counts[entry.filename] = counts.get(entry.filename, 0) + 1
                rows = []
                for index, entry in enumerate(entries):
                    problem = self._entry_problem(entry)
                    if counts[entry.filename] > 1:
                        problem = "ambiguous_duplicate_member"
                    rows.append({"member": entry.filename, "index": index,
                                 "size": entry.file_size, "compressed_size": entry.compress_size,
                                 "compression": entry.compress_type, "crc32": entry.CRC,
                                 "directory": entry.is_dir(), "status": "unsupported" if problem else "available",
                                 "reason": problem})
                return {**result, "status": "partial" if any(row["reason"] for row in rows)
                        else ("available" if rows else "empty"), "entries": rows}
        except AccessFailure as error:
            return {**self._failure(result, error), "entries": []}
        except (zipfile.BadZipFile, EOFError, ValueError, OSError, struct.error):
            return {**self._failure(result, AccessFailure("corrupt", "invalid_zip_container")), "entries": []}

    def validate_archive(self, data, *, recursive=True):
        """Eager, bounded ZIP/CRC preflight for an explicitly eager consumer.

        Refuses unsafe/ambiguous/encrypted members. Recursively validates members
        recognized as ZIP by signature or .zip extension. This is not needed for
        the primary lazy access path. Budgets count all decoded bytes and all
        inspected entries, so nested archives cannot each reset the allowance.
        """
        result = {"status": "unloaded", "provenance": [], "members_validated": 0,
                  "expanded_bytes": 0, "content_sha256": hashlib.sha256(data).hexdigest()}
        entries_seen = 0

        def visit(payload, path):
            nonlocal entries_seen
            self._check("max_nesting_depth", len(path))
            inventory = self.inspect_container(payload)
            if inventory["status"] not in {"available", "empty"}:
                raise AccessFailure(inventory["status"], inventory.get("reason", "unsafe_archive_inventory"),
                                    limit_source=inventory.get("limit_source"))
            entries_seen += len(inventory["entries"])
            self._check("max_archive_entries", entries_seen)
            for entry in inventory["entries"]:
                if entry["directory"]:
                    continue
                self._check("max_expanded_bytes", result["expanded_bytes"] + entry["size"])
                decoded = self.open_container(payload, (entry["member"],))
                if decoded["status"] not in {"available", "empty"}:
                    raise AccessFailure(decoded["status"], decoded.get("reason", "member_validation_failed"),
                                        limit_source=decoded.get("limit_source"))
                result["members_validated"] += 1
                result["expanded_bytes"] += len(decoded["data"])
                result["provenance"].append({"members": list(path + (entry["member"],)),
                                               "content_sha256": decoded["content_sha256"]})
                if recursive and (decoded["data"].startswith((b"PK\x03\x04", b"PK\x05\x06"))
                                  or entry["member"].lower().endswith(".zip")):
                    visit(decoded["data"], path + (entry["member"],))

        try:
            visit(data, ())
            result["status"] = "available" if result["members_validated"] else "empty"
            return result
        except AccessFailure as error:
            return self._failure(result, error)
