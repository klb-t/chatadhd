"""Token estimate for sources without measured counters (ChatGPT, claude.ai, Gemini, Grok exports, text files).

Method per message, always named in the output: ``tiktoken:<encoding>`` when the optional ``tiktoken`` package is
installed AND its encoding file is already on this machine (loading never touches the network: the load runs under a
guard that refuses every connection and name lookup, and a refusal means "not available offline"), else
``chars/<k> (<lang>)`` with ``k`` from the preset ``tokens.chars_per_token`` (the same constants as the inventory and
the cost estimate).  Which models use which encoding is data (``economics.estimate.tiktoken.encodings``).  The
expected error is measured where it can be (calibration against turns that carry counters), otherwise said to be
unknown with the reason — never a made-up number.
"""
import contextlib
import fnmatch
import logging
import os
import socket
import statistics
import threading
from typing import Any, Callable, Iterator, Optional

from .datafiles import DataStore, store as default_store

log = logging.getLogger(__name__)

_NET_LOCK = threading.Lock()


class NetworkRefused(OSError):
    pass


@contextlib.contextmanager
def no_network() -> Iterator[None]:
    """Refuse every outgoing connection and name lookup for the duration (restores whatever was there before)."""
    def refuse(*_a: Any, **_kw: Any) -> Any:
        raise NetworkRefused("network refused: the token estimate never downloads")
    with _NET_LOCK:
        saved = (socket.socket.connect, socket.socket.connect_ex, socket.create_connection, socket.getaddrinfo)
        socket.socket.connect = refuse  # type: ignore[assignment]
        socket.socket.connect_ex = refuse  # type: ignore[assignment]
        socket.create_connection = refuse  # type: ignore[assignment]
        socket.getaddrinfo = refuse  # type: ignore[assignment]
        try:
            yield
        finally:
            socket.socket.connect, socket.socket.connect_ex, socket.create_connection, socket.getaddrinfo = saved  # type: ignore[assignment]


class Estimator:
    def __init__(self, ds: Optional[DataStore] = None, method: Optional[str] = None) -> None:
        self.ds = ds or default_store()
        cfg = self.ds.preset("economics.estimate") or {}
        self.method = str(method or cfg.get("method") or "auto")
        self.tk_cfg = cfg.get("tiktoken") or {}
        self.cpt = self.ds.preset("tokens.chars_per_token") or {}
        self._enc: dict[str, Any] = {}
        self.unavailable: dict[str, str] = {}   # encoding → why it is not used (shown in the output)
        self.used: dict[str, int] = {}          # method → messages counted with it

    # -- method selection ------------------------------------------------------------------------------------
    def _encoding_for(self, model: Optional[str]) -> Optional[str]:
        if self.method == "chars":
            return None
        enc_map = self.tk_cfg.get("encodings") or {}
        name = (model or "").split("/", 1)[-1].lower()
        best: Optional[tuple[int, str]] = None
        for pat, enc in enc_map.items():
            if name and fnmatch.fnmatchcase(name, str(pat).lower()):
                score = sum(1 for ch in pat if ch not in "*?[]")
                if best is None or score > best[0]:
                    best = (score, str(enc))
        if best:
            return best[1]
        other = self.tk_cfg.get("other_models") or "chars"
        return None if other == "chars" else str(other)

    def _load(self, enc: str) -> Any:
        if enc in self._enc:
            return self._enc[enc]
        self._enc[enc] = None
        try:
            import tiktoken  # optional dependency
        except ImportError:
            self.unavailable[enc] = "tiktoken_missing"
            return None
        cache_dir = self.tk_cfg.get("cache_dir")
        old = os.environ.get("TIKTOKEN_CACHE_DIR")
        try:
            if cache_dir:
                os.environ["TIKTOKEN_CACHE_DIR"] = str(cache_dir)
            with no_network():
                self._enc[enc] = tiktoken.get_encoding(enc)
        except Exception as exc:  # noqa: BLE001 - not offline-available (would download) or broken: said, then chars
            log.debug("tiktoken encoding %s unavailable offline", enc, exc_info=True)
            self.unavailable[enc] = "tiktoken_offline_missing" if isinstance(exc, NetworkRefused) or "network refused" in str(exc) \
                else f"tiktoken_error:{type(exc).__name__}"
            self._enc[enc] = None
        finally:
            if cache_dir:
                if old is None:
                    os.environ.pop("TIKTOKEN_CACHE_DIR", None)
                else:
                    os.environ["TIKTOKEN_CACHE_DIR"] = old
        return self._enc[enc]

    def counter(self, model: Optional[str], lang: Optional[str]) -> tuple[Callable[[str], int], str]:
        """(count(text) → tokens, method name) for a message of ``model`` in language ``lang``."""
        enc_name = self._encoding_for(model)
        if enc_name and self.method in ("auto", "tiktoken"):
            enc = self._load(enc_name)
            if enc is not None:
                return (lambda text, _e=enc: len(_e.encode(text or "", disallowed_special=())) if text else 0), f"tiktoken:{enc_name}"
        lang_key = lang if lang in self.cpt else "default"
        k = float(self.cpt.get(lang_key) or self.cpt.get("default") or 3.6)
        return (lambda text, _k=k: int(round(len(text or "") / _k)) if text else 0), f"chars/{k:g} ({lang_key})"

    def count(self, text: str, model: Optional[str], lang: Optional[str]) -> tuple[int, str]:
        fn, how = self.counter(model, lang)
        self.used[how] = self.used.get(how, 0) + 1
        return fn(text), how

    def describe(self) -> dict[str, Any]:
        return {"method": self.method, "used": dict(sorted(self.used.items())),
                "unavailable": dict(sorted(self.unavailable.items())), "chars_per_token": {k: v for k, v in self.cpt.items() if k != "note"}}


def calibration(pairs: list[tuple[int, int]]) -> Optional[dict[str, Any]]:
    """Relative error of the estimate against measured counts: pairs (estimated, measured), measured > 0.
    Median and the 10th/90th percentile of (estimated − measured) / measured, with the number of turns."""
    rel = sorted((e - m) / m for e, m in pairs if m > 0)
    if not rel:
        return None

    def q(p: float) -> float:
        i = (len(rel) - 1) * p
        lo, hi = int(i), min(int(i) + 1, len(rel) - 1)
        return rel[lo] + (rel[hi] - rel[lo]) * (i - lo)
    return {"turns": len(rel), "median": round(statistics.median(rel), 4), "p10": round(q(0.1), 4), "p90": round(q(0.9), 4),
            "sum_estimated": sum(e for e, _ in pairs), "sum_measured": sum(m for _, m in pairs)}
