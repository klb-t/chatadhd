#!/usr/bin/env python3
"""Generate the synthetic evaluation corpus for Loom's Archive Intelligence /
self-discovery pipeline (docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md
R1, R2, R4, R5, R10; docs/architecture/LOOM_CONCEPTUAL_MODEL.md).

Everything this script produces is about a FICTIONAL persona ("Ola Testowa")
and FICTIONAL projects. Nothing here is the real owner's data, words or
history: the corpus is structurally isomorphic to the kind of multi-project,
multi-year, bilingual (Polish/English) mess Loom must handle, with EXACT
ground truth recorded in the conceptual model's own terms so an eval harness
can score catalog/version/status/fork/principle/operator discovery without
guessing.

Output (all under loom/tests/fixtures/eval/synthetic_dev/, all small, all
committed):
  chatgpt_export.zip   -- a ChatGPT-style export (conversations.json)
  claude_export.zip    -- a Claude-style export (conversations.json,
                          projects.json, memories.json)
  ground_truth.json    -- exact ground truth in conceptual-model terms
  README.md            -- is hand-written, not touched by this script

Regenerate with:  python3 loom/tools/gen_synthetic_eval_corpus.py

Determinism: no wall-clock, no randomness anywhere in this script; the same
source produces byte-identical zips (zipfile timestamps are pinned) and an
identical ground_truth.json every run.
"""
from __future__ import annotations

import calendar
import datetime
import json
import pathlib
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]  # loom/
OUT = ROOT / "tests" / "fixtures" / "eval" / "synthetic_dev"

# The temporal holdout cut (LOOM_CONCEPTUAL_MODEL.md §7.1 / NOTATKA_GPT §9):
# induce principles/operators/predictions from everything dated <= T, then
# compare with what actually happens after T.
T_CUT = "2026-05-01"

PERSONA = {
    "name": "Ola Testowa",
    "fictional": True,
    "note": "Wholly invented for this corpus. Any resemblance to the repo "
            "owner's real projects, principles or history is intentional "
            "ONLY at the level of structure (multi-project, PL/EN, forks, "
            "reversed decisions, a philosophy of principles+operators) and "
            "never at the level of content, names or wording.",
    "git_author": "Ola Testowa <ola.testowa@example.invalid>",
}

# ── project ids ──────────────────────────────────────────────────────
P_NF = "proj.noteflow"       # NoteFlow: mobile note/chat app, Python -> C++
P_RT = "proj.reeltime"       # Reeltime: text -> storyboard -> keyframes -> clips -> compose
P_WD = "proj.watchdog"       # Coding-agent watchdog ("Stroz")
P_LK = "proj.lokatorka"      # Fictional tenancy dispute / legal-deadline tracker
P_MU = "proj.analogghosts"   # "Analog Ghosts" music side project

PROJECTS_META = {
    P_NF: {
        "name": "NoteFlow", "kind": "multiplatform_app",
        "aliases": ["NoteFlow", "appka od notatek", "note-app", "ta apka z notatkami i czatem", "notatnik"],
        "one_line": "Mobile note-taking + AI chat app; Python core rewritten to C++ (0.1 -> 1.2).",
    },
    P_RT: {
        "name": "Reeltime", "kind": "pipeline",
        "aliases": ["Reeltime", "generator reelsow", "ten pipeline do wideo", "reels-gen", "generator klipow"],
        "one_line": "Staged text -> storyboard -> keyframes -> clips -> compose reel generator; never finished.",
    },
    P_WD: {
        "name": "Stroz (Watchdog)", "kind": "agent_system",
        "aliases": ["Stroz", "Watchdog-skrypt", "ten pilnujacy skrypt", "CodeWatchdog", "stroz.py"],
        "one_line": "Watches a long-running coding-agent session; restarts on hang, summarises, alerts.",
    },
    P_LK: {
        "name": "Sprawa Kwiatowa (Lokatorka)", "kind": "legal_case",
        "aliases": ["sprawa z Kwiatowej", "sprawa najmu", "sprawa z Zenonem", "sprawa o kaucje", "Lokatorka-sprawa"],
        "one_line": "Fictional tenancy dispute over a deposit + mould damage; deadlines, evidence, one court filing.",
    },
    P_MU: {
        "name": "Analog Ghosts", "kind": "music",
        "aliases": ["Analog Ghosts", "plyta", "EP", "projekt muzyczny", "te nagrania"],
        "one_line": "Solo lo-fi/synth side project; a handful of tracks, an unfinished EP.",
    },
}

VALUES = ["truth", "autonomy", "transparency", "honesty", "preservation_of_information",
          "minimal_arbitrariness", "optionality", "momentum"]

# ── message / conversation authoring helpers ────────────────────────


def msg(nid, parent, role, text, tags=None):
    return {"nid": nid, "parent": parent, "role": role, "text": text, "tags": tags or {}}


def linear(*turns):
    """turns: (role, text) or (role, text, tags_dict). Builds a plain chain."""
    out = []
    prev = None
    ui = ai = 0
    for t in turns:
        role, text = t[0], t[1]
        tags = t[2] if len(t) > 2 else {}
        if role == "user":
            ui += 1
            nid = f"u{ui}"
        else:
            ai += 1
            nid = f"a{ai}"
        out.append(msg(nid, prev, role, text, tags))
        prev = nid
    return out


def fork_edit(prefix_turns, branch_a_turns, branch_b_turns, current="b"):
    """A conversation where, after `prefix_turns`, the user EDITS their next
    message: two sibling continuations from the same parent (ChatADHD's own
    'edit -> branch, old version kept' pattern, mirrored here at the export
    level). `current` picks which branch is the kept/current one ('a'|'b')."""
    msgs = linear(*prefix_turns)
    parent = msgs[-1]["nid"] if msgs else None
    ui = ai = 0
    for m in msgs:
        if m["role"] == "user":
            ui = max(ui, int(m["nid"][1:]))
        else:
            ai = max(ai, int(m["nid"][1:]))

    def branch(turns, suffix):
        nonlocal ui, ai
        out = []
        p = parent
        first = True
        u2, a2 = ui, ai
        for t in turns:
            role, text = t[0], t[1]
            tags = t[2] if len(t) > 2 else {}
            if role == "user":
                u2 += 1
                nid = f"u{u2}{suffix}"
            else:
                a2 += 1
                nid = f"a{a2}{suffix}"
            out.append(msg(nid, p, role, text, tags))
            p = nid
            first = False
        return out

    ba = branch(branch_a_turns, "a")
    bb = branch(branch_b_turns, "b")
    msgs += ba + bb
    current_nid = (ba[-1] if current == "a" else bb[-1])["nid"]
    return msgs, current_nid


def dt(date_str):
    return datetime.datetime.strptime(date_str, "%Y-%m-%dT%H:%M:%SZ")


def epoch(d: datetime.datetime) -> float:
    return float(calendar.timegm(d.timetuple()))


def iso(d: datetime.datetime) -> str:
    return d.strftime("%Y-%m-%dT%H:%M:%SZ")


def is_before_cut(date_str: str) -> bool:
    return date_str[:10] <= T_CUT


# ── Principles (Ola's philosophy) ───────────────────────────────────
# Deliberately PARTLY different from loom/data/philosophy/seed_principles.json
# (the real owner's seeds): some independently convergent (same idea, her own
# phrasing/scope), some genuinely in tension with a seed principle
# (conflicts_with references the seed id directly), some with no seed analog.
PRINCIPLES = [
    {"id": "pr.optionality_over_speed", "level": "value", "form": "invariant",
     "statement": {"pl": "Zachowanie opcjonalności jest ważniejsze niż tymczasowa szybkość",
                   "en": "Preserving future options matters more than short-term speed"},
     "protects": ["optionality"], "scope": "wszystkie projekty", "conflicts_with": [],
     "derived_from": [], "validation_status": "supported",
     "note": "Independently convergent with the seed pack's general 'preservation of optionality' "
             "value (NOTATKA §4); Ola's own phrasing and evidence base."},
    {"id": "pr.honesty_over_comfort", "level": "value", "form": "invariant",
     "statement": {"pl": "Mów wprost, czego nie wiem albo czego nie zrobiłam, zamiast ładnie to opakować",
                   "en": "Say plainly what is unknown or undone, instead of dressing it up"},
     "protects": ["honesty", "transparency"], "scope": "wszystkie projekty", "conflicts_with": [],
     "derived_from": [], "validation_status": "supported", "note": ""},
    {"id": "pr.keep_both_branches", "level": "epistemic", "form": "invariant",
     "statement": {"pl": "Nie kasuję porzuconej gałęzi ani opcji — zostawiam ją z opisem, czemu odpadła",
                   "en": "Never delete an abandoned branch or option; keep it labelled with why it lost"},
     "protects": ["preservation_of_information", "optionality"], "scope": "wszystkie projekty",
     "conflicts_with": [], "derived_from": [], "validation_status": "confirmed",
     "note": "Overlaps in spirit with seed p.abandoned_not_unimportant, independently phrased/evidenced."},
    {"id": "pr.data_over_branches", "level": "strategy", "form": "invariant",
     "statement": {"pl": "Presety i konfiguracje to dane, nie nowe gałęzie kodu — ale algorytmy i UI zostają w kodzie",
                   "en": "Presets and configuration are data, not new code branches — but algorithms and UI stay in code"},
     "protects": ["minimal_arbitrariness"], "scope": "presety, config, listy providerów",
     "conflicts_with": [], "derived_from": [], "validation_status": "supported",
     "note": "Same core idea as seed p.kod_ne_dane, but Ola scopes it much narrower (explicitly NOT "
             "'wszystko dane' — see cross-project conv cx-05) and reached it independently."},
    {"id": "pr.write_down_the_blocker", "level": "epistemic", "form": "heuristic",
     "statement": {"pl": "Kiedy utknę na ponad 20 minut, zapisuję dokładnie na czym utknęłam, zanim przełączę zadanie",
                   "en": "When stuck for more than 20 minutes, write down the exact blocker before switching tasks"},
     "protects": ["preservation_of_information"], "scope": "praca własna", "conflicts_with": [],
     "derived_from": [], "validation_status": "candidate", "note": "No seed analog."},
    {"id": "pr.counterexample_hunting", "level": "epistemic", "form": "heuristic",
     "statement": {"pl": "Zanim uznam coś za zasadę, szukam kontrprzykładu z ostatnich dwóch tygodni",
                   "en": "Before promoting something to a rule, hunt for a counterexample from the last two weeks"},
     "protects": ["truth"], "scope": "tworzenie zasad", "conflicts_with": [],
     "derived_from": [], "validation_status": "candidate", "note": ""},
    {"id": "pr.provider_not_special_case", "level": "strategy", "form": "heuristic",
     "statement": {"pl": "Nowe API to nowy adapter za tym samym interfejsem, nie nowy if",
                   "en": "A new API is a new adapter behind the same interface, not a new if-branch"},
     "protects": ["minimal_arbitrariness", "optionality"], "scope": "integracje zewnętrzne",
     "conflicts_with": [], "derived_from": ["pr.data_over_branches"], "validation_status": "confirmed",
     "note": "Narrower, independently-evidenced version of seed p.capability_not_enum."},
    {"id": "pr.momentum_over_perfection", "level": "value", "form": "default",
     "statement": {"pl": "Utrzymanie tempa jest domyślnie ważniejsze niż idealna architektura na starcie",
                   "en": "Keeping momentum defaults to mattering more than a perfect architecture up front"},
     "protects": ["momentum"], "scope": "wczesne fazy projektu", "conflicts_with": [],
     "derived_from": [], "validation_status": "supported", "note": "No seed analog."},
    {"id": "pr.decide_fast_correct_later", "level": "strategy", "form": "default",
     "statement": {"pl": "Decyduj szybko na podstawie tego, co wiem teraz; koryguj, gdy pojawią się nowe dane — nie czekaj na pewność",
                   "en": "Decide fast on what you know now; correct later when new data arrives — do not wait for certainty"},
     "protects": ["momentum"], "scope": "decyzje techniczne o niskim koszcie cofnięcia",
     "conflicts_with": ["p.defer_decisions"], "derived_from": [], "validation_status": "supported",
     "note": "Deliberately in TENSION with the seed pack's p.defer_decisions ('nie podejmujemy decyzji, "
             "jeśli nie musimy'): Ola's own default is closer to the opposite bias. See pr.owner_confirmation_for_money "
             "for how she resolves this against optionality when the cost of being wrong rises."},
    {"id": "pr.small_reversible_steps", "level": "strategy", "form": "default",
     "statement": {"pl": "Domyślnie rób mały odwracalny krok zamiast dużej nieodwracalnej zmiany",
                   "en": "Default to a small reversible step over a big irreversible change"},
     "protects": ["optionality"], "scope": "wszystkie projekty", "conflicts_with": [],
     "derived_from": ["pr.optionality_over_speed"], "validation_status": "supported", "note": ""},
    {"id": "pr.owner_confirmation_for_money", "level": "strategy", "form": "conflict_resolution",
     "statement": {"pl": "Gdy koszt (pieniądze, czas, nieodwracalność) rośnie, zawsze pytam / zapisuję zgodę, zanim system działa dalej",
                   "en": "When cost (money, time, irreversibility) rises, always ask / record explicit confirmation before proceeding"},
     "protects": ["optionality", "autonomy"], "scope": "operacje kosztowne lub nieodwracalne",
     "conflicts_with": [], "derived_from": ["pr.optionality_over_speed"], "validation_status": "confirmed",
     "note": "Resolves the tension between pr.decide_fast_correct_later and pr.optionality_over_speed: "
             "decide fast UNTIL the cost of being wrong crosses a threshold, then gate."},
    {"id": "pr.two_masters_conflict", "level": "strategy", "form": "conflict_resolution",
     "statement": {"pl": "Kiedy dwie wartości się kłócą, wygrywa ta opcja, którą łatwiej cofnąć",
                   "en": "When two values conflict, the option that is easier to reverse wins"},
     "protects": ["optionality"], "scope": "wszystkie projekty",
     "conflicts_with": [], "derived_from": ["pr.owner_confirmation_for_money", "pr.optionality_over_speed"],
     "validation_status": "supported", "note": "Generalises pr.owner_confirmation_for_money beyond money."},
    {"id": "pr.meta_two_examples_rule", "level": "strategy", "form": "meta",
     "statement": {"pl": "Zapisuję coś jako 'zasadę' dopiero, gdy widzę ten sam wzorzec w co najmniej dwóch różnych projektach",
                   "en": "Something only becomes a written 'principle' once the same pattern shows up in at least two different projects"},
     "protects": ["minimal_arbitrariness"], "scope": "tworzenie zasad", "conflicts_with": [],
     "derived_from": [], "validation_status": "confirmed",
     "note": "Ola's own meta-principle for how principles form; by construction most entries here are "
             "evidenced across >=2 projects, which is itself a predicted consequence of this rule."},
]
PRINCIPLE_IDS = {p["id"] for p in PRINCIPLES}

