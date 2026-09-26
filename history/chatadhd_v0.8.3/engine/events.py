"""
ChatADHD v0.07.01 - Event Bus

Decoupled pub/sub so any component can emit events and any other
component can react.  The graph, UI, and analytics all subscribe
to message/node creation events without tight coupling.

Usage::

    bus = EventBus()
    bus.on("message:created", my_handler)
    bus.emit("message:created", {"id": "m_abc", "text": "hello"})
"""
import logging
from collections import defaultdict
from typing import Any, Callable

log = logging.getLogger(__name__)

# Event names (constants to prevent typos).
MSG_CREATED = "message:created"
MSG_UPDATED = "message:updated"
NODE_CREATED = "node:created"
EDGE_CREATED = "edge:created"
CONV_CREATED = "conv:created"
CONV_SWITCHED = "conv:switched"
GRAPH_CHANGED = "graph:changed"
IMPORT_DONE = "import:done"


class EventBus:
    """Simple synchronous event bus.  All handlers run in the emitter's thread."""

    def __init__(self) -> None:
        self._handlers: dict[str, list[Callable]] = defaultdict(list)

    def on(self, event: str, handler: Callable) -> None:
        self._handlers[event].append(handler)

    def off(self, event: str, handler: Callable) -> None:
        try:
            self._handlers[event].remove(handler)
        except ValueError:
            pass

    def emit(self, event: str, data: Any = None) -> None:
        for fn in self._handlers.get(event, []):
            try:
                fn(data)
            except Exception:
                log.exception("Event handler error for %s", event)


# Module-level singleton.
bus = EventBus()
