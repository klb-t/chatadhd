"""Mechanism tests only: fixtures are never dispatched to a model."""
from __future__ import annotations

import copy
import io
import json
from pathlib import Path
import stat
import tempfile
import unittest
import warnings
import zipfile

import intake as tool


LAYOUT = {"schema": "loom.real_source_capsule_layout/1",
          "source_directory": "source", "previous_freeze_directory": "prior"}


def native(mid, role, timestamp):
    return {"id": mid, "author": {"role": role}, "create_time": timestamp,
            "content": {"parts": ["PRIVATE_CONTENT_SENTINEL_" + mid]}}


def case_fixture():
    a, b, c = native("native-user", "user", 10), native("native-first", "assistant", 11), native("native-alternate", "assistant", 11)
    mapping = {"root": {"parent": None, "message": None},
               "a": {"parent": "root", "message": a},
               "b": {"parent": "a", "message": b},
               "c": {"parent": "a", "message": c}}
    original = {"mapping": mapping, "current_node": "b", "title": "PRIVATE_TITLE_SENTINEL"}
    wrapped = {"case_id": "case-1", "native_mapping_including_saved_branches": copy.deepcopy(mapping),
               "messages": [{"turn_id": "turn-0", "role": "assistant", "native_message": b},
                            {"turn_id": "turn-1", "role": "user", "native_message": a},
                            {"turn_id": "turn-2", "role": "assistant", "native_message": c}]}
    return copy.deepcopy(original), copy.deepcopy(wrapped)


def capsule_fixture():
    original, wrapped = case_fixture()
    files = {"source/configuration.json": b"{}\n",
             "source/case-1/original-conversation.json": tool.canonical(original) + b"\n",
             "source/case-1/messages-full.json": tool.canonical(wrapped) + b"\n",
             "prior/prepared/manifest.json": b'{"operations":[]}\n'}
    selection = {"configuration_sha256": tool.sha256(files["source/configuration.json"]),
                 "selected": [{"case_id": "case-1", "messages": 3,
                               "full_original_conversation_canonical_sha256": tool.sha256(tool.canonical(original)),
                               "full_original_conversation_canonical_utf8_bytes": len(tool.canonical(original))}]}
    files["source/selection-manifest.json"] = tool.canonical(selection) + b"\n"
    freeze = {"source_selection_sha256": tool.sha256(files["source/selection-manifest.json"]),
              "operations": 0, "files": {"prepared/manifest.json": tool.sha256(files["prior/prepared/manifest.json"])},
              "redactions": [{"case_id": "case-1", "source_messages_sha256": tool.sha256(files["source/case-1/messages-full.json"])}]}
    files["prior/FREEZE.json"] = tool.canonical(freeze) + b"\n"
    return files


