from typing import List, Any
import logging
log = logging.getLogger('selector')

class SelectorEngine:
    def __init__(self):
        self.mode = 'keywords'
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
        qw = set(query.lower().split())
        scored = [(len(qw & set(t.lower().split())), n) for t,n in zip(texts,nodes)]
        scored.sort(reverse=True, key=lambda x: x[0])
        return [n for _,n in scored[:top_n]]
