"""Shared test support: repository root on sys.path (the suite runs with
``python3 -I``), isolated data store (no user overlay), fixture paths, a
socket guard that refuses every connection outside the loopback, and helpers
that run the pipeline on fixtures."""
import json
import os
import socket
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))

from loom.tools.extract import i18n, lexical, langdetect  # noqa: E402
from loom.tools.extract.datafiles import DataStore, set_store, store  # noqa: E402

FIX = REPO / "loom" / "tests" / "fixtures"
EXPORTS = FIX / "exports"
IMPORT = FIX / "import"
EXTRACT = FIX / "extract"
EXPECTED_EXPORTS = json.loads((EXPORTS / "EXPECTED.json").read_text(encoding="utf-8"))
EXPECTED_EXTRACT = json.loads((EXTRACT / "EXPECTED.json").read_text(encoding="utf-8"))

_ORIG_CONNECT = socket.socket.connect
_ORIG_CREATE = socket.create_connection


def _loopback(address) -> bool:
    host = address[0] if isinstance(address, tuple) else str(address)
    return host in ("127.0.0.1", "::1", "localhost") or str(host).startswith("/")


def _guarded_connect(self, address):
    if not _loopback(address):
        raise AssertionError(f"network connection attempted in a test: {address!r}")
    return _ORIG_CONNECT(self, address)


def _guarded_create(address, *a, **kw):
    if not _loopback(address):
        raise AssertionError(f"network connection attempted in a test: {address!r}")
    return _ORIG_CREATE(address, *a, **kw)


class ExtractTest(unittest.TestCase):
    """Isolated data store (no overlay), Polish UI, no network."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory(prefix="xport_test_")
        self.tmp = Path(self._tmp.name)
        set_store(DataStore(overlay=self.tmp / "no-overlay"))
        i18n.reset_for_tests()
        i18n.set_language("pl")
        lexical.reset_for_tests()
        langdetect.reset_for_tests()
        socket.socket.connect = _guarded_connect  # type: ignore[assignment]
        socket.create_connection = _guarded_create  # type: ignore[assignment]
        self._env = dict(os.environ)
        os.environ.pop("LOOM_EXTRACT_LANG", None)
        os.environ["LOOM_EXTRACT_STATE"] = str(self.tmp / "state")  # per-user state (autosave, secret store) stays in the test
        self._engines: list = []

    def tearDown(self) -> None:
        for eng in self._engines:
            eng.close()
        socket.socket.connect = _ORIG_CONNECT  # type: ignore[assignment]
        socket.create_connection = _ORIG_CREATE  # type: ignore[assignment]
        os.environ.clear()
        os.environ.update(self._env)
        set_store(None)
        self._tmp.cleanup()

    # -- helpers -------------------------------------------------------------
    def engine(self, *sources, aggregate: bool = False):
        from loom.tools.extract.engine import Engine
        eng = Engine(ds=store(), aggregate=aggregate)
        self._engines.append(eng)
        if sources:
            eng.set_sources([str(s) for s in sources])
        return eng

    def units(self, *sources):
        """scan → read → merge; returns (engine, merged units)."""
        eng = self.engine(*sources)
        eng.ensure_units()
        return eng, eng.units

    @staticmethod
    def block_counts(unit) -> dict:
        out: dict = {}
        for m in unit.messages:
            for b in m.blocks:
                out[b.kind] = out.get(b.kind, 0) + 1
        return out

    @staticmethod
    def write(path: Path, text: str) -> Path:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path
