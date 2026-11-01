"""Byte-exact UTF-8 CSV cuts for Loom SourceRecord / Unit / Observation.

No sniffing, header inference, newline normalization, or replacement decoding.
Raw tokens are quotes; unquoted/unescaped values are explicitly derived attrs.
Only the caller/integrator persists records and invokes shared text genesis.
"""

from __future__ import annotations

import argparse
from bisect import bisect_right
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import sys


def canonical(value):
    """loom::json::canonical for this contract's integers/strings/bools/null."""
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def stable_id(prefix, *values):
    # model_core.cpp: Unit/Observation::make_id, kb::stable_id (16 hex).
    return prefix + sha256("\x1f".join(values).encode("utf-8"))[:16]


class CsvError(ValueError):
    def __init__(self, code, *, offset=None, line=None, original_sha256=None):
        self.detail = {"code": code}
        if offset is not None:
            self.detail["byte_start"] = offset
        if line is not None:
            self.detail["line"] = line
        if original_sha256 is not None:
            self.detail["original_sha256"] = original_sha256
        super().__init__(code)


@dataclass(frozen=True)
class Options:
    delimiter: str = ","
    quotechar: str = '"'
    header: bool = False
    language: str = ""

    def __post_init__(self):
        for value in (self.delimiter, self.quotechar):
            if not isinstance(value, str) or len(value) != 1 or value in "\r\n":
                raise CsvError("csv_invalid_dialect")
            try:
                value.encode("utf-8")
            except UnicodeEncodeError as exc:
                raise CsvError("csv_invalid_dialect") from exc
        if self.delimiter == self.quotechar or type(self.header) is not bool:
            raise CsvError("csv_invalid_dialect")
        if not isinstance(self.language, str):
            raise CsvError("csv_invalid_language")
        try:
            self.language.encode("utf-8")
        except UnicodeEncodeError as exc:
            raise CsvError("csv_invalid_language") from exc


def line_starts(raw):
    starts, i = [0], 0
    while i < len(raw):
        if raw[i:i + 2] == b"\r\n":
            i += 2
            starts.append(i)
        elif raw[i] in (10, 13):
            i += 1
            starts.append(i)
        else:
            i += 1
    return starts


def _scan(raw, options, starts, digest):
    """Linear byte scanner. Returned spans exclude delimiters/record endings."""
    delimiter, quote = options.delimiter.encode(), options.quotechar.encode()
    size = len(raw)
    cursor = 3 if raw.startswith(b"\xef\xbb\xbf") else 0
    rows = []

    def fail(code, offset):
        raise CsvError(code, offset=offset, line=bisect_right(starts, offset),
                       original_sha256=digest)

    while cursor < size:
        begin = cursor
        cells = []
        # A blank physical record has zero cells (distinct from an empty token).
        if raw[cursor] not in (10, 13):
            while True:
                start = cursor
                quoted = raw.startswith(quote, cursor)
                if quoted:
                    cursor += len(quote)
                    chunks, chunk = [], cursor
                    while True:
                        if cursor == size:
                            fail("csv_unclosed_quote", start)
                        if raw.startswith(quote, cursor):
                            chunks.append(raw[chunk:cursor])
                            cursor += len(quote)
                            if raw.startswith(quote, cursor):
                                chunks.append(quote)
                                cursor += len(quote)
                                chunk = cursor
                            else:
                                break
                        else:
                            cursor += 1
                    value = b"".join(chunks).decode("utf-8")
                    if cursor < size and raw[cursor] not in (10, 13) and not raw.startswith(delimiter, cursor):
                        fail("csv_after_quote", cursor)
                else:
                    while cursor < size and raw[cursor] not in (10, 13) and not raw.startswith(delimiter, cursor):
                        if raw.startswith(quote, cursor):
                            fail("csv_quote_in_unquoted_field", cursor)
                        cursor += 1
                    value = raw[start:cursor].decode("utf-8")
                cells.append((start, cursor, value, quoted))
                if not raw.startswith(delimiter, cursor):
                    break
                cursor += len(delimiter)
        end = cursor
        if cursor < size:
            cursor += 2 if raw[cursor:cursor + 2] == b"\r\n" else 1
        rows.append((begin, end, cursor, cells))
    return rows


def _locator(source, starts, begin, end):
    return {"source": source, "member": "", "json_pointer": "",
            "byte_start": begin, "byte_len": end - begin,
            "time_start": None, "time_end": None,
            "line": bisect_right(starts, begin)}


def _physical_lines(starts, begin, end):
    # Empty tokens belong to the line at their position; CRLF is one ending.
    return [bisect_right(starts, begin), bisect_right(starts, max(begin, end - 1))]


