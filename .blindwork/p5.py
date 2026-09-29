# ---------------------------------------------------------------------------
# PART 5 - export writers, self-checks, ground truth
# ---------------------------------------------------------------------------
import re
import sys
import unicodedata
import uuid
import zlib

NS = uuid.UUID("5b1c0de5-0000-4000-8000-000000000b02")
ACCOUNT = str(uuid.uuid5(NS, "account"))
ZIP_DATE = (2026, 9, 29, 0, 0, 0)

PERSONA = {
    "name": "Witold Sowa",
    "fictional": True,
    "note": ("Wholly invented for this corpus: a hobbyist tinkerer in Gdynia with a home-automation hub, a "
             "novel-in-progress and a small invoicing tool for his wife's ceramics shop. No real person, "
             "shop, product, statute or data. Any resemblance to real people or to the repo owner's "
             "projects is coincidental."),
}

PROJECTS = [
    {"id": K, "name": "Kotwica", "kind": "home_automation",
     "description": "Raspberry Pi hub in the basement: MQTT sensors (ESP32, DS18B20), boiler relay, heating "
                    "schedule with cheap-tariff pre-heating, presence detection, vacation mode, wall-switch fallback.",
     "aliases": ["Kotwica", "sterownik domu", "hub w piwnicy", "domowy mozg"],
     "inflected_forms": ["Kotwicy", "Kotwice", "Kotwica", "Kotwico", "sterownika domu", "hubie piwnicznym"],
     "alias_stems": ["kotwic", "sterownik dom", "hubie piwnicz", "hub w piwnic", "domowy mozg"]},
    {"id": L, "name": "Latarnik", "kind": "novel_and_writing_tools",
     "description": "A novel about a lighthouse keeper (Anzelm) on a fictional island, plus the writer's toolkit: "
                    "chapter versions as events, continuity checker for characters/timeline, word-count stats, epub export.",
     "aliases": ["Latarnik", "powiesc o latarniku", "ksiazka o latarniku"],
     "inflected_forms": ["Latarnika", "Latarnikowi", "Latarnikiem", "Latarniku", "ksiazce o latarniku", "powiesci o latarniku"],
     "alias_stems": ["latarnik"]},
    {"id": F, "name": "Fakturka", "kind": "small_business_tool",
     "description": "Invoicing tool for his wife Halina's ceramics shop: gapless numbering, VAT per rate, PDF, "
                    "corrections instead of edits, advance invoices, CSV for the accountant.",
     "aliases": ["Fakturka", "programik do faktur", "rozliczenia Haliny"],
     "inflected_forms": ["Fakturki", "Fakturce", "Fakturke", "Fakturka", "Fakturko", "programiku do faktur"],
     "alias_stems": ["fakturk", "fakturc", "programik do faktur", "programiku do faktur"]},
]

FOUNDATIONS = [
    {"id": ST, "name": "storage layer", "aliases": ["Szuflada", "warstwa zapisu", "event store", "dziennik zdarzen"],
     "used_by": [K, L, F],
     "description": "Append-only event store on SQLite, forward-only migrations, upcasting on read."},
    {"id": PL, "name": "plugin system", "aliases": ["system wtyczek", "dodatki", "plugins"],
     "used_by": [K, L, F],
     "description": "Folder-scanned plugins with manifest + declared capabilities; fault isolation."},
    {"id": BUS, "name": "event bus", "aliases": ["Szyna", "szyna zdarzen", "event bus"],
     "used_by": [K, L, F],
     "description": "In-process pub/sub, queue per subscriber, idempotent handlers, outbox."},
]

