"""Byte-preserving conversation export transport; no database or conversation importer.

The caller supplies ConversationExporter bytes and a complete attachment inventory.
Only hash-addressed BlobStore files are read. Restore validates a private snapshot,
then delegates once to the existing ZIP importer through an explicit callback.
"""
from __future__ import annotations

import contextlib
import dataclasses
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import struct
import tempfile
from typing import Callable, Iterator
import zipfile

SCHEMA = "loom.conversation_bundle/1"
INDEX = "bundle.index.json"
SNAPSHOT = "conversation.json"
_HASH = re.compile(r"[0-9a-f]{64}\Z")
_POINTER = re.compile(r"/messages/(0|[1-9][0-9]*)/attachments/(0|[1-9][0-9]*)\Z")


class BundleError(ValueError):
    """Rejected input. Messages contain mechanism diagnostics, never source data."""


def _require(condition: bool, code: str) -> None:
    if not condition:
        raise BundleError(code)


def _json_bytes(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
                      allow_nan=False).encode("utf-8")


def _pairs(pairs: list) -> dict:
    result = {}
    for key, value in pairs:
        _require(key not in result, "duplicate_json_key")
        result[key] = value
    return result


def _parse(data: bytes) -> object:
    try:
        return json.loads(data.decode("utf-8"), object_pairs_hook=_pairs,
                          parse_constant=lambda _: (_ for _ in ()).throw(BundleError("nonfinite_json")))
    except (UnicodeError, ValueError, RecursionError) as exc:
        raise BundleError("invalid_json") from exc


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@dataclasses.dataclass(frozen=True)
class Limits:
    # None disables an admission limit; positive values are caller policy.
    max_archive_bytes: int | None
    max_total_bytes: int | None
    max_member_bytes: int | None
    max_manifest_bytes: int | None
    max_members: int | None
    max_ratio: int | None
    chunk_bytes: int

    def __post_init__(self) -> None:
        for field in dataclasses.fields(self):
            value = getattr(self, field.name)
            _require(value is None and field.name != "chunk_bytes" or
                     type(value) is int and value > 0, "invalid_limit")

    def check(self, key: str, value: int) -> None:
        bound = getattr(self, key)
        _require(bound is None or value <= bound, key)


def preset(path: Path | None = None) -> tuple[Limits, dict[str, str]]:
    data = json.loads((path or Path(__file__).with_name("preset.json")).read_text(encoding="utf-8"))
    return Limits(**data["limits"]), data["formats"]


def _snapshot(data: bytes) -> tuple[str, set[str]]:
    doc = _parse(data)
    _require(isinstance(doc, dict) and isinstance(doc.get("conversation"), dict) and
             isinstance(doc.get("messages"), list), "export_snapshot_shape")
    cid = doc["conversation"].get("id")
    _require(isinstance(cid, str) and bool(cid), "conversation_id_required")
    pointers = set()
    for i, message in enumerate(doc["messages"]):
        _require(isinstance(message, dict), "message_shape")
        attachments = message.get("attachments", [])
        _require(isinstance(attachments, list), "attachments_shape")
        for j in range(len(attachments)):
            pointers.add(f"/messages/{i}/attachments/{j}")
    return cid, pointers


def _safe_member(name: str) -> bool:
    return (isinstance(name, str) and bool(name) and not name.startswith("/") and
            not any(c in name for c in ("\\", ":", "\x00")) and
            all(part not in ("", ".", "..") for part in name.split("/")))


def _bindings(bindings: list, pointers: set[str], incomplete: bool) -> set[str]:
    _require(isinstance(bindings, list), "bindings_shape")
    seen, hashes = set(), set()
    for binding in bindings:
        _require(isinstance(binding, dict) and set(binding) <=
                 {"pointer", "availability", "sha256", "name", "mime", "provenance"}, "binding_shape")
        pointer = binding.get("pointer")
        _require(isinstance(pointer, str) and _POINTER.fullmatch(pointer) is not None and
                 pointer in pointers and pointer not in seen, "attachment_pointer")
        seen.add(pointer)
        availability = binding.get("availability")
        _require(availability in ("included", "missing", "unmanaged"), "attachment_availability")
        if availability == "included":
            digest = binding.get("sha256")
            _require(isinstance(digest, str) and _HASH.fullmatch(digest) is not None, "original_hash")
            hashes.add(digest)
        else:
            _require(incomplete, "incomplete_bundle")
            # A known expected original identity survives loss of its bytes.
            # Availability, not the presence of a hash, certifies inclusion.
            digest = binding.get("sha256")
            _require(digest is None or isinstance(digest, str) and _HASH.fullmatch(digest) is not None,
                     "original_hash")
        for key in ("name", "mime"):
            _require(key not in binding or isinstance(binding[key], str), "binding_label")
        refs = binding.get("provenance", {})
        _require(isinstance(refs, dict) and set(refs) <= {"source_id", "unit_id", "observation_id"}
                 and all(isinstance(value, str) and value for value in refs.values()), "provenance_reference")
    _require(seen == pointers, "attachment_inventory_incomplete")
    return hashes


