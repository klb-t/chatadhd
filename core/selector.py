"""
ChatADHD v0.07.00 - Selector Engine (Semantic Search)

Three-tier fallback for memory/message retrieval:
  1. Embedding similarity (sentence-transformers) — best quality.
  2. TF-IDF cosine similarity — decent, no GPU needed.
  3. Keyword substring matching — always works, zero deps.

The engine auto-detects available libraries at import time and
picks the highest tier available.
"""
import logging
import math
import re
from collections import Counter
from typing import Any, Optional

log = logging.getLogger(__name__)

# ── Tier detection ─────────────────────────────────────────────────

_TIER = 3  # default: keyword fallback

try:
    from sklearn.feature_extraction.text import TfidfVectorizer
    from sklearn.metrics.pairwise import cosine_similarity as _cos_sim
    _TIER = min(_TIER, 2)
    log.info("Selector: TF-IDF tier available")
except ImportError:
    pass

try:
    from sentence_transformers import SentenceTransformer
    _TIER = 1
    log.info("Selector: Embedding tier available")
except ImportError:
    pass


class SelectorEngine:
    """
    Retrieve the most relevant items from a corpus given a query.

    Usage::

        sel = SelectorEngine()
        sel.index(corpus)           # list[str]
        results = sel.search(query, top_k=5)
    """

    def __init__(self, tier: Optional[int] = None) -> None:
        self._tier = tier or _TIER
        self._corpus: list[str] = []
        self._ids: list[str] = []

        # Tier-specific state
        self._embeddings = None
        self._model = None
        self._tfidf_matrix = None
        self._vectorizer = None

        if self._tier == 1:
            try:
                self._model = SentenceTransformer("all-MiniLM-L6-v2")
            except Exception as e:
                log.warning("Failed to load embedding model, falling back: %s", e)
                self._tier = 2

    @property
    def tier(self) -> int:
        return self._tier

    def index(self, texts: list[str], ids: Optional[list[str]] = None) -> None:
        """Build search index over *texts*."""
        self._corpus = list(texts)
        self._ids = list(ids) if ids else [str(i) for i in range(len(texts))]

        if not texts:
            return

        if self._tier == 1 and self._model is not None:
            self._embeddings = self._model.encode(texts, show_progress_bar=False)
            log.debug("Indexed %d items (embedding)", len(texts))

        elif self._tier == 2:
            self._vectorizer = TfidfVectorizer(max_features=5000)
            self._tfidf_matrix = self._vectorizer.fit_transform(texts)
            log.debug("Indexed %d items (TF-IDF)", len(texts))

        else:
            log.debug("Indexed %d items (keyword)", len(texts))

    def search(self, query: str, top_k: int = 5) -> list[dict[str, Any]]:
        """
        Return up to *top_k* results sorted by relevance (descending).
        Each result: ``{"id": str, "text": str, "score": float}``.
        """
        if not self._corpus:
            return []

        if self._tier == 1:
            return self._search_embedding(query, top_k)
        elif self._tier == 2:
            return self._search_tfidf(query, top_k)
        else:
            return self._search_keyword(query, top_k)

    # ── Tier 1: Embedding ──────────────────────────────────────────

    def _search_embedding(self, query: str, top_k: int) -> list[dict]:
        q_emb = self._model.encode([query], show_progress_bar=False)
        from sklearn.metrics.pairwise import cosine_similarity
        scores = cosine_similarity(q_emb, self._embeddings)[0]
        return self._rank(scores, top_k)

    # ── Tier 2: TF-IDF ────────────────────────────────────────────

    def _search_tfidf(self, query: str, top_k: int) -> list[dict]:
        q_vec = self._vectorizer.transform([query])
        scores = _cos_sim(q_vec, self._tfidf_matrix)[0].toarray().flatten()
        return self._rank(scores, top_k)

    # ── Tier 3: Keyword ────────────────────────────────────────────

    def _search_keyword(self, query: str, top_k: int) -> list[dict]:
        query_tokens = set(re.findall(r'\w+', query.lower()))
        if not query_tokens:
            return []

        scores = []
        for text in self._corpus:
            text_lower = text.lower()
            # Score: fraction of query tokens found in text
            hits = sum(1 for t in query_tokens if t in text_lower)
            score = hits / len(query_tokens)
            scores.append(score)

        return self._rank(scores, top_k)

    # ── Common ─────────────────────────────────────────────────────

    def _rank(self, scores, top_k: int) -> list[dict]:
        indexed = sorted(enumerate(scores), key=lambda x: x[1], reverse=True)
        results = []
        for idx, score in indexed[:top_k]:
            if score <= 0:
                break
            results.append({
                "id": self._ids[idx],
                "text": self._corpus[idx],
                "score": float(score),
            })
        return results
