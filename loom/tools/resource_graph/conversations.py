"""Optional bridge to the EXISTING lossless native conversation importer.

This is explicitly an eager, temporary-materialization compatibility adapter.
The general resource-reference/syntax path does not invoke it. No provider
message mapper, alternate database, or guessed source history lives here.
"""
from __future__ import annotations

from copy import deepcopy
import ctypes
import hashlib
import json
from pathlib import Path
import tempfile

from loom.tools.coordination.graph_store import NativeGraphStore
from .access import Access, sanitize_locator


class ConversationMappingError(ValueError):
    pass


def _report_view(report):
    if not isinstance(report, dict):
        return report
    result = deepcopy(report)
    for member in result.get("members", []):
        # These fields exist only when the caller requests provenance writes.
        for key in ("blob_hash", "locator", "source_id"):
            member.pop(key, None)
    if "parts" in result:
        for part in result["parts"]:
            part.pop("source_id", None)
            if "report" in part:
                part["report"] = _report_view(part["report"])
    return result


def semantic_view(snapshot: dict) -> dict:
    """Compare recognized content across native DBs using source identities.

Only generated DB IDs and readable native attachment paths are projected away.
Full native rows remain in ``snapshot['native']`` and transformation losses are
declared in the view; raw provider metadata is kept unchanged.
"""
    conversations = []
    for entry in snapshot["native"]["conversations"]:
        conv, rows = entry["conversation"], entry["messages"]
        keys = {row["id"]: row["metadata"].get("export", {}).get("key") for row in rows}
        groups = {}
        for row in rows:
            group = row.get("version_group_id")
            if group:
                groups.setdefault(group, []).append(keys[row["id"]])
        messages = []
        for row in rows:
            parent = row.get("parent_id")
            messages.append({
                "key": keys[row["id"]], "parent_key": keys.get(parent),
                "unresolved_native_parent": parent is not None and parent not in keys,
                "role": row["role"], "text": row["text"], "model": row["model"],
                "status": row["status"], "weight": row["weight"],
                "version_group_keys": sorted(groups.get(row.get("version_group_id"), [])),
                "version_num": row["version_num"], "created": row["created"],
                "metadata": deepcopy(row["metadata"]),
                "available_attachment_count": len(row["attachments"]),
            })
        conversations.append({"title": conv["title"], "source": conv["source"],
                              "metadata": deepcopy(conv["metadata"]), "messages": messages})
    return {"conversations": conversations,
            "export_report": _report_view(snapshot["native"]["result"].get("export_report")),
            "representation_changes": ["generated native IDs replaced with source keys",
                                       "native attachment paths omitted; source member/blob references retained in metadata",
                                       "native conversation/import timestamps excluded; source timestamps retained",
                                       "optional report member source/blob/locator bookkeeping excluded; original report retained"]}


