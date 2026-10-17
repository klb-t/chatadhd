"""Review P38 — a redacted package keeps nothing in clear and still reproduces:
the batch-edit redaction reaches preserve and every side file (F1, F27); the
recipe carries references, not masks, and runs only with the dictionary
(F2, F25); the manifest has no dictionary term (F26); attachment names and the
account are redactable (F16); Polish inflection is masked from the stem (F28);
the staged cut follows the package after an undo (F20); only used attachments
are copied (F3)."""
import contextlib
import io
import json
import shutil
import zipfile

from _support import EXPORTS, ExtractTest
from loom.tools.extract import cli
from loom.tools.extract.redact import Redactor

TERMS = ["Migracja", "schemat"]


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = cli.main([str(a) for a in argv])
    return code, out.getvalue(), err.getvalue()


def package_text(pkg) -> tuple[str, list[str]]:
    """Every byte of the package as text (zip members included) and every path in it."""
    blob, names = [], []
    for p in sorted(pkg.rglob("*")):
        if not p.is_file():
            continue
        names.append(p.relative_to(pkg).as_posix())
        if p.suffix == ".zip":
            with zipfile.ZipFile(p) as z:
                for n in z.namelist():
                    blob.append(z.read(n).decode("utf-8", errors="replace"))
        else:
            blob.append(p.read_bytes().decode("utf-8", errors="replace"))
    return "\n".join(blob), names