# ── Transformation operators ─────────────────────────────────────────
OPERATORS = [
    {"id": "op.new_source_new_adapter",
     "situation": "A new external data/API source appears that isn't behind an existing adapter.",
     "solution": "Extend the provider/capability abstraction; add an adapter behind the same interface, "
                 "never a hard-coded special case.",
     "principles": ["pr.provider_not_special_case", "pr.data_over_branches"],
     "pre_T_example": "dec.nf.transcription", "post_T_example": "dec.nf.transcription_fallback"},
    {"id": "op.new_artifact_shared_structure",
     "situation": "A new artifact/content type appears that doesn't fit the current model.",
     "solution": "Find the common structure across existing types; keep the type-specific bits as data "
                 "fields on the shared shape, not a new class or code path.",
     "principles": ["pr.data_over_branches"],
     "pre_T_example": "dec.nf.checklist_as_data", "post_T_example": "dec.rt.shared_artifact_model"},
    {"id": "op.observe_before_automate",
     "situation": "Ola notices she is repeating a manual chore.",
     "solution": "First make it observable (log it, timestamp it, keep the manual record), only then automate it.",
     "principles": ["pr.write_down_the_blocker"],
     "pre_T_example": "dec.wd.build", "post_T_example": "dec.lk.auto_deadlines"},
    {"id": "op.uncertainty_keep_alternatives",
     "situation": "Facing genuine uncertainty between >=2 options with no forcing evidence yet.",
     "solution": "Do not choose arbitrarily; keep both options recorded with their evidence, and run a "
                 "small, cheap, reversible probe instead of committing.",
     "principles": ["pr.small_reversible_steps", "pr.two_masters_conflict"],
     "pre_T_example": "dec.mu.arrangement_key_open", "post_T_example": "dec.mu.arrangement_key_final"},
    {"id": "op.impl_detail_not_propagated",
     "situation": "An implementation detail changes but the externally visible contract/behaviour does not.",
     "solution": "Do not propagate the change to architecture docs or decisions; it is invisible at that layer.",
     "principles": ["pr.data_over_branches"],
     "pre_T_example": "dec.nf.diff_algo_swap", "post_T_example": "dec.wd.io_swap"},
    {"id": "op.cost_gate_on_irreversibility",
     "situation": "The cost (money/time) or irreversibility of the next step increases sharply.",
     "solution": "Gate the step behind an explicit estimate and an explicit confirmation before proceeding.",
     "principles": ["pr.owner_confirmation_for_money"],
     "pre_T_example": "dec.nf.upload_gate", "post_T_example": "dec.rt.clips_gate"},
]
OPERATOR_IDS = {o["id"] for o in OPERATORS}

# A control: a post-T decision that is deliberately NOT predictable from any
# operator above (a negative control so the benchmark isn't gameable by
# "every post-T decision must match some operator").
UNPREDICTABLE_POST_T_DECISIONS = [
    {"decision_id": "dec.nf.encryption",
     "why_not_predictable": "The specific cipher choice (ChaCha20-Poly1305 over AES-256-GCM) turns on a "
                            "contingent fact (old-phone AES-NI performance she read about once), not on any "
                            "of Ola's recorded operators or principles. Only the FACT that encryption would "
                            "eventually get an adapter-shaped implementation was predictable "
                            "(op.new_source_new_adapter); the specific algorithm was not."},
]

# ── Decisions (project-scoped) ───────────────────────────────────────
# affected_values: list of {value, sign, rationale}; sign in {+,-,0}
DECISIONS = {
    "dec.nf.storage": {
        "project": P_NF, "date": "2025-01-18",
        "alternatives": ["SQLite", "Realm", "flat JSON files"], "chosen": "SQLite",
        "affected_values": [{"value": "preservation_of_information", "sign": "+", "rationale": "structured, queryable, crash-safe"},
                             {"value": "autonomy", "sign": "+", "rationale": "no vendor lock-in"},
                             {"value": "minimal_arbitrariness", "sign": "0", "rationale": ""}],
        "principle_evidence": ["pr.optionality_over_speed"], "supersedes": None, "operator": None},
    "dec.nf.sync_v1": {
        "project": P_NF, "date": "2025-03-01",
        "alternatives": ["custom binary protocol", "CRDT over WebRTC", "git-based sync"], "chosen": "git-based sync",
        "affected_values": [{"value": "momentum", "sign": "+", "rationale": "reuse git instead of writing a protocol"}],
        "principle_evidence": ["pr.momentum_over_perfection"], "supersedes": None, "operator": None},
    "dec.nf.transcription": {
        "project": P_NF, "date": "2025-04-02",
        "alternatives": ["on-device model", "cloud Whisper-style API", "Google Speech"], "chosen": "cloud Whisper-style API",
        "affected_values": [{"value": "momentum", "sign": "+", "rationale": "fastest to ship"}],
        "principle_evidence": ["pr.provider_not_special_case"], "supersedes": None,
        "operator": "op.new_source_new_adapter"},
    "dec.nf.checklist_as_data": {
        "project": P_NF, "date": "2025-05-10",
        "alternatives": ["new 'checklist' entity/class", "checklist as a data-shaped variant of the note entity"],
        "chosen": "checklist as a data-shaped variant of the note entity",
        "affected_values": [{"value": "minimal_arbitrariness", "sign": "+", "rationale": "one entity, one code path"}],
        "principle_evidence": ["pr.data_over_branches"], "supersedes": None,
        "operator": "op.new_artifact_shared_structure"},
    "dec.nf.encryption_defer": {
        "project": P_NF, "date": "2025-05-10",
        "alternatives": ["roll-your-own AES now", "libsodium-style AEAD now", "defer, add later as a layer over storage"],
        "chosen": "defer, add later as a layer over storage",
        "affected_values": [{"value": "momentum", "sign": "+", "rationale": "not core for MVP"}],
        "principle_evidence": ["pr.small_reversible_steps"], "supersedes": None, "operator": None},
    "dec.nf.core_language": {
        "project": P_NF, "date": "2025-08-10",
        "alternatives": ["Kotlin Multiplatform", "C++ core", "Rust core", "(status quo) keep patching Python"],
        "chosen": "C++ core",
        "affected_values": [{"value": "autonomy", "sign": "+", "rationale": "one core, every platform"},
                             {"value": "minimal_arbitrariness", "sign": "-", "rationale": "C++ ABI story wasn't proven yet when she committed"}],
        "principle_evidence": ["pr.decide_fast_correct_later"], "supersedes": None,
        "supersedes_branch": "python-quick", "operator": None},
    "dec.nf.diff_algo_swap": {
        "project": P_NF, "date": "2025-08-25",
        "alternatives": ["keep hand-rolled diff", "library-based diff (Myers-style)"], "chosen": "library-based diff",
        "affected_values": [{"value": "momentum", "sign": "+", "rationale": "less code to maintain"}],
        "principle_evidence": [], "supersedes": None, "operator": "op.impl_detail_not_propagated",
        "implementation_detail_not_propagated": True},
    "dec.nf.upload_gate": {
        "project": P_NF, "date": "2025-10-10",
        "alternatives": ["keep auto-uploading everything", "gate large uploads behind an estimate + confirmation"],
        "chosen": "gate large uploads behind an estimate + confirmation",
        "affected_values": [{"value": "autonomy", "sign": "+", "rationale": "no surprise bills"}],
        "principle_evidence": ["pr.owner_confirmation_for_money"], "supersedes": None,
        "operator": "op.cost_gate_on_irreversibility"},
    "dec.nf.sync_v2": {
        "project": P_NF, "date": "2025-12-01",
        "alternatives": ["keep optimising git-based sync", "REST-based custom sync", "third-party sync SDK"],
        "chosen": "REST-based custom sync",
        "affected_values": [{"value": "momentum", "sign": "+", "rationale": "large attachments were unusably slow over git-based sync"}],
        "principle_evidence": [], "supersedes": "dec.nf.sync_v1", "operator": None},
    "dec.nf.transcription_fallback": {
        "project": P_NF, "date": "2026-05-20",
        "alternatives": ["cloud-only, accept offline gaps", "on-device fallback behind the same provider interface", "drop the feature"],
        "chosen": "on-device fallback behind the same provider interface",
        "affected_values": [{"value": "optionality", "sign": "+", "rationale": "works with no signal"}],
        "principle_evidence": ["pr.provider_not_special_case"], "supersedes": None,
        "operator": "op.new_source_new_adapter"},
    "dec.nf.encryption": {
        "project": P_NF, "date": "2026-07-01",
        "alternatives": ["AES-256-GCM", "ChaCha20-Poly1305", "OS-keychain-only, no app-level crypto"],
        "chosen": "ChaCha20-Poly1305",
        "affected_values": [{"value": "autonomy", "sign": "+", "rationale": "faster on older phones without AES-NI"}],
        "principle_evidence": ["pr.provider_not_special_case"], "supersedes": None, "operator": None,
        "note_for_gt": "Deliberately NOT the same cipher the real ChatADHD/Loom chose (AES-256-GCM) -- see "
                       "UNPREDICTABLE_POST_T_DECISIONS."},
    "dec.rt.clips_gate": {
        "project": P_RT, "date": "2026-06-10",
        "alternatives": ["render clips freely, watch the bill", "gate clips behind a cost estimate + explicit go-ahead"],
        "chosen": "gate clips behind a cost estimate + explicit go-ahead",
        "affected_values": [{"value": "optionality", "sign": "+", "rationale": "no runaway GPU bill"}],
        "principle_evidence": ["pr.owner_confirmation_for_money"], "supersedes": None,
        "operator": "op.cost_gate_on_irreversibility", "resolves_open_question": "oq.rt.clips_cost"},
    "dec.rt.shared_artifact_model": {
        "project": P_RT, "date": "2026-06-25",
        "alternatives": ["separate keyframe/clip/compose classes", "one 'stage artifact' record, fields as data"],
        "chosen": "one 'stage artifact' record, fields as data",
        "affected_values": [{"value": "minimal_arbitrariness", "sign": "+", "rationale": ""}],
        "principle_evidence": ["pr.data_over_branches"], "supersedes": None,
        "operator": "op.new_artifact_shared_structure"},
    "dec.wd.build": {
        "project": P_WD, "date": "2025-11-08",
        "alternatives": ["keep restarting by hand", "build a watcher/restarter/summariser script"],
        "chosen": "build a watcher/restarter/summariser script",
        "affected_values": [{"value": "momentum", "sign": "+", "rationale": "stop losing sleep over it"}],
        "principle_evidence": ["pr.write_down_the_blocker"], "supersedes": None,
        "operator": "op.observe_before_automate"},
    "dec.wd.io_swap": {
        "project": P_WD, "date": "2026-06-01",
        "alternatives": ["keep polling the log file", "switch to inotify"], "chosen": "switch to inotify",
        "affected_values": [{"value": "momentum", "sign": "+", "rationale": "lower latency, less CPU"}],
        "principle_evidence": [], "supersedes": None, "operator": "op.impl_detail_not_propagated",
        "implementation_detail_not_propagated": True},
    "dec.lk.strategy": {
        "project": P_LK, "date": "2026-01-10",
        "alternatives": ["wait and negotiate informally", "send a formal written demand citing KL art. 12", "go straight to court"],
        "chosen": "send a formal written demand citing KL art. 12, escalate only if ignored 14+ days",
        "affected_values": [{"value": "transparency", "sign": "+", "rationale": ""}, {"value": "autonomy", "sign": "+", "rationale": "cheaper than a lawyer up front"}],
        "principle_evidence": [], "supersedes": None, "operator": None},
    "dec.lk.court_filing": {
        "project": P_LK, "date": "2026-03-10",
        "alternatives": ["send a second written reminder", "file in court"], "chosen": "file in court",
        "affected_values": [{"value": "autonomy", "sign": "+", "rationale": "the 14-day window was ignored"}],
        "principle_evidence": [], "supersedes": "dec.lk.strategy", "operator": None},
    "dec.lk.auto_deadlines": {
        "project": P_LK, "date": "2026-06-15",
        "alternatives": ["keep checking the spreadsheet every morning", "compute deadlines from dated events + auto-remind"],
        "chosen": "compute deadlines from dated events + auto-remind",
        "affected_values": [{"value": "preservation_of_information", "sign": "+", "rationale": "no more relying on memory"}],
        "principle_evidence": ["pr.write_down_the_blocker"], "supersedes": None,
        "operator": "op.observe_before_automate"},
    "dec.mu.arrangement_key_open": {
        "project": P_MU, "date": "2026-02-20",
        "alternatives": ["C minor", "D minor"], "chosen": None,
        "affected_values": [{"value": "optionality", "sign": "+", "rationale": "record both as demos, decide later"}],
        "principle_evidence": ["pr.small_reversible_steps"], "supersedes": None,
        "operator": "op.uncertainty_keep_alternatives", "kept_open": True},
    "dec.mu.arrangement_key_final": {
        "project": P_MU, "date": "2026-06-01",
        "alternatives": ["C minor", "D minor"], "chosen": "D minor",
        "affected_values": [{"value": "optionality", "sign": "0", "rationale": "resolved once both demos existed"}],
        "principle_evidence": [], "supersedes": None, "operator": "op.uncertainty_keep_alternatives"},
    "dec.mu.recording_medium": {
        "project": P_MU, "date": "2026-03-01",
        "alternatives": ["straight to tape (hard to undo, warmer)", "digital multitrack (easy to undo, cleaner)"],
        "chosen": "digital multitrack",
        "affected_values": [{"value": "optionality", "sign": "+", "rationale": "mistakes stay fixable"}],
        "principle_evidence": ["pr.two_masters_conflict"], "supersedes": None, "operator": None},
}
for _id, _d in DECISIONS.items():
    _d["id"] = _id
DECISION_IDS = set(DECISIONS)