PRINCIPLES = [
    {"id": "pr.boring_tech", "level": "strategy", "form": "heuristic",
     "statement_pl": "Wybieraj nudne, sprawdzone technologie zamiast nowinek.",
     "statement_en": "Prefer boring, proven technology over novelty."},
    {"id": "pr.append_only", "level": "value", "form": "invariant",
     "statement_pl": "Nigdy nie kasuj ani nie nadpisuj danych - dopisuj; stan to widok na historie.",
     "statement_en": "Never delete or overwrite data - append; state is a view over history."},
    {"id": "pr.manual_fallback", "level": "value", "form": "invariant",
     "statement_pl": "Kazda automatyka musi miec reczny tryb dzialajacy bez niej (test: trzecia w nocy, bez internetu).",
     "statement_en": "Every automation needs a manual mode that works without it (the 3 a.m. offline test)."},
    {"id": "pr.replaceable_pieces", "level": "strategy", "form": "heuristic",
     "statement_pl": "Buduj male klocki z wyraznymi granicami, wymienialne w jeden wieczor.",
     "statement_en": "Build small pieces with clear borders, replaceable in one evening."},
    {"id": "pr.write_the_why", "level": "epistemic", "form": "default",
     "statement_pl": "Zapisuj dlaczego, nie co.",
     "statement_en": "Record the why, not the what."},
    {"id": "pr.own_your_data", "level": "value", "form": "invariant",
     "statement_pl": "Dane w otwartych formatach, do odczytania zwyklym notatnikiem za 20 lat; zadnego vendor locka.",
     "statement_en": "Data in open formats readable by a plain editor in 20 years; no vendor lock-in."},
    {"id": "pr.ugly_first", "level": "strategy", "form": "heuristic",
     "statement_pl": "Brzydka, skonczona pierwsza wersja w ~14 dni; polerka dopiero potem; obcinaj zakres zamiast przedluzac.",
     "statement_en": "Ship an ugly, finished first version in ~14 days; polish later; cut scope instead of extending."},
]

DECISIONS = {
    "dec.manual_override_wall_switch": "Wall switch bypasses all electronics; automation is an add-on, not a pillar.",
    "dec.chapter_versions_as_events": "Every chapter save is an event; current text = latest, history retained.",
    "dec.gapless_numbering_at_commit": "Invoice numbers are assigned at commit inside a transaction, drafts have none.",
    "dec.issued_documents_immutable": "Issued invoices are never edited; a correcting document references the original.",
    "dec.csv_bom_semicolon": "Accountant export: CSV, semicolon, UTF-8 BOM, decimal comma, document number as text.",
    "dec.event_store_sqlite": "One SQLite file per project via a shared event-store library; domain lives in payloads.",
    "dec.forward_only_migrations": "Schema changes via PRAGMA user_version, forward-only, file copy before migrating.",
    "dec.plugin_manifest_capabilities": "Plugins live in a folder with manifest.toml and declared capabilities + core API version.",
    "dec.plugin_fault_isolation": "Plugin calls are wrapped with timeout; N consecutive failures disable the plugin.",
    "dec.inproc_bus_queue_per_subscriber": "In-process bus with hierarchical topics; one queue per subscriber.",
    "dec.idempotent_handlers": "Handlers are idempotent; subscriber offsets are stored in the same transaction as effects.",
    "dec.outbox_pattern": "Event + to-send marker written in one transaction; a separate worker publishes.",
    "dec.core_versioned_package": "Shared core is a versioned package with contract tests run in each project's CI.",
    "dec.advance_invoices_as_negative_lines": "Final invoice carries full value and a negative line for advances.",
    "dec.upcast_on_read": "Old event versions are upcast in memory on read; stored events are never rewritten.",
    "dec.snapshots_are_derived": "Snapshots are a rebuildable cache carrying the last event hash; the old log is archived, never deleted.",
    "dec.core_has_no_domain_deps": "Core never imports from domain code; CI enforces the dependency direction.",
}

HARDNESS = {"explicit_name": "easy", "inflected_alias": "medium", "architecture_unnamed": "hard",
            "paraphrase_only": "hard", "shared_foundation": "hard", "philosophy": "hard",
            "multi_topic_late": "hard"}

FORK_CONVS = {"a03", "p02", "s06", "m03", "n04", "n11", "t05", "e04"}   # ChatGPT regenerate-forks (off the current path)