class RedactedPackageTest(ExtractTest):
    def _eng(self):
        eng = self.engine(EXPORTS / "openai_2026_sharded.zip")
        eng.op("select.expr", {"expr": "kind:conversation"})
        eng.op("package.copy", {})
        return eng

    def assertClean(self, pkg, terms=TERMS):
        blob, names = package_text(pkg)
        low = blob.casefold()
        for term in terms:
            stem = term.casefold()[:-1] if term[-1].lower() in "aeiouy" else term.casefold()
            self.assertNotIn(stem, low, f"{term!r} left in the package")
            self.assertFalse([n for n in names if stem in n.casefold()], f"{term!r} in a file name")
        return blob

    def test_redaction_edit_reaches_preserve_and_every_side_file(self):
        eng = self._eng()
        eng.op("edit.redact", {"where": "conversation", "profile": "personal", "terms": TERMS})
        eng.op("cut", {})  # the presets: preserve + text
        eng.op("write", {"package": str(self.tmp / "pkg")})
        pkg = self.tmp / "pkg"
        self.assertTrue((pkg / "units.jsonl").exists() and (pkg / "timeline.jsonl").exists())
        self.assertClean(pkg)
        rep = (pkg / "RAPORT.md").read_text(encoding="utf-8")
        self.assertIn("redakcję z edycji zastosowano też do formatu źródłowego", rep)
        self.assertIn("preserve = bajty źródła", rep)
        red = json.loads((pkg / "redaction.json").read_text(encoding="utf-8"))
        self.assertGreater(red["entries"], 0, "preserve and the side files were masked too")

    def test_redaction_edit_without_a_cut_masks_the_side_files(self):
        eng = self._eng()
        eng.op("edit.redact", {"where": "conversation", "profile": "personal", "terms": TERMS})
        eng.op("write", {"package": str(self.tmp / "pkg")})
        self.assertClean(self.tmp / "pkg")

    def test_cut_redaction_recipe_reproduces_only_with_the_dictionary(self):
        eng = self._eng()
        eng.settings["sentence"] = "wytnij wszystko o Migracja"
        eng.op("select.expr", {"expr": "title~/Migracja/", "mode": "intersect"})
        eng.op("cut", {"profiles": ["preserve", "text", "flat"], "redaction": "personal", "dictionary": TERMS})
        w = eng.op("write", {"package": str(self.tmp / "a")})
        a = self.tmp / "a"
        self.assertClean(a)
        rec = json.loads((a / "recipe.json").read_text(encoding="utf-8"))
        cut = next(s for s in rec["steps"] if s["op"] == "cut")
        self.assertTrue(cut["params"]["dictionary"]["secret"].startswith("sha256:"), "a reference, not a mask")
        expr = next(s for s in rec["steps"] if s["op"] == "select.expr" and s["params"].get("mode") == "intersect")
        self.assertIn("secret", expr["params"]["expr"], "an expression literal with a dictionary term is a reference")
        man = json.loads((a / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(man["recipe"]["body"]["steps"], rec["steps"], "the manifest's recipe is the same protected one")
        self.assertNotIn("Migracja", json.dumps(man, ensure_ascii=False))
        # reproduce from a recipe file elsewhere: the local store (outside the package) resolves the references
        other = self.tmp / "elsewhere"
        other.mkdir()
        shutil.copyfile(a / "recipe.json", other / "r.json")
        code, out, err = run("przepis", "uruchom", other / "r.json", "--pakiet", self.tmp / "b")
        self.assertEqual(code, 0, err)
        b_man = json.loads((self.tmp / "b/manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(b_man["reproducible_hash"], w["reproducible_hash"])
        self.assertClean(self.tmp / "b")
        # without the dictionary nothing runs (no silent un-redacted package)
        store_copy = self.tmp / "store-copy.json"
        shutil.move(self.tmp / "state" / "secrets.json", store_copy)
        code, out, err = run("przepis", "uruchom", other / "r.json", "--pakiet", self.tmp / "c")
        self.assertNotEqual(code, 0)
        self.assertIn("słownik", err)
        self.assertFalse((self.tmp / "c/manifest.json").exists())
        code, out, err = run("odtworz", a, "--pakiet", self.tmp / "d")
        self.assertNotEqual(code, 0)
        self.assertIn("słownik", err)
        # the term list alone does not resolve the expression literal (a value of its own): still refused
        terms = self.write(self.tmp / "slownik.txt", "schemat\nMigracja\n")
        code, out, err = run("--slownik-plik", terms, "przepis", "uruchom", other / "r.json", "--pakiet", self.tmp / "e")
        self.assertNotEqual(code, 0)
        # the store file the user keeps: it runs again and gives the same package
        code, out, err = run("--slownik-plik", store_copy, "przepis", "uruchom", other / "r.json", "--pakiet", self.tmp / "e")
        self.assertEqual(code, 0, err)
        self.assertEqual(json.loads((self.tmp / "e/manifest.json").read_text(encoding="utf-8"))["reproducible_hash"], w["reproducible_hash"])

    def test_a_term_list_resolves_a_dictionary_only_recipe(self):
        eng = self._eng()
        eng.op("cut", {"profiles": ["text"], "redaction": "dictionary", "dictionary": TERMS})
        w = eng.op("write", {"package": str(self.tmp / "a")})
        (self.tmp / "state" / "secrets.json").unlink()
        terms = self.write(self.tmp / "slownik.txt", "schemat\nMigracja\n")
        code, out, err = run("--slownik-plik", terms, "przepis", "uruchom", self.tmp / "a/recipe.json", "--pakiet", self.tmp / "b")
        self.assertEqual(code, 0, err)
        self.assertEqual(json.loads((self.tmp / "b/manifest.json").read_text(encoding="utf-8"))["reproducible_hash"], w["reproducible_hash"])

    def test_store_file_supplied_by_the_user_resolves_every_reference(self):
        eng = self._eng()
        eng.op("select.expr", {"expr": "title~/Migracja/", "mode": "intersect"})
        eng.op("cut", {"profiles": ["text"], "redaction": "dictionary", "dictionary": TERMS})
        w = eng.op("write", {"package": str(self.tmp / "a")})
        store = self.tmp / "state" / "secrets.json"
        self.assertTrue(store.exists())
        self.assertEqual(oct(store.stat().st_mode & 0o777), "0o600")
        shutil.move(store, self.tmp / "moved-secrets.json")
        code, out, err = run("--slownik-plik", self.tmp / "moved-secrets.json", "odtworz", self.tmp / "a", "--pakiet", self.tmp / "b")
        self.assertEqual(code, 0, err)
        self.assertEqual(json.loads((self.tmp / "b/manifest.json").read_text(encoding="utf-8"))["reproducible_hash"], w["reproducible_hash"])

    def test_attachment_names_and_account_are_redacted(self):
        eng = self.engine(EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip")
        eng.op("select.expr", {"expr": "kind:conversation"})
        eng.op("cut", {"profiles": ["flat", "text"], "redaction": "dictionary", "dictionary": ["schemat", "makieta", "acct-0001"]})
        eng.op("write", {"package": str(self.tmp / "pkg")})
        pkg = self.tmp / "pkg"
        blob, names = package_text(pkg)
        flat = (pkg / "flat/messages.jsonl").read_text(encoding="utf-8") + (pkg / "flat/conversations.jsonl").read_text(encoding="utf-8")
        for term in ("schemat", "makieta", "acct-0001"):
            self.assertNotIn(term, flat, term)
        copied = [n for n in names if n.startswith("attachments/")]
        self.assertTrue(copied)
        self.assertFalse([n for n in copied if "schemat" in n], "a file name with a dictionary term is copied under an opaque name")
        self.assertTrue([n for n in copied if "/att-" in n])
        md = "".join(p.read_text(encoding="utf-8") for p in (pkg / "text").glob("*.md"))
        self.assertNotIn("schemat", md)

    def test_polish_inflection_is_masked_from_the_stem(self):
        r = Redactor("dictionary", terms=["Anna Kowalska", "Jan Nowicki", "migracja", "Marek", "Łódź", "Ewa Nowacka"])
        text = ("Spotkanie z Anną Kowalską, list od Anny Kowalskiej, rozmowa z Janem Nowickim; plan migracji i migrację "
                "omówiono z Markiem w Łodzi; u Ewy Nowackiej też; Annotation zostaje.")
        out = r.text(text, {})
        for form in ("Anną", "Kowalską", "Anny", "Kowalskiej", "Janem", "Nowickim", "migracji", "migrację", "Markiem", "Łodzi",
                     "Ewy", "Nowackiej"):
            self.assertNotIn(form, out, form)
        self.assertIn("Annotation", out, "a longer word is not a form of the name")
        self.assertEqual(out.count("[NAME#1]"), 2, "every form of one term gets the same mask")

    def test_undo_after_the_cut_remakes_the_staged_outputs(self):
        """F20: edit → cut → undo → write: the package holds the tree without the edit and reproduces."""
        eng = self._eng()
        eng.op("edit.relabel_roles", {"where": "message", "map": {"tool": "assistant"}})
        eng.op("cut", {"profiles": ["table"]})
        eng.op("undo", {})
        w = eng.op("write", {"package": str(self.tmp / "a")})
        self.assertEqual(w.get("recut"), ["cut"])
        self.assertIn(",tool,", (self.tmp / "a/table/messages.csv").read_text(encoding="utf-8"), "the undone edit is not in the package")
        rec = json.loads((self.tmp / "a/recipe.json").read_text(encoding="utf-8"))
        self.assertNotIn("edit.relabel_roles", [s["op"] for s in rec["steps"]])
        eng2 = self.engine()
        rec["steps"].append({"op": "write", "params": {"package": str(self.tmp / "b")}})
        eng2.run_recipe(rec)
        self.assertEqual(eng2.last_write["reproducible_hash"], w["reproducible_hash"])

    def test_export_after_undo_of_an_edit_before_the_cut(self):
        from loom.tools.extract.api import Session
        sess = Session(face="api")
        sess.call({"call": "open", "sources": [str(EXPORTS / "openai_2026_sharded.zip")]})
        sess.call({"call": "select", "how": "expr", "expr": "kind:conversation"})
        sess.call({"call": "op", "op": "package.copy", "params": {}})
        sess.call({"call": "op", "op": "edit.relabel_roles", "params": {"where": "message", "map": {"tool": "assistant"}}})
        sess.call({"call": "op", "op": "cut", "params": {"profiles": ["table"]}})
        sess.call({"call": "undo"})
        r = sess.call({"call": "export", "package": str(self.tmp / "x")})
        self.assertNotIn("error", r, r)
        self.assertIn(",tool,", (self.tmp / "x/table/messages.csv").read_text(encoding="utf-8"))
        sess.eng.close()

    def test_only_used_attachments_are_copied(self):
        """F3: a message-grain selection copies the attachments of its messages, not the conversation's others."""
        eng = self.engine(EXPORTS / "openai_2026_sharded.zip")
        eng.ensure_units()
        c3 = next(u for u in eng.conversations if u.ext_id == "c3-0000-cccc")
        eng.op("select.items", {"items": [f"{c3.uid}#c3-u1"]})
        eng.op("package.copy", {})
        self.assertEqual(eng.package.entries[0].messages, {"c3-u1"})
        eng.op("cut", {"profiles": ["flat"]})
        att = eng.cut_info["attachments"]
        self.assertEqual(att["copied"], 1, att)
        self.assertGreater(att["not_used"], 0)
        self.assertEqual(att["missing"], [], "attachments of messages not in the package are not 'missing'")
        eng.op("write", {"package": str(self.tmp / "pkg")})
        files = [p.name for p in (self.tmp / "pkg/attachments").rglob("*") if p.is_file()]
        self.assertEqual(len(files), 1)
        self.assertIn("schemat", files[0])
        rep = (self.tmp / "pkg/RAPORT.md").read_text(encoding="utf-8")
        self.assertIn("nieużytych (nie skopiowano)", rep)
