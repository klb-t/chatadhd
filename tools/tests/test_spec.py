# -*- coding: utf-8 -*-
"""Testy tools/spec.py (unittest, bez sieci, bez modeli).

Uruchomienie:  python3 -m unittest discover -s tools/tests -v
Fixtures to male, syntetyczne próbki trzech formatów, skopiowane z kształtu prawdziwych plików.
"""
import contextlib
import io
import json
import random
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import spec  # noqa: E402

OWNER = '''# Owner requirements log — 2026-09-26

Wstęp rejestru (nie jest pozycją).

## R1 — First thing
> "Pierwszy cytat dosłowny"

Restatement paragraph of R1.

## R2 — Second thing (owner, 2026-10-10)
> "Linia jeden
> linia dwa"
>
> "Druga część"

Body of R2 mentions PH01 and R1.

## R3 — Paraphrase only (note §1)
Only a paraphrase of GPT. (paraphrase)

## Decision — rebuild, don't recover
> "nic już nie szukam"

Body of the decision.
'''

SLOWA = '''# Słowa właściciela — komplet uwag (2026-10-16)
Polecenie właściciela (2026-10-16): „sprawdź wszystko”.
Jak powstał: coś. Cytaty dosłowne w „…”.

## Uzupełnienie 1 — 2026-10-16 12:38 UTC: pamięć
Słowa właściciela, dosłownie:
> pierwsza linia dodatku
> druga linia dodatku

Co z tego wynika (zapis Claude'a):
1. **Nie ma osobnych pojęć.** wniosek asystenta „Pamięć” coś

# Filozofia (`philosophy`)

### Filozofia (PH) — uwagi

Zasady czytania: coś bez numeru.

1. **PH01 — Zachowanie informacji** (2026-01-21)
   „Pierwszy cytat PH01” — `docs/x.md:10`
   Sens: nic nie ginie.

2. **PH02 — Wielowątkowość** (2026-01-23) [dosłownie]
   „Cytat z
   wieloma liniami” — `docs/x.md:11`
   - Sens: rozproszenie.
   Źródła wtórne: `principles.json:5` („Cytat wtórny nie liczy się”)

### Wymagania wyprowadzone (2)

- **PH01** Edycja nie kasuje. (2026-01-21)
- **PH02** Obsługa rozproszenia. (2026-01-23)

---

# Koszty (`costs`)

### Koszty

1. **CL01 — 2026-09-28 — Klucz z limitem**
   Sedno: limit USD 2.
   - „Quote CL01 a” (2026-09-28, `x.md:1`)

2. **AG02 — Edytor (2026-02-06).**
   „Cytat AG02” — 2026-02-06, `x.md:2`

3. **MR03** (2026-02-04) — „Cytat MR03 w nagłówku” (`x.md:3`).

4. **PC04 — stary tryb.** „Cytat PC04” (2026-01-29) [zastąpione przez PC05 — coś]

5. **PC05 — nowy tryb.** „Cytat PC05” (2026-02-01)

**Pominięto (nie są to słowa właściciela):**
- „cudzy cytat” z innego dokumentu

### Wymagania wyprowadzone (5)

- **CL01** Limit. (2026-09-28)
- **AG02** Edytor edytuje wszystko. (2026-02-06)
- **MR03** Zdanie streszczenia. (2026-02-04)
- **PC04** Stary tryb. (2026-01-29)
- **PC05** Nowy tryb. (2026-02-01)
'''

LUKI = '''# Luki — stan kodu (2026-10-16)

Razem: JEST 2, BRAK 2, CZĘŚCIOWO 1.

## Dopisane po przebiegach (2026-10-16)

- **BRAK** `ZZ01–ZZ03` Grupa zakresowa. — następny krok: zrobić.
- **BRAK** `ZZ09` Pojedyncza sierota.

## Filozofia (`philosophy`) — CZĘŚCIOWO 1, JEST 1
*źródło: przebieg 2*

- **CZĘŚCIOWO** `PH01` Opis luki.
  - dowód: plik.cpp:1
  - następny krok: zrobić X
- **JEST** `PH02` Jest.
  - dowód: ok

## Koszty (`costs`) — JEST 1

- **JEST** `CL01` Koszt.
  - dowód: a
'''