@contextlib.contextmanager
def _regular(path: Path) -> Iterator:
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        _require(stat.S_ISREG(os.fstat(fd).st_mode), "regular_file_required")
        with os.fdopen(fd, "rb", closefd=False) as stream:
            yield stream
    finally:
        os.close(fd)


@contextlib.contextmanager
def _blob(root: Path, digest: str) -> Iterator:
    # Walk only the three known BlobStore components, without following links.
    fds = []
    try:
        fds.append(os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW))
        for component in (digest[:2], digest[2:4]):
            fds.append(os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                               dir_fd=fds[-1]))
        fds.append(os.open(digest, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=fds[-1]))
        _require(stat.S_ISREG(os.fstat(fds[-1]).st_mode), "regular_blob_required")
        with os.fdopen(fds[-1], "rb", closefd=False) as stream:
            yield stream
    except FileNotFoundError as exc:
        raise BundleError("original_missing") from exc
    finally:
        for fd in reversed(fds):
            os.close(fd)


def _copy(source, target, limits: Limits, bound: str = "max_member_bytes") -> tuple[str, int]:
    digest, size = hashlib.sha256(), 0
    while block := source.read(limits.chunk_bytes):
        size += len(block)
        limits.check(bound, size)
        target.write(block)
        digest.update(block)
    return digest.hexdigest(), size


def _info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_STORED
    info.create_system = 3
    info.external_attr = (stat.S_IFREG | 0o600) << 16
    return info


def create_bundle(destination: Path, exports: dict[str, bytes], bindings: list[dict],
                  blob_root: Path, *, limits: Limits, formats: dict[str, str],
                  allow_incomplete: bool = False) -> dict:
    """Package existing exporter bytes, without opening a DB or arbitrary attachment paths.

    bindings cover EVERY snapshot attachment slot. An included slot has an
    original SHA. Missing/unmanaged slots require explicit allow_incomplete.
    The destination must not exist. Its parent is a trusted, existing directory.
    """
    _require(type(allow_incomplete) is bool, "invalid_incomplete_policy")
    _require(isinstance(exports, dict) and "json" in exports and
             all(isinstance(data, bytes) for data in exports.values()), "export_bytes_required")
    _require(formats.get("json") == SNAPSHOT and len(set(formats.values())) == len(formats) and
             all(_safe_member(name) and name != INDEX and not name.startswith("originals/")
                 for name in formats.values()), "format_contract")
    _require(set(exports) <= set(formats), "unknown_export_format")
    for data in exports.values():
        limits.check("max_member_bytes", len(data))
    cid, pointers = _snapshot(exports["json"])
    bindings = _parse(_json_bytes(bindings))  # private, JSON-only copy
    hashes = _bindings(bindings, pointers, allow_incomplete)
    limits.check("max_members", len(exports) + len(hashes) + 1)
    manifest = {"schema": SCHEMA, "conversation_id": cid,
                "complete": all(b["availability"] == "included" for b in bindings),
                "exports": [], "originals": [], "attachments": sorted(bindings, key=lambda b: b["pointer"])}
    fd, temporary = tempfile.mkstemp(prefix=".loom-bundle-", dir=destination.parent)
    os.close(fd)
    temporary = Path(temporary)
    total = 0
    try:
        with zipfile.ZipFile(temporary, "w", allowZip64=False) as archive:
            for fmt in sorted(exports):
                data, member = exports[fmt], formats[fmt]
                total += len(data)
                limits.check("max_total_bytes", total)
                archive.writestr(_info(member), data)
                manifest["exports"].append({"member": member, "format": fmt, "role": "derived",
                    "sha256": _sha(data), "size": len(data),
                    "genesis": {"method": "loom.ConversationExporter", "conversation_id": cid}})
                limits.check("max_archive_bytes", archive.fp.tell())
            for digest in sorted(hashes):
                member = "originals/" + digest
                with _blob(blob_root, digest) as source, archive.open(_info(member), "w") as target:
                    # Admission before writing based on fstat; _copy also checks actual bytes.
                    declared = os.fstat(source.fileno()).st_size
                    limits.check("max_member_bytes", declared)
                    limits.check("max_total_bytes", total + declared)
                    actual, size = _copy(source, target, limits)
                _require(actual == digest, "original_hash_mismatch")
                total += size
                limits.check("max_total_bytes", total)
                limits.check("max_archive_bytes", archive.fp.tell())
                manifest["originals"].append({"member": member, "sha256": digest, "size": size,
                    "role": "original", "genesis": {"method": "identity", "source": "sha256:" + digest}})
            index = _json_bytes(manifest)
            limits.check("max_manifest_bytes", len(index))
            limits.check("max_member_bytes", len(index))
            limits.check("max_total_bytes", total + len(index))
            archive.writestr(_info(INDEX), index)
        limits.check("max_archive_bytes", temporary.stat().st_size)
        # Exercise the same contract before publication. No partial destination on failure.
        validate_bundle(temporary, limits=limits, formats=formats, require_complete=False)
        with temporary.open("rb") as stream:
            archive_hash, size = _copy(stream, _Discard(), limits, "max_archive_bytes")
        os.link(temporary, destination)  # exclusive publication, never overwrites
        return {"sha256": archive_hash, "size": size, "manifest_sha256": _sha(index),
                "complete": manifest["complete"]}
    finally:
        temporary.unlink(missing_ok=True)


