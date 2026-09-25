"""engine.db.Database + engine.graph_engine.GraphEngine (regex-only, i.e.
semantic_llm=None) + engine.graph_memory.GraphMemorySelector <-> the Loom
equivalents, on fresh DBs, for the same message sequence.

Node/link identities are random ids, so both sides are normalised to
(kind,label) / message-index / conv-title keys before comparing - the
same normalisation policy the compat harness's Normalizer uses elsewhere in
spirit, applied here directly since the comparison is structural, not a
byte-for-byte DB dump.
"""
import unittest

from compat_common import CompatTestCase, tool


def node_key(kind, label):
    return f"NODE:{kind}:{label}"


def msg_key(i):
    return f"MSG:{i}"


def conv_key(title):
    return f"CONV:{title}"


def run_python(db_path, spec):
    from engine.db import Database
    from engine.events import bus, MSG_CREATED
    from engine.graph_engine import GraphEngine
    from engine.graph_memory import GraphMemorySelector

    class _Cfg:
        def get(self, key, default=None):
            return default

    db = Database(db_path)
    ge = GraphEngine(db, semantic_llm=None)
    gm = GraphMemorySelector(db, _Cfg())
    try:
        conv_title_to_id = {}
        conv_id_to_key = {}
        msg_id_to_key = {}
        for i, m in enumerate(spec["messages"]):
            title = m.get("conv", "default")
            if title not in conv_title_to_id:
                conv = db.create_conv(title)
                conv_title_to_id[title] = conv["id"]
                conv_id_to_key[conv["id"]] = conv_key(title)
            cid = conv_title_to_id[title]
            mid = db.create_msg(cid, m["text"], m.get("role", "user"))
            msg_id_to_key[mid] = msg_key(i)
            bus.emit(MSG_CREATED, {"id": mid, "text": m["text"], "conv_id": cid, "role": m.get("role", "user")})

        nodes = db.list_nodes(limit=1_000_000)
        node_id_to_key = {n["id"]: node_key(n["kind"], n["label"]) for n in nodes}

        def resolve(nid):
            if nid in node_id_to_key:
                return node_id_to_key[nid]
            if nid in msg_id_to_key:
                return msg_id_to_key[nid]
            if nid in conv_id_to_key:
                return conv_id_to_key[nid]
            return f"UNKNOWN:{nid}"

        links = db.get_links()
        node_rows = sorted([[n["kind"], n["label"]] for n in nodes], key=repr)
        link_rows = sorted(
            [[resolve(link["src"]), resolve(link["dst"]), link["link_type"], round(link["weight"], 6)]
             for link in links],
            key=repr,
        )

        contexts = []
        for q in spec.get("queries", []):
            exclude_conv = conv_title_to_id.get(q["exclude_conv"]) if q.get("exclude_conv") else None
            contexts.append(gm.select_context(q["query"], depth=q.get("depth"), current_conv_id=exclude_conv))

        return {"nodes": node_rows, "links": link_rows, "contexts": contexts}
    finally:
        ge.stop()


class GraphCompat(CompatTestCase):
    def _run(self, spec):
        py_dir = self.tmp.path / "py"
        cpp_dir = self.tmp.path / "cpp"
        py_dir.mkdir(parents=True)
        cpp_dir.mkdir(parents=True)

        py = run_python(py_dir / "g.db", spec)
        cpp = tool("graph-run", cpp_dir, self.tmp.file_json("spec.json", spec))

        self.assertEqual(py["nodes"], cpp["nodes"], "node set differs")
        self.assertEqual(py["links"], cpp["links"], "link set differs")
        self.assertEqual(py["contexts"], cpp["contexts"], "select_context output differs")

    def test_entities_topics_relations_across_conversations(self):
        spec = {
            "messages": [
                {"conv": "Conversation A", "role": "user",
                 "text": "please email me at alice@example.com about the deploy pipeline and API design"},
                {"conv": "Conversation A", "role": "assistant",
                 "text": "sure, the deploy pipeline depends on the container registry being available"},
                {"conv": "Conversation B", "role": "user",
                 "text": "let's talk about the database and API integration for the new backend service"},
                {"conv": "Conversation B", "role": "assistant",
                 "text": "the backend service requires the database migration to run first"},
                {"conv": "Conversation A", "role": "user", "text": "hi"},  # too short, no-op
            ],
            "queries": [
                {"query": "tell me about the deploy pipeline and API", "exclude_conv": "Conversation B"},
                {"query": "database and API notes", "exclude_conv": "Conversation A"},
                {"query": "nothing related at all here", "exclude_conv": None},
            ],
        }
        self._run(spec)

    def test_reindex_like_sequence_single_conversation(self):
        spec = {
            "messages": [
                {"conv": "Solo", "role": "user", "text": "contact admin@example.com about the security audit"},
                {"conv": "Solo", "role": "assistant", "text": "noted, security review depends on the audit results"},
                {"conv": "Solo", "role": "user", "text": "also see http://example.com/docs for reference"},
            ],
            "queries": [{"query": "security audit", "exclude_conv": None}],
        }
        self._run(spec)

    def test_empty_and_short_messages_produce_no_graph_writes(self):
        spec = {
            "messages": [
                {"conv": "X", "role": "user", "text": "hi"},
                {"conv": "X", "role": "user", "text": "ok"},
            ],
            "queries": [{"query": "hi", "exclude_conv": None}],
        }
        self._run(spec)


if __name__ == "__main__":
    unittest.main(verbosity=2)
