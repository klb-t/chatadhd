"""Stdlib-only local ODT extraction; raw bytes and derived coordinates stay separate."""

from .reader import extract, read_file
from .container import OdtError
from .adapter import prepare_import

__all__ = ["OdtError", "extract", "read_file", "prepare_import"]
