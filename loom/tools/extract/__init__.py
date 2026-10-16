"""X-Port — archive extract tool for ChatADHD / Loom (P38; slices A + B).

One engine, three faces (CLI, commander TUI, serve API).  Slice A: the engine,
readers, inventory, timeline, diff, selection (masks, lexical, vectors,
expression language), recipes.  Slice B: the package tree and batch edits,
the cut (source format 1:1 + restructuring profiles as data), outputs, the
hand-off for the app's importer, redaction, statistics, the manifest and
reproduction, model steps behind an explicit flag, sentence and task modes.
See docs/architecture/EKSTRAKT_ARCHIWOW_2026-10-16.md.

Run:  python3 -m loom.tools.extract --help   (from the repository root)
      python3 loom/tools/extract --help      (from anywhere)
"""
from pathlib import Path

__version__ = "0.3.0"
EXTRACT_SCHEMA = "1"

PACKAGE_DIR = Path(__file__).resolve().parent
DATA_DIR = PACKAGE_DIR / "data"
REPO_ROOT = PACKAGE_DIR.parents[2]