# ── Features / status-per-branch-version (with oscillation) ─────────
# events: list of {version, branch, status, date, note}
FEATURES = {
    "feat.nf.voice_transcription": {"project": P_NF, "label": "voice memo transcription", "events": [
        {"version": "0.4.0", "branch": "main", "status": "implemented", "date": "2025-04-02", "note": "cloud Whisper-style API"},
        {"version": "0.9.0", "branch": "main", "status": "lost", "date": "2025-10-10", "note": "dropped silently during the C++ port"},
        {"version": "1.1.0", "branch": "main", "status": "restored", "date": "2026-05-20", "note": "restored + on-device fallback added"},
    ]},
    "feat.nf.checklist_notes": {"project": P_NF, "label": "checklist note type", "events": [
        {"version": "0.5.0", "branch": "main", "status": "implemented", "date": "2025-05-10", "note": "data-shaped variant of the note entity"},
        {"version": "0.9.0", "branch": "main", "status": "lost", "date": "2025-10-10", "note": "dropped during the C++ port, same sweep as transcription"},
        {"version": "1.0.0", "branch": "main", "status": "restored", "date": "2025-12-01", "note": ""},
        {"version": "1.2.0", "branch": "main", "status": "lost", "date": "2026-07-01", "note": "an unrelated UI refactor dropped it again; still lost at the end of this corpus"},
    ]},
    "feat.nf.sync_git": {"project": P_NF, "label": "sync (git-based transport)", "events": [
        {"version": "0.2.0", "branch": "main", "status": "planned", "date": "2025-02-05", "note": ""},
        {"version": "0.3.0", "branch": "main", "status": "implemented", "date": "2025-03-01", "note": ""},
        {"version": "1.0.0", "branch": "main", "status": "superseded", "date": "2025-12-01", "note": "replaced by feat.nf.sync_rest"},
    ]},
    "feat.nf.sync_rest": {"project": P_NF, "label": "sync (REST transport)", "events": [
        {"version": "1.0.0", "branch": "main", "status": "implemented", "date": "2025-12-01", "note": "supersedes feat.nf.sync_git"},
    ]},
    "feat.nf.encryption": {"project": P_NF, "label": "at-rest encryption", "events": [
        {"version": "0.2.0", "branch": "main", "status": "planned", "date": "2025-02-05", "note": ""},
        {"version": "0.5.0", "branch": "main", "status": "abandoned", "date": "2025-05-10", "note": "deferred for MVP, see dec.nf.encryption_defer"},
        {"version": "1.2.0", "branch": "main", "status": "implemented", "date": "2026-07-01", "note": "ChaCha20-Poly1305, see dec.nf.encryption"},
    ]},
    "feat.nf.graph_memory": {"project": P_NF, "label": "graph memory (v1)", "events": [
        {"version": "0.8.0", "branch": "main", "status": "partial", "date": "2025-09-15", "note": ""},
        {"version": "1.0.0", "branch": "main", "status": "superseded", "date": "2025-12-01", "note": "replaced by feat.nf.knowledge_graph_v2"},
    ]},
    "feat.nf.knowledge_graph_v2": {"project": P_NF, "label": "knowledge graph v2", "events": [
        {"version": "1.0.0", "branch": "main", "status": "implemented", "date": "2025-12-01", "note": "supersedes feat.nf.graph_memory"},
    ]},
    "feat.nf.upload_gate": {"project": P_NF, "label": "confirm-before-large-upload gate", "events": [
        {"version": "0.9.0", "branch": "main", "status": "implemented", "date": "2025-10-10", "note": "see dec.nf.upload_gate"},
    ]},
    "feat.rt.storyboard": {"project": P_RT, "label": "storyboard stage", "events": [
        {"version": "0.1", "branch": "main", "status": "implemented", "date": "2025-09-20", "note": "text -> storyboard"},
    ]},
    "feat.rt.keyframes": {"project": P_RT, "label": "keyframes stage", "events": [
        {"version": "0.2", "branch": "main", "status": "partial", "date": "2025-10-05", "note": "only one image-gen provider wired"},
    ]},
    "feat.rt.clips": {"project": P_RT, "label": "clips stage", "events": [
        {"version": "0.2", "branch": "main", "status": "planned", "date": "2025-10-12", "note": "cost worry raised, see oq.rt.clips_cost"},
        {"version": "0.3", "branch": "main", "status": "implemented", "date": "2026-06-10", "note": "implemented WITH the cost gate, see dec.rt.clips_gate"},
    ]},
    "feat.rt.compose": {"project": P_RT, "label": "compose/edit stage", "events": [
        {"version": "0.1", "branch": "main", "status": "planned", "date": "2025-09-05", "note": "never reached"},
    ]},
    "feat.wd.watcher_restart": {"project": P_WD, "label": "watcher + restarter", "events": [
        {"version": "0.1", "branch": "main", "status": "implemented", "date": "2025-11-10", "note": ""},
    ]},
    "feat.wd.summarizer": {"project": P_WD, "label": "session summariser on restart", "events": [
        {"version": "0.2", "branch": "main", "status": "implemented", "date": "2025-12-05", "note": ""},
    ]},
    "feat.wd.desktop_notify": {"project": P_WD, "label": "desktop notification channel", "events": [
        {"version": "0.3", "branch": "main", "status": "implemented", "date": "2026-01-20", "note": "see area.watchdog.alerts"},
    ]},
    "feat.lk.deposit_claim": {"project": P_LK, "label": "deposit-return claim (proceeding)", "events": [
        {"version": "n/a", "branch": "main", "status": "planned", "date": "2026-01-10", "note": "see dec.lk.strategy"},
        {"version": "n/a", "branch": "main", "status": "implemented", "date": "2026-03-10", "note": "filed in court, see dec.lk.court_filing"},
    ]},
    "feat.mu.slow_fracture": {"project": P_MU, "label": "track: 'Slow Fracture'", "events": [
        {"version": "n/a", "branch": "main", "status": "planned", "date": "2026-02-08", "note": "sketch only"},
        {"version": "n/a", "branch": "main", "status": "abandoned", "date": "2026-04-10", "note": "idea doesn't hold up; demo kept, not deleted"},
    ]},
    "feat.mu.paper_weather": {"project": P_MU, "label": "track: 'Paper Weather'", "events": [
        {"version": "n/a", "branch": "main", "status": "implemented", "date": "2026-07-10", "note": "one of two finished tracks"},
    ]},
}
for _id, _f in FEATURES.items():
    _f["id"] = _id
FEATURE_IDS = set(FEATURES)

# ── Versions + lineage (NoteFlow has the fork; others are simple chains) ──
VERSIONS = {
    P_NF: {
        "forks": [{
            "id": "fork.nf.core_rewrite", "base_version": "0.6.0", "base_date": "2025-06-20",
            "branches": [
                {"name": "python-quick", "from_version": "0.6.0", "outcome": "abandoned",
                 "versions": ["0.6.1", "0.6.2", "0.6.3"]},
                {"name": "cpp-core", "from_version": "0.6.0", "outcome": "became main",
                 "versions": ["0.7.0", "0.7.1", "0.8.0", "0.9.0", "1.0.0", "1.0.1", "1.0.2",
                              "1.1.0", "1.2.0"]},
            ],
            "resolved_by_decision": "dec.nf.core_language",
        }],
        "versions": [
            {"version": "0.1.0", "date": "2025-01-10", "branch": "main", "source": "chat+repo"},
            {"version": "0.1.2", "date": "2025-01-18", "branch": "main", "source": "chat+repo"},
            {"version": "0.2.0", "date": "2025-02-05", "branch": "main", "source": "chat+repo"},
            {"version": "0.3.0", "date": "2025-03-01", "branch": "main", "source": "chat+repo"},
            {"version": "0.4.0", "date": "2025-04-02", "branch": "main", "source": "chat+repo"},
            {"version": "0.5.0", "date": "2025-05-10", "branch": "main", "source": "chat+repo"},
            {"version": "0.6.0", "date": "2025-06-20", "branch": "main", "source": "chat+repo", "note": "fork point"},
            {"version": "0.6.1", "date": "2025-07-01", "branch": "python-quick", "source": "chat+repo"},
            {"version": "0.6.2", "date": "2025-07-15", "branch": "python-quick", "source": "chat+repo"},
            {"version": "0.6.3", "date": "2025-08-01", "branch": "python-quick", "source": "repo",
               "note": "branch stops here, never merged; last mention in chat is 0.6.2"},
            {"version": "0.7.0", "date": "2025-08-10", "branch": "cpp-core", "source": "chat+repo"},
            {"version": "0.7.1", "date": "2025-08-25", "branch": "main", "source": "chat+repo"},
            {"version": "0.8.0", "date": "2025-09-15", "branch": "main", "source": "chat+repo"},
            {"version": "0.9.0", "date": "2025-10-10", "branch": "main", "source": "chat+repo"},
            {"version": "1.0.0", "date": "2025-12-01", "branch": "main", "source": "chat+repo"},
            {"version": "1.0.1", "date": "2026-01-12", "branch": "main", "source": "repo",
               "note": "bugfix only; EXISTS ONLY in git history, not mentioned in any conversation -- "
                       "a deliberate test of code-lineage-only evidence"},
            {"version": "1.0.2", "date": "2026-02-20", "branch": "main", "source": "repo", "note": "bugfix only, repo-only"},
            {"version": "1.1.0", "date": "2026-05-20", "branch": "main", "source": "chat+repo"},
            {"version": "1.2.0", "date": "2026-07-01", "branch": "main", "source": "chat+repo"},
        ],
    },
    P_RT: {"forks": [], "versions": [
        {"version": "0.1", "date": "2025-09-20", "branch": "main", "source": "chat"},
        {"version": "0.2", "date": "2025-10-05", "branch": "main", "source": "chat"},
        {"version": "0.3", "date": "2026-06-10", "branch": "main", "source": "chat"},
    ]},
    P_WD: {"forks": [], "versions": [
        {"version": "0.1", "date": "2025-11-10", "branch": "main", "source": "chat"},
        {"version": "0.2", "date": "2025-12-05", "branch": "main", "source": "chat"},
        {"version": "0.3", "date": "2026-01-20", "branch": "main", "source": "chat"},
    ]},
    P_LK: {"forks": [], "versions": []},
    P_MU: {"forks": [], "versions": []},
}

# ── Areas (brainstorm generalizations) ───────────────────────────────
AREAS = [
    {"id": "area.noteflow.storage", "project": P_NF,
     "generalization_pl": "wszystko o storage: SQLite dla danych strukturalnych, pliki blob dla załączników, nic w chmurze bez zgody użytkownika",
     "generalization_en": "everything about storage: SQLite for structured data, blob files for attachments, nothing in the cloud without user consent",
     "listed_members": ["SQLite for structured data", "blob files for attachments"],
     "inferable_members": [{"value": "backup/export follows the same local-first-by-default rule",
                            "expected_property": "consistent_with(pr.optionality_over_speed) & requires_explicit_opt_in(remote_destination)",
                            "inference_basis": "the generalization's 'nic w chmurze bez zgody' clause applies to any future "
                                                "storage-adjacent feature, not only the two listed members"}]},
    {"id": "area.reeltime.stages", "project": P_RT,
     "generalization_pl": "wszystko o etapach pipeline'u: storyboard z tekstu, potem keyframe'y, potem klipy, na końcu compose — każdy etap droższy niż poprzedni, więc tanie najpierw, drogie na końcu, i pytamy zanim odpalimy drogi etap",
     "generalization_en": "everything about the pipeline stages: storyboard from text, then keyframes, then clips, "
                          "then compose -- each stage pricier than the last, so cheap first, expensive last, and "
                          "we ask before firing the expensive one",
     "listed_members": ["storyboard", "keyframes", "clips", "compose"],
     "inferable_members": [{"value": "cache/skip already-rendered keyframes to avoid recomputation",
                            "expected_property": "consistent_with(pr.owner_confirmation_for_money) & in_class(operation, idempotent)",
                            "inference_basis": "a direct corollary of 'cheap first' plus pr.optionality_over_speed: paying twice "
                                                "for the same keyframe has no upside"}]},
    {"id": "area.watchdog.alerts", "project": P_WD,
     "generalization_pl": "wszystko co dotyczy alertów: powiadomienie na pulpit, wpis do logu, opcjonalnie mail — zawsze najpierw lokalnie",
     "generalization_en": "everything about alerts: desktop notification, a log entry, optionally e-mail -- always local-first",
     "listed_members": ["desktop notification", "log entry", "e-mail (optional)"],
     "inferable_members": [{"value": "a push notification to her phone as a further optional channel",
                            "expected_property": "optional & local_first_fallback_required",
                            "inference_basis": "'opcjonalnie mail' establishes a pattern of optional secondary channels behind "
                                                "the local-first default; a phone push is the same shape"}]},
    {"id": "area.lokatorka.deadlines", "project": P_LK,
     "generalization_pl": "wszystko co dotyczy terminów: liczone od dostarczenia pisma, zawsze plus dwa dni zapasu, zawsze zapisana podstawa prawna",
     "generalization_en": "everything about deadlines: counted from delivery of the letter, always with a "
                          "2-day buffer, always with the statute basis recorded",
     "listed_members": ["counted from delivery date", "2-day buffer", "statute basis recorded"],
     "inferable_members": [{"value": "every deadline gets a reminder a few days before it is due",
                            "expected_property": "has_derived_property(reminder_offset) & consistent_with(area.watchdog.alerts)",
                            "inference_basis": "cross-project analogy (morphism transfer, depth 1): Watchdog's alerts area "
                                                "already establishes 'always alert ahead of a bad outcome'; the deadline-handling "
                                                "generalization implies the same for legal deadlines even though no brainstorm "
                                                "item says 'reminder' explicitly here"}]},
    {"id": "area.music.sound_palette", "project": P_MU,
     "generalization_pl": "wszystko o brzmieniu: ciepłe analogowe, dużo tape saturation, bez zbyt cyfrowego pogłosu",
     "generalization_en": "everything about the sound: warm analog, lots of tape saturation, not too digital reverb",
     "listed_members": ["warm analog", "tape saturation"],
     "inferable_members": [{"value": "a tape-delay style effect rather than a clean digital delay",
                            "expected_property": "consistent_with(area.music.sound_palette statement)",
                            "inference_basis": "a delay effect is unlisted but must satisfy 'ciepłe analogowe, bez cyfrowego "
                                                "pogłosu' the same way reverb does"}]},
]
AREA_IDS = {a["id"] for a in AREAS}

# ── Open questions ────────────────────────────────────────────────────
OPEN_QUESTIONS = {
    "oq.rt.clips_cost": {"project": P_RT, "text": "generowanie klipów będzie drogie, trzeba to jakoś ograniczyć",
                         "resolved_by": "dec.rt.clips_gate"},
    "oq.nf.encryption_worth_it": {"project": P_NF, "text": "czy w ogóle warto szyfrować, skoro to tylko moje notatki?",
                                  "resolved_by": "dec.nf.encryption"},
    "oq.wd.restart_budget": {"project": P_WD, "text": "czy dodać dzienny budżet restartów, żeby nie restartował w kółko w pętli",
                             "resolved_by": None},
    "oq.lk.appeal_path": {"project": P_LK, "text": "co dalej, jeśli decyzja sądu będzie niekorzystna",
                          "resolved_by": None},
    "oq.mu.vocals_or_instrumental": {"project": P_MU, "text": "czy dogrywać wokal, czy zostać instrumentalnie",
                                     "resolved_by": None},
}

# ── Contradictions ────────────────────────────────────────────────────
CONTRADICTIONS = [
    {"id": "contra.lokatorka.mold_notice", "status": "contested_then_resolved",
     "claim_a": "Landlord (Zenon Kowalczyk) states in Feb 2026: no mould was ever reported before March.",
     "claim_b": "Ola's own January e-mail (dated, in her evidence set) already reports the mould.",
     "resolution": "Cross-evidence contradiction detected from the dated e-mail; the landlord's claim is flagged "
                   "contested and the January e-mail date is used in the court filing.",
     "resolved_by_decision": "dec.lk.court_filing"},
    {"id": "contra.noteflow.sync_vs_local", "status": "resolved_not_a_real_conflict",
     "claim_a": "area.noteflow.storage generalization: 'nic w chmurze bez zgody użytkownika' (nothing in the "
                "cloud without user consent).",
     "claim_b": "feat.nf.sync_git / feat.nf.sync_rest send note data off-device automatically once configured.",
     "resolution": "Not a real conflict once scoped correctly: the area's actual invariant is opt-in cloud "
                   "(consent given once, at setup), not zero-cloud ever. A naive reading would over-flag this "
                   "as a contradiction; the correct reading narrows the generalization's scope instead.",
     "resolved_by_decision": None},
]

# ── Predictions (temporal holdout) ────────────────────────────────────
PREDICTIONS = [
    {"id": f"pred.{o['id']}", "operator": o["id"], "before_T_decision": o["pre_T_example"],
     "predicted_solution_class": o["solution"], "actual_decision": o["post_T_example"], "matched": True}
    for o in OPERATORS
]

print(f"[gen] {len(PRINCIPLES)} principles, {len(OPERATORS)} operators, {len(DECISIONS)} decisions, "
      f"{len(FEATURES)} features, {len(AREAS)} areas")

