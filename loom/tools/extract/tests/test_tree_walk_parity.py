"""The lossless tree walk (ChatGPT mapping / Claude chat_messages) against the SAME oracle as the
C++ importer: loom/tests/fixtures/exports/EXPECTED.json.  Conversations, messages, fork points and
current-path messages must be equal.  Attachment references are >= the oracle's count: X-Port also
registers content-part pointers without a metadata entry (never dropped, reported as missing or
resolved); branch (leaf) counts may exceed the oracle's when a mapping holds message-less leaves —
both differences are listed in the spec's decisions table."""
from _support import EXPECTED_EXPORTS, EXPORTS, ExtractTest


class TreeWalkParityTest(ExtractTest):
    def test_counts_match_the_cpp_oracle(self):
        for name, exp in EXPECTED_EXPORTS.items():
            with self.subTest(name):
                eng, units = self.units(EXPORTS / name)
                convs = [u for u in units if u.kind == "conversation"]
                c = exp["counts"]
                self.assertEqual(len(convs), c["conversation"])
                self.assertEqual(sum(len(u.messages) for u in convs), c["message"])
                self.assertEqual(sum(len(u.tree.get("forks") or []) for u in convs), c["fork_points"])
                self.assertEqual(sum(1 for u in convs for m in u.messages if m.on_current_path), c["current_path_messages"])
                self.assertGreaterEqual(sum(len(u.attachments) for u in convs), c["attachment"])
                self.assertGreaterEqual(sum(u.leaf_count() for u in convs), c["branch"])
                self.assertEqual(exp["provider"], convs[0].service if convs else exp["provider"])

    def test_forks_have_alternatives_and_off_path_messages_are_kept(self):
        eng, units = self.units(EXPORTS / "openai_2026_sharded.zip")
        forked = [u for u in units if u.kind == "conversation" and u.tree.get("forks")]
        self.assertTrue(forked)
        for u in forked:
            for f in u.tree["forks"]:
                alts = f.get("alternatives", 0)
                self.assertGreaterEqual(len(alts) if isinstance(alts, list) else int(alts), 2)
            self.assertTrue(any(not m.on_current_path for m in u.messages), "edited/regenerated branches stay")
            self.assertGreater(len(u.branches()), 1)

    def test_every_message_keeps_a_raw_pointer(self):
        for name in ("openai_2026_sharded.zip", "anthropic_2026_full.zip"):
            eng, units = self.units(EXPORTS / name)
            for u in units:
                if u.kind != "conversation":
                    continue
                for m in u.messages:
                    self.assertTrue(m.raw_pointer, (name, u.title, m.id))
