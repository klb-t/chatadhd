"""Local CSV recognized-parts adapter; no storage or provider side effects."""

from .reader import CsvError, Options, recognize, read_selection

__all__ = ["CsvError", "Options", "recognize", "read_selection"]
