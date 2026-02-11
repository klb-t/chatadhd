"""
ChatADHD v0.07.01 - Graph Engine

Builds the knowledge graph in real-time as messages flow.
Subscribes to the event bus and creates nodes + edges from
semantic analysis (LLM-powered with regex fallback).

Node kinds: entity, topic, concept, code_ref, file, person, org, message
Edge types: mentions, depends_on, references, reply_to, part_of,
            generated_by, tagged_with, derived_from, contradicts, implements
"""
import logging
from typing import Any, Optional

from engine.events import (
    bus, MSG_CREATED, NODE_CREATED, EDGE_CREATED, GRAPH_CHANGED,
)

log = logging.getLogger(__name__)


class GraphEngine:
    """
    Listens to message events, runs semantic analysis, and materializes
    entities / topics / relations as graph nodes + edges in the DB.
    """

    def __init__(self, db, semantic_llm=None) -> None:
        self.db = db
        self.semantic_llm = semantic_llm  # engine.semantic_llm.SemanticLLM or None
        self._active = True
        bus.on(MSG_CREATED, self._on_message)
        log.info("GraphEngine started (LLM=%s)", "yes" if semantic_llm else "regex-only")

    def stop(self) -> None:
        self._active = False
        bus.off(MSG_CREATED, self._on_message)

    # ── Event handler ─────────────────────────────────────────────

    def _on_message(self, data: dict[str, Any]) -> None:
        """Called for every new message (user or assistant)."""
        if not self._active:
            return
        try:
            mid = data["id"]
            text = data.get("text", "")
            conv_id = data.get("conv_id", "")

            if len(text) < 10:
                return

            # Run analysis (LLM if available, regex fallback).
            if self.semantic_llm and self.semantic_llm.enabled:
                analysis = self.semantic_llm.analyse(text)
            else:
                from core.semantic import analyzer as regex
                raw = regex.analyse(text)
                analysis = {
                    "entities": [
                        {"name": e.text, "kind": e.entity_type.value, "relevance": e.confidence}
                        for e in raw.get("entities", [])
                    ],
                    "topics": [
                        {"label": t, "confidence": 0.5}
                        for t in raw.get("topics", [])
                    ],
                    "relations": [
                        {"subject": r.subject, "predicate": r.predicate.value, "object": r.obj}
                        for r in raw.get("relations", [])
                    ],
                }

            changed = False

            # ── Entities → nodes ───────────────────────────────
            for ent in analysis.get("entities", []):
                name = ent.get("name", "")
                kind = ent.get("kind", "entity")
                relevance = ent.get("relevance", 0.5)
                if not name or len(name) < 2 or relevance < 0.3:
                    continue
                nid = self.db.get_or_create_node(label=name, kind=kind)
                self.db.create_link(mid, nid, "mentions", weight=relevance)
                changed = True

            # ── Topics → nodes ─────────────────────────────────
            for topic in analysis.get("topics", []):
                label = topic.get("label", "") if isinstance(topic, dict) else str(topic)
                conf = topic.get("confidence", 0.5) if isinstance(topic, dict) else 0.5
                if not label or conf < 0.3:
                    continue
                nid = self.db.get_or_create_node(label=label.lower(), kind="topic")
                self.db.create_link(mid, nid, "tagged_with", weight=conf)
                changed = True

            # ── Relations → edges between entity nodes ─────────
            for rel in analysis.get("relations", []):
                subj = rel.get("subject", "")
                obj = rel.get("object", "")
                pred = rel.get("predicate", "related")
                if not subj or not obj:
                    continue
                src_node = self.db.find_node(subj)
                dst_node = self.db.find_node(obj)
                if src_node and dst_node:
                    self.db.create_link(src_node["id"], dst_node["id"], pred, weight=0.7)
                    changed = True

            # ── Conversation edge ──────────────────────────────
            if conv_id:
                self.db.create_link(mid, conv_id, "part_of", weight=0.3)

            # Store analysis in message metadata.
            try:
                self.db.update_msg(mid, metadata={
                    "semantic": {
                        "source": analysis.get("source", "unknown"),
                        "summary": analysis.get("summary", ""),
                        "sentiment": analysis.get("sentiment", ""),
                        "entity_count": len(analysis.get("entities", [])),
                        "topic_count": len(analysis.get("topics", [])),
                    }
                })
                # Mark as done so background worker skips this message.
                self.db.mark_analysed(mid, analysis)
            except Exception:
                pass

            if changed:
                bus.emit(GRAPH_CHANGED, {"conv_id": conv_id, "trigger": mid})

        except Exception:
            log.exception("GraphEngine failed processing message %s",
                          data.get("id", "?"))

    # ── Manual operations ─────────────────────────────────────────

    def ingest_analysis(self, msg_id: str, conv_id: str, analysis: dict) -> bool:
        """Ingest pre-computed analysis into graph (called by SemanticWorker).
        Creates nodes and edges without re-running LLM.
        """
        try:
            changed = False

            for ent in analysis.get("entities", []):
                name = ent.get("name", "")
                kind = ent.get("kind", "entity")
                relevance = ent.get("relevance", 0.5)
                if not name or len(name) < 2 or relevance < 0.3:
                    continue
                nid = self.db.get_or_create_node(label=name, kind=kind)
                self.db.create_link(msg_id, nid, "mentions", weight=relevance)
                changed = True

            for topic in analysis.get("topics", []):
                label = topic.get("label", "") if isinstance(topic, dict) else str(topic)
                conf = topic.get("confidence", 0.5) if isinstance(topic, dict) else 0.5
                if not label or conf < 0.3:
                    continue
                nid = self.db.get_or_create_node(label=label.lower(), kind="topic")
                self.db.create_link(msg_id, nid, "tagged_with", weight=conf)
                changed = True

            for rel in analysis.get("relations", []):
                subj = rel.get("subject", "")
                obj = rel.get("object", "")
                pred = rel.get("predicate", "related")
                if not subj or not obj:
                    continue
                src_node = self.db.find_node(subj)
                dst_node = self.db.find_node(obj)
                if src_node and dst_node:
                    self.db.create_link(src_node["id"], dst_node["id"], pred, weight=0.7)
                    changed = True

            if conv_id:
                self.db.create_link(msg_id, conv_id, "part_of", weight=0.3)

            if changed:
                bus.emit(GRAPH_CHANGED, {"conv_id": conv_id, "trigger": msg_id})
            return changed

        except Exception:
            log.debug("ingest_analysis failed for %s", msg_id, exc_info=True)
            return False

    def reindex_conversation(self, conv_id: str) -> int:
        msgs = self.db.get_msgs(conv_id, include_all=True)
        count = 0
        for m in msgs:
            self._on_message({
                "id": m["id"], "text": m["text"],
                "conv_id": conv_id, "role": m["role"],
            })
            count += 1
        log.info("Reindexed %d messages for conv %s", count, conv_id)
        return count

    def reindex_all(self) -> int:
        total = 0
        for conv in self.db.list_convs(limit=9999):
            total += self.reindex_conversation(conv["id"])
        return total
