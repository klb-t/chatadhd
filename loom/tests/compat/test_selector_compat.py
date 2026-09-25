"""Differential test: loom::SelectorEngine's native TF-IDF tier vs a small
pure-Python reference implementation of sklearn's TfidfVectorizer formula
(sklearn itself is not installed in this environment, per policy).

Reference formula (TfidfVectorizer defaults, max_features=5000):
  - lowercase, token pattern r'(?u)\\b\\w\\w+\\b'
  - raw term frequency (no sublinear scaling)
  - smooth idf: idf(t) = ln((1+n)/(1+df(t))) + 1
  - l2-normalise each document vector (and the query vector at transform time)
  - cosine similarity between l2-normalised vectors == their dot product
  - vocabulary capped at max_features, kept by (-total_count, term) and then
    re-sorted alphabetically (this is also what loom/selector.h documents).
"""
import math
import re
import unittest
from collections import Counter

from compat_common import CompatTestCase, tool

TOKEN_RE = re.compile(r'(?u)\b\w\w+\b')


def tokenize(text):
    return TOKEN_RE.findall(text.lower())


def fit_tfidf(corpus, max_features=5000):
    doc_tokens = [tokenize(t) for t in corpus]
    total_count = Counter()
    doc_freq = Counter()
    for toks in doc_tokens:
        total_count.update(toks)
        doc_freq.update(set(toks))

    terms = list(total_count)
    if len(terms) > max_features:
        terms.sort(key=lambda t: (-total_count[t], t))
        terms = terms[:max_features]
    terms.sort()
    vocab = {t: i for i, t in enumerate(terms)}

    n = len(corpus)
    idf = [math.log((1 + n) / (1 + doc_freq[t])) + 1.0 for t in terms]

    def vectorize(tokens):
        counts = Counter(t for t in tokens if t in vocab)
        vec = {vocab[t]: c * idf[vocab[t]] for t, c in counts.items()}
        norm = math.sqrt(sum(v * v for v in vec.values()))
        if norm > 0:
            vec = {k: v / norm for k, v in vec.items()}
        return vec

    doc_vecs = [vectorize(toks) for toks in doc_tokens]
    return vocab, idf, doc_vecs, vectorize


def cosine_sparse(a, b):
    if len(a) > len(b):
        a, b = b, a
    return sum(v * b.get(k, 0.0) for k, v in a.items())


def rank(scores, ids, corpus, top_k):
    order = sorted(range(len(scores)), key=lambda i: -scores[i])
    out = []
    for i in order[:len(scores)]:
        if len(out) >= top_k:
            break
        if scores[i] <= 0:
            break
        out.append({"id": ids[i], "text": corpus[i], "score": scores[i]})
    return out


class SelectorTfIdfCompat(CompatTestCase):
    def _compare(self, corpus, queries, ids=None, top_k=5):
        req = {"tier": 2, "corpus": corpus, "queries": [{"query": q, "top_k": top_k} for q in queries]}
        if ids is not None:
            req["ids"] = ids
        cpp = tool("selector-search", self.tmp.file_json("req.json", req))

        vocab, idf, doc_vecs, vectorize = fit_tfidf(corpus)
        use_ids = ids if ids is not None else [str(i) for i in range(len(corpus))]
        for qi, query in enumerate(queries):
            qvec = vectorize(tokenize(query))
            scores = [cosine_sparse(qvec, dv) for dv in doc_vecs]
            py_hits = rank(scores, use_ids, corpus, top_k)
            cpp_hits = cpp[qi]
            self.assertEqual(len(py_hits), len(cpp_hits), f"query={query!r}")
            for py_h, cpp_h in zip(py_hits, cpp_hits):
                self.assertEqual(py_h["id"], cpp_h["id"], f"query={query!r}")
                self.assertAlmostEqual(py_h["score"], cpp_h["score"], places=9, msg=f"query={query!r}")

    def test_small_corpus(self):
        corpus = [
            "the quick brown fox jumps over the lazy dog",
            "a fast fox runs through the forest quickly",
            "database migration and API design notes",
            "the graph engine builds nodes and edges from analysis",
            "completely unrelated cooking recipe about pasta and cheese",
            "another database note about API keys and tokens",
        ]
        self._compare(corpus, ["fox jumps", "database API", "graph nodes edges", "nothing matches this at all",
                               "the the the a a fox"])

    def test_custom_ids_and_repeated_terms(self):
        corpus = ["alpha alpha beta", "beta gamma gamma gamma", "alpha beta gamma delta"]
        ids = ["doc-a", "doc-b", "doc-c"]
        self._compare(corpus, ["alpha", "gamma gamma", "delta"], ids=ids, top_k=2)

    def test_single_char_tokens_excluded(self):
        # sklearn's default token pattern requires >=2 word chars; single
        # letters (and lone digits) must not become vocabulary terms.
        corpus = ["a b cat", "x y dog cat", "I am a cat person"]
        self._compare(corpus, ["cat", "a b x y", "person"])

    def test_empty_and_whitespace_only(self):
        corpus = ["", "   ", "real content here about testing"]
        self._compare(corpus, ["testing", ""])


if __name__ == "__main__":
    unittest.main()
