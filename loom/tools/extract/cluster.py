"""Discovery of "other small projects" without a model (``klastry``).
Features per conversation: proper nouns (capitalised tokens not at a sentence
start), code identifiers (snake_case / camelCase), file names and paths, and
recurring stemmed terms; weights are TF-IDF against the corpus (contrast, as
the ``expand`` stage of ``archive.h``), proper nouns and files boosted by a
preset.  Conversations sharing weighted terms above ``min_similarity`` are
linked; deterministic label propagation (fixed node order, ties to the
smaller label) yields candidate clusters ``{id, top_terms, sample_titles,
conversations, period}`` for the user to confirm; a cluster becomes a
criterion (``cluster_criteria``).  Every number is a count, never a guess.
"""
import math
import re
from typing import Any, Optional

from .criteria import Criteria, build_criteria
from .datafiles import DataStore, store as default_store
from .lexical import Lexicon, lexicon as default_lexicon
from .model import Unit

SCHEMA = "loom.extract.clusters/1"
CONV_KINDS = ("conversation", "session")

_PROPER = re.compile(r"(?<![.!?\n]\s)(?<!^)\b([A-ZĄĆĘŁŃÓŚŹŻ][a-ząćęłńóśźż]{2,}(?:[A-Z][a-ząćęłńóśźż]+)*)\b")
_IDENT = re.compile(r"\b([a-z]+(?:_[a-z0-9]+)+|[a-z]+(?:[A-Z][a-z0-9]+)+)\b")
_FILE = re.compile(r"(?<![\w/])((?:[\w.-]+/)*[\w.-]+\.(?:py|cpp|cc|h|hpp|json|jsonl|md|js|ts|tsx|txt|zip|db|kv|toml|yaml|yml|csv|sql|html))\b")


def features(text: str, lex: Lexicon, boost: float) -> dict[str, float]:
    """term → raw weight (count × boost for proper nouns / identifiers / files)."""
    out: dict[str, float] = {}
    for rx, kind in ((_PROPER, "proper"), (_IDENT, "ident"), (_FILE, "file")):
        for m in rx.finditer(text):
            tok = m.group(1)
            key = tok if kind != "proper" else lex.fold(tok)
            if kind == "proper" and key in lex.stop_all:
                continue
            out[key] = out.get(key, 0.0) + boost
    for t in lex.term_keys(text):
        out[t] = out.get(t, 0.0) + 1.0
    return out


