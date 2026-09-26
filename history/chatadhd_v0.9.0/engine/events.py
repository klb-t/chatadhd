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
import os
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

# Fundamentalna zasada: każde zdarzenie runtime ma być śledzalne.
# Domyślnie loggujemy emity na DEBUG (plik loga — pełny ślad).
# Z CHATADHD_TRACE_EVENTS=1 idą na INFO (konsola/debug HTTP).
_TRACE_LEVEL = logging.INFO if os.environ.get("CHATADHD_TRACE_EVENTS") == "1" \
    else logging.DEBUG


class EventBus:
    """Simple synchronous event bus.  All handlers run in the emitter's thread."""

    def __init__(self) -> None:
        self._handlers: dict[str, list[Callable]] = defaultdict(list)

    def on(self, event: str, handler: Callable) -> None:
        self._handlers[event].append(handler)
        log.debug("bus.on: subscribed %s to '%s' (now %d handlers)",
                   getattr(handler, "__qualname__", repr(handler)),
                   event, len(self._handlers[event]))

    def off(self, event: str, handler: Callable) -> None:
        try:
            self._handlers[event].remove(handler)
            log.debug("bus.off: unsubscribed from '%s'", event)
        except ValueError:
            pass

    def emit(self, event: str, data: Any = None) -> None:
        handlers = self._handlers.get(event, [])
        # Skrócony podgląd danych — żeby długie payloady nie zaśmiecały logów.
        data_preview = _short_repr(data)
        log.log(_TRACE_LEVEL,
                 "bus.emit '%s' → %d handlers | data=%s",
                 event, len(handlers), data_preview)
        for fn in handlers:
            try:
                fn(data)
            except Exception:
                log.exception("Event handler error for %s (handler=%s)",
                              event, getattr(fn, "__qualname__", repr(fn)))


def _short_repr(data: Any, max_len: int = 200) -> str:
    """Krótka reprezentacja do logów. Dict → klucze; lista → len; inne → repr truncated."""
    if data is None:
        return "None"
    if isinstance(data, dict):
        keys = list(data.keys())
        if len(keys) <= 6:
            return "{" + ", ".join(str(k) for k in keys) + "}"
        return "{" + ", ".join(str(k) for k in keys[:6]) + f", +{len(keys)-6}}}"
    if isinstance(data, (list, tuple)):
        return f"{type(data).__name__}[{len(data)}]"
    s = repr(data)
    if len(s) > max_len:
        return s[:max_len] + "..."
    return s


# Module-level singleton.
bus = EventBus()