CLAUDE_PROJECTS = [
    {"code": "warsztat", "uuid": str(uuid.uuid5(NS, "cproj-warsztat")), "name": "Warsztat",
     "description": "Jak buduje rzeczy: styl pracy i zasady.",
     "prompt_template": ("Pomagasz Witoldowi. Lubi nudne, sprawdzone narzedzia, male wymienialne klocki i dane w "
                         "otwartych plikach. Kazda automatyka ma miec reczny tryb. Odpowiadaj krotko."),
     "docs": [{"filename": "zasady.md", "content": (
         "# Zasady\n- nigdy nie kasuj, dopisuj\n- pierwsza wersja brzydka, 14 dni\n- zapisuj dlaczego\n"
         "- test trzeciej w nocy: co gdy internet lezy?\n")}],
     "relevance": {"kind": "philosophy", "principles": ["pr.boring_tech", "pr.append_only", "pr.manual_fallback",
                                                        "pr.replaceable_pieces", "pr.write_the_why", "pr.ugly_first"]}},
    {"code": "sklep", "uuid": str(uuid.uuid5(NS, "cproj-sklep")), "name": "Sklep Haliny",
     "description": "Kontekst sklepu z ceramika mojej zony.",
     "prompt_template": "Pomagasz z drobnymi sprawami sklepu z ceramika (kubki, miski, jarmarki).",
     "docs": [{"filename": "cennik.md", "content": "# Cennik\n- kubek maly 45 zl\n- miska 60 zl\n- komplet 12 kubkow z nadrukiem 480 zl\n"}],
     "relevance": {"kind": "project_context", "projects": [F]}},
]
CLAUDE_MEMORIES = {
    "conversations_memory": ("Witold (Gdynia) buduje dla siebie kilka narzedzi po godzinach. Woli nudne technologie, "
                             "dopisywanie zamiast kasowania i reczny tryb dla kazdej automatyki."),
    "project_memories": {CLAUDE_PROJECTS[0]["uuid"]: "Zasady budowania: male klocki, otwarte formaty, brzydka pierwsza wersja.",
                         CLAUDE_PROJECTS[1]["uuid"]: "Halina prowadzi sklep z ceramika; ksiegowa to pani Grazyna."},
}


def fold(s: str) -> str:
    s = s.replace("ł", "l").replace("Ł", "L")
    return "".join(ch for ch in unicodedata.normalize("NFKD", s) if not unicodedata.combining(ch)).lower()


def uid(*parts: str) -> str:
    return str(uuid.uuid5(NS, "|".join(parts)))


def crc(s: str) -> int:
    return zlib.crc32(s.encode("utf-8"))


PROTECTED = ("kotwic", "latarnik", "fakturk", "fakturc", "szuflad", "szyn", "wtyczk", "sterownik", "hub", "programik",
             "storage", "migracj", "manuskrypt", "zdarzeni", "czujnik", "rozdzia", "faktur", "harmonogram", "przekaznik",
             "watchdog", "zaliczk")
_WORD = re.compile(r"[A-Za-zĄ-ż]+")


def _typo_word(w: str, h: int) -> str:
    if len(w) < 6 or any(p in fold(w) for p in PROTECTED):
        return w
    i = 1 + h % (len(w) - 3)
    return w[:i] + w[i + 1] + w[i] + w[i + 2:]


def add_typos(text: str, seed: str) -> str:
    """Swap two adjacent inner letters in ~1 word of a message (never in protected alias stems)."""
    words = [m for m in _WORD.finditer(text) if len(m.group()) >= 6]
    if not words:
        return text
    m = words[crc(seed + text[:20]) % len(words)]
    return text[:m.start()] + _typo_word(m.group(), crc(seed + m.group())) + text[m.end():]


