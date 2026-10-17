"""Zero network by default (review F30/F39): with sentence-transformers installed, the vector level must not
download the embedding model.  A fake ``sentence_transformers`` module stands in (it is not installed here):
the Hugging Face libraries are put offline before the app's selector is imported, a model that is not cached
means TF-IDF/keywords with a visible note, a cached one runs offline, and a download needs the explicit preset
plus a yes."""
import os
import sys
import types

from _support import ExtractTest
from loom.tools.extract.vectors import EMBEDDING_MODEL, VectorIndex


class VectorsOfflineTest(ExtractTest):
    def setUp(self) -> None:
        super().setUp()
        os.environ["HF_HOME"] = str(self.tmp / "hf")
        os.environ["SENTENCE_TRANSFORMERS_HOME"] = str(self.tmp / "st")
        os.environ.pop("HF_HUB_CACHE", None)
        os.environ.pop("HF_HUB_OFFLINE", None)
        os.environ.pop("TRANSFORMERS_OFFLINE", None)
        self.seen: dict = {}
        seen = self.seen

        class FakeST:
            def __init__(self, name, **kw):
                seen["constructed"] = name
                seen["offline_at_load"] = os.environ.get("HF_HUB_OFFLINE")
                if os.environ.get("HF_HUB_OFFLINE") != "1":
                    raise AssertionError("the model would be downloaded")

            def encode(self, texts, show_progress_bar=False):
                return [[float(len(t) % 7), 1.0] for t in texts]

        fake = types.ModuleType("sentence_transformers")
        fake.SentenceTransformer = FakeST
        fake.__dict__["_import_env"] = os.environ.get("HF_HUB_OFFLINE")
        self._saved = {k: sys.modules.get(k) for k in ("sentence_transformers", "core.selector")}
        sys.modules["sentence_transformers"] = fake
        sys.modules.pop("core.selector", None)

    def tearDown(self) -> None:
        for k, v in self._saved.items():
            if v is None:
                sys.modules.pop(k, None)
            else:
                sys.modules[k] = v
        super().tearDown()

    def test_not_cached_model_falls_back_with_a_note_and_no_download(self):
        idx = VectorIndex()
        self.assertEqual(os.environ.get("HF_HUB_OFFLINE"), "1")
        self.assertEqual(os.environ.get("TRANSFORMERS_OFFLINE"), "1")
        self.assertNotIn("constructed", self.seen, "the embedding model is not even constructed")
        self.assertIn(idx.tier, (2, 3))
        self.assertIn("not cached", idx.note or "")
        idx.index([("a", "migracja bazy"), ("b", "obrazy")])
        self.assertTrue(idx.query("migracja"))

    def test_cached_model_runs_offline(self):
        (self.tmp / "hf" / "hub" / f"models--sentence-transformers--{EMBEDDING_MODEL}").mkdir(parents=True)
        idx = VectorIndex()
        self.assertEqual(idx.tier, 1)
        self.assertEqual(self.seen.get("offline_at_load"), "1", "even a cached model loads with the hub offline")

    def test_download_needs_the_preset_and_a_yes(self):
        idx = VectorIndex(allow_download=True, ask=lambda q: False)
        self.assertNotIn("constructed", self.seen)
        self.assertNotEqual(idx.tier, 1)
        asked = []
        os.environ.pop("HF_HUB_OFFLINE", None)
        sys.modules.pop("core.selector", None)
        VectorIndex(allow_download=True, ask=lambda q: asked.append(q) or False)
        self.assertEqual(asked[0]["kind"], "download")