class _Discard:
    def write(self, data: bytes) -> None:
        pass


def _directory(stream, limits: Limits) -> None:
    """Bound entry count before ZipFile allocates the central directory objects.

    v1 writes conventional, single-disk ZIP without comments or ZIP64.
    These restrictions are format capabilities, separate from admission presets.
    """
    size = os.fstat(stream.fileno()).st_size
    limits.check("max_archive_bytes", size)
    _require(size >= 22, "invalid_zip")
    stream.seek(-22, os.SEEK_END)
    signature, disk, central_disk, disk_count, count, central_size, offset, comment = struct.unpack(
        "<4s4H2LH", stream.read(22))
    _require(signature == b"PK\x05\x06" and disk == central_disk == comment == 0 and
             disk_count == count and count != 0xffff and
             central_size != 0xffffffff and offset != 0xffffffff and
             offset + central_size == size - 22, "unsupported_zip_layout")
    limits.check("max_members", count)
    # Do not trust an understated EOCD count: Python's ZipFile iterates the
    # directory bytes regardless of that count and otherwise allocates first.
    cursor, actual_count = offset, 0
    while cursor < offset + central_size:
        stream.seek(cursor)
        header = stream.read(46)
        _require(len(header) == 46 and header[:4] == b"PK\x01\x02", "invalid_central_directory")
        name_bytes, extra_bytes, comment_bytes = struct.unpack_from("<3H", header, 28)
        cursor += 46 + name_bytes + extra_bytes + comment_bytes
        actual_count += 1
        limits.check("max_members", actual_count)
    _require(cursor == offset + central_size and actual_count == count, "central_directory_count")
    stream.seek(0)
    _require(stream.read(4) == b"PK\x03\x04", "unsupported_zip_prefix")
    stream.seek(0)


def _read(archive: zipfile.ZipFile, member: str, limits: Limits, *, capture: bool = False) -> tuple[str, int, bytes]:
    sink = io.BytesIO() if capture else _Discard()
    with archive.open(member) as source:
        digest, size = _copy(source, sink, limits)
    return digest, size, sink.getvalue() if capture else b""