# ══════════════════════════════════════════════════════════════════════
# Conversations. Each one is either hand-linked with linear()/fork_edit()
# (defined above) or, for the two structural-fork demos, built directly with
# msg(nid, parent, role, text, tags). Tags on individual messages are the
# ONLY source of the ground truth's locators (conv id + node id): the
# generator scans them (see collect_ground_truth below) rather than
# duplicating references by hand, so export and ground truth cannot drift
# apart.
# ══════════════════════════════════════════════════════════════════════
CONVERSATIONS = []


def add(**kw):
    CONVERSATIONS.append(kw)


# ─────────────────────────── NoteFlow (12) ──────────────────────────
add(id="nf-01-kickoff", provider="claude", project=P_NF, kind="signal", model="claude-3-5-sonnet",
    title="notatki", date="2025-01-10T09:14:00Z",
    messages=linear(
        ("user", "Zaczynam nowy projekt: notatnik + czat z modelem w jednym, ale offline-first, "
                 "zeby nie zalezec od chmury.", {"role": [(P_NF, "intent")]}),
        ("assistant", "OK. Od czego zaczynamy - jaki storage i jakie platformy?"),
        ("user", "Docelowo telefon i desktop, jeden core. Zero telemetrii domyslnie, dziala offline, "
                 "a jak juz musi cos wyjsc na zewnatrz to tylko po mojej wyraznej zgodzie.",
         {"role": [(P_NF, "constraint")]}),
        ("assistant", "Rozumiem. Zanim zaczniesz kodowac - burza mozgow?"),
        ("user", "Tak, burza zanim cokolwiek zakoduje. Wszystko o storage: SQLite dla danych "
                 "strukturalnych, pliki blob dla zalacznikow, nic w chmurze bez zgody uzytkownika.",
         {"area": "area.noteflow.storage"}),
        ("assistant", "Zapisuje to jako granice obszaru 'storage'. Modul do syncu osobno?"),
        ("user", "Osobno, ale najpierw dziala lokalnie, sync to bonus."),
    ))

add(id="nf-02-storage", provider="chatgpt", project=P_NF, kind="signal", model="gpt-4o",
    title="gdzie to trzymac", date="2025-01-18T18:02:00Z",
    messages=linear(
        ("user", "SQLite, Realm czy zwykle pliki JSON na notatki? nie chce sie pozniej meczyc z migracja"),
        ("assistant", "Realm daje ladne API ale to vendor lock-in i gorzej z multiplatform C++. Flat "
                      "JSON boli przy duzej liczbie notatek i braku transakcji. SQLite jest nudny ale "
                      "dziala wszedzie i ma WAL."),
        ("user", "no to SQLite. structured data + osobny folder na blob/zalaczniki, tak jak "
                 "ustalilam w burzy mozgow.",
         {"decision": "dec.nf.storage",
          "phrasing": "SQLite dla structured data + osobny folder na blob/zalaczniki, zero vendor lock-in."}),
        ("assistant", "Dobra, wersja 0.1.2 z tym schematem."),
    ))

add(id="nf-03-encryption-planning", provider="chatgpt", project=P_NF, kind="signal", model="gpt-4o",
    title="notatki 0.2 i co z szyfrowaniem", date="2025-02-05T20:11:00Z",
    messages=linear(
        ("user", "0.2 gotowe. mysle o szyfrowaniu at-rest, warto teraz czy pozniej?"),
        ("assistant", "Zalezy od zagrozenia - to lokalne notatki, prawda? Mozna zaplanowac jako warstwe "
                      "nad storage i dograc, kiedy bedzie potrzeba."),
        ("user", "no wlasnie nie wiem, czy w ogole warto szyfrowac, skoro to tylko moje notatki",
         {"open_question": "oq.nf.encryption_worth_it"}),
        ("assistant", "Zapisz to jako otwarte pytanie, na razie 'planned', nie 'implemented'."),
    ))

add(id="nf-04-sync-v1", provider="claude", project=P_NF, kind="signal", model="claude-3-5-sonnet",
    title="synchronizacja pierwsza wersja", date="2025-03-01T11:00:00Z",
    messages=linear(
        ("user", "sync miedzy telefonem a desktopem - wlasny protokol, CRDT po WebRTC, czy po prostu "
                 "sync przez gita?"),
        ("assistant", "Wlasny protokol to duzo pracy na start. CRDT/WebRTC dobre pozniej dla realtime, "
                      "ale to overkill teraz. Sync przez repo gita jest szybki do zrobienia i masz "
                      "historie za darmo."),
        ("user", "biore sync po gicie, szybciej ruszymy, dopieszcze pozniej jak bedzie trzeba",
         {"decision": "dec.nf.sync_v1", "principle": "pr.momentum_over_perfection",
          "phrasing": "utrzymanie tempa jest wazniejsze niz idealne rozwiazanie na starcie"}),
        ("assistant", "0.3.0: sync po gicie, commit per note."),
    ))

_nf05_msgs, _nf05_cur = fork_edit(
    prefix_turns=[
        ("user", "dodaje nagrywanie notatek glosowych, ale nie wiem czy model lokalny czy chmura"),
    ],
    branch_a_turns=[
        ("user", "actually, pytanie edytowane: co z Google Speech zamiast Whisper?"),
        ("assistant", "Mozna, ale gorsza jakosc dla PL. Zostawmy to jako alternatywny adapter na "
                      "przyszlosc, nie teraz."),
    ],
    branch_b_turns=[
        ("user", "pytanie edytowane: bierzemy API w stylu Whisper w chmurze, ale schowane za "
                 "interfejsem transcription-provider, zeby pozniej dolozyc on-device bez przepisywania.",
         {"decision": "dec.nf.transcription", "principle": "pr.provider_not_special_case",
          "phrasing": "nowe API to nowy adapter za tym samym interfejsem, nie nowy if",
          "feature": ["feat.nf.voice_transcription"]}),
        ("assistant", "0.4.0: transkrypcja glosowa dziala przez cloud Whisper-style API za "
                      "interfejsem TranscriptionProvider."),
    ],
    current="b",
)
add(id="nf-05-transcription", provider="chatgpt", project=P_NF, kind="signal", model="gpt-4o",
    title="notatki glosowe dzialaja", date="2025-04-02T08:30:00Z",
    messages=_nf05_msgs, current=_nf05_cur)

add(id="nf-06-checklist-and-defer-crypto", provider="chatgpt", project=P_NF, kind="signal", model="gpt-4o",
    title="rozne", date="2025-05-10T19:45:00Z",
    messages=linear(
        ("user", "checklisty - robimy nowa klase Checklist czy cos prostszego?"),
        ("assistant", "Skoro to w gruncie rzeczy notatka z liniami do odhaczenia, moze po prostu pole "
                      "'items' na istniejacym typie note zamiast nowej klasy?"),
        ("user", "tak, checklist to zwykla notatka z dodatkowym polem items w danych, nie nowy typ/klasa",
         {"decision": "dec.nf.checklist_as_data", "principle": "pr.data_over_branches",
          "feature": ["feat.nf.checklist_notes"]}),
        ("assistant", "0.5.0: checklisty jako wariant danych notatki."),
        ("user", "i olewam szyfrowanie na razie, dolozylabym to jako osobna warstwe nad storage pozniej, "
                 "bez przepisywania calosci - MVP tego nie potrzebuje",
         {"decision": "dec.nf.encryption_defer", "principle": "pr.small_reversible_steps",
          "feature": ["feat.nf.encryption"]}),
        ("assistant", "Zanotowane: encryption = abandoned-for-now, nie planned-na-zawsze."),
    ))

_nf07_prefix = linear(
    ("user", "0.6.0 gotowe. Dwie galezie z tego samego punktu w glowie: zostac w Pythonie i szybko "
             "patchowac, albo przepisac rdzen na C++. Nie wiem jeszcze ktora.",
     {"role": [(P_NF, "question")]}),
)
_nf07_p1 = _nf07_prefix[-1]["nid"]
_nf07_msgs = _nf07_prefix + [
    msg("u2a", _nf07_p1, "user",
        "galaz A: zostajemy w Pythonie, tylko szybkie patche zeby dowiezc 0.6.x.",
        {"fork": "fork.nf.core_rewrite"}),
    msg("a2a", "u2a", "assistant", "python-quick: male, szybkie iteracje, ale kazda nowa platforma to "
                                   "osobna warstwa kompatybilnosci."),
    msg("u2b", _nf07_p1, "user",
        "galaz B: przepisujemy rdzen na C++, jeden silnik pod kazda platforme, wolniej na start ale "
        "bez podwojnej roboty pozniej.",
        {"fork": "fork.nf.core_rewrite"}),
    msg("a2b", "u2b", "assistant",
        "cpp-core: wiecej pracy teraz, jeden kernel dla telefonu i desktopu pozniej."),
]
add(id="nf-07-fork-python-or-cpp", provider="claude", project=P_NF, kind="signal", model="claude-3-5-sonnet",
    title="rozjazd: python czy c++", date="2025-06-20T21:00:00Z",
    messages=_nf07_msgs, current="a2b", claude_fork=True)

add(id="nf-08-cpp-wins", provider="claude", project=P_NF, kind="signal", model="claude-3-5-sonnet",
    title="wygrywa c++", date="2025-08-10T10:00:00Z",
    messages=linear(
        ("user", "decyzja: bierzemy cpp-core. Python-quick zostaje porzucona, nie kasuje brancha, "
                 "tylko opisuje czemu odpadl i zostawiam go jak jest.",
         {"decision": "dec.nf.core_language", "principle": "pr.decide_fast_correct_later",
          "phrasing": "decyduje szybko na tym co wiem teraz, koryguje pozniej jak beda nowe dane"}),
        ("assistant", "0.7.0: rdzen w C++. Chcesz opisac tez czemu python-quick nie wygral?"),
        ("user", "python-quick byl ok na 0.6.x ale kazda nowa platforma to byla osobna warstwa "
                 "kompatybilnosci - C++ core rozwiazuje to raz a porzadnie, mimo ze ABI jeszcze nie "
                 "jest w 100% dopracowane.",
         {"principle": "pr.keep_both_branches",
          "phrasing": "nie kasuje porzuconej galezi, zostawiam ja z opisem czemu odpadla"}),
        ("assistant", "Zanotowane jako explicit reversal, nie cichy zanik."),
        ("user", "przy okazji zamienilam recznie pisany diff na biblioteczny (Myers-style) do historii "
                 "wersji notatek - to czysto wewnetrzne, nie zmienia kontraktu na zewnatrz, nie zapisuje "
                 "tego jako architecture decision.",
         {"decision": "dec.nf.diff_algo_swap"}),
        ("assistant", "0.7.1, tylko wpis w changelogu, bez ADR."),
    ))

add(id="nf-09-lost-and-billed", provider="chatgpt", project=P_NF, kind="signal", model="gpt-4.1",
    title="pytanie", date="2025-10-10T22:15:00Z",
    messages=linear(
        ("user", "cos jest nie tak z 0.9.0, gdzie zniknal przycisk do nagrywania notatki glosowej??"),
        ("assistant", "Sprawdzmy, czy modul transcription w ogole jest teraz linkowany do UI po "
                      "przepisaniu rdzenia."),
        ("user", "no nie ma go, zgubilo sie to gdzies przy porcie na c++, checklisty tez zreszta nie dzialaja",
         {"feature": ["feat.nf.voice_transcription", "feat.nf.checklist_notes"]}),
        ("assistant", "Zanotowane jako lost w 0.9.0, nie superseded - nic ich swiadomie nie zastapilo."),
        ("user", "a osobno, dostalam rachunek za chmure na 340 zl bo cos auto-uploadowalo wielkie "
                 "zalaczniki w tle bez pytania"),
        ("assistant", "To trzeba zgasic zanim urosnie."),
        ("user", "tak, dodaje bramke: przed duzym uploadem zawsze pytam / pokazuje estymowany koszt i "
                 "czekam na potwierdzenie, zanim system dziala dalej",
         {"decision": "dec.nf.upload_gate", "principle": "pr.owner_confirmation_for_money",
          "phrasing": "gdy koszt rosnie, zawsze pytam / zapisuje zgode, zanim system dziala dalej",
          "feature": ["feat.nf.upload_gate"]}),
        ("assistant", "0.9.0 dostaje upload-confirmation gate."),
        ("user", "swietny tydzien, najpierw gubie feature'y, potem placę za to ze straty ;)"),
    ))

add(id="nf-10-sync-v2-and-graph-v2", provider="claude", project=P_NF, kind="signal", model="claude-3-5-sonnet",
    title="nowa synchronizacja i graf wiedzy v2", date="2025-12-01T14:20:00Z",
    messages=linear(
        ("user", "sync po gicie sie nie skaluje, dua zalaczniki (nagrania glosowe!) sa bolesnie wolne "
                 "po pol roku uzywania to widac", {"role": [(P_NF, "question")]}),
        ("assistant", "Wlasny REST sync z chunkowaniem zalacznikow?"),
        ("user", "tak, REST-based custom sync zastepuje sync po gicie - jawnie odwracam tamta decyzje, "
                 "git-based bylo dobre na start ale juz nie wystarcza",
         {"decision": "dec.nf.sync_v2", "feature": ["feat.nf.sync_git", "feat.nf.sync_rest"]}),
        ("assistant", "1.0.0: sync v2. A checklisty?"),
        ("user", "przywrocone w tej samej wersji, przy okazji ogarniania encji po porcie",
         {"feature": ["feat.nf.checklist_notes"]}),
        ("assistant", "I graf pamieci?"),
        ("user", "stary graph_memory (0.8.0, partial) zastepuje nowy 'knowledge graph v2', lepszy model "
                 "encji, dziala tez offline z lokalnym modelem embeddingow.",
         {"feature": ["feat.nf.graph_memory", "feat.nf.knowledge_graph_v2"],
          "role": [(P_NF, "resource")]}),
    ))

add(id="nf-11-transcription-restored", provider="chatgpt", project=P_NF, kind="signal", model="gpt-4.1",
    title="transkrypcja wrocila + offline fallback", date="2026-05-20T09:00:00Z",
    messages=linear(
        ("user", "wracam do transkrypcji glosowej - ludzie (no, ja) narzekaja ze nie dziala bez neta"),
        ("assistant", "Skoro juz jest za TranscriptionProvider, dopisujemy drugi adapter on-device jako "
                      "fallback, zero zmian w reszcie apki."),
        ("user", "tak dokladnie, przywracam cloud Whisper-style + dodaje on-device fallback za tym samym "
                 "interfejsem providera, dokladnie tak jak mialo byc od poczatku",
         {"decision": "dec.nf.transcription_fallback", "principle": "pr.provider_not_special_case",
          "feature": ["feat.nf.voice_transcription"]}),
        ("assistant", "1.1.0: transkrypcja restored + fallback on-device."),
    ))

