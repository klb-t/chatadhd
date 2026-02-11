from typing import List, Any
import logging
log = logging.getLogger('selector')

class SelectorEngine:
    def __init__(self):
        self.mode = 'keywords'
        self.model = None
        try:
            from sentence_transformers import SentenceTransformer
            self.model = SentenceTransformer('all-MiniLM-L6-v2')
            self.mode = 'embed'
            log.info('Selector: embeddings')
        except:
            try:
                from sklearn.feature_extraction.text import TfidfVectorizer
                self.vectorizer = TfidfVectorizer(stop_words='english')
                self.mode = 'tfidf'
                log.info('Selector: TF-IDF')
            except:
                log.info('Selector: keywords')
    
    def select_relevant(self, nodes: List[Any], query: str, top_n: int = 8):
        if not nodes: return []
        texts = [n.content or '' for n in nodes]
        
        if self.mode == 'embed':
            vecs = self.model.encode(texts, show_progress_bar=False)
            qv = self.model.encode([query])[0]
            scored = [(sum(a*b for a,b in zip(v,qv))/((sum(a**2 for a in v)**0.5)*(sum(b**2 for b in qv)**0.5)+1e-9), n) for v,n in zip(vecs,nodes)]
        elif self.mode == 'tfidf':
            try:
                X = self.vectorizer.fit_transform(texts + [query])
                qv = X[-1].toarray().ravel()
                scored = [(sum(X[i].toarray().ravel()*qv)/(sum(X[i].toarray().ravel()**2)**0.5*sum(qv**2)**0.5+1e-9), n) for i,n in enumerate(nodes)]
            except:
                return self._kw(nodes, texts, query, top_n)
        else:
            return self._kw(nodes, texts, query, top_n)
        scored.sort(reverse=True, key=lambda x: x[0])
        return [n for _,n in scored[:top_n]]
    
    def _kw(self, nodes, texts, query, top_n):
        qw = set(query.lower().split())
        scored = [(len(qw & set(t.lower().split())), n) for t,n in zip(texts,nodes)]
        scored.sort(reverse=True, key=lambda x: x[0])
        return [n for _,n in scored[:top_n]]