class ProjectionTests(unittest.TestCase):
    def setUp(self):
        self.original, self.wrapped = case_fixture()

    def sync_mapping(self):
        self.wrapped["native_mapping_including_saved_branches"] = copy.deepcopy(self.original["mapping"])

    def test_reverse_array_is_not_chronology(self):
        view, counts = tool.project_case(self.original, self.wrapped)
        self.assertEqual(view["source_array_turn_ids"], ["turn-0", "turn-1", "turn-2"])
        self.assertEqual(view["topological_turn_ids"][0], "turn-1")
        self.assertEqual(counts["array_parent_inversions"], 1)

    def test_all_saved_branches_survive(self):
        view, counts = tool.project_case(self.original, self.wrapped)
        self.assertEqual(counts["saved_leaves"], 2)
        self.assertEqual(counts["messages"], 3)
        self.assertEqual(len(view["current_path_node_ids"]), 3)
        self.assertEqual(len(view["nodes"]), 4)

    def test_source_objects_and_roles_do_not_change(self):
        before = tool.canonical([self.original, self.wrapped])
        view, _ = tool.project_case(self.original, self.wrapped)
        self.assertEqual(before, tool.canonical([self.original, self.wrapped]))
        returned = {n["turn_id"]: n for n in view["nodes"] if n["turn_id"] is not None}
        for row in self.wrapped["messages"]:
            self.assertEqual(returned[row["turn_id"]]["native_message"], row["native_message"])
            self.assertEqual(returned[row["turn_id"]]["role"], row["role"])

    def test_speaker_mutation_is_rejected(self):
        self.wrapped["messages"][0]["role"] = "user"
        with self.assertRaisesRegex(tool.IntakeError, "speaker_role_mismatch"):
            tool.project_case(self.original, self.wrapped)

    def test_message_mutation_is_rejected(self):
        self.wrapped["messages"][0]["native_message"]["content"] = {"parts": ["modified"]}
        with self.assertRaisesRegex(tool.IntakeError, "native_message_mismatch"):
            tool.project_case(self.original, self.wrapped)

    def test_dropped_branch_message_is_rejected(self):
        self.wrapped["messages"].pop()
        with self.assertRaisesRegex(tool.IntakeError, "native_message_coverage_gap"):
            tool.project_case(self.original, self.wrapped)

    def test_duplicate_message_is_rejected(self):
        self.wrapped["messages"].append(copy.deepcopy(self.wrapped["messages"][0]))
        with self.assertRaisesRegex(tool.IntakeError, "duplicate_projected_message"):
            tool.project_case(self.original, self.wrapped)

    def test_duplicate_turn_is_rejected(self):
        self.wrapped["messages"][1]["turn_id"] = "turn-0"
        with self.assertRaisesRegex(tool.IntakeError, "invalid_or_duplicate_turn_id"):
            tool.project_case(self.original, self.wrapped)

    def test_mapping_mutation_is_rejected(self):
        self.wrapped["native_mapping_including_saved_branches"]["b"]["parent"] = "c"
        with self.assertRaisesRegex(tool.IntakeError, "native_mapping_mismatch"):
            tool.project_case(self.original, self.wrapped)

    def test_cycle_is_rejected(self):
        self.original["mapping"]["a"]["parent"] = "b"
        self.sync_mapping()
        with self.assertRaisesRegex(tool.IntakeError, "native_parent_cycle"):
            tool.project_case(self.original, self.wrapped)

    def test_missing_parent_is_rejected(self):
        self.original["mapping"]["a"]["parent"] = "absent"
        self.sync_mapping()
        with self.assertRaisesRegex(tool.IntakeError, "missing_native_parent"):
            tool.project_case(self.original, self.wrapped)

    def test_missing_current_node_is_rejected(self):
        self.original["current_node"] = "absent"
        with self.assertRaisesRegex(tool.IntakeError, "missing_current_node"):
            tool.project_case(self.original, self.wrapped)

    def test_absent_timestamp_is_not_invented(self):
        self.original["mapping"]["a"]["message"].pop("create_time")
        self.wrapped["messages"][1]["native_message"].pop("create_time")
        self.sync_mapping()
        view, counts = tool.project_case(self.original, self.wrapped)
        self.assertEqual(counts["messages_without_create_time"], 1)
        self.assertIsNone(next(n for n in view["nodes"] if n["turn_id"] == "turn-1")["source_create_time"])

    def test_clock_conflict_does_not_override_ancestry(self):
        self.original["mapping"]["b"]["message"]["create_time"] = 9
        self.wrapped["messages"][0]["native_message"]["create_time"] = 9
        self.sync_mapping()
        view, counts = tool.project_case(self.original, self.wrapped)
        self.assertEqual(counts["parent_clock_conflicts"], 1)
        self.assertLess(view["topological_turn_ids"].index("turn-1"), view["topological_turn_ids"].index("turn-0"))

    def test_unknown_native_fields_remain(self):
        for item in (self.original["mapping"]["a"]["message"], self.wrapped["messages"][1]["native_message"]):
            item["future_extension"] = {"opaque": [None, 4]}
        self.sync_mapping()
        view, _ = tool.project_case(self.original, self.wrapped)
        self.assertEqual(next(n for n in view["nodes"] if n["turn_id"] == "turn-1")["native_message"]["future_extension"], {"opaque": [None, 4]})


    def test_invalid_case_container_is_rejected(self):
        with self.assertRaisesRegex(tool.IntakeError, "invalid_case_container"):
            tool.project_case([], self.wrapped)

    def test_invalid_native_author_is_rejected(self):
        self.original["mapping"]["a"]["message"]["author"] = None
        self.sync_mapping()
        with self.assertRaisesRegex(tool.IntakeError, "invalid_native_author"):
            tool.project_case(self.original, self.wrapped)

    def test_declared_children_match_parent_edges(self):
        self.original["mapping"]["a"]["children"] = ["c", "b"]
        self.sync_mapping()
        _, counts = tool.project_case(self.original, self.wrapped)
        self.assertEqual(counts["parent_edges"], 3)

    def test_contradictory_child_list_is_rejected(self):
        self.original["mapping"]["a"]["children"] = ["b"]
        self.sync_mapping()
        with self.assertRaisesRegex(tool.IntakeError, "native_parent_children_mismatch"):
            tool.project_case(self.original, self.wrapped)

    def test_duplicate_declared_child_is_rejected(self):
        self.original["mapping"]["a"]["children"] = ["b", "c", "c"]
        self.sync_mapping()
        with self.assertRaisesRegex(tool.IntakeError, "native_parent_children_mismatch"):
            tool.project_case(self.original, self.wrapped)

    def test_malformed_current_node_is_rejected(self):
        self.original["current_node"] = ["b"]
        with self.assertRaisesRegex(tool.IntakeError, "missing_current_node"):
            tool.project_case(self.original, self.wrapped)