add(id="nf-12-encryption-and-lost-again", provider="claude", project=P_NF, kind="signal", model="claude-3-5-sonnet",
    title="szyfrowanie w koncu + zgubione checklisty (znowu)", date="2026-07-01T17:30:00Z",
    messages=linear(
        ("user", "w koncu szyfrowanie at-rest. AES-256-GCM czy ChaCha20-Poly1305? czytalam ze na "
                 "starszych telefonach bez AES-NI ChaCha jest wyraznie szybszy"),
        ("assistant", "Jesli celujesz w starsze/tansze telefony, ChaCha20-Poly1305 ma sens wlasnie "
                      "dlatego."),
        ("user", "biore ChaCha20-Poly1305, tez za wymiennym cipher-providerem, zeby zmiana byla "
                 "znowu tylko adapterem a nie przepisywaniem storage",
         {"decision": "dec.nf.encryption", "principle": "pr.provider_not_special_case",
          "feature": ["feat.nf.encryption"]}),
        ("assistant", "1.2.0: encryption implemented."),
        ("user", "a przy okazji refaktoru UI kolejny raz zgubily sie checklisty, trzeci raz z rzedu "
                 "cos im sie dzieje przy duzych zmianach - zostawiam jak jest, zaznaczam lost, nie kasuje "
                 "kodu ktory je robil",
         {"feature": ["feat.nf.checklist_notes"], "principle": "pr.keep_both_branches"}),
    ))

# ─────────────────────────── Reeltime (7) ───────────────────────────
add(id="rt-01-kickoff", provider="claude", project=P_RT, kind="signal", model="claude-3-5-sonnet",
    title="pomysl na generator reelsow", date="2025-09-05T19:00:00Z",
    messages=linear(
        ("user", "nowy projekt z boku: chce szybko robic krotkie wideo/reele z samego tekstu, bez "
                 "kamery.", {"role": [(P_RT, "intent")]}),
        ("assistant", "Jak sobie to wyobrazasz etapami?"),
        ("user", "burza mozgow: wszystko o etapach pipeline'u: storyboard z tekstu, potem keyframe'y, "
                 "potem generowanie klipow, na koncu compose/montaz - kazdy etap drozszy niz "
                 "poprzedni, wiec tanie najpierw, drogie na koncu, i pytam zanim odpale drogi etap.",
         {"area": "area.reeltime.stages"}),
        ("assistant", "Czyli musi byc gate kosztowy przed najdrozszym etapem. Ktory provider do "
                      "obrazow?"),
        ("user", "na razie jeden, PixelForge API (fikcyjne nazwy roboczo), zobaczymy.",
         {"role": [(P_RT, "resource")]}),
        ("assistant", "OK, budzet zerowy narazie, tylko testy."),
    ))

add(id="rt-02-storyboard", provider="chatgpt", project=P_RT, kind="signal", model="gpt-4o",
    title="storyboard z tekstu dziala", date="2025-09-20T10:00:00Z",
    messages=linear(
        ("user", "pierwszy etap gotowy: wrzucam tekst, model dzieli go na sceny i opisy kadrow"),
        ("assistant", "0.1: storyboard stage implemented.", {"feature": ["feat.rt.storyboard"]}),
        ("user", "eksport na razie do .mp4 planuje na koniec calego pipeline'u, nie teraz",
         {"role": [(P_RT, "interface")]}),
    ))

add(id="rt-03-keyframes", provider="chatgpt", project=P_RT, kind="signal", model="gpt-4o",
    title="keyframe'y - tylko jeden provider", date="2025-10-05T15:30:00Z",
    messages=linear(
        ("user", "keyframe'y z opisow scen dzialaja, ale tylko z jednym providerem obrazow, jak padnie "
                 "to nie mam planu B", {"feature": ["feat.rt.keyframes"]}),
        ("assistant", "Zanotowane jako partial - dziala, ale bez fallbacku."),
        ("user", "i nie mam zadnego checku spojnosci miedzy keyframe'ami tej samej postaci, wiem ze "
                 "to dziura", {"role": [(P_RT, "check")]}),
    ))

add(id="rt-04-clips-cost-worry", provider="claude", project=P_RT, kind="signal", model="claude-3-5-sonnet",
    title="klipy beda drogie, trzeba to ogarnac", date="2025-10-12T20:00:00Z",
    messages=linear(
        ("user", "licze ile by kosztowalo wygenerowanie klipow do jednego reela z 8 scen - wychodzi "
                 "spora kwota jak zle zestawie parametry",
         {"open_question": "oq.rt.clips_cost", "feature": ["feat.rt.clips"]}),
        ("assistant", "To brzmi jak kandydat na gate kosztowy zanim to wgramy na stale."),
        ("user", "no wlasnie, generowanie klipow bedzie drogie, trzeba to jakos ograniczyc, ale nie "
                 "mam teraz czasu tego zaprojektowac", {"open_question": "oq.rt.clips_cost"}),
    ))

add(id="rt-05-clips-gate", provider="chatgpt", project=P_RT, kind="signal", model="gpt-5",
    title="brama kosztowa przed klipami", date="2026-06-10T13:00:00Z",
    messages=linear(
        ("user", "wracam do reelsow po dlugiej przerwie. w koncu robie ten gate przed generowaniem "
                 "klipow"),
        ("assistant", "Estymacja kosztu + potwierdzenie zanim odpali sie render?"),
        ("user", "dokladnie, klipy dostaja bramke: pokazuje estymowany koszt, czekam na moje jawne "
                 "potwierdzenie, dopiero potem render - dokladnie ten sam wzorzec co przy uploadach w "
                 "notatniku", {"decision": "dec.rt.clips_gate", "principle": "pr.owner_confirmation_for_money",
                                "feature": ["feat.rt.clips"]}),
        ("assistant", "0.3: clips stage implemented, gated."),
    ))

add(id="rt-06-shared-artifact-model", provider="claude", project=P_RT, kind="signal", model="claude-3-5-sonnet",
    title="compose dalej czeka", date="2026-06-25T18:00:00Z",
    messages=linear(
        ("user", "compose/montaz dalej nie ruszony, ale przy okazji klipow pomyslalam: keyframe, klip "
                 "i (kiedys) sekcja compose to w sumie ten sam ksztalt rekordu - etap, wejscie, wyjscie, "
                 "koszt, status - roznia sie tylko polami danych",
         {"decision": "dec.rt.shared_artifact_model", "principle": "pr.data_over_branches"}),
        ("assistant", "Czyli jeden 'stage artifact', pola jako dane, zamiast osobnych klas na etap?"),
        ("user", "tak, tak to widze, nawet jesli compose nigdy nie powstanie to schemat juz jest gotowy",
         {"role": [(P_RT, "part")]}),
    ))

add(id="rt-07-stalled", provider="chatgpt", project=P_RT, kind="signal", model="gpt-5",
    title="dawno tego nie ruszalam", date="2026-09-02T21:00:00Z",
    messages=linear(
        ("user", "reeltime - dawno tego nie ruszalam, od lipca chyba nic, compose dalej tylko "
                 "planned"),
        ("assistant", "Chcesz to formalnie zamknac jako abandoned?"),
        ("user", "nie wiem, nie podjelam takiej decyzji, po prostu inne rzeczy byly wazniejsze - "
                 "niech zostanie jak jest, nie zgaduj za mnie statusu"),
    ))

# ─────────────────────────── Watchdog (7) ───────────────────────────
add(id="wd-01-manual-pain", provider="claude", project=P_WD, kind="signal", model="claude-3-5-sonnet",
    title="ile razy w tym tygodniu recznie zabijalam zawieszony proces",
    date="2025-11-02T23:40:00Z",
    messages=linear(
        ("user", "trzeci raz w tym tygodniu wstaje w nocy bo agent kodujacy sie zawiesil i nic nie "
                 "robi az do rana", {"role": [(P_WD, "intent")]}),
        ("assistant", "Zapisujesz gdzies te przypadki, zanim to zautomatyzujesz?"),
        ("user", "od dwoch tygodni notuje w pliku kiedy i dlaczego recznie restartowalam - to na "
                 "razie tylko obserwacja, nic wiecej", {"principle": "pr.write_down_the_blocker",
          "phrasing": "kiedy utknelam, zapisuje dokladnie co sie stalo, zanim cokolwiek zautomatyzuje"}),
    ))

add(id="wd-02-build-decision", provider="claude", project=P_WD, kind="signal", model="claude-3-5-sonnet",
    title="buduje stroza", date="2025-11-08T20:00:00Z",
    messages=linear(
        ("user", "mam juz dwa tygodnie notatek o recznych restartach, wystarczy, buduje maly skrypt "
                 "ktory to robi za mnie",
         {"decision": "dec.wd.build", "principle": "pr.write_down_the_blocker"}),
        ("assistant", "Jakie role ma miec?"),
        ("user", "watcher, restarter, summarizer jako osobne, wymienne kawalki - to ma byc pakiet "
                 "polityk a nie jedna wielka klasa", {"role": [(P_WD, "part")]}),
        ("assistant", "A alerty?"),
        ("user", "burza mozgow: wszystko co dotyczy alertow: powiadomienie na pulpit, wpis do logu, "
                 "opcjonalnie mail - zawsze najpierw lokalnie.", {"area": "area.watchdog.alerts"}),
    ))

add(id="wd-03-v01", provider="chatgpt", project=P_WD, kind="signal", model="gpt-4o",
    title="watcher + restart dziala", date="2025-11-10T22:00:00Z",
    messages=linear(
        ("user", "0.1 gotowe: czyta log, wykrywa brak postepu przez >90s, zabija i odpala od nowa",
         {"feature": ["feat.wd.watcher_restart"]}),
        ("assistant", "Heartbeat plik czy tylko log?"),
        ("user", "na razie tylko log, heartbeat plik pozniej moze",
         {"role": [(P_WD, "resource")]}),
    ))

add(id="wd-04-summarizer", provider="chatgpt", project=P_WD, kind="signal", model="gpt-4o",
    title="podsumowania po restarcie", date="2025-12-05T21:00:00Z",
    messages=linear(
        ("user", "dodaje summarizer - po restarcie ma streszczac co sie stalo w sesji zanim padla",
         {"feature": ["feat.wd.summarizer"]}),
        ("assistant", "0.2: summarizer, tani model do streszczenia logow."),
    ))

add(id="wd-05-desktop-notify", provider="claude", project=P_WD, kind="signal", model="claude-3-5-sonnet",
    title="powiadomienia na pulpit", date="2026-01-20T19:00:00Z",
    messages=linear(
        ("user", "dodaje powiadomienie na pulpit przy kazdym restarcie, zeby nie musiec zagladac do "
                 "logu recznie", {"feature": ["feat.wd.desktop_notify"]}),
        ("assistant", "0.3: desktop notification channel. Mail jako opcja tez zostaje?"),
        ("user", "tak, mail opcjonalnie, ale domyslnie tylko lokalne powiadomienie",
         {"role": [(P_WD, "output")]}),
    ))

add(id="wd-06-restart-budget-question", provider="chatgpt", project=P_WD, kind="signal", model="gpt-4.1",
    title="pytanie", date="2026-02-01T10:00:00Z",
    messages=linear(
        ("user", "czy dodac dzienny budzet restartow, zeby stroz nie restartowal w kolko w petli jak "
                 "cos jest systemowo zepsute?", {"open_question": "oq.wd.restart_budget"}),
        ("assistant", "Sensowne, ale to osobna decyzja - chcesz to teraz rozstrzygac?"),
        ("user", "nie, zostawiam otwarte na razie", {"open_question": "oq.wd.restart_budget"}),
    ))

add(id="wd-07-io-swap", provider="chatgpt", project=P_WD, kind="signal", model="gpt-5",
    title="zmieniam polling na inotify", date="2026-06-01T16:00:00Z",
    messages=linear(
        ("user", "polling logu co sekunde marnuje CPU, zamieniam na inotify, zero zmian na zewnatrz, "
                 "zachowanie identyczne",
         {"decision": "dec.wd.io_swap"}),
        ("assistant", "To czysto wewnetrzne, nie zapisuje jako decyzje architektoniczna, tylko commit."),
    ))

# ─────────────────────────── Legal tracker (7) ──────────────────────
add(id="lk-01-evidence", provider="claude", project=P_LK, kind="signal", model="claude-3-5-sonnet",
    title="zbieram dowody - Kwiatowa", date="2026-01-05T21:00:00Z",
    messages=linear(
        ("user", "sprawa z Kwiatowej: zbieram wszystko - maile od Zenona (3 watki), zdjecia pleśni z "
                 "ledenia (styczen), sms-y", {"role": [(P_LK, "resource")]}),
        ("assistant", "Masz tez jakas pierwsza zasade jak to prowadzic?"),
        ("user", "tak: kazdy termin musi byc powiazany z konkretna podstawa prawna, inaczej to sie nie "
                 "liczy. i burza mozgow: wszystko co dotyczy terminow: liczone od dostarczenia pisma, "
                 "zawsze plus dwa dni zapasu, zawsze zapisana podstawa prawna.",
         {"area": "area.lokatorka.deadlines", "role": [(P_LK, "constraint")]}),
    ))

_lk02_msgs, _lk02_cur = fork_edit(
    prefix_turns=[
        ("user", "co robimy - pismo do Zenona czy od razu sad?"),
    ],
    branch_a_turns=[
        ("user", "poprawka: od razu sad, nie chce sie certolic z pismami", {}),
        ("assistant", "Mozna, ale bez wczesniejszego formalnego wezwania sad moze patrzec na to gorzej."),
    ],
    branch_b_turns=[
        ("user", "poprawka: najpierw formalne pisemne wezwanie cytujace KL art. 12, eskalacja do sadu "
                 "tylko jesli zignorowane przez 14+ dni",
         {"decision": "dec.lk.strategy"}),
        ("assistant", "To zostawia lepszy slad na pozniej, jesli trzeba bedzie i tak isc do sadu."),
    ],
    current="b",
)
add(id="lk-02-strategy", provider="chatgpt", project=P_LK, kind="signal", model="gpt-4o",
    title="strategia: pismo czy od razu sad", date="2026-01-10T18:00:00Z",
    messages=_lk02_msgs, current=_lk02_cur)

add(id="lk-03-deadline-computed", provider="chatgpt", project=P_LK, kind="signal", model="gpt-4o",
    title="termin na kaucje policzony", date="2026-01-12T09:00:00Z",
    messages=linear(
        ("user", "wezwanie dorecozone 10 stycznia. termin na odpowiedz w sprawie kaucji: 30 dni od "
                 "doreczenia + 2 dni zapasu, podstawa: KL art. 12 ust. 3 (fikcyjny w tym zbiorze)",
         {"feature": ["feat.lk.deposit_claim"]}),
        ("assistant", "Czyli termin krytyczny to 11 lutego. Ustawiam przypomnienie 3 dni wczesniej?"),
        ("user", "tak, zawsze przypomnienie przed terminem, tak jak przy alertach w stroziu"),
    ))

add(id="lk-04-contradiction", provider="claude", project=P_LK, kind="signal", model="claude-3-5-sonnet",
    title="Zenon sie wypiera, ale mam maila ze stycznia", date="2026-02-01T20:00:00Z",
    messages=linear(
        ("user", "Zenon pisze, ze nigdy przed marcem nie zglaszalam zadnej plesni",
         {"contradiction": ("contra.lokatorka.mold_notice", "claim_a")}),
        ("assistant", "A twoj mail ze stycznia?"),
        ("user", "wlasnie, mam datowanego maila ze stycznia gdzie zglaszam plesn - to sie wprost "
                 "przeczy z tym co teraz pisze",
         {"contradiction": ("contra.lokatorka.mold_notice", "claim_b")}),
        ("assistant", "Zapisuje to jako sprzecznosc, uzyjemy daty maila w pismie do sadu jesli dojdzie "
                      "do tego."),
    ))