def _selector(digest, method_hash, kind, rows, columns=None):
    result = {"schema": "loom.csv_selection/1", "original_sha256": digest,
              "method_sha256": method_hash, "kind": kind, "rows": rows}
    if columns is not None:
        result["columns"] = columns
    return result


def recognize(raw: bytes, *, source_key: str, name: str = "", options: Options | None = None):
    """Return rebuildable records. Input must be the unchanged original bytes."""
    options = Options() if options is None else options
    if not isinstance(options, Options):
        raise CsvError("csv_invalid_dialect")
    if not isinstance(raw, bytes):
        raise CsvError("csv_expected_original_bytes")
    if not isinstance(source_key, str) or not source_key or not isinstance(name, str):
        raise CsvError("csv_invalid_source_identity")
    try:
        source_key.encode("utf-8")
        name.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise CsvError("csv_invalid_source_identity") from exc
    digest = sha256(raw)
    starts = line_starts(raw)
    try:
        raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise CsvError("csv_invalid_utf8", offset=exc.start,
                       line=bisect_right(starts, exc.start), original_sha256=digest) from exc
    parsed = _scan(raw, options, starts, digest)
    # Pin the complete parser and explicit options, not an unavailable global preset.
    method = {"id": "loom.csv_structure", "version": 1,
              "reader_sha256": sha256(Path(__file__).read_bytes()),
              "coordinate": "source_utf8_bytes", "encoding": "utf-8",
              "options": asdict(options)}
    method_hash = sha256(canonical(method).encode())
    # Source identity remains separate from method projections and display names.
    source_id = "src_text_" + sha256(canonical([source_key, digest]).encode())
    source = {"id": source_id, "kind": "file", "uri": source_key, "blob_hash": digest,
              "size": len(raw), "mime": "text/csv", "format": "csv", "title": name,
              "parser": "loom.csv_structure", "parser_version": "1", "imported_at": "",
              "metadata": {"source_key": source_key, "coordinate": "source_utf8_bytes"}}
    units, observations, parts, genesis = [], [], [], []
    for ordinal, (begin, end, next_begin, cells) in enumerate(parsed, 1):
        loc = _locator(source_id, starts, begin, end)
        unit_id = stable_id("un_", source_id, canonical(loc))
        row_selector = _selector(digest, method_hash, "row", [ordinal, ordinal])
        physical = _physical_lines(starts, begin, end)
        units.append({"id": unit_id, "source": source_id, "kind": "csv_row", "locator": loc,
                      "title": "", "date": "", "artifact_type": "csv", "lang": options.language,
                      "bytes": end - begin,
                      "attrs": {"row": ordinal, "physical_lines": physical,
                                "header": options.header and ordinal == 1,
                                "csv_locator": row_selector}})
        row = {"unit": unit_id, "csv_locator": row_selector, "locator": loc,
               "physical_lines": physical, "record_end": next_begin, "cells": []}
        for column, (start, stop, value, quoted) in enumerate(cells, 1):
            cell_loc = _locator(source_id, starts, start, stop)
            quote_text = raw[start:stop].decode("utf-8")
            selector = _selector(digest, method_hash, "cell", [ordinal, ordinal], [column, column])
            ob_id = stable_id("ob_", unit_id, canonical(cell_loc), sha256(raw[start:stop]))
            observations.append({"id": ob_id, "unit": unit_id, "kind": "field",
                                 "text": quote_text, "locator": cell_loc, "lang": options.language,
                                 "date": "", "ordinal": column - 1, "artifact_type": "csv", "speaker": "",
                                 "attrs": {"evidence_class": "observed", "csv_locator": selector,
                                           "physical_lines": _physical_lines(starts, start, stop),
                                           "quoted": quoted,
                                           "decoded": {"value": value, "evidence_class": "derived",
                                                       "method_sha256": method_hash}}})
            row["cells"].append(ob_id)
        parts.append(row)
        # Caller passes these original-byte cuts to shared Segmenter/genesis.
        # This adapter does not invent fingerprints, edits, or genesis events.
        genesis.append({"unit": unit_id, "index": ordinal - 1, "begin": begin, "end": end,
                        "text": raw[begin:end].decode("utf-8")})
    widths = [len(row[3]) for row in parsed]
    columns = []
    for column in range(1, max(widths, default=0) + 1):
        entry = {"ordinal": column,
                 "csv_locator": _selector(digest, method_hash, "column", [1, len(parts)], [column, column]),
                 "observations": [row["cells"][column - 1] for row in parts if len(row["cells"]) >= column]}
        if options.header and parsed and len(parsed[0][3]) >= column:
            entry["header_observation"] = parts[0]["cells"][column - 1]
        columns.append(entry)
    return {"schema": "loom.csv_structure.result/1", "coordinate": "source_utf8_bytes",
            "original_sha256": digest, "method": method, "method_sha256": method_hash,
            "source": source, "units": units, "observations": observations,
            "rows": parts, "columns": columns, "genesis_input": genesis,
            "warnings": ([{"code": "csv_ragged_rows", "widths": widths}] if len(set(widths)) > 1 else [])}