class IntegrityTests(unittest.TestCase):
    def test_full_capsule_and_public_allowlist(self):
        files = capsule_fixture()
        before = copy.deepcopy(files)
        private, public = tool.verify_and_project(files, "a" * 64, LAYOUT)
        self.assertEqual(files, before)
        self.assertEqual(public["private_projection_sha256"], tool.sha256(tool.canonical(private)))
        for forbidden in ("PRIVATE_CONTENT", "PRIVATE_TITLE", "native-user", "current_node", "source_pointer"):
            self.assertNotIn(forbidden, tool.canonical(public).decode())
        self.assertFalse(public["dispatch_ready"])
        self.assertIsNone(public["quality"])
        self.assertEqual(public["new_paid_calls"], 0)

    def test_original_hash_is_canonical_not_pretty_bytes(self):
        files = capsule_fixture()
        key = "source/case-1/original-conversation.json"
        files[key] = json.dumps(tool.decode(files[key]), indent=2).encode() + b"\n"
        tool.verify_and_project(files, "a" * 64, LAYOUT)

    def test_previous_request_bytes_are_frozen(self):
        files = capsule_fixture()
        files["prior/prepared/manifest.json"] += b" "
        with self.assertRaisesRegex(tool.IntakeError, "previous_freeze_member_hash_mismatch"):
            tool.verify_and_project(files, "a" * 64, LAYOUT)

    def test_projected_source_bytes_are_frozen(self):
        files = capsule_fixture()
        files["source/case-1/messages-full.json"] += b" "
        with self.assertRaisesRegex(tool.IntakeError, "previous_source_messages_hash_mismatch"):
            tool.verify_and_project(files, "a" * 64, LAYOUT)

    def test_native_content_hash_is_frozen(self):
        files = capsule_fixture()
        key = "source/case-1/original-conversation.json"
        item = tool.decode(files[key]); item["title"] = "changed"
        files[key] = tool.canonical(item)
        with self.assertRaisesRegex(tool.IntakeError, "original_canonical_hash_mismatch"):
            tool.verify_and_project(files, "a" * 64, LAYOUT)

    def test_layout_schema_is_checked(self):
        with self.assertRaisesRegex(tool.IntakeError, "unsupported_layout_schema"):
            tool.verify_and_project(capsule_fixture(), "a" * 64, {**LAYOUT, "schema": "other"})

    def test_duplicate_json_keys_are_rejected(self):
        with self.assertRaisesRegex(tool.IntakeError, "duplicate_json_key"):
            tool.decode(b'{"x":1,"x":2}')

    def test_nonfinite_json_numbers_are_rejected(self):
        for value in (b"NaN", b"Infinity", b"-Infinity", b"1e1000"):
            with self.subTest(value=value), self.assertRaises(tool.IntakeError):
                tool.decode(value)

    def test_unsafe_paths_are_rejected(self):
        for name in ("../x", "/x", "a/../x", "a//x", "a\\x", "C:/x", "./x"):
            with self.subTest(name=name), self.assertRaises(tool.IntakeError):
                tool.safe_member(name)

    def zip_check(self, entries, error=None, bad_hash=False):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "capsule.zip"
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", UserWarning)
                with zipfile.ZipFile(path, "w") as archive:
                    for key, data in entries:
                        archive.writestr(key, data)
            digest = "0" * 64 if bad_hash else tool.sha256(path.read_bytes())
            if error:
                with self.assertRaisesRegex(tool.IntakeError, error):
                    tool.load_capsule(path, digest)
            else:
                files, _ = tool.load_capsule(path, digest)
                self.assertEqual(files, {"safe.json": b"{}"})

    def test_zip_load(self):
        self.zip_check([("safe.json", b"{}")])

    def test_zip_hash_mismatch(self):
        self.zip_check([("safe.json", b"{}")], "capsule_hash_mismatch", bad_hash=True)

    def test_zip_duplicate(self):
        self.zip_check([("safe.json", b"{}"), ("safe.json", b"{}")], "duplicate_zip_member")

    def test_zip_traversal(self):
        self.zip_check([("../secret", b"{}")], "unsafe_member_path")

    def test_zip_symlink(self):
        info = zipfile.ZipInfo("link"); info.create_system = 3
        info.external_attr = (stat.S_IFLNK | 0o777) << 16
        self.zip_check([(info, b"outside")], "zip_symlink")

    def test_private_output_cannot_enter_git(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp); (repo / ".git").mkdir()
            with self.assertRaisesRegex(tool.IntakeError, "private_output_inside_git"):
                tool.write_new(repo / "sub" / "secret.json", {"private": 1}, True)
            self.assertFalse((repo / "sub").exists())

    def test_private_output_never_overwrites(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "data" / "secret.json"
            tool.write_new(path, {"private": 1}, True)
            original = path.read_bytes()
            with self.assertRaises(FileExistsError):
                tool.write_new(path, {"private": 2}, True)
            self.assertEqual(path.read_bytes(), original)
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)


if __name__ == "__main__":
    unittest.main()
