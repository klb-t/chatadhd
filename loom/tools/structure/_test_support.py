"""Shared test fixtures; not used by production runners."""
from pathlib import Path
import tempfile


def private_temporary_directory():
    """Keep fabricated private sessions outside Git even with an in-repo TMPDIR.

    Production path checks stay enabled. Only this fixture chooses a suitable
    parent; no process-global environment or tempfile setting is changed.
    """
    for candidate in dict.fromkeys((tempfile.gettempdir(), "/tmp", "/var/tmp")):
        parent = Path(candidate).resolve()
        try:
            repo = Path(__file__).resolve().parents[3]
            if parent == repo or repo in parent.parents or any(
                    (ancestor / ".git").exists() for ancestor in (parent, *parent.parents)):
                continue
            return tempfile.TemporaryDirectory(dir=parent)
        except (OSError, ValueError):
            continue
    raise RuntimeError("credential tests need a writable temporary directory outside Git")