def _validate(stream, limits: Limits, formats: dict[str, str], require_complete: bool) -> dict:
    _directory(stream, limits)
    with zipfile.ZipFile(stream) as archive:
        infos = archive.infolist()
        limits.check("max_members", len(infos))
        seen, total = set(), 0
        for info in infos:
            name = info.filename
            _require(_safe_member(name) and info.orig_filename == name and name.casefold() not in seen,
                     "unsafe_or_duplicate_member")
            seen.add(name.casefold())
            mode = info.external_attr >> 16
            _require(stat.S_IFMT(mode) in (0, stat.S_IFREG) and not info.is_dir() and
                     not info.flag_bits & 1 and info.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED),
                     "unsupported_member_type")
            limits.check("max_member_bytes", info.file_size)
            total += info.file_size
            limits.check("max_total_bytes", total)
            if limits.max_ratio is not None:
                _require(info.file_size <= max(1, info.compress_size) * limits.max_ratio, "max_ratio")
        _require(INDEX in archive.namelist(), "manifest_missing")
        limits.check("max_manifest_bytes", archive.getinfo(INDEX).file_size)
        _, _, index = _read(archive, INDEX, limits, capture=True)
        manifest = _parse(index)
        _require(isinstance(manifest, dict) and set(manifest) ==
                 {"schema", "conversation_id", "complete", "exports", "originals", "attachments"} and
                 manifest["schema"] == SCHEMA and type(manifest["complete"]) is bool and
                 isinstance(manifest["exports"], list) and isinstance(manifest["originals"], list),
                 "manifest_shape")
        expected, original_hashes, export_formats = {INDEX}, set(), set()
        snapshot = None
        for group, role in (("exports", "derived"), ("originals", "original")):
            for entry in manifest[group]:
                _require(isinstance(entry, dict) and set(entry) ==
                         ({"member", "sha256", "size", "role", "genesis", "format"} if role == "derived" else
                          {"member", "sha256", "size", "role", "genesis"}), "index_entry_shape")
                name, digest, size = entry["member"], entry["sha256"], entry["size"]
                _require(isinstance(name, str) and isinstance(digest, str) and _HASH.fullmatch(digest) is not None
                         and type(size) is int and size >= 0 and entry["role"] == role, "index_entry")
                _require(name not in expected and name in archive.namelist(), "indexed_member_missing_or_duplicate")
                expected.add(name)
                _require(archive.getinfo(name).file_size == size, "member_size_mismatch")
                if role == "original":
                    _require(name == "originals/" + digest and entry["genesis"] ==
                             {"method": "identity", "source": "sha256:" + digest}, "original_provenance")
                    original_hashes.add(digest)
                else:
                    fmt = entry["format"]
                    _require(isinstance(fmt, str) and fmt in formats and fmt not in export_formats and
                             name == formats[fmt] and entry["genesis"] ==
                             {"method": "loom.ConversationExporter", "conversation_id": manifest["conversation_id"]},
                             "export_provenance")
                    export_formats.add(fmt)
                actual, actual_size, data = _read(archive, name, limits, capture=name == SNAPSHOT)
                _require(actual == digest and actual_size == size, "member_hash_mismatch")
                if name == SNAPSHOT:
                    snapshot = data
        _require(expected == set(archive.namelist()), "unindexed_member")
        _require(snapshot is not None and "json" in export_formats, "snapshot_missing")
        cid, pointers = _snapshot(snapshot)
        _require(cid == manifest["conversation_id"], "conversation_id_mismatch")
        hashes = _bindings(manifest["attachments"], pointers, not require_complete)
        _require(hashes == original_hashes, "original_inventory_mismatch")
        complete = all(b["availability"] == "included" for b in manifest["attachments"])
        _require(manifest["complete"] == complete and (complete or not require_complete), "incomplete_bundle")
        return {"manifest": manifest, "manifest_sha256": _sha(index)}


def validate_bundle(path: Path, *, limits: Limits, formats: dict[str, str],
                    require_complete: bool = True) -> dict:
    """Integrity check only. Internal hashes are not authentication; pin the receipt SHA on restore."""
    try:
        with _regular(path) as stream:
            return _validate(stream, limits, formats, require_complete)
    except (zipfile.BadZipFile, EOFError, struct.error, NotImplementedError, RuntimeError) as exc:
        raise BundleError("invalid_zip") from exc


@dataclasses.dataclass(frozen=True)
class RestorePlan:
    archive: Path
    members: Path
    sha256: str
    manifest_sha256: str
    manifest: dict
    interpret_members: tuple[str, ...] = (SNAPSHOT,)

    def admission(self) -> dict:
        """JSON bridge receipt; caller maps these values to native ImportOptions.

        For an out-of-process preflight, bind the native copy to this hash/size.
        Prefer handing the native importer an outer caller-owned private copy.
        This descriptor does not itself execute or configure the native API.
        """
        return {"sha256": self.sha256, "size": self.archive.stat().st_size,
                "manifest_sha256": self.manifest_sha256, "manifest": self.manifest,
                "native_options": {"export_mode": "off", "record_provenance": True,
                    "expected_source_hash": self.sha256,
                    "expected_source_bytes": self.archive.stat().st_size,
                    "zip_interpret_members": list(self.interpret_members)}}


