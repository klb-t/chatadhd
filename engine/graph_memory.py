"""
ChatADHD v0.07.01 - Graph-Based Memory Selector

Instead of embedding similarity, we use the knowledge graph itself as
the index.  Given a new message:

1. Run semantic analysis → extract entities & topics.
2. Find matching nodes in the graph (by label).
3. BFS/traverse outward N hops from those nodes.
4. Collect connected message nodes.
5. Return their text as context for the main model.

This is fundamentally different from vector search:
- No embeddings needed (works offline, zero cost).
- Relationships are explicit, not latent.
- The graph grows richer with every message.
- Proximity = semantic relevance (messages that share entities cluster).
"""
import logging
from collections import deque
from typing import Any, Optional

from core.semantic import analyzer as regex_analyzer

log = logging.getLogger(__name__)


class GraphMemorySelector:
    """Select relevant memory/message context using graph proximity."""

    def __init__(self, db, config) -> None:
        self.db = db
        self.config = config

    def select_context(
        self, text: str,
        analysis: Optional[dict] = None,
        max_nodes: Optional[int] = None,
        depth: Optional[int] = None,
        current_conv_id: Optional[str] = None,
    ) -> str:
        """
        Given input text, find relevant context from the knowledge graph.

        Returns a formatted string ready to inject into the system prompt.
        """
        max_n = max_nodes or self.config.get("graph_memory_max_nodes", 20)
        max_d = depth or self.config.get("graph_memory_depth", 2)

        # Step 1: Extract entities/topics from the input.
        seed_labels = self._extract_seed_labels(text, analysis)
        if not seed_labels:
            return ""

        # Step 2: Find matching graph nodes.
        seed_ids: set[str] = set()
        for label in seed_labels:
            node = self.db.find_node(label)
            if node:
                seed_ids.add(node["id"])
            # Also try case-insensitive partial match via topic kind.
            node_topic = self.db.find_node(label.lower(), kind="topic")
            if node_topic:
                seed_ids.add(node_topic["id"])

        if not seed_ids:
            return ""

        # Step 3: BFS traversal — collect nearby message nodes.
        visited: set[str] = set()
        message_ids: list[str] = []
        queue: deque[tuple[str, int]] = deque((sid, 0) for sid in seed_ids)

        while queue and len(message_ids) < max_n:
            node_id, d = queue.popleft()
            if node_id in visited:
                continue
            visited.add(node_id)

            # If this is a message node, collect it.
            if node_id.startswith("m_"):
                # Skip messages from current conversation (already in context).
                msg = self.db.get_msg(node_id)
                if msg and msg.get("conv_id") != current_conv_id:
                    message_ids.append(node_id)

            # Expand neighbors if within depth limit.
            if d < max_d:
                links = self.db.get_links(node_id)
                for link in links:
                    neighbor = link["dst"] if link["src"] == node_id else link["src"]
                    if neighbor not in visited:
                        queue.append((neighbor, d + 1))

        if not message_ids:
            return ""

        # Step 4: Build context string.
        context_parts: list[str] = []
        for mid in message_ids[:max_n]:
            msg = self.db.get_msg(mid)
            if not msg:
                continue
            # Get conversation title for context.
            conv = self.db.get_conv(msg["conv_id"])
            conv_title = conv["title"][:30] if conv else "?"
            # Truncate long messages.
            text_snip = msg["text"][:500]
            context_parts.append(
                f"[{conv_title} | {msg['role']}]: {text_snip}"
            )

        if not context_parts:
            return ""

        result = "Relevant context from past conversations:\n" + "\n---\n".join(context_parts)
        log.info("Graph memory: %d seeds → %d messages selected",
                 len(seed_ids), len(context_parts))
        return result

    def _extract_seed_labels(
        self, text: str, analysis: Optional[dict] = None
    ) -> list[str]:
        """Extract entity/topic labels to use as graph seeds."""
        labels: list[str] = []

        if analysis:
            # Use pre-computed LLM analysis.
            for ent in analysis.get("entities", []):
                name = ent.get("name", "") if isinstance(ent, dict) else str(ent)
                if name and len(name) > 1:
                    labels.append(name)
            for topic in analysis.get("topics", []):
                label = topic.get("label", "") if isinstance(topic, dict) else str(topic)
                if label:
                    labels.append(label)
        else:
            # Fallback: regex extraction.
            result = regex_analyzer.analyse(text)
            for ent in result.get("entities", []):
                if len(ent.text) > 1:
                    labels.append(ent.text)
            for topic in result.get("topics", []):
                labels.append(topic)

        return labels
