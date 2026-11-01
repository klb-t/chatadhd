#!/usr/bin/env python3
"""Project existing Poppler output; never open/decode a PDF or execute its text."""
import hashlib
import json
import re
import sys


def digest(value):
    return hashlib.sha256(value).hexdigest()


def integer(value, minimum=0):
    if type(value) is not int or not minimum <= value <= 2**63 - 1:
        raise ValueError("pdf_structure_integer_invalid")
    return value


def project(request):
    if not isinstance(request, dict) or request.get("schema") != "loom.pdf_structure.input/1":
        raise ValueError("pdf_structure_input_invalid")
    source = request["source"]
    if not re.fullmatch("[a-f0-9]{64}", source["sha256"]):
        raise ValueError("pdf_structure_source_invalid")
    integer(source["size"])
    reader = request["reader"]
    if reader["adapter"] != "poppler_glib" or not isinstance(reader["library"], str):
        raise ValueError("pdf_structure_reader_invalid")
    options = request["options"]
    blank_lines = integer(options["blank_lines"])
    max_chars = integer(options["chapter_max_chars"])
    prefixes = options["chapter_prefixes"]
    if (options["chapter_mode"] not in ("off", "prefix") or
            not isinstance(prefixes, list) or
            any(not isinstance(p, str) or not p or any(c in p for c in "\r\n\f") for p in prefixes)):
        raise ValueError("pdf_structure_options_invalid")
    # Options are data. Unknown options cannot silently change the replay method.
    if set(options) != {"blank_lines", "chapter_mode", "chapter_prefixes", "chapter_max_chars"}:
        raise ValueError("pdf_structure_options_invalid")
    document = request["document"]
    if not isinstance(document["text"], str):
        raise ValueError("pdf_structure_text_invalid")
    raw = document["text"].encode("utf-8", errors="strict")
    count = integer(document["pages"], 1)
    page_texts = raw.split(b"\f")
    # Current Poppler bridge exposes only a joined string. Reject ambiguous FFs,
    # rather than silently inventing a page number for an in-page form feed.
    if len(page_texts) != count:
        raise ValueError("pdf_structure_page_boundaries_ambiguous")
    missing = [i + 1 for i, text in enumerate(page_texts) if not text.strip(b" \t\r\n\v")]
    if (not isinstance(document["pages_without_text"], list) or
            any(type(n) is not int for n in document["pages_without_text"]) or
            missing != document["pages_without_text"]):
        raise ValueError("pdf_structure_blank_pages_mismatch")
    if len(missing) == count:
        raise ValueError("pdf_structure_no_text")
    # A blank line is a separator in derived text, not evidence of PDF layout.
    newline = rb"(?:\r\n|\n|\r(?!\n))"
    separator = (re.compile(newline + rb"[ \t]*(?:" + newline + rb"[ \t]*){" + str(blank_lines).encode() + rb",}")
                 if blank_lines <= len(raw) else None)
    pages, blocks, ranges = [], [], []
    offset = 0
    for number, page in enumerate(page_texts, 1):
        end = offset + len(page)
        pages.append({"page": number, "byte_range": [offset, end], "has_text": number not in missing})
        cuts = [m.end() for m in separator.finditer(page)] if separator else []
        # Poppler commonly preserves line breaks without empty lines. A pinned
        # prefix recognizes a standalone heading line, not the whole following
        # paragraph. Split at its byte boundaries; classification stays inferred.
        if options["chapter_mode"] == "prefix":
            line_start = 0
            for line in page.splitlines(keepends=True):
                title = line.strip(b" \t\r\n\v").decode("utf-8")
                if title and (not max_chars or len(title) <= max_chars) and any(title.startswith(p) for p in prefixes):
                    if line_start: cuts.append(line_start)
                    cuts.append(line_start + len(line))
                line_start += len(line)
        cuts = sorted(set(cuts + [len(page)]))
        begin = 0
        ordinal = 0
        for stop in cuts:
            chunk = page[begin:stop]
            left = len(chunk) - len(chunk.lstrip(b" \t\r\n\v"))
            right = len(chunk.rstrip(b" \t\r\n\v"))
            if right < left:
                left = right = 0
            value = chunk[left:right].decode("utf-8")
            kind = "paragraph" if value else "page_gap"
            if (value and options["chapter_mode"] == "prefix" and
                    not any(c in value for c in "\r\n") and
                    (not max_chars or len(value) <= max_chars) and
                    any(value.startswith(p) for p in prefixes)):
                kind = "heading"
            # Retain every separator byte in the genesis cut; quote ranges exclude it.
            finish = offset + stop
            if stop == len(page) and number < count:
                finish += 1
            if finish > offset + begin:
                locator = {"page": number, "block": ordinal,
                           "page_text_range": [begin + left, begin + right]}
                blocks.append({"kind": kind, "text": value, "locator": locator,
                               "structure_evidence": "inferred", "quote_evidence": "derived"})
                ranges.append({"byte_range": [offset + begin, finish],
                               "text_range": [offset + begin + left, offset + begin + right]})
                ordinal += 1
            begin = stop
        offset = end + (1 if number < count else 0)
    headings = [i for i, b in enumerate(blocks) if b["kind"] == "heading"]
    chapters = []
    for n, i in enumerate(headings):
        next_i = headings[n + 1] if n + 1 < len(headings) else len(blocks)
        chapters.append({"title": blocks[i]["text"], "heading_block": i,
                         "block_range": [i, next_i],
                         "byte_range": [ranges[i]["byte_range"][0],
                                        ranges[next_i - 1]["byte_range"][1]],
                         "evidence_class": "inferred", "method": "prefix"})
    return {"schema": "loom.pdf_structure.result/1", "extractor": "loom.pdf_structure/1",
            "source": {"sha256": source["sha256"], "size": source["size"]},
            "reader": {"adapter": reader["adapter"], "library": reader["library"]},
            "options": options, "partial": True,
            "warnings": ["pdf_text_layer_only", "paragraph_and_chapter_structure_inferred",
                         "page_breaks_do_not_establish_paragraph_continuation"],
            "text": document["text"], "page_count": count, "pages_without_text": missing,
            "pages": pages, "blocks": blocks, "chapters": chapters,
            "projection": {"schema": "loom.pdf_structure.projection/1",
                           "coordinate": "derived_utf8_bytes", "sha256": digest(raw),
                           "size": len(raw), "ranges": ranges}}


def replay(extraction, original_sha256, original_size):
    """Validate a saved receipt by recomputation, without opening the original."""
    integer(original_size)
    if extraction["source"] != {"sha256": original_sha256, "size": original_size}:
        raise ValueError("pdf_structure_source_mismatch")
    expected = project({"schema": "loom.pdf_structure.input/1", "source": extraction["source"],
                        "reader": extraction["reader"], "options": extraction["options"],
                        "document": {"text": extraction["text"], "pages": extraction["page_count"],
                                     "pages_without_text": extraction["pages_without_text"]}})
    # JSON identity distinguishes booleans, integers and floats (Python == does not).
    canonical = lambda obj: json.dumps(obj, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)
    if canonical(extraction) != canonical(expected):
        raise ValueError("pdf_structure_receipt_mismatch")
    return expected


def main():
    try:
        result = project(json.load(sys.stdin))
        json.dump(result, sys.stdout, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
        sys.stdout.write("\n")
    except (ValueError, TypeError, KeyError, UnicodeError, OverflowError):
        # No source text, paths or input values in diagnostics.
        sys.stderr.write("pdf_structure_invalid_input\n")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