add(id="lk-05-manual-checking", provider="chatgpt", project=P_LK, kind="signal", model="gpt-4.1",
    title="rozne", date="2026-02-14T08:00:00Z",
    messages=linear(
        ("user", "znowu rano sprawdzalam recznie excel z terminami, zamiast cos innego zrobic",
         {"principle": "pr.write_down_the_blocker"}),
        ("assistant", "Masz juz ten wzorzec ze Stroza tez - obserwacja przed automatyzacja."),
        ("user", "no wlasnie, ale na razie nie mam czasu tego zautomatyzowac, robie to recznie dalej"),
    ))

add(id="lk-06-court-filing", provider="claude", project=P_LK, kind="signal", model="claude-3-5-sonnet",
    title="olal pismo, idziemy do sadu", date="2026-03-10T19:00:00Z",
    messages=linear(
        ("user", "minelo 14+ dni, Zenon olal pismo calkowicie, zadnej odpowiedzi",
         {"feature": ["feat.lk.deposit_claim"]}),
        ("assistant", "Wiec eskalacja zgodnie z tym co ustalilas w styczniu?"),
        ("user", "tak, skladamy w sadzie. jawnie odwracam wczesniejsza strategie 'pismo najpierw' - "
                 "ta faza sie skonczyla, nie dziala",
         {"decision": "dec.lk.court_filing"}),
    ))

add(id="lk-07-auto-deadlines", provider="claude", project=P_LK, kind="signal", model="claude-3-5-sonnet",
    title="terminy licza sie same", date="2026-06-15T10:00:00Z",
    messages=linear(
        ("user", "mam juz dosc recznego sprawdzania excela codziennie rano, licze to teraz automatycznie "
                 "z dat zdarzen plus przypomnienie, zamiast pamietac samej",
         {"decision": "dec.lk.auto_deadlines", "principle": "pr.write_down_the_blocker"}),
        ("assistant", "Dokladnie ten sam wzorzec co przy Strozu: obserwuj recznie, potem automatyzuj."),
        ("user", "rozprawa zreszta wyznaczona na 20 sierpnia, wpisuje to tez do tego samego systemu "
                 "terminow", {"role": [(P_LK, "event")]}),
    ))

# ─────────────────────────── Music: Analog Ghosts (6) ───────────────
add(id="mu-01-kickoff", provider="claude", project=P_MU, kind="signal", model="claude-3-5-sonnet",
    title="riffy i pomysly na EP", date="2026-02-08T22:00:00Z",
    messages=linear(
        ("user", "projekt z boku zeby miec cos skonczonego poza kodem: krotkie EP, robie sama, "
                 "wieczorami, budzet zero", {"role": [(P_MU, "intent")]}),
        ("assistant", "Jaki sprzet i jakie brzmienie?"),
        ("user", "syntek analogowy-klon, automat perkusyjny, gitara. burza mozgow o brzmieniu: "
                 "wszystko o brzmieniu: cieple analogowe, duzo tape saturation, bez zbyt cyfrowego "
                 "poglosu.", {"area": "area.music.sound_palette", "role": [(P_MU, "resource")]}),
        ("assistant", "Jakies robocze tytuly utworow?"),
        ("user", "'Paper Weather' i 'Slow Fracture' na razie jako szkice",
         {"feature": ["feat.mu.slow_fracture"]}),
    ))

_mu02_msgs, _mu02_cur = fork_edit(
    prefix_turns=[
        ("user", "'Paper Weather' - nie moge sie zdecydowac na tonacje",
         {"decision": "dec.mu.arrangement_key_open"}),
    ],
    branch_a_turns=[
        ("user", "demo w c-moll", {}),
        ("assistant", "Ciemniej, bardziej ciezko."),
    ],
    branch_b_turns=[
        ("user", "i osobno demo w d-moll, zostawiam obie wersje, zdecyduje jak poslucham na swiezo "
                 "za jakis czas", {}),
        ("assistant", "Dwa tanie demo zamiast jednej decyzji na sile - rozsadne przy niepewnosci."),
    ],
    current="b",
)
add(id="mu-02-key-open", provider="claude", project=P_MU, kind="signal", model="claude-3-5-sonnet",
    title="molowy czy inny - jeszcze nie wiem", date="2026-02-20T21:00:00Z",
    messages=_mu02_msgs, current=_mu02_cur, claude_fork=True)

add(id="mu-03-recording-medium", provider="chatgpt", project=P_MU, kind="signal", model="gpt-4.1",
    title="tasma czy cyfrowo", date="2026-03-01T20:00:00Z",
    messages=linear(
        ("user", "nagrywac prosto na tasme (cieplej, ale trudno cofnac) czy cyfrowy multitrack "
                 "(latwo poprawic, gorzej brzmi)?"),
        ("assistant", "Jak bardzo pewna jestes aranzu?"),
        ("user", "wcale nie jestem pewna, wiec bioce cyfrowy multitrack - latwiej cofnac bledy, jak "
                 "dwie rzeczy sie kloca to wygrywa ta latwiejsza do cofniecia",
         {"decision": "dec.mu.recording_medium", "principle": "pr.two_masters_conflict",
          "phrasing": "kiedy dwie wartosci sie kloca, wygrywa ta opcja, ktora latwiej cofnac"}),
    ))

add(id="mu-04-abandon-slow-fracture", provider="chatgpt", project=P_MU, kind="signal", model="gpt-4.1",
    title="porzucam Slow Fracture (na razie)", date="2026-04-10T19:00:00Z",
    messages=linear(
        ("user", "'Slow Fracture' nie broni sie, cos w tym pomysle nie gra od poczatku"),
        ("assistant", "Kasujesz demo?"),
        ("user", "nie, zostawiam demo jak jest, opisuje czemu odpadlo, moze kiedys do tego wroce",
         {"feature": ["feat.mu.slow_fracture"], "principle": "pr.keep_both_branches"}),
    ))

add(id="mu-05-key-final", provider="claude", project=P_MU, kind="signal", model="claude-3-5-sonnet",
    title="d-moll wygrywa", date="2026-06-01T21:00:00Z",
    messages=linear(
        ("user", "wrocilam do obu demo 'Paper Weather' po miesiacach - d-moll broni sie lepiej na "
                 "swiezo", {"decision": "dec.mu.arrangement_key_final"}),
        ("assistant", "Czyli c-moll demo zostaje jako odrzucona, ale zapisana, opcja?"),
        ("user", "tak, nie kasuje, tylko oznaczam jako rejected"),
    ))

add(id="mu-06-ep-status", provider="chatgpt", project=P_MU, kind="signal", model="gpt-5",
    title="rozne", date="2026-07-10T20:00:00Z",
    messages=linear(
        ("user", "status EP: 'Paper Weather' skonczone, to drugi gotowy utwor, reszta dalej tylko "
                 "szkice", {"feature": ["feat.mu.paper_weather"]}),
        ("assistant", "A wokal?"),
        ("user", "wciaz nie wiem czy dogrywac wokal czy zostac instrumentalnie",
         {"open_question": "oq.mu.vocals_or_instrumental"}),
    ))

# ─────────────────── Cross-project / philosophy (6) ──────────────────
add(id="cx-01-manifesto", provider="claude", project=None, kind="signal", model="claude-3-5-sonnet",
    title="notatki", date="2025-01-08T08:00:00Z",
    messages=linear(
        ("user", "zanim zacznę cokolwiek kodować w tym roku, zapisuję sobie parę rzeczy, żeby nie "
                 "musieć ich za każdym razem wymyślać od nowa."),
        ("assistant", "Śmiało, słucham."),
        ("user", "po pierwsze: opcjonalność jest ważniejsza niż tymczasowa szybkość - wolę zostawić "
                 "sobie drogę odwrotu niż zaoszczędzić dzień teraz.",
         {"principle": "pr.optionality_over_speed"}),
        ("user", "po drugie: mówię wprost czego nie wiem albo czego nie zrobiłam, zamiast to ładnie "
                 "opakowywać - i to dotyczy też rozmów z modelem, nie tylko z ludźmi.",
         {"principle": "pr.honesty_over_comfort"}),
        ("user", "i po trzecie, na razie tylko dla presetów/configów: to dane, nie nowa gałąź kodu.",
         {"principle": "pr.data_over_branches"}),
        ("assistant", "Zapisane. To brzmi jak coś, do czego będziesz wracać."),
    ))

_cx02_msgs, _cx02_cur = fork_edit(
    prefix_turns=[
        ("user", "czemu ja ciagle zmieniam zdanie w sprawie tego rdzenia w c++ czy zostac w pythonie"),
    ],
    branch_a_turns=[
        ("user", "poprawka pytania: czy to w ogole zle, ze zmieniam zdanie tak czesto?", {}),
        ("assistant", "Niekoniecznie - zalezy czy zmieniasz zdanie na podstawie nowych danych, czy z "
                      "niepokoju."),
    ],
    branch_b_turns=[
        ("user", "poprawka pytania: no dobra, prawda jest taka ze ja po prostu decyduje szybko na tym "
                 "co wiem, a potem koryguje jak sie cos zmieni - nie czekam na pewnosc, bo pewnosci "
                 "nigdy nie ma",
         {"principle": "pr.decide_fast_correct_later"}),
        ("assistant", "To brzmi jak swiadomy default, nie chaos."),
        ("user", "no i przy okazji: python-quick zostawiam jako branch, nie kasuje, dopisuje tylko "
                 "czemu przegral - a regule zapisuje sobie dopiero jak zobacze ten sam wzorzec w co "
                 "najmniej dwoch roznych projektach, nie po jednym razie",
         {"principle": "pr.meta_two_examples_rule"}),
    ],
    current="b",
)
add(id="cx-02-changing-mind", provider="chatgpt", project=None, kind="signal", model="gpt-4o",
    title="czemu ciagle zmieniam zdanie", date="2025-07-20T22:30:00Z",
    messages=_cx02_msgs, current=_cx02_cur)

add(id="cx-03-drifting-thread", provider="claude", project=None, kind="signal", model="claude-3-5-sonnet",
    title="hmm", date="2025-11-15T21:00:00Z",
    messages=linear(
        ("user", "stroz mi dzis znowu cos dziwnego zrobil, restartowal 3 razy pod rzad"),
        ("assistant", "Masz log z tych trzech restartow?"),
        ("user", "nie mam, nie zapisalam, a szkoda, bo teraz nie wiem czy to ten sam powod"),
        ("assistant", "A jak tam poza tym?"),
        ("user", "zmeczona, sprawa z Kwiatowej mnie meczy psychicznie bardziej niz caly kod razem "
                 "wziety, ciagle mysle czy dobrze licze te terminy"),
        ("assistant", "To rozumiem, to duzo naraz."),
        ("user", "no i wlasnie widze teraz ze to jest ten sam problem co ze stroziem - nie zapisuje "
                 "na czym utknelam zanim przeskocze do czegos innego, robie to caly czas i sie potem "
                 "gubie", {"principle": "pr.write_down_the_blocker",
                            "phrasing": "kiedy utknę na ponad 20 minut, zapisuję dokładnie na czym "
                                        "utknęłam, zanim przełączę zadanie"}),
        ("assistant", "Czyli od teraz zapisujesz blocker zanim zmienisz kontekst?"),
        ("user", "tak, probuje, zobaczymy czy sie utrzyma"),
    ))

add(id="cx-04-venting", provider="chatgpt", project=None, kind="signal", model="gpt-4.1",
    title="co mnie wkurza w tym jak pracuje", date="2026-02-25T20:00:00Z",
    messages=linear(
        ("user", "wkurza mnie jak sama sobie wmawiam ze cos jest 'prawie gotowe' zamiast powiedziec "
                 "wprost ze utknelam - zawsze konczy sie gorzej", {"principle": "pr.honesty_over_comfort"}),
        ("assistant", "To brzmi jak swiadomy wzorzec, nie jednorazowy grzech."),
        ("user", "i zanim cos nazwe 'zasada u mnie' to naprawde szukam kontrprzykladu z ostatnich "
                 "dwoch tygodni, zeby sie nie oszukiwac", {"principle": "pr.counterexample_hunting"}),
        ("assistant", "A propos zasad - co z tym rachunkiem za chmure w notatniku?"),
        ("user", "no wlasnie to mi uswiadomilo cos ogolniejszego: jak koszt (pieniadze, czas, "
                 "nieodwracalnosc) rosnie, to zawsze musze pytac / zapisywac zgode zanim cokolwiek "
                 "dzieje sie dalej - to nie jest tylko regula dla notatnika",
         {"principle": "pr.owner_confirmation_for_money", "mentions_projects": [P_NF]}),
    ))

add(id="cx-05-midyear-review", provider="claude", project=None, kind="signal", model="claude-3-5-sonnet",
    title="przeglad zasad w polowie roku", date="2026-05-05T19:00:00Z",
    messages=linear(
        ("user", "polroczny przeglad tego jak pracuje, zanim zapomne dlaczego cos robie tak a nie "
                 "inaczej"),
        ("assistant", "Zaczynajmy."),
        ("user", "opcjonalnosc dalej na pierwszym miejscu - wole zaplacic troche czasu teraz zeby nie "
                 "zamykac sobie drzwi pozniej", {"principle": "pr.optionality_over_speed"}),
        ("user", "presety/config jako dane - to zostaje, ale podobno niektorzy robia WSZYSTKO jako "
                 "dane, az do logiki wlacznie. ja bym sie az tak daleko nie posunela, algorytmy i UI "
                 "zostaja u mnie w kodzie", {"principle": "pr.data_over_branches"}),
        ("assistant", "A te dwie sytuacje z gate'ami kosztowymi i z tasma w muzyce?"),
        ("user", "to w sumie ta sama regula w szerszej wersji: kiedy dwie wartosci sie kloca, wygrywa "
                 "ta opcja, ktora latwiej cofnac - pieniadze to tylko jeden szczegolny przypadek "
                 "nieodwracalnosci", {"principle": "pr.two_masters_conflict"}),
        ("assistant", "I to zauwazylas po ilu projektach?"),
        ("user", "po dwoch, jak zawsze - u mnie regula staje sie regula dopiero za drugim razem, nie "
                 "za pierwszym", {"principle": "pr.meta_two_examples_rule"}),
    ))

add(id="cx-06-whats-next", provider="chatgpt", project=None, kind="signal", model="gpt-5",
    title="co dalej ze wszystkimi tymi projektami", date="2026-08-10T21:00:00Z",
    messages=linear(
        ("user", "szybki przeglad: appka od notatek (NoteFlow) - 1.2 wyszlo, ale checklisty znowu "
                 "padly. generator reelsow stoi od lipca. Stroz dziala bez zmian od czerwca. sprawa z "
                 "Kwiatowej czeka na rozprawe w sierpniu. plyta - dwa utwory gotowe.",
         {"mentions_projects": [P_NF, P_RT, P_WD, P_LK, P_MU]}),
        ("assistant", "Sporo naraz. Co dalej?"),
        ("user", "nic wielkiego na raz - male odwracalne kroki jak zawsze, zero wielkich przepisywan "
                 "bez potrzeby", {"principle": "pr.small_reversible_steps"}),
        ("user", "generator reelsow chyba faktycznie stoi, ale formalnie tego nie zamykam, zobaczymy "
                 "czy wroce"),
        ("assistant", "A nowe pytania?"),
        ("user", "glownie czy iso apelacyjna droge w sprawie Kwiatowej jesli przegram, ale to za "
                 "wczesnie zeby o tym myslec", {"open_question": "oq.lk.appeal_path"}),
    ))