class NativeConversationMapper:
    """Reuse public Loom import/query ABI; workers disabled by NativeGraphStore."""
    def __init__(self, library_path, *, access=None):
        self.library_path = Path(library_path).resolve()
        self.access = access or Access()
        if not self.library_path.is_file():
            raise FileNotFoundError(self.library_path)

    @staticmethod
    def _call(store, function, arguments):
        pointer = function(store.context, *arguments)
        if not pointer:
            raise ConversationMappingError("native_mapper_null_response")
        try:
            result = json.loads(ctypes.string_at(pointer).decode("utf-8"))
        finally:
            store.library.loom_free_string(pointer)
        if isinstance(result, dict) and "error" in result:
            # Native diagnostics may contain original text; don't propagate it.
            raise ConversationMappingError("native_mapper_error:" + result["error"]["code"])
        return result

    def import_path(self, path, *, data_directory, source_identity: str,
                    record_provenance: bool = True) -> dict:
        """Explicit durable full import for comparison or owner-selected import."""
        if not isinstance(source_identity, str) or not source_identity:
            raise ValueError("logical_source_identity_required")
        path = Path(path).resolve()
        acquired = self.access.read_transport(str(path))
        if acquired["status"] not in {"available", "empty"}:
            raise ConversationMappingError("native_mapper_source:" + acquired["status"])
        raw = acquired["data"]
        before = hashlib.sha256(raw).hexdigest()
        preflight = None
        if path.suffix.lower() == ".zip" or raw.startswith(b"PK"):
            preflight = self.access.validate_archive(raw, recursive=True)
            if preflight["status"] not in {"available", "empty"}:
                raise ConversationMappingError("native_mapper_archive:" + preflight["status"])
        # The public import ABI has no expected-source-hash argument. Hand it a
        # private immutable staging snapshot so it cannot reread a changed or
        # replaced original after archive preflight. This optional eager bridge
        # discloses the staging copy; the primary reference path never uses it.
        with tempfile.TemporaryDirectory(prefix="loom-resource-native-input-") as staging, \
                NativeGraphStore(self.library_path, data_directory) as store:
            staged = Path(staging) / path.name
            staged.write_bytes(raw)
            lib = store.library
            lib.loom_import_file_ex.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                                               ctypes.c_void_p, ctypes.c_void_p]
            lib.loom_import_file_ex.restype = ctypes.c_void_p
            lib.loom_get_messages_ex.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
            lib.loom_get_messages_ex.restype = ctypes.c_void_p
            result = self._call(store, lib.loom_import_file_ex,
                                [str(staged).encode(), json.dumps({"export_mode": "on", "force": True,
                                 "record_provenance": record_provenance}).encode(), None, None])
            if result.get("export_report") is None:
                raise ConversationMappingError("native_mapper_no_lossless_provider_mapping")
            conversations = []
            for conversation in result["conversations"]:
                messages = self._call(store, lib.loom_get_messages_ex,
                                      [conversation["id"].encode(), 1])
                conversations.append({"conversation": conversation, "messages": messages})
        after = hashlib.sha256(path.read_bytes()).hexdigest()
        if after != before:
            raise ConversationMappingError("source_changed_during_native_import")
        snapshot = {
            "schema": "loom.resource_graph.native_conversations/1",
            "source": {"logical_id": source_identity, "content_sha256": before,
                       "locator": str(path)},
            "mapping": {"api": "loom_import_file_ex", "export_mode": "on",
                        "library_sha256": hashlib.sha256(self.library_path.read_bytes()).hexdigest(),
                        "ownership": "existing_native_importer"},
            "archive_preflight": preflight,
            "execution": {"mode": "native_durable_import", "materialized_native_rows": True,
                          "store_retained": True, "source_copy_requested": record_provenance,
                          "private_staging_copy": True},
            "native": {"result": result, "conversations": conversations},
        }
        snapshot["semantic_view"] = semantic_view(snapshot)
        return snapshot

    def map_bytes(self, data: bytes, *, filename: str, source_identity: str,
                  source_locator=None) -> dict:
        """Explicit compatibility mapping of already acquired source bytes.

This creates/removes a temporary native store. It is never represented as a
pure lazy/reference projection and does not retain an attachment's temp path.
Attachments remain reachable using the source member references in metadata.
"""
        if not isinstance(data, bytes):
            raise TypeError("native_mapper_requires_bytes")
        if Path(filename).name != filename or filename in ("", ".", ".."):
            raise ValueError("native_mapper_requires_basename")
        with tempfile.TemporaryDirectory(prefix="loom-resource-conversation-") as folder:
            path = Path(folder) / filename
            path.write_bytes(data)
            result = self.import_path(path, data_directory=Path(folder) / "native",
                                      source_identity=source_identity, record_provenance=False)
        result["source"]["locator"] = None if source_locator is None else sanitize_locator(source_locator)
        result["execution"] = {"mode": "native_temporary_materialization", "materialized_native_rows": True,
                               "store_retained": False, "source_copy_requested": False,
                               "native_attachment_paths_expired": True, "private_staging_copy": True}
        return result