def restore_diacritics(text: str) -> str:
    def rep(m):
        w = m.group()
        r = DIAC.get(w.lower())
        if r is None:
            return w
        if w.isupper() and len(w) > 1:
            return r.upper()
        if w[0].isupper():
            return r[0].upper() + r[1:]
        return r
    return _WORD.sub(rep, text)


_PL_HINT = {"sie", "nie", "jak", "czy", "jest", "ze", "zeby", "ale", "ktory", "mam", "chce", "dla", "przy", "bez",
            "tak", "juz", "tylko", "teraz", "dobra", "dobrze", "dzieki", "trzeba", "moze", "mozna", "tez", "po", "od",
            "sa", "co", "ok", "na", "do", "w", "z", "i"}
_PL_STRONG = {"sie", "nie", "jak", "czy", "jest", "ze", "zeby", "ale", "ktory", "mam", "chce", "dla", "przy", "bez",
              "tak", "juz", "tylko", "teraz", "dobra", "dobrze", "dzieki", "trzeba", "moze", "mozna", "tez", "po", "od", "sa"}


def looks_polish(text: str) -> bool:
    return any(w.lower() in _PL_STRONG for w in _WORD.findall(text))


_FILLERS = ["eee ", "no wiec ", "yyy ", "halo "]


def dictate(text: str, seed: str) -> str:
    """Speech-to-text artefacts: lower-case, spoken punctuation, a filler word."""
    t = text[:1].lower() + text[1:]
    t = t.replace(", ", " przecinek ", 2).replace(". ", " kropka ", 1).replace("?", " znak zapytania", 1)
    return _FILLERS[crc(seed) % len(_FILLERS)] + t


def render_messages(c: dict) -> list[str]:
    """Deterministic surface noise: ~12% of convs dictated, ~30% typos in user turns, ~55% Polish diacritics."""
    h = crc("surface|" + c["id"])
    typos = (h % 100) < 30
    diac = ((h >> 8) % 100) < 55
    dictated = ((h >> 16) % 100) < 12
    out = []
    for i, t in enumerate(c["msgs"]):
        if dictated and i % 2 == 0 and looks_polish(t):
            t = dictate(t, c["id"] + str(i))
        if typos and i % 2 == 0:
            t = add_typos(t, c["id"] + str(i))
        if diac and looks_polish(t):
            t = restore_diacritics(t)
        out.append(t)
    return out


def msg_times(c: dict) -> list[datetime.datetime]:
    d = datetime.date.fromisoformat(c["date"])
    h = crc("time|" + c["id"])
    t0 = datetime.datetime(d.year, d.month, d.day, 7 + h % 14, (h >> 4) % 50, (h >> 9) % 60,
                           tzinfo=datetime.timezone.utc)
    out, t = [], t0
    for i in range(len(c["msgs"])):
        t = t + datetime.timedelta(seconds=90 + (h >> (i % 7)) % 400 + (i % 2) * 60)
        out.append(t)
    return out


def chatgpt_conv(c: dict) -> dict:
    cid = uid("chatgpt", c["id"])
    texts, times = render_messages(c), msg_times(c)
    root = uid(cid, "root")
    sysn = uid(cid, "sys")
    mapping = {root: {"id": root, "parent": None, "children": [sysn], "message": None},
               sysn: {"id": sysn, "parent": root, "children": [],
                      "message": {"id": sysn, "author": {"role": "system", "name": None},
                                  "content": {"content_type": "text", "parts": [""]},
                                  "create_time": None, "status": "finished_successfully",
                                  "metadata": {"is_visually_hidden_from_conversation": True}}}}
    prev, last = sysn, sysn
    for i, text in enumerate(texts):
        nid = uid(cid, str(i))
        role = "user" if i % 2 == 0 else "assistant"
        meta = {"model_slug": "gpt-4o"} if role == "assistant" else {}
        mapping[nid] = {"id": nid, "parent": prev, "children": [],
                        "message": {"id": nid, "author": {"role": role, "name": None},
                                    "content": {"content_type": "text", "parts": [text]},
                                    "create_time": times[i].timestamp(), "status": "finished_successfully",
                                    "metadata": meta}}
        mapping[prev]["children"].append(nid)
        if c["id"] in FORK_CONVS and i == 1:
            alt = uid(cid, "alt1")
            mapping[alt] = {"id": alt, "parent": prev, "children": [],
                            "message": {"id": alt, "author": {"role": "assistant", "name": None},
                                        "content": {"content_type": "text", "parts": [
                                            "Zalezy od szczegolow - opisz prosze wiecej, wtedy podpowiem konkretniej."]},
                                        "create_time": times[i].timestamp() - 5, "status": "finished_successfully",
                                        "metadata": {"model_slug": "gpt-4o"}}}
            mapping[prev]["children"].insert(0, alt)   # abandoned regenerate sibling, off the current path
        prev = last = nid
    return {"id": cid, "conversation_id": cid, "title": c["title"], "create_time": times[0].timestamp(),
            "update_time": times[-1].timestamp(), "current_node": last, "mapping": mapping,
            "default_model_slug": "gpt-4o"}


