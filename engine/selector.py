
"""SelectorEngine: chooses relevant nodes for a query.
Attempts sentence-transformers -> sklearn TF-IDF -> keyword overlap fallback."""
from typing import List, Dict, Any
import logging, math

log = logging.getLogger('selector')

def _cosine(a,b):
    num = sum(x*y for x,y in zip(a,b))
    den = (sum(x*x for x in a)**0.5)*(sum(y*y for y in b)**0.5)
    return num/den if den else 0.0

class SelectorEngine:
    def __init__(self):
        self.mode = 'none'
        # try to load heavy model
        try:
            from sentence_transformers import SentenceTransformer
            self.model = SentenceTransformer('all-MiniLM-L6-v2')
            self.mode = 'embed'
            log.info('Selector: using sentence-transformers embeddings')
        except Exception as e:
            log.info('sentence-transformers not available: %s', e)
            try:
                from sklearn.feature_extraction.text import TfidfVectorizer
                self.vectorizer = TfidfVectorizer(stop_words='english')
                self.mode = 'tfidf'
                log.info('Selector: using sklearn TF-IDF fallback')
            except Exception as e2:
                log.info('sklearn TF-IDF not available: %s', e2)
                self.mode = 'keywords'
                log.info('Selector: using keyword-overlap fallback')

    def embed_texts(self, texts: List[str]):
        if self.mode=='embed':
            return self.model.encode(texts, show_progress_bar=False).tolist()
        if self.mode=='tfidf':
            # return sparse vectors as dense lists (small scale)
            X = self.vectorizer.fit_transform(texts)
            return [row.toarray().ravel().tolist() for row in X]
        # keywords fallback - simple bag-of-words counts
        return [[len(t.split())] for t in texts]

    def select_relevant(self, nodes: List[Any], query: str, top_n: int=8) -> List[Any]:
        texts = [n.content or '' for n in nodes]
        if not texts:
            return []
        if self.mode=='embed':
            vecs = self.embed_texts(texts)
            qv = self.model.encode([query])[0].tolist()
            scored = [(sum(x*y for x,y in zip(v,qv))/( (sum(x*x for x in v)**0.5)*(sum(y*y for y in qv)**0.5) or 1e-9), n) for v,n in zip(vecs,nodes)]
            scored.sort(reverse=True, key=lambda x:x[0])
            return [n for s,n in scored[:top_n]]
        if self.mode=='tfidf':
            # use vectorizer to transform query and compute cosine with fitted tfidf
            try:
                docs = texts + [query]
                X = self.vectorizer.fit_transform(docs)
                import numpy as np
                qv = X[-1].toarray().ravel()
                vecs = [X[i].toarray().ravel() for i in range(len(texts))]
                scored = [( (_dot(qv,v)/(_norm(qv)*_norm(v)+1e-12)), n) for v,n in zip(vecs,nodes)]
                scored.sort(reverse=True, key=lambda x:x[0])
                return [n for s,n in scored[:top_n]]
            except Exception as e:
                log.info('tfidf selection failed: %s', e)
                # fallback to keywords
        # keywords fallback: count keyword overlap
        qset = set(query.lower().split())
        scored = [ (len(qset & set((n.content or '').lower().split())), n) for n in nodes ]
        scored.sort(reverse=True, key=lambda x:x[0])
        return [n for s,n in scored[:top_n]]

def _dot(a,b):
    return sum(x*y for x,y in zip(a,b))
def _norm(a):
    return sum(x*x for x in a)**0.5