def discover(units: list[Unit], ds: Optional[DataStore] = None, lex: Optional[Lexicon] = None, *,
             min_conversations: Optional[int] = None, top_terms: Optional[int] = None, max_clusters: Optional[int] = None,
             min_term_df: Optional[int] = None, min_similarity: Optional[float] = None, aggregate: bool = False) -> dict[str, Any]:
    ds = ds or default_store()
    lex = lex or default_lexicon()
    cfg = dict(ds.preset("clusters", {}) or {})
    min_conv = int(cfg.get("min_conversations", 2) if min_conversations is None else min_conversations)
    k = int(cfg.get("top_terms", 8) if top_terms is None else top_terms)
    max_c = int(cfg.get("max_clusters", 0) if max_clusters is None else max_clusters)
    min_df = int(cfg.get("min_term_df", 2) if min_term_df is None else min_term_df)
    min_sim = float(cfg.get("min_similarity", 0.15) if min_similarity is None else min_similarity)
    boost = float(cfg.get("feature_boost", 2.0))
    max_df_share = float(cfg.get("max_df_share", 0.5))

    convs = [u for u in units if u.kind in CONV_KINDS]
    n = len(convs)
    raw: list[dict[str, float]] = [features((u.title or "") + "\n" + u.text(), lex, boost) for u in convs]
    df: dict[str, int] = {}
    for f in raw:
        for t in f:
            df[t] = df.get(t, 0) + 1
    max_df = max(1, int(n * max_df_share)) if n > 2 else n
    vocab = {t for t, d in df.items() if d >= min_df and d <= max_df}
    vecs: list[dict[str, float]] = []
    for f in raw:
        v = {t: w * math.log(1.0 + n / df[t]) for t, w in f.items() if t in vocab}
        norm = math.sqrt(sum(x * x for x in v.values())) or 1.0
        vecs.append({t: x / norm for t, x in v.items()})
    top: list[dict[str, float]] = []
    for v in vecs:
        items = sorted(v.items(), key=lambda x: (-x[1], x[0]))
        top.append(dict(items[:k] if k > 0 else items))

    # similarity graph over top terms (cosine of the restricted vectors)
    edges: dict[int, list[tuple[int, float]]] = {i: [] for i in range(n)}
    for i in range(n):
        for j in range(i + 1, n):
            common = set(top[i]) & set(top[j])
            if not common:
                continue
            s = sum(top[i][t] * top[j][t] for t in common)
            if s >= min_sim:
                edges[i].append((j, s))
                edges[j].append((i, s))
    # deterministic label propagation
    labels = list(range(n))
    for _ in range(50):
        changed = False
        for i in range(n):
            if not edges[i]:
                continue
            votes: dict[int, float] = {}
            for j, s in edges[i]:
                votes[labels[j]] = votes.get(labels[j], 0.0) + s
            best = min(votes.items(), key=lambda x: (-x[1], x[0]))[0]
            if best != labels[i]:
                labels[i] = best
                changed = True
        if not changed:
            break
    groups: dict[int, list[int]] = {}
    for i, lab in enumerate(labels):
        groups.setdefault(lab, []).append(i)
    clusters: list[dict[str, Any]] = []
    unclustered: list[str] = []
    for lab, members in groups.items():
        if len(members) < min_conv:
            unclustered.extend(convs[i].uid for i in members)
            continue
        terms: dict[str, float] = {}
        for i in members:
            for t, w in top[i].items():
                terms[t] = terms.get(t, 0.0) + w
        tt = sorted(terms.items(), key=lambda x: (-x[1], x[0]))
        tt = tt[:k] if k > 0 else tt
        stamps = sorted(c for c in (convs[i].created_utc for i in members) if c)
        ordered = sorted(members, key=lambda i: (convs[i].created_utc or "", convs[i].uid))
        clusters.append({"size": len(members), "top_terms": [{"term": t, "weight": round(w, 4)} for t, w in tt],
                         "sample_titles": [] if aggregate else [convs[i].title for i in ordered[:3] if convs[i].title],
                         "conversations": [convs[i].uid for i in ordered],
                         "period": {"from": stamps[0] if stamps else None, "to": stamps[-1] if stamps else None},
                         "services": sorted({convs[i].service or "" for i in members})})
    clusters.sort(key=lambda c: (-c["size"], c["conversations"][0]))
    if max_c > 0:
        for c in clusters[max_c:]:
            unclustered.extend(c["conversations"])
        clusters = clusters[:max_c]
    for i, c in enumerate(clusters, 1):
        c["id"] = f"k{i}"
    return {"schema": SCHEMA, "clusters": clusters, "unclustered": sorted(unclustered),
            "settings": {"min_conversations": min_conv, "top_terms": k, "max_clusters": max_c, "min_term_df": min_df,
                         "min_similarity": min_sim, "feature_boost": boost, "max_df_share": max_df_share},
            "counts": {"conversations": n, "clustered": sum(c["size"] for c in clusters), "clusters": len(clusters),
                       "vocabulary": len(vocab)},
            "how": "features: proper nouns, identifiers, file names (boosted) + stemmed terms; TF-IDF vs corpus; "
                   "cosine over top terms ≥ min_similarity → edges; deterministic label propagation; no model"}


def cluster_criteria(cluster: dict[str, Any], ds: Optional[DataStore] = None, lex: Optional[Lexicon] = None) -> Criteria:
    """A cluster as criteria: its top terms become keywords with normalised weights."""
    terms = cluster.get("top_terms") or []
    top = max((t["weight"] for t in terms), default=1.0) or 1.0
    kws = [{"term": t["term"], "inflect": "rules", "weight": round(0.5 + 1.5 * t["weight"] / top, 3)} for t in terms]
    obj = {"id": f"cluster:{cluster.get('id', '?')}", "keywords": kws, "clusters": [cluster.get("id")],
           "vectors": [{"query": " ".join(t["term"] for t in terms), "grain": "window"}] if terms else []}
    return build_criteria(obj, ds or default_store(), lex or default_lexicon(), source="cluster")