def iso(t: datetime.datetime, h: int) -> str:
    return t.replace(microsecond=(h % 900000) + 1000).isoformat().replace("+00:00", "Z")


def claude_conv(c: dict) -> dict:
    cid = uid("claude", c["id"])
    texts, times = render_messages(c), msg_times(c)
    h = crc("iso|" + c["id"])
    msgs = []
    for i, text in enumerate(texts):
        role = "human" if i % 2 == 0 else "assistant"
        msgs.append({"uuid": uid(cid, str(i)), "text": text,
                     "content": [{"type": "text", "text": text}], "sender": role,
                     "created_at": iso(times[i], h + i), "updated_at": iso(times[i], h + i),
                     "attachments": [], "files": []})
    return {"uuid": cid, "name": c["title"], "created_at": iso(times[0], h), "updated_at": iso(times[-1], h),
            "account": {"uuid": ACCOUNT}, "chat_messages": msgs}


def export_id(c: dict) -> str:
    return uid(c["provider"], c["id"])


def zip_bytes(files: dict[str, bytes]) -> bytes:
    import io
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as z:
        for name in sorted(files):
            zi = zipfile.ZipInfo(name, ZIP_DATE)
            zi.compress_type = zipfile.ZIP_STORED
            zi.external_attr = 0o644 << 16
            z.writestr(zi, files[name])
    return buf.getvalue()


def dumps(o) -> bytes:
    return (json.dumps(o, ensure_ascii=False, indent=1) + "\n").encode("utf-8")


# ---- self-checks ------------------------------------------------------------
def stems_hit(c: dict) -> list[str]:
    txt = fold(" ".join(c["msgs"]))
    return [p["id"] for p in PROJECTS if any(s in txt for s in p["alias_stems"])]


def self_check() -> None:
    by_id = {c["id"]: c for c in CONVS}
    for c in CONVS:
        for i, m in enumerate(c["msgs"]):
            assert isinstance(m, str) and m.strip(), (c["id"], i)
        if c["kind"] == "relevant":
            cat = c["category"]
            base = by_id[c["duplicate_of"]]["category"] if cat == "near_duplicate" else cat
            hits = stems_hit(c)
            if base in ("explicit_name", "inflected_alias"):
                assert set(c["projects"]) <= set(hits) and hits, ("must name project", c["id"], hits)
            else:
                assert not hits, ("must NOT name project", c["id"], hits)
            assert c["projects"] or base == "philosophy", c["id"]
            for d in c["decisions"]:
                assert d in DECISIONS, d
            for p in c["principles"]:
                assert any(x["id"] == p for x in PRINCIPLES), p
        elif c["kind"] == "noise":
            assert not stems_hit(c), ("noise names a project", c["id"])
        elif c["kind"] == "trap":
            assert fold(c["term"]) in fold(" ".join(c["msgs"])), ("trap term absent", c["id"])
            assert c["collides_with"], c["id"]
    ids = {export_id(c) for c in CONVS}
    assert len(ids) == len(CONVS)
    # surface noise (typos/dictation/diacritics) must not destroy the truth-defining tokens
    for c in CONVS:
        rt = fold(" ".join(render_messages(c)))
        if c["kind"] == "trap":
            assert fold(c["term"]) in rt, ("trap term lost in rendering", c["id"])
        if c["kind"] == "relevant":
            base = by_id[c["duplicate_of"]]["category"] if c["category"] == "near_duplicate" else c["category"]
            hits = [p["id"] for p in PROJECTS if any(s in rt for s in p["alias_stems"])]
            if base in ("explicit_name", "inflected_alias"):
                assert set(c["projects"]) <= set(hits), ("alias lost in rendering", c["id"])
            else:
                assert not hits, ("alias appeared in rendering", c["id"])


