"""
ChatADHD v0.07.01 - Semantic Analysis Pipeline

Multi-level extraction with zero external dependencies at the base level:
  Level 1: Regex-based NER — fast, local, always available.
  Level 2: LLM-based extraction — optional, higher accuracy.
  Level 3: Embedding similarity — optional, requires sentence-transformers.

Each level degrades gracefully if its dependencies are missing.
"""
import logging
import re
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import Optional

log = logging.getLogger(__name__)


class EntityType(Enum):
    EMAIL = "email"
    URL = "url"
    DATE = "date"
    TIME = "time"
    MONEY = "money"
    PHONE = "phone"
    HASHTAG = "hashtag"
    MENTION = "mention"
    FILE_PATH = "file_path"
    IP_ADDRESS = "ip_address"
    CODE_REF = "code_ref"       # function/class/module names
    PERSON = "person"
    ORGANISATION = "organisation"
    UNKNOWN = "unknown"


class RelationType(Enum):
    MENTIONS = "mentions"
    DEPENDS_ON = "depends_on"
    RELATED_TO = "related_to"
    CREATED_BY = "created_by"
    PART_OF = "part_of"
    REFERENCES = "references"


@dataclass
class ExtractedEntity:
    text: str
    entity_type: EntityType
    confidence: float = 1.0
    start: int = 0
    end: int = 0
    metadata: dict = field(default_factory=dict)


@dataclass
class ExtractedRelation:
    subject: str
    predicate: RelationType
    obj: str
    confidence: float = 0.5
    source_text: str = ""


# ── Regex Patterns ─────────────────────────────────────────────────

_PATTERNS: list[tuple[EntityType, re.Pattern]] = [
    (EntityType.EMAIL,       re.compile(r'\b[\w.+-]+@[\w-]+\.[\w.-]+\b')),
    (EntityType.URL,         re.compile(r'https?://[^\s<>\'")\]]+', re.I)),
    (EntityType.IP_ADDRESS,  re.compile(r'\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b')),
    (EntityType.PHONE,       re.compile(r'(?:\+\d{1,3}[-.\s]?)?\(?\d{2,4}\)?[-.\s]?\d{3,4}[-.\s]?\d{3,4}\b')),
    (EntityType.MONEY,       re.compile(r'[$€£¥]\s?\d[\d,]*\.?\d*|\d[\d,]*\.?\d*\s?(?:USD|EUR|PLN|GBP)', re.I)),
    (EntityType.DATE,        re.compile(r'\b\d{4}[-/]\d{1,2}[-/]\d{1,2}\b')),
    (EntityType.TIME,        re.compile(r'\b\d{1,2}:\d{2}(?::\d{2})?\s?(?:AM|PM)?\b', re.I)),
    (EntityType.HASHTAG,     re.compile(r'#[A-Za-z_]\w{1,39}')),
    (EntityType.MENTION,     re.compile(r'@[A-Za-z_]\w{1,39}')),
    (EntityType.FILE_PATH,   re.compile(r'(?:/[\w.-]+){2,}|[A-Z]:\\[\w\\.-]+')),
    (EntityType.CODE_REF,    re.compile(r'\b(?:class|def|import|from)\s+(\w+)', re.M)),
]


# ── Topic keywords (lightweight zero-dep topic tagging) ────────────

_TOPIC_KEYWORDS: dict[str, list[str]] = {
    "legal":       ["court", "claim", "evidence", "tribunal", "lawsuit", "litigation",
                    "sąd", "pozew", "dowód", "orzeczenie"],
    "finance":     ["invoice", "payment", "budget", "cost", "revenue", "tax",
                    "faktura", "płatność", "budżet", "przychód"],
    "tech":        ["API", "database", "deploy", "server", "container", "git",
                    "backend", "frontend", "pipeline", "kubernetes"],
    "ai_ml":       ["model", "training", "embedding", "transformer", "fine-tune",
                    "inference", "prompt", "token", "LLM", "GPT", "Claude"],
    "security":    ["encryption", "auth", "vulnerability", "CVE", "zero-knowledge",
                    "firewall", "certificate", "szyfrowanie"],
    "health":      ["diagnosis", "medication", "therapy", "symptom",
                    "diagnoza", "leczenie", "objaw"],
    "project_mgmt": ["deadline", "milestone", "sprint", "backlog", "roadmap",
                     "deliverable", "termin", "kamień milowy"],
}


class SemanticAnalyzer:
    """Extract entities, relations, and topics from text."""

    # ── Level 1: Pattern-based NER ─────────────────────────────────

    def extract_entities(self, text: str) -> list[ExtractedEntity]:
        """Fast regex-based entity extraction. No dependencies."""
        entities: list[ExtractedEntity] = []
        seen: set[str] = set()

        for etype, pattern in _PATTERNS:
            for m in pattern.finditer(text):
                value = m.group(1) if m.lastindex else m.group(0)
                value = value.strip()
                key = (etype.value, value.lower())
                if key in seen:
                    continue
                seen.add(key)
                entities.append(ExtractedEntity(
                    text=value,
                    entity_type=etype,
                    start=m.start(),
                    end=m.end(),
                ))
        return entities

    # ── Topic tagging ──────────────────────────────────────────────

    def extract_topics(self, text: str, threshold: int = 2) -> list[str]:
        """
        Return topic labels whose keyword count in ``text`` meets *threshold*.
        Lightweight alternative to ML-based topic modelling.
        """
        text_lower = text.lower()
        topics: list[str] = []
        for topic, keywords in _TOPIC_KEYWORDS.items():
            hits = sum(1 for kw in keywords if kw.lower() in text_lower)
            if hits >= threshold:
                topics.append(topic)
        return topics

    # ── Level 2: Relation extraction (heuristic) ──────────────────

    def extract_relations(self, text: str) -> list[ExtractedRelation]:
        """
        Simple subject-verb-object patterns.  Not a full NLP pipeline,
        but good enough for linking nodes in the knowledge graph.
        """
        relations: list[ExtractedRelation] = []
        # "X depends on Y", "X is part of Y", "X references Y"
        dep_pat = re.compile(
            r'(\b\w[\w\s]{1,30}?)\s+(?:depends?\s+on|requires?|needs?)\s+(\b\w[\w\s]{1,30})',
            re.I,
        )
        for m in dep_pat.finditer(text):
            relations.append(ExtractedRelation(
                subject=m.group(1).strip(),
                predicate=RelationType.DEPENDS_ON,
                obj=m.group(2).strip(),
                confidence=0.6,
                source_text=m.group(0),
            ))

        ref_pat = re.compile(
            r'(\b\w[\w\s]{1,30}?)\s+(?:references?|refers?\s+to|see|cf\.?)\s+(\b\w[\w\s]{1,30})',
            re.I,
        )
        for m in ref_pat.finditer(text):
            relations.append(ExtractedRelation(
                subject=m.group(1).strip(),
                predicate=RelationType.REFERENCES,
                obj=m.group(2).strip(),
                confidence=0.5,
                source_text=m.group(0),
            ))

        return relations

    # ── Convenience ────────────────────────────────────────────────

    def analyse(self, text: str) -> dict:
        """Run all extractors and return a combined result dict."""
        return {
            "entities": self.extract_entities(text),
            "topics": self.extract_topics(text),
            "relations": self.extract_relations(text),
            "timestamp": datetime.utcnow().isoformat() + "Z",
        }


# Module-level singleton.
analyzer = SemanticAnalyzer()