@contextlib.contextmanager
def stage_bundle(path: Path, *, staging_parent: Path, limits: Limits, formats: dict[str, str],
                 expected_sha256: str | None = None, require_complete: bool = True) -> Iterator[RestorePlan]:
    """Private, fresh staging; invalid input never reaches DB callbacks.

    Use a caller-owned trusted staging_parent. Paths exist only within this
    context. Never validate a pathname and then reopen the caller's original.
    """
    _require(expected_sha256 is None or isinstance(expected_sha256, str) and
             _HASH.fullmatch(expected_sha256) is not None, "expected_hash")
    with tempfile.TemporaryDirectory(prefix="loom-bundle-", dir=staging_parent) as temporary:
        root = Path(temporary)
        snapshot = root / "restore.zip"
        with _regular(path) as source, snapshot.open("xb") as target:
            limits.check("max_archive_bytes", os.fstat(source.fileno()).st_size)
            digest, _ = _copy(source, target, limits, "max_archive_bytes")
        _require(expected_sha256 is None or expected_sha256 == digest, "archive_hash_mismatch")
        result = validate_bundle(snapshot, limits=limits, formats=formats, require_complete=require_complete)
        members = root / "members"
        members.mkdir(mode=0o700)
        with zipfile.ZipFile(snapshot) as archive:
            for info in archive.infolist():
                dest = members / info.filename  # full allowlist/path validation already completed
                dest.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
                with archive.open(info) as source, dest.open("xb") as target:
                    actual, size = _copy(source, target, limits)
                # Recheck the extraction against the validated index as well.
                if info.filename == INDEX:
                    _require(actual == result["manifest_sha256"], "staging_manifest_mismatch")
                else:
                    entries = result["manifest"]["exports"] + result["manifest"]["originals"]
                    entry = next(e for e in entries if e["member"] == info.filename)
                    _require(actual == entry["sha256"] and size == entry["size"], "staging_member_mismatch")
                dest.chmod(0o400)
        snapshot.chmod(0o400)
        yield RestorePlan(snapshot, members, digest, result["manifest_sha256"], result["manifest"])


def restore_bundle(path: Path, import_zip: Callable[[RestorePlan], object], **options) -> object:
    """Delegate exactly once after staging succeeds; propagate importer failures.

    The native bridge MUST enforce interpret_members, record provenance and
    rebind managed attachments by source pointers. This is not a DB transaction
    manager: native atomicity/rollback belongs to that existing importer.
    """
    with stage_bundle(path, **options) as plan:
        return import_zip(plan)


@dataclasses.dataclass(frozen=True)
class RestoredTarget:
    """Native bridge readbacks, never a second persistence implementation.

    message_ids maps SOURCE message JSON pointers to actual restored IDs.
    read_attachments must read the persisted target row; resolve_hash uses the
    target's managed-attachment registry. Neither may echo the input manifest.
    """
    blob_root: Path
    message_ids: dict[str, str]
    read_attachments: Callable[[str], list]
    resolve_hash: Callable[[object], str]