def build_ground_truth() -> dict:
    order = sorted(CONVS, key=lambda c: (c["date"], c["provider"], c["id"]))
    label = {c["id"]: f"U{n + 1:03d}" for n, c in enumerate(order)}
    by_id = {c["id"]: c for c in CONVS}

    def base_row(c):
        return {"label": label[c["id"]], "conv_id": export_id(c), "provider": c["provider"], "title": c["title"],
                "date": c["date"], "message_count": len(c["msgs"])}

    relevant, traps, noise = [], [], []
    for c in order:
        r = base_row(c)
        if c["kind"] == "relevant":
            cat = c["category"]
            basecat = by_id[c["duplicate_of"]]["category"] if cat == "near_duplicate" else cat
            r.update({"project": c["projects"][0] if c["projects"] else None, "projects": c["projects"],
                      "mentions_projects": [], "foundations": c["foundations"], "category": cat,
                      "hardness": HARDNESS[basecat], "names_project_explicitly": bool(stems_hit(c)),
                      "principles": c["principles"], "decisions": c["decisions"]})
            if "duplicate_of" in c:
                r["duplicate_of"] = export_id(by_id[c["duplicate_of"]])
                r["duplicate_of_label"] = label[c["duplicate_of"]]
                r["base_category"] = basecat
            if c["note"]:
                r["note"] = c["note"]
            relevant.append(r)
        elif c["kind"] == "trap":
            r.update({"term": c["term"], "real_sense": c["real_sense"], "collides_with": c["collides_with"],
                      "why_not_relevant": "uses a project/module vocabulary word in an unrelated real-world sense"})
            traps.append(r)
        else:
            r.update({"categories": [c["topic"]]})
            noise.append(r)

    dup_groups = [[export_id(by_id[c["duplicate_of"]]), export_id(c)] for c in order if "duplicate_of" in c]

    principles = []
    for p in PRINCIPLES:
        units = [x["conv_id"] for x in relevant if p["id"] in x["principles"]]
        principles.append(dict(p, phrasing_units=units,
                               philosophy_only_units=[x["conv_id"] for x in relevant
                                                      if p["id"] in x["principles"] and x["category"] == "philosophy"]))
    decisions = []
    for did, stmt in DECISIONS.items():
        us = [x for x in relevant if did in x["decisions"]]
        decisions.append({"id": did, "statement": stmt, "units": [x["conv_id"] for x in us],
                          "projects": sorted({p for x in us for p in x["projects"]})})

    projects = []
    for p in PROJECTS:
        keep = {k: v for k, v in p.items() if k != "alias_stems"}
        keep["relevant_units"] = [x["conv_id"] for x in relevant if p["id"] in x["projects"]]
        keep["units_not_naming_project"] = [x["conv_id"] for x in relevant
                                            if p["id"] in x["projects"] and not x["names_project_explicitly"]]
        projects.append(keep)
    foundations = [dict(f, units=[x["conv_id"] for x in relevant if f["id"] in x["foundations"]]) for f in FOUNDATIONS]

    cat_counts = {k: sum(1 for x in relevant if x["category"] == k) for k in CATS}
    hard_counts = {}
    for x in relevant:
        hard_counts[x["hardness"]] = hard_counts.get(x["hardness"], 0) + 1
    multi = [x["conv_id"] for x in relevant if len(x["projects"]) > 1]
    artifacts = [{"kind": "claude_project", "uuid": p["uuid"], "name": p["name"], "relevance": p["relevance"]}
                 for p in CLAUDE_PROJECTS]
    counts = {
        "conversations_total": len(CONVS), "relevant": len(relevant), "noise_traps": len(traps),
        "noise_generic": len(noise),
        "conversations_chatgpt": sum(1 for c in CONVS if c["provider"] == "chatgpt"),
        "conversations_claude": sum(1 for c in CONVS if c["provider"] == "claude"),
        "messages_total": sum(len(c["msgs"]) for c in CONVS),
        "relevant_by_category": cat_counts, "relevant_by_hardness": hard_counts,
        "relevant_multi_label": len(multi), "relevant_not_naming_project": sum(1 for x in relevant if not x["names_project_explicitly"]),
        "relevant_by_project": {p["id"]: sum(1 for x in relevant if p["id"] in x["projects"]) for p in PROJECTS},
        "relevant_philosophy_only": cat_counts["philosophy"], "near_duplicate_pairs": len(dup_groups),
        "principles": len(PRINCIPLES), "decisions": len(DECISIONS), "foundations": len(FOUNDATIONS),
    }
    return {
        "schema": "loom.eval.ground_truth/1", "corpus_id": "blind_catalog_v2",
        "generated_by": "loom/tools/gen_blind_catalog_v2.py", "persona": dict(PERSONA, git_author="Witold Sowa <witold.sowa@example.invalid>"),
        "purpose": ("Blind validation set for selective-import catalog scoring: find every relevant conversation "
                    "(project work, shared foundations, philosophy) even when no project is named."),
        "units": {"relevant": relevant, "noise_traps": traps, "noise_generic": noise},
        "multi_label_units": multi, "duplicate_groups": dup_groups,
        "projects": projects, "foundations": foundations, "principles": principles, "decisions": decisions,
        "non_conversation_artifacts": artifacts, "counts": counts,
    }


