#!/usr/bin/env python3
"""Deterministic generator for the FICTIONAL blind catalog validation corpus
`loom/tests/fixtures/eval/blind_catalog_v2/`.

Persona "Witold Sowa" is wholly invented (hobbyist, Gdynia). No real people,
data or projects. Output: chatgpt_export.zip, claude_export.zip,
ground_truth.json. No wall clock, no randomness.

    python3 loom/tools/gen_blind_catalog_v2.py

The corpus content lives in this file (relevant / noise / traps sections),
followed by the export writers and the ground-truth builder.
"""
from __future__ import annotations

import datetime
import json
import zipfile
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "tests/fixtures/eval/blind_catalog_v2"

CATS = ["explicit_name", "architecture_unnamed", "inflected_alias", "paraphrase_only",
        "shared_foundation", "philosophy", "multi_topic_late", "near_duplicate"]
K, L, F = "proj.kotwica", "proj.latarnik", "proj.fakturka"
ST, PL, BUS = "found.storage", "found.plugins", "found.bus"

CONVS: list[dict] = []


def _add(kind, cid, prov, title, date, msgs, **kw):
    assert prov in ("chatgpt", "claude")
    assert all(c["id"] != cid for c in CONVS), cid
    assert len(msgs) >= 2, cid
    CONVS.append(dict(kind=kind, id=cid, provider=prov, title=title, date=date, msgs=msgs, **kw))


def R(cid, prov, title, date, cat, projects, msgs, principles=(), decisions=(), foundations=(), note=""):
    """Relevant conversation. msgs alternate user, assistant, user, ..."""
    assert cat in CATS
    _add("relevant", cid, prov, title, date, msgs, category=cat, projects=list(projects),
         principles=list(principles), decisions=list(decisions), foundations=list(foundations), note=note)


def N(cid, prov, title, date, topic, msgs):
    _add("noise", cid, prov, title, date, msgs, topic=topic)


def T(cid, prov, title, date, term, sense, collides_with, msgs):
    _add("trap", cid, prov, title, date, msgs, term=term, real_sense=sense, collides_with=collides_with)


def DUP(cid, base, prov, title, date, edits=(), extra=None, lower_first=False):
    b = next(c for c in CONVS if c["id"] == base)
    assert b["provider"] != prov
    msgs = list(b["msgs"])
    for old, new in edits:
        assert any(old in m for m in msgs), (cid, old)
        msgs = [m.replace(old, new) for m in msgs]
    if lower_first:
        msgs[0] = msgs[0][:1].lower() + msgs[0][1:]
    if extra:
        msgs.extend(extra)
    _add("relevant", cid, prov, title, date, msgs, category="near_duplicate",
         projects=list(b["projects"]), principles=list(b["principles"]),
         decisions=list(b["decisions"]), foundations=list(b["foundations"]),
         duplicate_of=base, note="re-export / edited version of " + base)