def _interval(value):
    if (not isinstance(value, list) or len(value) != 2 or
            any(type(v) is not int for v in value) or not 1 <= value[0] <= value[1]):
        raise CsvError("csv_invalid_selection")
    return value


def read_selection(raw: bytes, selector: dict, *, source_key: str, name: str = "", options: Options | None = None):
    """Replay before reading. A column/range is a list of exact raw cell spans.

    Ragged rows return explicit missing coordinates; no padding or source quote
    is fabricated. Rows and columns are 1-based, inclusive, including headers.
    """
    if not isinstance(raw, bytes):
        raise CsvError("csv_expected_original_bytes")
    if not isinstance(selector, dict):
        raise CsvError("csv_invalid_selection")
    if selector.get("original_sha256") != sha256(raw):
        raise CsvError("csv_original_mismatch")
    result = recognize(raw, source_key=source_key, name=name, options=options)
    if selector.get("method_sha256") != result["method_sha256"]:
        raise CsvError("csv_method_mismatch")
    kind = selector.get("kind")
    if selector.get("schema") != "loom.csv_selection/1" or kind not in ("row", "cell", "column", "range"):
        raise CsvError("csv_invalid_selection")
    first, last = _interval(selector.get("rows"))
    if last > len(result["rows"]):
        raise CsvError("csv_selection_out_of_bounds")
    if kind == "row":
        if first != last or "columns" in selector:
            raise CsvError("csv_invalid_selection")
        unit = result["units"][first - 1]
        loc = unit["locator"]
        start, size = loc["byte_start"], loc["byte_len"]
        citation = (f"csv:sha256:{result['original_sha256']}:{result['method_sha256']}"
                    f"#row={first}")
        return {"csv_locator": selector, "citation": citation, "locator": loc,
                "text": raw[start:start + size].decode(),
                "physical_lines": unit["attrs"]["physical_lines"]}
    left, right = _interval(selector.get("columns"))
    if right > len(result["columns"]):
        raise CsvError("csv_selection_out_of_bounds")
    if kind == "cell" and (first != last or left != right):
        raise CsvError("csv_invalid_selection")
    if kind == "column" and (left != right or first != 1 or last != len(result["rows"])):
        raise CsvError("csv_invalid_selection")
    by_id = {ob["id"]: ob for ob in result["observations"]}
    selected, missing = [], []
    for row in range(first, last + 1):
        refs = result["rows"][row - 1]["cells"]
        for col in range(left, right + 1):
            if col > len(refs):
                missing.append({"row": row, "column": col})
            else:
                selected.append(by_id[refs[col - 1]])
    if kind == "cell" and missing:
        raise CsvError("csv_cell_missing")
    # A stable, hash/method-bound citation; byte locators remain the evidence.
    citation = (f"csv:sha256:{result['original_sha256']}:{result['method_sha256']}"
                f"#cell={first},{left}-{last},{right}")
    return {"csv_locator": selector, "citation": citation, "cells": selected, "missing": missing}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--source-key", required=True)
    parser.add_argument("--name", default="")
    parser.add_argument("--delimiter", default=",")
    parser.add_argument("--quotechar", default='"')
    parser.add_argument("--header", action="store_true")
    parser.add_argument("--language", default="")
    parser.add_argument("--selection", type=Path, help="JSON hash/method-bound selector to replay")
    parser.add_argument("--max-output-bytes", type=int, default=32 * 1024 * 1024,
                        help="output byte preset; 0 disables the limit")
    args = parser.parse_args(argv)
    try:
        if args.max_output_bytes < 0:
            raise CsvError("csv_invalid_output_limit")
        raw = args.input.read_bytes()
        options = Options(args.delimiter, args.quotechar, args.header, args.language)
        kwargs = {"source_key": args.source_key, "name": args.name, "options": options}
        if args.selection:
            result = read_selection(raw, json.loads(args.selection.read_text(encoding="utf-8")), **kwargs)
        else:
            result = recognize(raw, **kwargs)
        output = canonical(result).encode("utf-8") + b"\n"
        if args.max_output_bytes and len(output) > args.max_output_bytes:
            raise CsvError("csv_output_limit")
        sys.stdout.buffer.write(output)
        return 0
    except CsvError as exc:
        print(canonical({"error": exc.detail}), file=sys.stderr)
    except (OSError, ValueError) as exc:
        # Diagnostics must not include source paths, cell values, or file contents.
        print(canonical({"error": {"code": "csv_input_unavailable" if isinstance(exc, OSError)
                                  else "csv_invalid_selection_json"}}), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