def build_all() -> dict[str, bytes]:
    self_check()
    order = sorted(CONVS, key=lambda c: (c["date"], c["id"]))
    gpt = [chatgpt_conv(c) for c in order if c["provider"] == "chatgpt"]
    cla = [claude_conv(c) for c in order if c["provider"] == "claude"]
    projects_json = [{"uuid": p["uuid"], "name": p["name"], "description": p["description"],
                      "prompt_template": p["prompt_template"], "is_private": True,
                      "docs": [{"uuid": uid(p["uuid"], d["filename"]), "filename": d["filename"], "content": d["content"],
                                "created_at": "2025-01-05T10:00:00Z"} for d in p["docs"]],
                      "created_at": "2025-01-05T10:00:00Z"} for p in CLAUDE_PROJECTS]
    return {
        "chatgpt_export.zip": zip_bytes({"conversations.json": dumps(gpt)}),
        "claude_export.zip": zip_bytes({"conversations.json": dumps(cla), "projects.json": dumps(projects_json),
                                        "memories.json": dumps(CLAUDE_MEMORIES)}),
        "ground_truth.json": dumps(build_ground_truth()),
    }


def main(argv: list[str]) -> int:
    files = build_all()
    if "--check" in argv:
        bad = [n for n, b in files.items() if not (OUT / n).exists() or (OUT / n).read_bytes() != b]
        print("MISMATCH: " + ", ".join(bad) if bad else "OK: committed corpus matches generator")
        return 1 if bad else 0
    OUT.mkdir(parents=True, exist_ok=True)
    for n, b in files.items():
        (OUT / n).write_bytes(b)
    c = json.loads(files["ground_truth.json"])["counts"]
    print(json.dumps(c, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