# ══════════════════════════════ Noise ═══════════════════════════════
# Five conversations deliberately collide, at the word level only, with a
# signal project's own vocabulary ("loom", "pipeline", "agent", "watchdog",
# "ADHD"), in a completely different real-world sense. Fifteen more are
# generic life-admin/health/cooking/other-people's-code/small-talk filler.
# All are tagged kind="noise" and none reference any project id.

def trap(term, sense):
    return {"trap": {"term": term, "real_sense": sense}}


add(id="nt-01-krosno", provider="claude", project=None, kind="noise", model="claude-3-5-sonnet",
    title="krosno do majsterkowania", date="2025-03-15T17:00:00Z",
    messages=linear(
        ("user", "szukam prezentu dla mamy, myslalam o malym krosnie tkackim (loom), robi ostatnio "
                 "makramy", trap("loom", "weaving loom, a fibre-arts device, unrelated to software")),
        ("assistant", "Male stolowe krosno czy raczej podlogowe? Cena mocno zalezy od rozmiaru."),
        ("user", "stolowe, na poczatek, zeby sprawdzila czy w ogole ja to wciaga"),
    ))

add(id="nt-02-rurociag", provider="chatgpt", project=None, kind="noise", model="gpt-4o",
    title="ten artykul o rurociagu", date="2025-06-05T21:00:00Z",
    messages=linear(
        ("user", "czytalas ten artykul o protescie przeciwko nowemu rurociagowi naftowemu (pipeline)? "
                 "ciekawe jak to sie skonczy", trap("pipeline", "oil pipeline, a physical infrastructure project")),
        ("assistant", "Nie widzialem, o ktory region chodzi?"),
        ("user", "chyba jakis lokalny spor, nic wielkiego, tak sobie wspominam"),
    ))

add(id="nt-03-agent-nieruchomosci", provider="chatgpt", project=None, kind="noise", model="gpt-4o",
    title="dzwonil agent, mieszkanie na Gornej", date="2025-09-10T18:00:00Z",
    messages=linear(
        ("user", "dzwonil dzis agent nieruchomosci w sprawie tego mieszkania na Gornej, umowilam "
                 "ogladanie na sobote", trap("agent", "a real-estate agent, unrelated to any coding agent")),
        ("assistant", "Ile pokoi, jakie pietro?"),
        ("user", "dwa pokoje, trzecie pietro, bez windy niestety"),
    ))

add(id="nt-04-watchdog-timer", provider="claude", project=None, kind="noise", model="claude-3-5-sonnet",
    title="resetuje sie losowo", date="2025-12-15T20:00:00Z",
    messages=linear(
        ("user", "pomagam znajomemu z ESP32, plytka resetuje sie losowo w petli, chyba trzeba "
                 "skonfigurowac watchdog timer zeby nie wisiala w nieskonczonosc jak main loop sie "
                 "zablokuje", trap("watchdog", "a hardware watchdog timer on a microcontroller")),
        ("assistant", "WDT z timeoutem kilku sekund i regularny feed() w petli glownej powinno "
                      "wystarczyc."),
        ("user", "ok, sprobuje, to nie ma nic wspolnego z moim skryptem od pilnowania agenta btw"),
    ))

add(id="nt-05-adhd-kolezanka", provider="chatgpt", project=None, kind="noise", model="gpt-4.1",
    title="kolezanka i diagnoza u syna", date="2026-01-25T19:00:00Z",
    messages=linear(
        ("user", "kolezanka dostala dzis diagnoze ADHD dla swojego syna, siedmiolatek, pyta mnie o "
                 "jakies materialy dla rodzicow", trap("ADHD", "a medical/psychiatric diagnosis in a child, not software")),
        ("assistant", "Moge podac kilka ogolnych zrodel, ale to warto skonsultowac z jej psychologiem."),
        ("user", "jasne, przekaze jej, dzieki"),
    ))

add(id="n06-pit", provider="chatgpt", project=None, kind="noise", model="gpt-4o",
    title="PIT / podatki", date="2025-02-01T10:00:00Z",
    messages=linear(
        ("user", "trzeba w koncu ogarnac PIT, odkladam to od tygodnia"),
        ("assistant", "Masz juz wszystkie zaswiadczenia?"),
        ("user", "prawie, brakuje jednego z bankowosci"),
    ))

add(id="n07-dentysta", provider="claude", project=None, kind="noise", model="claude-3-5-sonnet",
    title="przeglad u dentysty", date="2025-02-18T09:00:00Z",
    messages=linear(
        ("user", "przeglad u dentysty jutro, boje sie ze znowu cos znajdzie"),
        ("assistant", "Kiedy byla ostatnia wizyta?"),
        ("user", "pol roku temu, powinno byc ok"),
    ))

add(id="n08-zurek", provider="chatgpt", project=None, kind="noise", model="gpt-4o",
    title="przepis na zurek", date="2025-04-20T17:00:00Z",
    messages=linear(
        ("user", "masz dobry przepis na zurek na zakwasie? robie go pierwszy raz"),
        ("assistant", "Podstawa to zywy zakwas zytni, dobra kielbasa i sporo chrzanu na koniec."),
        ("user", "zakwas juz mam, dzieki"),
    ))

add(id="n09-review-kuby-backend", provider="claude", project=None, kind="noise", model="claude-3-5-sonnet",
    title="review kodu Kuby", date="2025-05-25T20:00:00Z",
    messages=linear(
        ("user", "Kuba prosil o review swojej apki magazynowej w Springu, moglabys rzucic okiem na "
                 "jeden kontroler?", {"noise_category": "other_peoples_code"}),
        ("assistant", "Jasne, wklej fragment."),
        ("user", "@RestController, jeden endpoint do zwrotow, sporo powtorzonej walidacji, nic "
                 "zwiazanego z moimi projektami"),
    ))

add(id="n10-opony", provider="chatgpt", project=None, kind="noise", model="gpt-4o",
    title="wymiana opon", date="2025-07-05T14:00:00Z",
    messages=linear(
        ("user", "trzeba przed wakacjami zmienic opony, umowic warsztat"),
        ("assistant", "Letnie czy juz myslisz o zimowych na pozniej?"),
        ("user", "letnie, zimowe maja jeszcze sezon"),
    ))

add(id="n11-prezent-mama", provider="claude", project=None, kind="noise", model="claude-3-5-sonnet",
    title="urodziny mamy prezent", date="2025-08-15T18:00:00Z",
    messages=linear(
        ("user", "poza krosnem myslalam jeszcze o ksiazce kucharskiej na urodziny mamy"),
        ("assistant", "Jakas konkretna kuchnia ja interesuje ostatnio?"),
        ("user", "wloska, ostatnio duzo gotuje wloskie"),
    ))

add(id="n12-focia", provider="chatgpt", project=None, kind="noise", model="gpt-4o",
    title="focia z wakacji", date="2025-10-01T21:00:00Z",
    messages=linear(
        ("user", "musze w koncu poukladac focia z wakacji, mam z 2000 zdjec nieposortowanych"),
        ("assistant", "Moze najpierw usunac oczywiste duplikaty?"),
        ("user", "no wlasnie tak zrobie, jak znajde czas"),
    ))

add(id="n13-przeziebienie", provider="claude", project=None, kind="noise", model="claude-3-5-sonnet",
    title="przeziebienie", date="2025-11-20T09:00:00Z",
    messages=linear(
        ("user", "zlapalam jakies przeziebienie, katar i gardlo bola"),
        ("assistant", "Odpoczywaj, duzo plynow."),
        ("user", "probuje, ciezko sie powstrzymac od pracy"),
    ))

add(id="n14-sernik", provider="chatgpt", project=None, kind="noise", model="gpt-4.1",
    title="przepis na sernik", date="2026-01-05T16:00:00Z",
    messages=linear(
        ("user", "przepis na sernik na zimno na swieta, bez pieczenia"),
        ("assistant", "Podstawa: twarog, smietana kremowka, zelatyna, herbatniki na spod."),
        ("user", "dzieki, sprobuje w weekend"),
    ))

add(id="n15-review-kuby-frontend", provider="claude", project=None, kind="noise", model="claude-3-5-sonnet",
    title="kolejny review dla Kuby, tym razem frontend", date="2026-02-10T19:00:00Z",
    messages=linear(
        ("user", "Kuba znowu prosi o review, tym razem jakis React frontend do tej samej apki "
                 "magazynowej", {"noise_category": "other_peoples_code"}),
        ("assistant", "Co konkretnie ma sprawdzic?"),
        ("user", "glownie czy formularz zwrotow ma sensowna walidacje po stronie klienta"),
    ))

add(id="n16-ubezpieczenie", provider="chatgpt", project=None, kind="noise", model="gpt-4.1",
    title="ubezpieczenie mieszkania", date="2026-03-05T10:00:00Z",
    messages=linear(
        ("user", "trzeba odnowic ubezpieczenie mieszkania, konczy sie w tym miesiacu"),
        ("assistant", "Ten sam zakres czy chcesz porownac oferty?"),
        ("user", "chyba warto porownac, dawno tego nie robilam"),
    ))

add(id="n17-zab", provider="claude", project=None, kind="noise", model="claude-3-5-sonnet",
    title="zab madrosci", date="2026-04-18T11:00:00Z",
    messages=linear(
        ("user", "zab madrosci znowu daje o sobie znac, chyba trzeba go w koncu usunac"),
        ("assistant", "Bolesnie czy tylko doskwiera?"),
        ("user", "na razie doskwiera, ale wiem ze to sie tylko pogorszy"),
    ))

add(id="n18-grill", provider="chatgpt", project=None, kind="noise", model="gpt-5",
    title="co ugotowac na grilla", date="2026-06-25T17:00:00Z",
    messages=linear(
        ("user", "co ugotowac w ten weekend na grilla, cos poza standardowa kielbasa?"),
        ("assistant", "Warzywa w folii, grillowany ser, marynowany kurczak z ziolami."),
        ("user", "ser grillowany brzmi dobrze, sprobuje"),
    ))

add(id="n19-silownia", provider="claude", project=None, kind="noise", model="claude-3-5-sonnet",
    title="silownia plan na wakacje", date="2026-07-15T08:00:00Z",
    messages=linear(
        ("user", "planuje jak utrzymac jakikolwiek ruch podczas wakacji, bez silowni pod reka"),
        ("assistant", "Masowa pasmo do biegania w pobliżu?"),
        ("user", "tak, powinno wystarczyc na trzy tygodnie"),
    ))

add(id="n20-wordpress-znajomego", provider="chatgpt", project=None, kind="noise", model="gpt-5",
    title="kod znajomego do przejrzenia - stary WordPress", date="2026-08-28T20:00:00Z",
    messages=linear(
        ("user", "znajomy prosi o rzut oka na stara strone WordPress, PHP sprzed lat, nic wspolnego z "
                 "moimi rzeczami", {"noise_category": "other_peoples_code"}),
        ("assistant", "Jaki jest problem - wydajnosc, bezpieczenstwo?"),
        ("user", "podejrzewa ze cos go zainfekowalo, chce zeby ktos zerknal zanim zadzwoni do hostingu"),
    ))

print(f"[gen] {len(CONVERSATIONS)} conversations authored "
      f"({sum(1 for c in CONVERSATIONS if c['kind'] == 'signal')} signal, "
      f"{sum(1 for c in CONVERSATIONS if c['kind'] == 'noise')} noise)")

# ══════════════════════ Claude Projects + Memories ═══════════════════
# Exercises the `claude_projects` / `claude_memories` sniff paths
# (loom/src/archive/ingest_parse.cpp: sniff_export_element). A handful of
# notable phrasings/claims embedded here are hand-listed in ground truth
# under "claude_project_and_memory_evidence" rather than tag-scanned (this
# side of the corpus is small enough that hand-listing is clearer).
CLAUDE_PROJECTS = [
    {"uuid": "claude-project-noteflow", "name": "NoteFlow dev",
     "created_at": "2025-01-10T09:00:00Z",
     "description": "Praca nad NoteFlow: notatnik + czat, docelowo Python -> C++.",
     "prompt_template": "Zawsze pytaj o alternatywy zanim zaproponujesz jedno rozwiazanie. "
                        "Preferuj maly odwracalny krok nad duza nieodwracalna zmiane.",
     "docs": [{"filename": "principles.md",
               "content": "# Zasady dla tego projektu\n\nDomyslnie: maly odwracalny krok zamiast "
                          "duzej nieodwracalnej zmiany. Presety i config to dane; algorytmy i UI "
                          "zostaja w kodzie."}]},
    {"uuid": "claude-project-lokatorka", "name": "Sprawa Kwiatowa",
     "created_at": "2026-01-05T21:00:00Z",
     "description": "Fikcyjna sprawa najmu - kaucja i szkody po zalaniu/plesni.",
     "prompt_template": "Kazdy termin musi miec zapisana podstawe prawna i przypomnienie kilka dni "
                        "wczesniej. Nie wysylaj niczego bez sprawdzenia podstawy.",
     "docs": [{"filename": "deadlines.md",
               "content": "# Terminy\n\nWszystko co dotyczy terminow: liczone od dostarczenia pisma, "
                          "plus dwa dni zapasu, zawsze zapisana podstawa prawna."}]},
]
CLAUDE_MEMORIES = {
    "conversations_memory": "Ola pracuje rownolegle nad kilkoma projektami (NoteFlow/appka od "
                            "notatek, generator reelsow, Stroz, sprawa z Kwiatowej, plyta). Wraca do "
                            "tego samego wzorca: opcjonalnosc jest wazniejsza niz tymczasowa "
                            "szybkosc, wiec czesto zostawia sobie odwrot zamiast oszczedzac dzien.",
    "project_memories": {
        "claude-project-noteflow": "NoteFlow: rdzen w C++, sync REST-based (nie git-based), "
                                   "transkrypcja glosowa przez wymienny TranscriptionProvider.",
    },
}
CLAUDE_PROJECT_AND_MEMORY_EVIDENCE = [
    {"locator": {"provider": "claude", "kind": "project", "id": "claude-project-noteflow", "field": "prompt_template"},
     "principle": "pr.small_reversible_steps",
     "quote": "Preferuj maly odwracalny krok nad duza nieodwracalna zmiane."},
    {"locator": {"provider": "claude", "kind": "project", "id": "claude-project-noteflow", "field": "docs[0]"},
     "principle": "pr.data_over_branches",
     "quote": "Presety i config to dane; algorytmy i UI zostaja w kodzie."},
    {"locator": {"provider": "claude", "kind": "project", "id": "claude-project-lokatorka", "field": "docs[0]"},
     "area": "area.lokatorka.deadlines",
     "quote": "Wszystko co dotyczy terminow: liczone od dostarczenia pisma, plus dwa dni zapasu, "
              "zawsze zapisana podstawa prawna."},
    {"locator": {"provider": "claude", "kind": "memories", "id": "claude-memories", "field": "conversations_memory"},
     "principle": "pr.optionality_over_speed",
     "quote": "opcjonalnosc jest wazniejsza niz tymczasowa szybkosc"},
]

# ══════════════════════════════ Renderers ═════════════════════════════


def assign_times(messages, date_start):
    d0 = dt(date_start)
    return {m["nid"]: d0 + datetime.timedelta(minutes=4 * i) for i, m in enumerate(messages)}