def verify_restored(plan: RestorePlan, target: RestoredTarget, *, limits: Limits) -> dict:
    """Independently hash target originals and inspect persisted attachment slots.

    Call while the plan is live, on an isolated target or within the native
    caller's transaction. Failure is never reported as successful restore.
    The caller owns rollback; this verifier performs no database writes.
    """
    _require(isinstance(target.message_ids, dict) and
             all(isinstance(k, str) and isinstance(v, str) and v for k, v in target.message_ids.items()) and
             len(set(target.message_ids.values())) == len(target.message_ids), "restore_message_mapping")
    verified_originals = 0
    for original in plan.manifest["originals"]:
        with _blob(target.blob_root, original["sha256"]) as source:
            digest, size = _copy(source, _Discard(), limits)
        _require(digest == original["sha256"] and size == original["size"], "restored_original_mismatch")
        verified_originals += 1
    grouped = {}
    for binding in plan.manifest["attachments"]:
        message_pointer, slot = binding["pointer"].rsplit("/attachments/", 1)
        grouped.setdefault(message_pointer, []).append((int(slot), binding))
    verified_slots, unavailable = 0, 0
    for pointer, slots in grouped.items():
        _require(pointer in target.message_ids, "restored_message_missing")
        actual = target.read_attachments(target.message_ids[pointer])
        _require(isinstance(actual, list) and len(actual) == len(slots), "restored_attachment_slots")
        for slot, binding in slots:
            if binding["availability"] != "included":
                # Partial restores retain a positional gap; an unresolved path
                # must not masquerade as a restored managed attachment.
                _require(actual[slot] is None, "unavailable_attachment_bound")
                unavailable += 1
                continue
            _require(target.resolve_hash(actual[slot]) == binding["sha256"], "restored_attachment_mismatch")
            verified_slots += 1
    return {"archive_sha256": plan.sha256, "manifest_sha256": plan.manifest_sha256,
            "complete": plan.manifest["complete"] and unavailable == 0,
            "originals": {"verified": verified_originals, "total": len(plan.manifest["originals"])},
            "attachment_slots": {"verified": verified_slots, "total": len(plan.manifest["attachments"]),
                                 "unavailable": unavailable}}


def restore_verified_bundle(path: Path, import_zip: Callable[[RestorePlan], object],
                            inspect_target: Callable[[object], RestoredTarget], **options) -> tuple[object, dict]:
    """Existing ZIP import followed by target readback/hash checks; no retries."""
    with stage_bundle(path, **options) as plan:
        reference = dataclasses.replace(plan, manifest=_parse(_json_bytes(plan.manifest)))
        result = import_zip(plan)
        report = verify_restored(reference, inspect_target(result), limits=options["limits"])
        return result, report


def main(argv: list[str] | None = None) -> int:
    """Small stdlib bridge. Invoke with an absolute trusted script/interpreter, -I -B.

    A pack request names exporter output files explicitly; attachment paths are
    never read from it or from conversation JSON. Native launch/DB hooks remain
    the integrator's responsibility; no PATH lookup, subprocess or networking.
    """
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy", type=Path, help="trusted limit/format preset override")
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("pack")
    pack.add_argument("request", type=Path)
    pack.add_argument("destination", type=Path)
    pack.add_argument("--allow-incomplete", action="store_true")
    verify = commands.add_parser("verify")
    verify.add_argument("archive", type=Path)
    verify.add_argument("--staging-parent", type=Path, required=True)
    verify.add_argument("--expected-sha256")
    verify.add_argument("--allow-incomplete", action="store_true")
    args = parser.parse_args(argv)
    try:
        limits, formats = preset(args.policy)
        if args.command == "pack":
            with _regular(args.request) as source:
                request_data = io.BytesIO()
                _copy(source, request_data, limits, "max_manifest_bytes")
            request = _parse(request_data.getvalue())
            _require(isinstance(request, dict) and set(request) ==
                     {"exports", "attachments", "blob_root"} and isinstance(request["exports"], dict)
                     and "json" in request["exports"] and set(request["exports"]) <= set(formats)
                     and all(isinstance(p, str) for p in request["exports"].values())
                     and isinstance(request["blob_root"], str), "pack_request_shape")
            exports = {}
            # Relative request paths are anchored to the request, never HOME/PATH.
            for fmt, filename in request["exports"].items():
                with _regular(args.request.parent / filename) as source:
                    data = io.BytesIO()
                    _copy(source, data, limits)
                    exports[fmt] = data.getvalue()
                    limits.check("max_total_bytes", sum(len(value) for value in exports.values()))
            result = create_bundle(args.destination, exports, request["attachments"],
                                   args.request.parent / request["blob_root"], limits=limits,
                                   formats=formats, allow_incomplete=args.allow_incomplete)
        else:
            with stage_bundle(args.archive, staging_parent=args.staging_parent, limits=limits,
                              formats=formats, expected_sha256=args.expected_sha256,
                              require_complete=not args.allow_incomplete) as plan:
                result = plan.admission()
        print(_json_bytes(result).decode("utf-8"))
        return 0
    except (BundleError, OSError, zipfile.BadZipFile, zipfile.LargeZipFile) as exc:
        code = str(exc) if isinstance(exc, BundleError) else "bundle_io_error"
        print(_json_bytes({"error": code}).decode("utf-8"))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