def make_root(owner=OWNER, slowa=SLOWA, luki=LUKI) -> Path:
    root = Path(tempfile.mkdtemp(prefix="spec_test_"))
    for role, text in (("owner", owner), ("slowa", slowa), ("luki", luki)):
        p = root / spec.DEFAULT_CONFIG["sources"][role]
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8")
    return root


def run(root, *argv):
    """Uruchamia main, zwraca (kod, stdout). SystemExit z komunikatem jest przekazywany."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = spec.main(["--root", str(root), *argv])
    return rc, buf.getvalue()


def load(root):
    return json.loads((root / "docs/spec/spec.json").read_text(encoding="utf-8"))


def node(sp, alias):
    for n in sp["nodes"]:
        if alias in n["aliases"]:
            return n
    raise KeyError(alias)


class Base(unittest.TestCase):
    def setUp(self):
        self.root = make_root()
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)


class ParseFormats(Base):
    def setUp(self):
        super().setUp()
        run(self.root, "build")
        self.sp = load(self.root)

    def test_counts_and_schema(self):
        self.assertEqual(self.sp["schema"], "loom.spec/1")
        # R1 R2 R3 + decision, preambula + dodatek, PH01 PH02 CL01 AG02 MR03 PC04 PC05, ZZ01-03, ZZ09
        self.assertEqual(len(self.sp["nodes"]), 4 + 2 + 7 + 2)

    def test_owner_multiline_blockquote_and_paraphrase(self):
        r2 = node(self.sp, "R2")
        self.assertEqual(r2["quotes"][0]["text"], '"Linia jeden\nlinia dwa"\n\n"Druga część"')
        self.assertEqual(r2["date"], "2026-10-10")
        self.assertEqual(r2["kind"], "requirement")
        r3 = node(self.sp, "R3")
        self.assertEqual(r3["quotes"], [])
        self.assertTrue(r3["restatement"][0]["text"].endswith("(paraphrase)"))
        self.assertEqual(r3["heading_note"], "note §1")
        dec = node(self.sp, "owner:decision-rebuild-don-t-recover")
        self.assertEqual(dec["kind"], "decision")

    def test_slowa_heading_variants(self):
        ph1 = node(self.sp, "PH01")
        self.assertEqual((ph1["title"], ph1["date"], ph1["kind"]), ("Zachowanie informacji", "2026-01-21", "philosophy"))
        self.assertEqual(ph1["sense"]["text"], "nic nie ginie.")
        self.assertEqual(ph1["summary"]["text"], "Edycja nie kasuje. (2026-01-21)")
        cl = node(self.sp, "CL01")
        self.assertEqual((cl["title"], cl["date"]), ("Klucz z limitem", "2026-09-28"))
        self.assertEqual(cl["sense"]["label"], "Sedno")
        ag = node(self.sp, "AG02")
        self.assertEqual((ag["title"], ag["date"]), ("Edytor", "2026-02-06"))
        mr = node(self.sp, "MR03")
        self.assertIsNone(mr["title"])
        self.assertEqual([q["text"] for q in mr["quotes"]], ["Cytat MR03 w nagłówku"])
        self.assertEqual(mr["date"], "2026-02-04")

    def test_multiline_inline_quote_and_secondary_not_a_quote(self):
        ph2 = node(self.sp, "PH02")
        self.assertEqual([q["text"] for q in ph2["quotes"]], ["Cytat z\n   wieloma liniami"])
        self.assertIn("dosłownie", ph2["tags"])
        self.assertEqual(ph2["quotes"][0]["ref"], "docs/x.md:11")
        self.assertTrue(any(c["label"] == "Źródła wtórne" for c in ph2["commentary"]))

    def test_addenda_nodes(self):
        pre = node(self.sp, "slowa:preambula")
        self.assertEqual([q["text"] for q in pre["quotes"]], ["sprawdź wszystko"])  # „…” bez liter odpada
        add = next(n for n in self.sp["nodes"] if n["theme"] == "addenda" and n["aliases"] != ["slowa:preambula"])
        self.assertEqual(add["kind"], "statement")
        self.assertEqual(add["quotes"][0]["text"], "pierwsza linia dodatku\ndruga linia dodatku")
        self.assertEqual(add["date"], "2026-10-16")

    def test_luki_merge_range_and_orphans(self):
        ph1 = node(self.sp, "PH01")
        self.assertEqual(ph1["status"]["value"], "CZĘŚCIOWO")
        self.assertEqual(ph1["status"]["checked_on"], "2026-10-16")
        self.assertTrue(ph1["status"]["observation"])
        self.assertEqual(ph1["next_step"]["text"], "zrobić X")
        self.assertEqual(ph1["evidence"][0]["text"], "plik.cpp:1")
        rng = node(self.sp, "ZZ02")
        self.assertEqual(rng["files"], ["luki"])
        self.assertIn("ZZ01–ZZ03", rng["aliases"])
        self.assertEqual(rng["next_step"]["text"], "zrobić.")
        self.assertEqual(node(self.sp, "ZZ01")["id"], node(self.sp, "ZZ03")["id"])
        self.assertNotEqual(node(self.sp, "ZZ01")["id"], node(self.sp, "ZZ09")["id"])

    def test_refs_and_supersession(self):
        r2 = node(self.sp, "R2")
        ids = {node(self.sp, a)["id"] for a in ("PH01", "R1")}
        self.assertEqual(set(r2["refs"]), ids)
        pc4, pc5 = node(self.sp, "PC04"), node(self.sp, "PC05")
        self.assertEqual(pc4["superseded_by"], [pc5["id"]])
        self.assertEqual(pc5["supersedes"], [pc4["id"]])
        self.assertIn("zastąpione przez PC05", pc4["supersession_notes"][0]["text"])

    def test_unparsed_prose_is_reported_not_dropped(self):
        gaps = " ".join(g["preview"] for g in self.sp["gaps"])
        self.assertIn("Wstęp rejestru", gaps)
        self.assertIn("Zasady czytania", gaps)
        self.assertIn("Pominięto", gaps)

    def test_expand_codes(self):
        self.assertEqual(spec.expand_codes("MM01–MM10")[:2], ["MM01", "MM02"])
        self.assertEqual(len(spec.expand_codes("MM01–MM10")), 10)
        self.assertEqual(spec.expand_codes("PH05"), ["PH05"])
        self.assertEqual(spec.expand_codes("A1, B2"), ["A1", "B2"])

    def test_check_is_clean(self):
        rc, out = run(self.root, "check")
        self.assertEqual(rc, 0, out)
        self.assertIn("sierota LUKI", out)
        self.assertIn("tylko w LUKI (sieroty, bez SLOWA/R): 2", out)
        self.assertIn("check: czysto", out)
        rc, out = run(self.root, "check", "--strict")
        self.assertEqual(rc, 1, out)


class QuoteGuard(Base):
    def test_altered_quote_in_spec_json_fails_check(self):
        run(self.root, "build")
        p = self.root / "docs/spec/spec.json"
        d = json.loads(p.read_text(encoding="utf-8"))
        node(d, "PH01")["quotes"][0]["text"] = "Pierwszy cytat PH01 (parafraza)"
        p.write_text(spec.jdump(d), encoding="utf-8")
        rc, out = run(self.root, "check")
        self.assertEqual(rc, 1)
        self.assertIn("NIE jest dosłownym fragmentem", out)

    def test_source_edit_without_build_is_stale(self):
        run(self.root, "build")
        own = self.root / spec.DEFAULT_CONFIG["sources"]["owner"]
        own.write_text(own.read_text(encoding="utf-8").replace("Pierwszy cytat", "Pierwszy zmieniony cytat"), encoding="utf-8")
        rc, out = run(self.root, "check")
        self.assertEqual(rc, 1)
        self.assertIn("uruchom: python3 tools/spec.py build", out)
        run(self.root, "build")
        rc, out = run(self.root, "check")
        self.assertEqual(rc, 0, out)

    def test_moved_quote_is_warning_not_error_until_build(self):
        run(self.root, "build")
        p = self.root / "docs/spec/spec.json"
        d = json.loads(p.read_text(encoding="utf-8"))
        q = node(d, "R1")["quotes"][0]
        path, a, b = spec.parse_src(q["src"])
        q["src"] = spec.src_str(path, a + 1, b + 1)  # zly wiersz, tekst jest w pliku
        p.write_text(spec.jdump(d), encoding="utf-8")
        rc, out = run(self.root, "check")
        self.assertIn("przesunięty", out)

    def test_typo_correction_keeps_id(self):
        run(self.root, "build")
        before = node(load(self.root), "PH01")["id"]
        sl = self.root / spec.DEFAULT_CONFIG["sources"]["slowa"]
        sl.write_text(sl.read_text(encoding="utf-8").replace("Pierwszy cytat PH01", "Pierwszy cytat PH01 poprawiony"), encoding="utf-8")
        run(self.root, "build")
        sp = load(self.root)
        self.assertEqual(node(sp, "PH01")["id"], before)
        self.assertEqual(node(sp, "PH01")["quotes"][0]["text"], "Pierwszy cytat PH01 poprawiony")
        rc, out = run(self.root, "check")
        self.assertEqual(rc, 0, out)


class Determinism(Base):
    def read_all(self):
        d = self.root / "docs/spec"
        return {n: (d / n).read_bytes() for n in ("spec.json", "ids.json", "SPEC.md")}

    def test_two_builds_identical_and_ids_allocated_once(self):
        self.assertFalse((self.root / "docs/spec/ids.json").exists())
        run(self.root, "build")
        self.assertTrue((self.root / "docs/spec/ids.json").exists())
        a = self.read_all()
        run(self.root, "build")
        self.assertEqual(a, self.read_all())

    def test_ids_are_opaque_and_unique(self):
        run(self.root, "build")
        sp = load(self.root)
        ids = [n["id"] for n in sp["nodes"]]
        self.assertEqual(len(ids), len(set(ids)))
        for n in sp["nodes"]:
            self.assertRegex(n["id"], r"^s_[0-9a-f]{12}$")

    def test_without_registry_new_ids_but_with_registry_same_ids(self):
        run(self.root, "build")
        first = load(self.root)
        # skasowany spec.json, ids.json zostaje: te same numery
        (self.root / "docs/spec/spec.json").unlink()
        run(self.root, "build")
        self.assertEqual(first, load(self.root))
        # skasowany rejestr: przydzielone na nowo (inne), ale deterministyczne od tego momentu
        (self.root / "docs/spec/ids.json").unlink()
        run(self.root, "build")
        again = load(self.root)
        self.assertNotEqual(first["nodes"][0]["id"], again["nodes"][0]["id"])

    def test_deterministic_with_injected_factory(self):
        counter = iter(range(1, 10000))
        old = spec.ID_FACTORY
        spec.ID_FACTORY = lambda: "s_%012x" % next(counter)
        try:
            run(self.root, "build")
        finally:
            spec.ID_FACTORY = old
        self.assertEqual(load(self.root)["nodes"][0]["id"], "s_000000000001")


class AddCommand(Base):
    def setUp(self):
        super().setUp()
        run(self.root, "build")
        self.qf = self.root / "quote.txt"
        self.qf.write_text('"Nowa wypowiedź\ndruga linia"\n', encoding="utf-8")

    def owner_path(self):
        return self.root / spec.DEFAULT_CONFIG["sources"]["owner"]

    def test_add_new_and_existing_text_byte_identical(self):
        before = self.owner_path().read_text(encoding="utf-8")
        rc, out = run(self.root, "add", "--id", "R999", "--date", "2026-10-23", "--quote-file", str(self.qf),
                      "--title", "Test", "--sense", "Opis.")
        self.assertEqual(rc, 0, out)
        after = self.owner_path().read_text(encoding="utf-8")
        self.assertIn("## R999 — Test (owner, 2026-10-23)", after)
        # usuniecie wstawionej sekcji daje dokladnie stary plik
        start = after.index("## R999")
        end = after.index("## Decision")
        self.assertEqual(after[:start] + after[end:], before)
        sp = load(self.root)
        n = node(sp, "R999")
        self.assertEqual(n["quotes"][0]["text"], '"Nowa wypowiedź\ndruga linia"')
        self.assertEqual(n["date"], "2026-10-23")
        rc, out = run(self.root, "check")
        self.assertEqual(rc, 0, out)
        # istniejace cytaty nietkniete
        self.assertEqual(node(sp, "R1")["quotes"][0]["text"], '"Pierwszy cytat dosłowny"')

    def test_add_refuses_existing_id(self):
        before = self.owner_path().read_bytes()
        for bad in ("R1", "R3"):
            with self.assertRaises(SystemExit) as cm:
                run(self.root, "add", "--id", bad, "--date", "2026-10-23", "--quote-file", str(self.qf))
            self.assertIn("już istnieje", str(cm.exception))
        self.assertEqual(before, self.owner_path().read_bytes())

    def test_add_dry_run_writes_nothing(self):
        before = self.owner_path().read_bytes()
        rc, out = run(self.root, "add", "--id", "R999", "--date", "2026-10-23", "--quote-file", str(self.qf), "--dry-run")
        self.assertIn("dry-run", out)
        self.assertEqual(before, self.owner_path().read_bytes())

    def test_add_rejects_bad_id(self):
        with self.assertRaises(SystemExit):
            run(self.root, "add", "--id", "X5", "--date", "2026-10-23", "--quote-file", str(self.qf))


class Understanding(Base):
    def setUp(self):
        super().setUp()
        run(self.root, "build")

    def test_approve_needs_proposal_and_never_automatic(self):
        sp = load(self.root)
        self.assertTrue(all(n["understanding"]["state"] == "none" for n in sp["nodes"]))
        with self.assertRaises(SystemExit):
            run(self.root, "approve", "R1")
        run(self.root, "propose", "R1", "--text", "Moje rozumienie.", "--date", "2026-10-23")
        self.assertEqual(node(load(self.root), "R1")["understanding"]["state"], "proposed")
        rc, out = run(self.root, "list", "--understanding", "proposed")
        self.assertIn("R1", out)
        run(self.root, "approve", "R1", "--date", "2026-10-24")
        u = node(load(self.root), "R1")["understanding"]
        self.assertEqual((u["state"], u["by"], u["date"], u["text"]), ("approved", "owner", "2026-10-24", "Moje rozumienie."))
        run(self.root, "correct", "R1", "--text", "Raczej tak.", "--date", "2026-10-25")
        u = node(load(self.root), "R1")["understanding"]
        self.assertEqual((u["state"], u["corrected_text"], u["text"]), ("corrected", "Raczej tak.", "Moje rozumienie."))
        self.assertEqual([h["state"] for h in u["history"]], ["proposed", "approved", "corrected"])

    def test_quote_untouched_and_survives_rebuild(self):
        before = node(load(self.root), "PH01")["quotes"]
        run(self.root, "propose", "PH01", "--text", "x")
        run(self.root, "correct", "PH01", "--text", "y")
        run(self.root, "build")
        n = node(load(self.root), "PH01")
        self.assertEqual(n["quotes"], before)
        self.assertEqual(n["understanding"]["state"], "corrected")
        rc, out = run(self.root, "check")
        self.assertEqual(rc, 0, out)

    def test_show_and_list_resolve_aliases_and_ids(self):
        sp = load(self.root)
        nid = node(sp, "PH01")["id"]
        rc, out = run(self.root, "show", "ph01")
        self.assertIn(nid, out)
        self.assertIn("Pierwszy cytat PH01", out)
        rc, out2 = run(self.root, "show", nid)
        self.assertEqual(out, out2)
        rc, out = run(self.root, "list", "--status", "czesciowo")
        self.assertIn("PH01", out)
        self.assertNotIn("PH02", out)
        rc, out = run(self.root, "list", "--theme", "philosophy")
        self.assertIn("PH02", out)


# ---------------------------------------------------------------------------
# test generatywny: losowe korpusy, niezmienniki

WORDS = ["alfa", "żółw", "gęślą", "jaźń", "graf", "węzeł", "ustawienia", "łódź", "model", "— kreska", "przecinek,", "100%"]


def rand_text(rng, n=(2, 9)):
    return " ".join(rng.choice(WORDS) for _ in range(rng.randint(*n)))


def gen_slowa(rng):
    lines = ["# Słowa — test (2026-10-16)", ""]
    expected = {}
    ntheme = rng.randint(1, 4)
    k = 0
    for t in range(ntheme):
        lines += [f"# Temat {t} (`t{t}`)", "", f"### Temat {t}", ""]
        theme_codes = []
        for i in range(rng.randint(1, 8)):
            k += 1
            code = rng.choice(["PH", "CL", "UI", "RE", "OT"]) + f"{k:02d}"
            shape = rng.choice("ABCD")
            quotes = []
            for _ in range(rng.randint(0 if shape != "C" else 1, 3)):
                q = rand_text(rng)
                if rng.random() < 0.2:
                    q += " „wewnętrzny " + rand_text(rng, (1, 3)) + "”"
                if rng.random() < 0.25:
                    parts = q.split(" ")
                    h = len(parts) // 2
                    q = " ".join(parts[:h]) + "\n   " + " ".join(parts[h:])
                quotes.append(q)
            title = rand_text(rng, (2, 5))
            date = f"2026-{rng.randint(1, 12):02d}-{rng.randint(1, 28):02d}"
            body = []
            if shape == "A":
                head = f"{k}. **{code} — {title}** ({date})"
            elif shape == "B":
                head = f"{k}. **{code} — {date} — {title}**"
            elif shape == "D":
                head = f"{k}. **{code} — {title} ({date}).**"
            else:
                head = f"{k}. **{code}** ({date}) — „{quotes[0]}” (`x.md:1`)."
                quotes_body = quotes[1:]
                quotes = quotes
                body = [f"   „{q}” — `x.md:{j}`" for j, q in enumerate(quotes_body, 2)]
            if shape != "C":
                body = [f"   „{q}” — `x.md:{j}`" for j, q in enumerate(quotes, 2)]
            if rng.random() < 0.7:
                body.append(f"   Sens: {rand_text(rng)}.")
            lines.append(head)
            lines += body
            lines.append("")
            expected[code] = {"quotes": quotes, "title": None if shape == "C" else title, "date": date}
            theme_codes.append(code)
        lines += [f"### Wymagania wyprowadzone ({len(theme_codes)})", ""]
        for code in theme_codes:
            lines.append(f"- **{code}** Streszczenie {code}. ({expected[code]['date']})")
        lines += ["", "---", ""]
    return "\n".join(lines) + "\n", expected


class Generative(unittest.TestCase):
    def test_random_corpora_invariants(self):
        for seed in range(40):
            rng = random.Random(seed)
            slowa, expected = gen_slowa(rng)
            luki_items = []
            for code in expected:
                if rng.random() < 0.8:
                    luki_items.append(f"- **{rng.choice(['JEST', 'BRAK', 'CZĘŚCIOWO', 'NIEAKTUALNE', 'SPRZECZNE'])}** `{code}` Luka {code}.")
            luki_items.append("- **BRAK** `QQ01–QQ04` Sierota zakresowa.")
            luki = "# Luki (2026-10-16)\n\n## Temat (`t0`)\n\n" + "\n".join(luki_items) + "\n"
            root = make_root(slowa=slowa, luki=luki)
            try:
                rc, out = run(root, "build")
                self.assertEqual(rc, 0, f"seed {seed}: {out}")
                sp = load(root)
                for code, exp in expected.items():
                    n = node(sp, code)
                    self.assertEqual([q["text"] for q in n["quotes"]], exp["quotes"], f"seed {seed} {code}")
                    self.assertEqual(n["title"], exp["title"], f"seed {seed} {code}")
                    self.assertEqual(n["date"], exp["date"], f"seed {seed} {code}")
                    self.assertIsNotNone(n["summary"], f"seed {seed} {code}")
                coded = [n for n in sp["nodes"] if "slowa" in n["files"] and n["theme"] != "addenda"]
                self.assertEqual(len(coded), len(expected))
                self.assertEqual(node(sp, "QQ03")["id"], node(sp, "QQ01")["id"])
                rc, out = run(root, "check")
                self.assertEqual(rc, 0, f"seed {seed}: {out}")
                a = (root / "docs/spec/spec.json").read_bytes()
                run(root, "build")
                self.assertEqual(a, (root / "docs/spec/spec.json").read_bytes(), f"seed {seed}")
            finally:
                shutil.rmtree(root, ignore_errors=True)


class RealFilesSmoke(unittest.TestCase):
    """Na prawdziwych plikach repozytorium: build w kopii, check czysto (pomijany, gdy plików brak)."""

    def test_real_corpus(self):
        repo = spec.REPO
        cfg = spec.DEFAULT_CONFIG
        if not all((repo / p).exists() for p in cfg["sources"].values()):
            self.skipTest("brak plików źródłowych")
        root = Path(tempfile.mkdtemp(prefix="spec_real_"))
        try:
            for p in cfg["sources"].values():
                (root / p).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(repo / p, root / p)
            rc, out = run(root, "build")
            self.assertEqual(rc, 0)
            rc, out = run(root, "check")
            self.assertEqual(rc, 0, out)
            sp = load(root)
            self.assertGreaterEqual(len(sp["nodes"]), 400)
        finally:
            shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
