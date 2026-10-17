"""Vector level: the app's three-tier selector (``core/selector.py``:
embeddings → TF-IDF → keywords) over grains, with the tier always reported
(a TF-IDF or keyword result never pretends to be an embedding).  When the
app's module cannot be imported the tool degrades to its own keyword tier
with a visible note."""
import logging
import math
import os
import re
from pathlib import Path
from typing import Any, Callable, Optional

from .paths import ensure_repo_on_path

log = logging.getLogger(__name__)

TIER_NAMES = {1: "embeddings", 2: "tfidf", 3: "keywords"}


class _KeywordFallback:
    """Tier 3 of core.selector, re-implemented only for the case the app module is absent."""
    tier = 3

    def __init__(self) -> None:
        self._corpus: list[str] = []
        self._ids: list[str] = []

    def index(self, texts: list[str], ids: Optional[list[str]] = None) -> None:
        self._corpus = list(texts)
        self._ids = list(ids) if ids else [str(i) for i in range(len(texts))]

    def search(self, query: str, top_k: int = 5) -> list[dict[str, Any]]:
        toks = set(re.findall(r"\w+", query.lower()))
        if not toks or not self._corpus:
            return []
        scored = []
        for i, text in enumerate(self._corpus):
            low = text.lower()
            hits = sum(1 for t in toks if t in low)
            scored.append((hits / len(toks), i))
        scored.sort(key=lambda x: (-x[0], x[1]))
        return [{"id": self._ids[i], "text": self._corpus[i], "score": float(s)} for s, i in scored[:top_k] if s > 0]


EMBEDDING_MODEL = "all-MiniLM-L6-v2"  # the model core.selector loads for tier 1


def model_cached(name: str = EMBEDDING_MODEL) -> bool:
    """Whether the embedding model is already on this machine (Hugging Face hub cache or the older
    sentence-transformers cache) — so tier 1 can run without the network."""
    hub = os.environ.get("HF_HUB_CACHE") or (Path(os.environ["HF_HOME"]) / "hub" if os.environ.get("HF_HOME") else None) \
        or Path.home() / ".cache" / "huggingface" / "hub"
    st = os.environ.get("SENTENCE_TRANSFORMERS_HOME") or Path.home() / ".cache" / "torch" / "sentence_transformers"
    cands = [Path(hub) / f"models--sentence-transformers--{name}", Path(hub) / f"models--{name}",
             Path(st) / f"sentence-transformers_{name}", Path(st) / name]
    return any(c.is_dir() for c in cands)


class VectorIndex:
    def __init__(self, tier: Optional[int] = None, *, allow_download: bool = False,
                 ask: Optional[Callable[[dict[str, Any]], bool]] = None) -> None:
        """Zero network by default: before the app's selector is imported the Hugging Face libraries are put
        offline (``HF_HUB_OFFLINE``/``TRANSFORMERS_OFFLINE``), and tier 1 runs only when the embedding model is
        already cached; otherwise TF-IDF (or keywords) with a visible note.  A download happens only with the
        preset ``vectors.allow_download: true`` AND a yes to the question (no question asked = no)."""
        self.note: Optional[str] = None
        self.engine: Any = None
        download = False
        if allow_download:
            download = bool(ask({"kind": "download", "what": EMBEDDING_MODEL})) if ask is not None else False
        if not download:
            os.environ["HF_HUB_OFFLINE"] = "1"
            os.environ["TRANSFORMERS_OFFLINE"] = "1"
        try:
            ensure_repo_on_path()
            import core.selector as _sel  # noqa: WPS433
            from core.selector import SelectorEngine  # noqa: WPS433
            want = tier or int(getattr(_sel, "_TIER", 3))
            if want == 1 and not download and not model_cached():
                fallback = 2 if hasattr(_sel, "TfidfVectorizer") else 3
                self.note = (f"embedding model {EMBEDDING_MODEL} not cached on this machine; "
                             f"{TIER_NAMES[fallback]} tier used, no download (preset vectors.allow_download)")
                want = fallback
            self.engine = SelectorEngine(tier=want)
            self.tier = int(self.engine.tier)
        except Exception as exc:  # noqa: BLE001 - degrade visibly
            log.debug("core.selector unavailable", exc_info=True)
            self.engine = _KeywordFallback()
            self.tier = 3
            self.note = f"core.selector unavailable ({type(exc).__name__}); keyword tier used"
        self.ids: list[str] = []
        self.texts: list[str] = []

    @property
    def tier_name(self) -> str:
        return TIER_NAMES.get(self.tier, str(self.tier))

    def index(self, items: list[tuple[str, str]]) -> None:
        self.ids = [i for i, _ in items]
        self.texts = [t for _, t in items]
        self.engine.index(self.texts, self.ids)

    def query(self, text: str, *, candidates: int = 0, threshold: float = 0.0) -> list[tuple[str, float]]:
        """(id, score) sorted by score desc then id; ``candidates`` 0 = no cut."""
        if not self.ids:
            return []
        k = len(self.ids) if candidates <= 0 else min(candidates, len(self.ids))
        res = self.engine.search(text, top_k=k)
        out = [(r["id"], float(r["score"])) for r in res if float(r["score"]) >= threshold]
        out.sort(key=lambda x: (-x[1], x[0]))
        return out

    def similar_to(self, ident: str, *, candidates: int = 0, threshold: float = 0.0) -> list[tuple[str, float]]:
        """"More like this": the indexed text of ``ident`` as the query."""
        try:
            pos = self.ids.index(ident)
        except ValueError:
            return []
        return [(i, s) for i, s in self.query(self.texts[pos], candidates=candidates, threshold=threshold) if i != ident]

    def describe(self) -> dict[str, Any]:
        d: dict[str, Any] = {"engine": "core.selector", "tier": self.tier, "tier_name": self.tier_name, "indexed": len(self.ids)}
        if self.note:
            d["note"] = self.note
        return d


def cosine_sets(a: set[str], b: set[str]) -> float:
    if not a or not b:
        return 0.0
    return len(a & b) / math.sqrt(len(a) * len(b))
