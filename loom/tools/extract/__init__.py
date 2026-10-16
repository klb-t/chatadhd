"""X-Port — archive extract tool for ChatADHD / Loom (P38, slice A: engine core).

One engine, three faces (CLI, commander TUI, serve API); this slice ships the
engine, the readers, inventory, timeline, diff, selection (masks, lexical,
vectors, expression language), recipes and the CLI verbs that need no package
writer yet.  See docs/architecture/EKSTRAKT_ARCHIWOW_2026-10-16.md.

Run:  python3 -m loom.tools.extract --help   (from the repository root)
      python3 loom/tools/extract --help      (from anywhere)
"""
from pathlib import Path

__version__ = "0.1.0"
EXTRACT_SCHEMA = "1"

PACKAGE_DIR = Path(__file__).resolve().parent
DATA_DIR = PACKAGE_DIR / "data"
REPO_ROOT = PACKAGE_DIR.parents[2]
