"""Entry point: ``python3 -m loom.tools.extract`` or ``python3 loom/tools/extract``.

When run as a bare directory the package has no parent on sys.path, so the
repository root (which holds ``engine/``, ``core/`` and the ``loom`` namespace)
is inserted first and the CLI is imported by its absolute name.
"""
import sys
from pathlib import Path

if not __package__:
    _root = Path(__file__).resolve().parents[3]
    if str(_root) not in sys.path:
        sys.path.insert(0, str(_root))
    from loom.tools.extract.cli import main  # noqa: E402
else:
    from .cli import main

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