def to_chatgpt_export(conv):
    messages = conv["messages"]
    times = assign_times(messages, conv["date"])
    child_map = {}
    for m in messages:
        child_map.setdefault(m["parent"] or "root", []).append(m["nid"])
    mapping = {"root": {"id": "root", "parent": None, "children": child_map.get("root", []), "message": None}}
    for m in messages:
        mapping[m["nid"]] = {
            "id": m["nid"], "parent": m["parent"] or "root", "children": child_map.get(m["nid"], []),
            "message": {
                "id": m["nid"], "author": {"role": m["role"]},
                "content": {"content_type": "text", "parts": [m["text"]]},
                "create_time": epoch(times[m["nid"]]),
                "metadata": {"model_slug": conv.get("model", "")} if m["role"] == "assistant" else {},
            },
        }
    current = conv.get("current") or (messages[-1]["nid"] if messages else "root")
    return {"id": conv["id"], "title": conv["title"], "create_time": epoch(dt(conv["date"])),
            "current_node": current, "mapping": mapping}


def to_claude_export(conv):
    messages = conv["messages"]
    times = assign_times(messages, conv["date"])
    use_parent = bool(conv.get("claude_fork"))
    out_msgs = []
    for m in messages:
        entry = {"uuid": m["nid"], "sender": "human" if m["role"] == "user" else "assistant",
                 "text": m["text"], "created_at": iso(times[m["nid"]])}
        if use_parent:
            entry["parent_message_uuid"] = m["parent"] or ""
        out_msgs.append(entry)
    current_leaf = conv.get("current") or (messages[-1]["nid"] if messages else "")
    return {"uuid": conv["id"], "name": conv["title"], "created_at": iso(dt(conv["date"])),
            "current_leaf_message_uuid": current_leaf, "chat_messages": out_msgs}


def render_claude_projects():
    return [{"uuid": p["uuid"], "name": p["name"], "created_at": p["created_at"],
             "description": p["description"], "prompt_template": p["prompt_template"],
             "docs": p["docs"]} for p in CLAUDE_PROJECTS]


# ═══════════════════════ Ground-truth extraction ══════════════════════
# Walks every conversation's messages once, reading the SAME tags used to
# author them, and fills in the conceptual-model structures above. This is
# the only place export <-> ground_truth correspondence is computed, so the
# two cannot drift apart.

def collect_ground_truth(conversations):
    principle_phrasings = {p["id"]: [] for p in PRINCIPLES}
    feature_units = {f: [] for f in FEATURE_IDS}
    role_evidence = {pid: {} for pid in PROJECTS_META}
    open_question_units = {q: [] for q in OPEN_QUESTIONS}
    contradiction_evidence = {c["id"]: {} for c in CONTRADICTIONS}
    area_units = {a["id"]: None for a in AREAS}
    fork_units = {}
    alias_mentions = []
    traps = []
    noise = []
    units_relevant = []
    decisions_seen = set()

    for conv in conversations:
        times = assign_times(conv["messages"], conv["date"])
        conv_trap = None
        conv_noise_cats = []
        for m in conv["messages"]:
            tags = m["tags"]
            locator = {"provider": conv["provider"], "conv_id": conv["id"], "node_id": m["nid"],
                       "date": iso(times[m["nid"]])}
            if "principle" in tags:
                pid = tags["principle"]
                assert pid in PRINCIPLE_IDS, f"unknown principle id {pid} in {conv['id']}/{m['nid']}"
                principle_phrasings[pid].append({**locator, "text": tags.get("phrasing", m["text"])})
            if "decision" in tags:
                did = tags["decision"]
                assert did in DECISION_IDS, f"unknown decision id {did} in {conv['id']}/{m['nid']}"
                DECISIONS[did]["unit"] = {**locator, "quote": tags.get("phrasing", m["text"])}
                decisions_seen.add(did)
            if "feature" in tags:
                for fid in tags["feature"]:
                    assert fid in FEATURE_IDS, f"unknown feature id {fid} in {conv['id']}/{m['nid']}"
                    feature_units[fid].append({**locator, "quote": m["text"]})
            if "area" in tags:
                aid = tags["area"]
                assert aid in AREA_IDS, f"unknown area id {aid} in {conv['id']}/{m['nid']}"
                area_units[aid] = {**locator, "quote": m["text"]}
            if "role" in tags:
                for proj_id, role_name in tags["role"]:
                    role_evidence[proj_id].setdefault(role_name, []).append({**locator, "quote": m["text"]})
            if "contradiction" in tags:
                cid, side = tags["contradiction"]
                contradiction_evidence[cid][side] = {**locator, "quote": m["text"]}
            if "open_question" in tags:
                qid = tags["open_question"]
                open_question_units[qid].append({**locator, "quote": m["text"]})
            if "mentions_projects" in tags:
                alias_mentions.append({**locator, "projects": tags["mentions_projects"], "quote": m["text"]})
            if "fork" in tags:
                fork_units.setdefault(tags["fork"], []).append({**locator, "quote": m["text"]})
            if "trap" in tags:
                conv_trap = tags["trap"]
            if "noise_category" in tags:
                conv_noise_cats.append(tags["noise_category"])

        if conv["kind"] == "signal":
            units_relevant.append({
                "conv_id": conv["id"], "provider": conv["provider"], "title": conv["title"],
                "date": conv["date"][:10], "project": conv.get("project"),
                "mentions_projects": sorted({p for m in conv["messages"]
                                             for p in m["tags"].get("mentions_projects", [])}),
                "message_count": len(conv["messages"]),
            })
        elif conv_trap:
            traps.append({"conv_id": conv["id"], "provider": conv["provider"], "title": conv["title"],
                          "date": conv["date"][:10], "term": conv_trap["term"],
                          "real_sense": conv_trap["real_sense"],
                          "why_not_relevant": "uses a project-vocabulary word in an unrelated real-world sense"})
        else:
            noise.append({"conv_id": conv["id"], "provider": conv["provider"], "title": conv["title"],
                          "date": conv["date"][:10], "categories": conv_noise_cats or ["life_admin_or_small_talk"]})

    for p in PRINCIPLES:
        p["phrasings"] = principle_phrasings[p["id"]]
    for fid, units in feature_units.items():
        FEATURES[fid]["units"] = units
    for a in AREAS:
        a["unit"] = area_units[a["id"]]
    for qid, q in OPEN_QUESTIONS.items():
        q["units"] = open_question_units[qid]
    for c in CONTRADICTIONS:
        c["evidence"] = contradiction_evidence[c["id"]]

    missing_decisions = DECISION_IDS - decisions_seen
    assert not missing_decisions, f"decisions with no locator tag: {missing_decisions}"

    return {
        "units_relevant": units_relevant, "noise_traps": traps, "noise_generic": noise,
        "fork_units": fork_units, "alias_mentions": alias_mentions, "role_evidence": role_evidence,
    }


# Hand-authored inferable/absent universal-role slots (§3.4 of the
# conceptual model): these are, BY DEFINITION, not stated anywhere in the
# corpus, so they cannot come from a tag scan. Each one names the checkable
# Expected Property (§2.4) a correct inference must vouch for.
ROLES_INFERABLE_ABSENT = {
    P_NF: {
        "part": {"inferable": [
            {"value": "an embeddings module behind knowledge graph v2",
             "expected_property": "consistent_with(feat.nf.knowledge_graph_v2) & exists_symbol_like('embed')",
             "inference_basis": "nf-10 says knowledge graph v2 'dziala tez offline z lokalnym modelem "
                                 "embeddingow' -- that requires an embeddings module, never named as such"}]},
        "resource": {"inferable": [
            {"value": "a bundled local embedding model file",
             "expected_property": "available_on(offline)",
             "inference_basis": "same nf-10 sentence: an offline-capable local model implies a bundled "
                                 "model artifact even though no filename or format is ever given"}]},
        "check": {"absent": [
            {"question": "any CI / automated test suite",
             "note": "only ad hoc 'testy migracji' mentioned once (nf-08); no CI pipeline is mentioned "
                     "across any of the 12 NoteFlow conversations or the fake repo"}]},
    },
    P_RT: {
        "check": {"absent": [
            {"question": "a consistency check between keyframes of the same character",
             "note": "named as a known gap directly in rt-03, never closed"}]},
        "output": {"absent": [
            {"question": "any finished/published reel",
             "note": "the pipeline never reaches compose; only test renders of earlier stages exist"}]},
    },
    P_WD: {
        "transformation": {"absent": [
            {"question": "any build/release process",
             "note": "Stroz stays a small script across all 7 conversations; no build step is ever mentioned"}]},
    },
    P_LK: {
        "actor": {"inferable": [
            {"value": "a court registry/clerk contact at Sad Rejonowy w Fikcyjnowie",
             "expected_property": "exists_role(institution_contact)",
             "inference_basis": "any filed court proceeding implies a registry contact for procedural "
                                 "correspondence, even though lk-06/lk-07 never name one"}]},
    },
    P_MU: {
        "output": {"absent": [
            {"question": "a fully mixed/mastered EP file",
             "note": "only two finished tracks exist by the end of the corpus (mu-06); no EP-level "
                     "master is ever mentioned"}]},
    },
}


def build_ground_truth():
    gt = collect_ground_truth(CONVERSATIONS)

    projects_out = []
    for pid, meta in PROJECTS_META.items():
        roles = {}
        for role_name, observed in gt["role_evidence"][pid].items():
            roles[role_name] = {"observed": observed, "inferable": [], "absent": []}
        for role_name, extra in ROLES_INFERABLE_ABSENT.get(pid, {}).items():
            roles.setdefault(role_name, {"observed": [], "inferable": [], "absent": []})
            roles[role_name]["inferable"] = extra.get("inferable", [])
            roles[role_name]["absent"] = extra.get("absent", [])
        decisions = sorted((d for d in DECISIONS.values() if d["project"] == pid), key=lambda d: d["date"])
        features = sorted((f for f in FEATURES.values() if f["project"] == pid), key=lambda f: f["id"])
        areas = [a for a in AREAS if a["project"] == pid]
        oqs = [{"id": qid, **q} for qid, q in OPEN_QUESTIONS.items() if q["project"] == pid]
        projects_out.append({
            "id": pid, "name": meta["name"], "kind": meta["kind"], "aliases": meta["aliases"],
            "one_line": meta["one_line"], "universal_roles": roles,
            "versions": VERSIONS[pid]["versions"], "forks": VERSIONS[pid]["forks"],
            "decisions": decisions, "features_status": features, "areas": areas, "open_questions": oqs,
        })

    operators_out = []
    for o in OPERATORS:
        pre = DECISIONS[o["pre_T_example"]]
        post = DECISIONS[o["post_T_example"]]
        operators_out.append({
            **{k: v for k, v in o.items() if k not in ("pre_T_example", "post_T_example")},
            "examples": [
                {"decision_id": pre["id"], "date": pre["date"], "side": "pre_T", "project": pre["project"]},
                {"decision_id": post["id"], "date": post["date"], "side": "post_T", "project": post["project"]},
            ],
        })

    return {
        "schema": "loom.eval.ground_truth/1",
        "corpus_id": "synthetic_dev",
        "generated_by": "loom/tools/gen_synthetic_eval_corpus.py",
        "persona": PERSONA,
        "temporal_cut": {
            "date": T_CUT,
            "rationale": "Induce principles/operators/the project model from everything dated <= T; "
                        "the decisions in `predictions` and each project's decisions after T are the "
                        "temporal-holdout answer key (LOOM_CONCEPTUAL_MODEL.md §7.1).",
        },
        "units": {
            "relevant": gt["units_relevant"],
            "noise_traps": gt["noise_traps"],
            "noise_generic": gt["noise_generic"],
        },
        "cross_project_alias_mentions": gt["alias_mentions"],
        "fork_chat_units": gt["fork_units"],
        "projects": projects_out,
        "principles": PRINCIPLES,
        "operators": operators_out,
        "areas": AREAS,
        "open_questions": [{"id": qid, **q} for qid, q in OPEN_QUESTIONS.items()],
        "contradictions": CONTRADICTIONS,
        "predictions": PREDICTIONS,
        "unpredictable_post_T_decisions": UNPREDICTABLE_POST_T_DECISIONS,
        "claude_project_and_memory_evidence": CLAUDE_PROJECT_AND_MEMORY_EVIDENCE,
        "counts": {
            "conversations_total": len(CONVERSATIONS),
            "conversations_signal": sum(1 for c in CONVERSATIONS if c["kind"] == "signal"),
            "conversations_noise_generic": len(gt["noise_generic"]),
            "conversations_noise_traps": len(gt["noise_traps"]),
            "conversations_chatgpt": sum(1 for c in CONVERSATIONS if c["provider"] == "chatgpt"),
            "conversations_claude": sum(1 for c in CONVERSATIONS if c["provider"] == "claude"),
            "messages_total": sum(len(c["messages"]) for c in CONVERSATIONS),
            "projects": len(PROJECTS_META),
            "principles": len(PRINCIPLES),
            "principle_phrasings_total": sum(len(p["phrasings"]) for p in PRINCIPLES),
            "operators": len(OPERATORS),
            "decisions": len(DECISIONS),
            "features_tracked": len(FEATURES),
            "areas": len(AREAS),
            "open_questions": len(OPEN_QUESTIONS),
            "open_questions_resolved": sum(1 for q in OPEN_QUESTIONS.values() if q["resolved_by"]),
            "contradictions": len(CONTRADICTIONS),
            "predictions": len(PREDICTIONS),
            "claude_projects": len(CLAUDE_PROJECTS),
            "forks_code_lineage": sum(len(VERSIONS[p]["forks"]) for p in VERSIONS),
            "forks_chat_structural": len(gt["fork_units"]) + 3,  # + the 3 pure chat edit-forks (nf-05, lk-02, cx-02)
        },
    }


# ══════════════════════════════════ main ═══════════════════════════════

def _zip_write(zf: zipfile.ZipFile, arcname: str, data: str):
    info = zipfile.ZipInfo(arcname, date_time=(2026, 9, 26, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    zf.writestr(info, data.encode("utf-8"))


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    chatgpt_convs = [to_chatgpt_export(c) for c in CONVERSATIONS if c["provider"] == "chatgpt"]
    claude_convs = [to_claude_export(c) for c in CONVERSATIONS if c["provider"] == "claude"]
    claude_projects = render_claude_projects()

    chatgpt_zip = OUT / "chatgpt_export.zip"
    with zipfile.ZipFile(chatgpt_zip, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        _zip_write(zf, "conversations.json", json.dumps(chatgpt_convs, ensure_ascii=False, indent=1))

    claude_zip = OUT / "claude_export.zip"
    with zipfile.ZipFile(claude_zip, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        _zip_write(zf, "conversations.json", json.dumps(claude_convs, ensure_ascii=False, indent=1))
        _zip_write(zf, "projects.json", json.dumps(claude_projects, ensure_ascii=False, indent=1))
        _zip_write(zf, "memories.json", json.dumps(CLAUDE_MEMORIES, ensure_ascii=False, indent=1))

    gt = build_ground_truth()
    gt_path = OUT / "ground_truth.json"
    gt_path.write_text(json.dumps(gt, ensure_ascii=False, indent=2, sort_keys=False) + "\n", encoding="utf-8")

    for p in (chatgpt_zip, claude_zip, gt_path):
        print(f"[gen] wrote {p.relative_to(ROOT.parent)} ({p.stat().st_size:,} bytes)")
    print(f"[gen] counts: {json.dumps(gt['counts'])}")


if __name__ == "__main__":
    main()

