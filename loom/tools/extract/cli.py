"""CLI face: Polish verbs with English aliases, options as data in ``VERBS``.
Every verb builds a recipe and runs it through the engine (no logic of its
own); ``--json`` prints the engine's result, otherwise a short summary from the
i18n table.  Exit codes: 0 ok, 1 error (message on stderr), 2 usage.
"""
import argparse
import json
import logging
import sys
from pathlib import Path
from typing import Any, Optional

from . import __version__
from .datafiles import DataFileError, DataStore, set_store
from .engine import Engine, EngineError
from .i18n import label, resolve_language, set_language, t
from .recipe import RecipeError, load_recipe, normalise, recipe_hash, save_recipe, validate

log = logging.getLogger(__name__)


# ───────────────────────────── option table ─────────────────────────────
def _o(*flags: str, **kw: Any) -> tuple[tuple[str, ...], dict[str, Any]]:
    return flags, kw


SOURCES = _o("zrodla", nargs="+", metavar="ŹRÓDŁO", help="cli.opt.sources")
PACKAGE = _o("--pakiet", "--package", "-p", metavar="KATALOG", help="cli.opt.package")
SAVE_RECIPE = _o("--zapisz-przepis", "--save-recipe", metavar="PLIK", help="cli.opt.save_recipe")
DRY = _o("--na-sucho", "--dry-run", "-n", action="store_true", help="cli.opt.dry_run")
SELECT_OPTS = [
    _o("--slowa", "--terms", "-s", nargs="+", metavar="SŁOWO", help="cli.opt.terms"),
    _o("--regex", "-r", nargs="+", metavar="REGEX", help="cli.opt.regex"),
    _o("--kryteria", "--criteria", "-k", metavar="ID|PLIK", help="cli.opt.criteria"),
    _o("--wyrazenie", "--expr", "-e", action="append", metavar="WYRAŻENIE", help="cli.opt.expr"),
    _o("--wzorzec", "--pattern", action="append", metavar="NAZWA", help="cli.opt.pattern"),
    _o("--podobne", "--like", nargs="+", metavar="UID", help="cli.opt.like"),
    _o("--klaster", "--cluster", action="append", metavar="ID", help="cli.opt.cluster"),
    _o("--ziarno", "--grain", metavar="ZIARNO", help="cli.opt.grain"),
    _o("--sasiedztwo", "--expand", metavar="PRESET", help="cli.opt.expand"),
    _o("--kaskada", "--cascade", metavar="ID", help="cli.opt.cascade"),
    _o("--decyzje", "--decisions", "-d", metavar="PLIK", help="cli.opt.decisions"),
    _o("--tryb", "--mode", choices=["replace", "add", "subtract", "intersect"], help="cli.opt.mode"),
    _o("--od-manifestu", "--since", metavar="PAKIET", help="cli.opt.since"),
    _o("--tylko", "--only", choices=["only_new", "changed", "removed", "all", "unchanged"], help="cli.opt.only"),
    _o("--model", action="store_true", help="cli.opt.model"),
]
TIMELINE_OPTS = [
    _o("--klucz", "--key", metavar="KLUCZ", help="cli.opt.key"),
    _o("--strefa", "--zone", metavar="STREFA", help="cli.opt.zone"),
    _o("--kubelek", "--bucket", choices=["day", "week", "month"], help="cli.opt.bucket"),
    _o("--od", "--from", dest="od", metavar="DATA", help="cli.opt.from"),
    _o("--do", "--to", dest="do", metavar="DATA", help="cli.opt.to"),
    _o("--luka", "--gap-days", type=float, metavar="DNI", help="cli.opt.gap"),
    _o("--serie", "--burst-ratio", type=float, metavar="K", help="cli.opt.burst"),
    _o("--przesun", "--shift", metavar="CZAS", help="cli.opt.shift"),
    _o("--anonimizuj", "--anonymise", action="store_true", help="cli.opt.anonymise"),
    _o("--ziarnistosc", "--granularity", metavar="JEDN", help="cli.opt.granularity"),
    _o("--bez-kontynuacji", "--no-continuations", action="store_true", help="cli.opt.no_continuations"),
]
VERBS: list[dict[str, Any]] = [
    {"name": "inwentarz", "aliases": ["inventory", "inv"], "help": "cli.verb.inventory", "opts": [SOURCES, PACKAGE, SAVE_RECIPE]},
    {"name": "os-czasu", "aliases": ["timeline", "oś-czasu"], "help": "cli.verb.timeline", "opts": [SOURCES, PACKAGE, SAVE_RECIPE] + TIMELINE_OPTS},
    {"name": "roznica", "aliases": ["diff", "różnica"], "help": "cli.verb.diff",
     "opts": [_o("stare", metavar="STARE", help="cli.opt.old"), _o("nowe", nargs="+", metavar="NOWE", help="cli.opt.new"),
              _o("--tozsamosc", "--identity", choices=["ext_id", "content_hash"], help="cli.opt.identity"),
              _o("--tylko-liczby", "--summary", action="store_true", help="cli.opt.summary"), PACKAGE]},
    {"name": "szukaj", "aliases": ["search", "select"], "help": "cli.verb.search",
     "opts": [SOURCES, PACKAGE, SAVE_RECIPE, DRY] + SELECT_OPTS + TIMELINE_OPTS[:1]},
    {"name": "wyjasnij", "aliases": ["explain", "wyjaśnij"], "help": "cli.verb.explain",
     "opts": [SOURCES, _o("--jednostka", "--unit", "-u", required=True, nargs="+", metavar="UID", help="cli.opt.unit")] + SELECT_OPTS},
    {"name": "zaznacz", "aliases": ["mark"], "help": "cli.verb.mark",
     "opts": [_o("--decyzje", "--decisions", "-d", required=True, metavar="PLIK", help="cli.opt.decisions"),
              _o("jednostka", nargs="?", metavar="UID", help="cli.opt.unit"),
              _o("decyzja", nargs="?", choices=["include", "exclude", "ambiguous", "remove", "wlacz", "wyklucz", "usun"], help="cli.opt.decision"),
              _o("--powod", "--reason", default="", metavar="TEKST", help="cli.opt.reason"),
              _o("--ziarno", "--grain", default="conversation", metavar="ZIARNO", help="cli.opt.grain"),
              _o("--wiadomosc", "--message", metavar="ID", help="cli.opt.message"),
              _o("--propozycje", "--proposals", nargs="*", metavar="ŹRÓDŁO", help="cli.opt.proposals"),
              _o("--lista", "--list", action="store_true", help="cli.opt.list")]},
    {"name": "klastry", "aliases": ["clusters"], "help": "cli.verb.clusters",
     "opts": [SOURCES, PACKAGE, _o("--min", type=int, metavar="N", help="cli.opt.cluster_min"),
              _o("--terminy", "--terms-per-cluster", type=int, metavar="K", help="cli.opt.cluster_terms"),
              _o("--prog", "--min-similarity", type=float, metavar="S", help="cli.opt.cluster_sim")]},
    {"name": "przepis", "aliases": ["recipe"], "help": "cli.verb.recipe",
     "opts": [_o("akcja", choices=["sprawdz", "check", "normalizuj", "normalize", "uruchom", "run", "hash"], help="cli.opt.recipe_action"),
              _o("plik", metavar="PLIK", help="cli.opt.recipe_file"), PACKAGE, DRY]},
    {"name": "formaty", "aliases": ["formats"], "help": "cli.verb.formats", "opts": [_o("pliki", nargs="*", metavar="PLIK", help="cli.opt.files")]},
    {"name": "operacje", "aliases": ["ops"], "help": "cli.verb.ops", "opts": []},
]
DECISION_ALIASES = {"wlacz": "include", "wyklucz": "exclude", "usun": "remove"}


def build_parser(lang: Optional[str] = None) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python3 -m loom.tools.extract", description=t("cli.description"))
    parser.add_argument("--jezyk", "--lang", dest="lang", help=t("cli.opt.lang"))
    parser.add_argument("--json", action="store_true", help=t("cli.opt.json"))
    parser.add_argument("--zbiorczo", "--aggregate", dest="aggregate", action="store_true", help=t("cli.opt.aggregate"))
    parser.add_argument("--dane", "--data-dir", dest="data_dir", metavar="KATALOG", help=t("cli.opt.data_dir"))
    parser.add_argument("--debug", action="store_true", help=t("cli.opt.debug"))
    parser.add_argument("--wersja", "--version", action="version", version=f"loom.tools.extract {__version__}")
    sub = parser.add_subparsers(dest="verb", metavar="CZASOWNIK")
    for v in VERBS:
        sp = sub.add_parser(v["name"], aliases=v["aliases"], help=t(v["help"]), description=t(v["help"]))
        for flags, kw in v["opts"]:
            kw = dict(kw)
            kw["help"] = t(kw["help"]) if isinstance(kw.get("help"), str) else kw.get("help")
            sp.add_argument(*flags, **kw)
        sp.set_defaults(verb=v["name"])
    return parser


# ───────────────────────────── verbs ─────────────────────────────
def _engine(args: argparse.Namespace) -> Engine:
    return Engine(aggregate=bool(args.aggregate), progress=(_progress if not args.json else None))


def _progress(ev: dict[str, Any]) -> None:
    if ev.get("kind") == "note":
        print(f"[{t('cli.note')}] {ev.get('text')}", file=sys.stderr)


def _timeline_params(args: argparse.Namespace) -> dict[str, Any]:
    p: dict[str, Any] = {}
    for src, dst in (("klucz", "key"), ("strefa", "zone"), ("kubelek", "bucket"), ("od", "from"), ("do", "to"), ("luka", "gap_days"),
                     ("serie", "burst_ratio"), ("przesun", "shift"), ("ziarnistosc", "granularity")):
        val = getattr(args, src, None)
        if val is not None:
            p[dst] = val
    if getattr(args, "anonimizuj", False):
        p["anonymise"] = True
    if getattr(args, "bez_kontynuacji", False):
        p["continuations"] = False
    return p


def _select_steps(args: argparse.Namespace) -> list[tuple[str, dict[str, Any]]]:
    steps: list[tuple[str, dict[str, Any]]] = []
    mode = getattr(args, "tryb", None)
    if getattr(args, "od_manifestu", None):
        steps.append(("diff", {"against": [args.od_manifestu], "mode": "none"}))
    if getattr(args, "decyzje", None):
        steps.append(("select.manual", {"decisions": args.decyzje}))
    crit: dict[str, Any] = {}
    if getattr(args, "kryteria", None):
        crit["criteria"] = args.kryteria
    if getattr(args, "slowa", None):
        crit["terms"] = list(args.slowa)
    if getattr(args, "regex", None):
        crit["regex"] = list(args.regex)
    if crit:
        for k, v in (("kaskada", "cascade"), ("ziarno", "grain")):
            if getattr(args, k, None):
                crit[v] = getattr(args, k)
        if getattr(args, "model", False):
            crit["model"] = True
        if mode:
            crit["mode"] = mode
        steps.append(("select.criteria", crit))
    for e in getattr(args, "wyrazenie", None) or []:
        steps.append(("select.expr", {"expr": e, **({"mode": mode} if mode else {})}))
    for pat in getattr(args, "wzorzec", None) or []:
        steps.append(("select.structural", {"pattern": pat, **({"mode": mode} if mode else {})}))
    if getattr(args, "podobne", None):
        steps.append(("select.similar", {"to": list(args.podobne), **({"mode": mode} if mode else {})}))
    for cid in getattr(args, "klaster", None) or []:
        steps.append(("select.cluster", {"cluster": cid, **({"mode": mode} if mode else {})}))
    if getattr(args, "tylko", None):
        steps.append(("select.diff", {"mode": args.tylko}))
    if getattr(args, "sasiedztwo", None):
        steps.append(("expand", {"preset": args.sasiedztwo}))
    return steps


def _run_steps(eng: Engine, sources: list[str], steps: list[tuple[str, dict[str, Any]]], *, by: str = "cli") -> list[dict[str, Any]]:
    eng.set_sources(sources)
    return [eng.op(name, params, by=by) for name, params in steps]


def _finish(eng: Engine, args: argparse.Namespace, results: list[dict[str, Any]], summary_lines: list[str]) -> int:
    if getattr(args, "zapisz_przepis", None):
        save_recipe(eng.normalised_recipe(), args.zapisz_przepis)
        summary_lines.append(t("cli.recipe_saved", path=args.zapisz_przepis))
    if args.json:
        print(json.dumps({"results": results, "recipe": eng.normalised_recipe(), "events": [e for e in eng.events if e.get("kind") == "note"]},
                         ensure_ascii=False, indent=1, default=str))
    else:
        for line in summary_lines:
            print(line)
    return 0


def verb_inventory(args: argparse.Namespace) -> int:
    with _engine(args) as eng:
        steps: list[tuple[str, dict[str, Any]]] = [("inventory", {})]
        if args.pakiet:
            steps.append(("write", {"package": args.pakiet}))
        res = _run_steps(eng, args.zrodla, steps)
        return _finish(eng, args, res, _inventory_lines(eng, res[0]["inventory"]) + _write_lines(res[1:]))


def _inventory_lines(eng: Engine, inv: dict[str, Any]) -> list[str]:
    lines = [t("cli.inventory.header", archives=len(inv.get("archives") or []), units=inv.get("units", {}).get("total", 0))]
    for a in inv.get("archives") or []:
        mem = a.get("members") or {}
        lines.append("  " + t("cli.inventory.archive", label=a.get("label"), format=a.get("format") or "?",
                              hypothesis=(" " + t("cli.hypothesis")) if a.get("format_hypothesis") else "",
                              known=mem.get("known", 0), unknown=mem.get("unknown", 0), units=sum((a.get("units") or {}).values())))
    conv = inv.get("conversations") or 0
    msgs = inv.get("messages") or {}
    lines.append(t("cli.inventory.totals", conversations=conv if isinstance(conv, int) else conv.get("total", 0), messages=msgs.get("total", 0),
                   roles=", ".join(f"{k}: {v}" for k, v in (msgs.get("roles") or {}).items()) or "-"))
    blocks = inv.get("blocks") or {}
    if blocks.get("common"):
        lines.append(t("cli.inventory.blocks", kinds=", ".join(f"{k}: {v}" for k, v in blocks["common"].items())))
    tools = inv.get("tools") or {}
    if tools:
        lines.append(t("cli.inventory.tools", calls=tools.get("calls", 0),
                       names=", ".join(sorted((tools.get("names") or {}).keys())[:10]) or "-"))
    att = inv.get("attachments") or {}
    if att:
        lines.append(t("cli.inventory.attachments", referenced=att.get("referenced", 0), resolved=att.get("resolved", 0), missing=att.get("missing", 0)))
    dates = inv.get("dates") or {}
    if dates:
        lines.append(t("cli.inventory.dates", **{"from": dates.get("from") or "?", "to": dates.get("to") or "?"}))
    merge = inv.get("merge") or {}
    if merge:
        lines.append(t("cli.inventory.merge", duplicates=merge.get("duplicates", 0), versions=merge.get("versions", 0)))
    absent = inv.get("absent") or {}
    if absent:
        lines.append(t("cli.inventory.absent", n=len(absent)))
    return lines


def _write_lines(results: list[dict[str, Any]]) -> list[str]:
    return [t("cli.package_written", package=r["package"], outputs=r["counts"]["outputs"], hash=r["reproducible_hash"][:16])
            for r in results if r.get("op") == "write"]


def verb_timeline(args: argparse.Namespace) -> int:
    with _engine(args) as eng:
        steps: list[tuple[str, dict[str, Any]]] = [("timeline", _timeline_params(args))]
        if args.pakiet:
            steps.append(("write", {"package": args.pakiet}))
        res = _run_steps(eng, args.zrodla, steps)
        tl = res[0]["timeline"]
        lines = [t("cli.timeline.header", key=tl.get("key"), rows=res[0]["counts"]["rows"],
                   **{"from": (tl.get("range") or {}).get("from") or "?", "to": (tl.get("range") or {}).get("to") or "?"}),
                 t("cli.timeline.buckets", unit=(tl.get("buckets") or {}).get("unit"), n=res[0]["counts"]["buckets"],
                   gaps=res[0]["counts"]["gaps"], bursts=res[0]["counts"]["bursts"], continuations=res[0]["counts"]["continuations"])]
        for g in (tl.get("gaps") or [])[:10]:
            lines.append("  " + t("cli.timeline.gap", **{"from": g.get("from"), "to": g.get("to"), "days": g.get("days")}))
        for c in (tl.get("continuations") or [])[:10]:
            lines.append("  " + t("cli.timeline.continuation", a=c.get("from"), b=c.get("to"), minutes=c.get("gap_minutes"),
                                  confidence=c.get("confidence")))
        if not args.aggregate and not args.json:
            for r in (eng.timeline_rows or [])[:20]:
                lines.append(f"  {r.get('t')}  {r.get('service') or '':10} {r.get('kind')}  {str(r.get('title') or r.get('role') or '')[:60]}")
        return _finish(eng, args, res, lines + _write_lines(res[1:]))


def verb_diff(args: argparse.Namespace) -> int:
    with _engine(args) as eng:
        params: dict[str, Any] = {"against": [args.stare], "summary_only": bool(args.tylko_liczby)}
        if args.tozsamosc:
            params["identity"] = args.tozsamosc
        steps: list[tuple[str, dict[str, Any]]] = [("diff", params)]
        if args.pakiet:
            steps.append(("write", {"package": args.pakiet}))
        res = _run_steps(eng, args.nowe, steps)
        c = res[0]["counts"]
        lines = [t("cli.diff.header", new=c["new"], changed=c["changed"], removed=c["removed"], unchanged=c["unchanged"],
                   added=c["messages_added"], mchanged=c["messages_changed"], mremoved=c["messages_removed"])]
        d = res[0]["diff"]
        if not args.tylko_liczby and not args.aggregate:
            for e in d.get("new_units") or []:
                lines.append("  + " + f"{e['uid']}  {e.get('service')}  {e.get('title') or ''}")
            for e in d.get("changed") or []:
                lines.append("  ~ " + f"{e['new_uid']}  {e.get('service')}  {e.get('title') or ''}  (+{e['messages']['added']} ~{e['messages']['changed']} -{e['messages']['removed']})")
            for e in d.get("removed") or []:
                lines.append("  - " + f"{e['uid']}  {e.get('service')}  {e.get('title') or ''}")
        return _finish(eng, args, res, lines + _write_lines(res[1:]))


def verb_search(args: argparse.Namespace) -> int:
    with _engine(args) as eng:
        steps = _select_steps(args)
        if not steps:
            print(t("cli.search.nothing"), file=sys.stderr)
            return 2
        steps.append(("dry_run", {}))
        if args.pakiet and not args.na_sucho:
            steps.append(("write", {"package": args.pakiet}))
        res = _run_steps(eng, args.zrodla, steps)
        dry = next(r for r in res if r.get("op") == "dry_run")
        lines = []
        for r in res:
            if r.get("op") == "select.criteria":
                c = r["counts"]
                lines.append(t("cli.search.cascade", include=c.get("include", 0), ambiguous=c.get("ambiguous", 0), exclude=c.get("exclude", 0),
                               grains=c.get("grains", 0), criteria=r["criteria"]["id"]))
                if r.get("model_note"):
                    lines.append("  " + t("cli.search.model_note"))
            elif r.get("op") in ("select.expr", "select.structural", "select.similar", "select.cluster", "select.diff"):
                lines.append(t("cli.search.matched", op=r["op"], matched=r["counts"].get("matched", 0), after=r["selection"]["after"]))
            elif r.get("op") == "select.manual":
                lines.append(t("cli.search.manual", include=r["counts"]["include"], exclude=r["counts"]["exclude"]))
            elif r.get("op") == "expand":
                lines.append(t("cli.search.expand", preset=r["preset"], added=r["counts"]["added_messages"], linked=r["counts"]["linked_conversations"]))
        c = dry["counts"]
        lines.append(t("cli.search.dry", conversations=c["conversations"], messages=c["messages"], attachments=c["attachments"],
                       missing=c["attachments_missing"], tokens=c["tokens_est"], bytes=dry["package_bytes_est"]))
        if not args.aggregate:
            for u in dry.get("units") or []:
                lines.append(f"  {u['uid']}  {u.get('service') or '':10} {str(u.get('title') or '')[:50]:50}  {u['messages']:4}  {'; '.join(u['reasons'])[:80]}")
        if args.na_sucho:
            lines.append(t("cli.search.dry_only"))
        return _finish(eng, args, res, lines + _write_lines(res))


def verb_explain(args: argparse.Namespace) -> int:
    with _engine(args) as eng:
        steps = _select_steps(args)
        res = _run_steps(eng, args.zrodla, steps)
        lines = []
        for uid in args.jednostka:
            r = eng.op("explain", {"unit": uid}, by="cli")
            res.append(r)
            d = r.get("decision") or {}
            lines.append(t("cli.explain.unit", uid=r["unit"], title=r.get("title") or "", decision=d.get("decision") or "-",
                           score=d.get("score"), source=d.get("source") or "-", selected=r["selected"]))
            for reason in (d.get("reasons") or [])[:10]:
                lines.append("    " + json.dumps(reason, ensure_ascii=False))
            for reason in r.get("selection_reasons") or []:
                lines.append("    " + reason)
        return _finish(eng, args, res, lines)


def verb_mark(args: argparse.Namespace) -> int:
    from .decisions import Decisions
    dec = Decisions.load(args.decyzje)
    if args.lista:
        if args.json:
            print(json.dumps(dec.as_dict(), ensure_ascii=False, indent=1))
        else:
            for e in dec.entries:
                print(f"{e['unit']}  {e['decision']:9}  {e.get('grain', 'conversation')}  {e.get('reason', '')}")
        return 0
    if args.propozycje is not None:
        with _engine(args) as eng:
            if args.propozycje:
                eng.set_sources(args.propozycje)
                eng.decisions = dec
                r = eng.op("propose", {}, by="cli")
                dec.save()
                lines = [t("cli.mark.proposals", n=r["counts"]["proposals"], corrections=r["counts"]["corrections"])]
                for p in r["proposals"]:
                    lines.append("  " + json.dumps(p, ensure_ascii=False))
                return _finish(eng, args, [r], lines)
            print(t("cli.mark.proposals_need_sources"), file=sys.stderr)
            return 2
    if not args.jednostka or not args.decyzja:
        print(t("cli.mark.usage"), file=sys.stderr)
        return 2
    decision = DECISION_ALIASES.get(args.decyzja, args.decyzja)
    if decision == "remove":
        ok = dec.unmark(args.jednostka, grain=args.ziarno, message=args.wiadomosc)
        dec.save()
        print(t("cli.mark.removed" if ok else "cli.mark.not_found", uid=args.jednostka))
        return 0
    entry = dec.mark(args.jednostka, decision, args.powod, grain=args.ziarno, message=args.wiadomosc, by="cli")
    path = dec.save()
    if args.json:
        print(json.dumps(entry, ensure_ascii=False))
    else:
        print(t("cli.mark.saved", uid=args.jednostka, decision=decision, path=str(path), n=len(dec.entries)))
    return 0


def verb_clusters(args: argparse.Namespace) -> int:
    with _engine(args) as eng:
        params: dict[str, Any] = {}
        if args.min is not None:
            params["min_conversations"] = args.min
        if args.terminy is not None:
            params["top_terms"] = args.terminy
        if args.prog is not None:
            params["min_similarity"] = args.prog
        steps: list[tuple[str, dict[str, Any]]] = [("clusters", params)]
        if args.pakiet:
            steps.append(("write", {"package": args.pakiet}))
        res = _run_steps(eng, args.zrodla, steps)
        c = res[0]["counts"]
        lines = [t("cli.clusters.header", clusters=c["clusters"], clustered=c["clustered"], conversations=c["conversations"])]
        for cl in res[0]["clusters"]:
            lines.append("  " + t("cli.clusters.line", id=cl["id"], size=cl["size"], terms=", ".join(x["term"] for x in cl["top_terms"][:6]),
                                  titles="" if args.aggregate else " | ".join(str(x)[:40] for x in cl.get("sample_titles") or [])))
        return _finish(eng, args, res, lines + _write_lines(res[1:]))


def verb_recipe(args: argparse.Namespace) -> int:
    action = {"check": "sprawdz", "normalize": "normalizuj", "run": "uruchom"}.get(args.akcja, args.akcja)
    recipe = load_recipe(args.plik)
    base = Path(args.plik).expanduser().resolve().parent
    if action == "sprawdz":
        problems = validate(recipe)
        if args.json:
            print(json.dumps({"ok": not problems, "problems": problems}, ensure_ascii=False, indent=1))
        elif problems:
            print(t("cli.recipe.problems", n=len(problems)))
            for p in problems:
                print("  - " + p)
        else:
            print(t("cli.recipe.ok", steps=len(recipe.get("steps") or [])))
        return 1 if problems else 0
    if action == "normalizuj":
        print(json.dumps(normalise(recipe, base_dir=base), ensure_ascii=False, indent=1))
        return 0
    if action == "hash":
        print(recipe_hash(normalise(recipe, base_dir=base)))
        return 0
    with _engine(args) as eng:
        if args.pakiet and not args.na_sucho:
            recipe.setdefault("steps", []).append({"op": "write", "params": {"package": args.pakiet}, "by": "cli"})
        if args.na_sucho:
            recipe["steps"] = [s for s in recipe.get("steps") or [] if s.get("op") != "write"] + [{"op": "dry_run", "by": "cli"}]
        res = eng.run_recipe(recipe, base_dir=base)
        lines = [t("cli.recipe.ran", steps=len(res), id=recipe.get("id"))]
        for r in res:
            lines.append(f"  {r.get('step', '-'):4} {r['op']:18} {json.dumps(r.get('counts', {}), ensure_ascii=False)}")
        return _finish(eng, args, res, lines + _write_lines(res))


def verb_formats(args: argparse.Namespace) -> int:
    from .sources import FormatCatalog, scan
    from .datafiles import store
    ds = store()
    if not args.pliki:
        fmts = ds.entries("formats")
        if args.json:
            print(json.dumps({k: {kk: vv for kk, vv in v.items() if kk in ("id", "label", "vendor", "product", "containers", "hypothesis", "reader", "known_absent")}
                              for k, v in fmts.items()}, ensure_ascii=False, indent=1))
        else:
            for fid, f in sorted(fmts.items()):
                hyp = f" [{t('cli.hypothesis')}]" if f.get("hypothesis") else ""
                print(f"{fid:32} {label(f.get('label')):60} {','.join(f.get('containers') or [])}{hyp}")
        return 0
    archives = scan(args.pliki, catalog=FormatCatalog(ds))
    out = []
    for a in archives:
        out.append({"path": None if args.aggregate else a.path, "label": a.label, "kind": a.kind, "format": a.format,
                    "members": [{"name": None if args.aggregate else m.name, "format": m.format, "kind": m.kind, "how": m.sniff_how, "size": m.size}
                                for m in a.members]})
    if args.json:
        print(json.dumps(out, ensure_ascii=False, indent=1))
    else:
        for a in out:
            print(f"{a['label']}: {a['kind']} → {a['format'] or t('cli.unknown')}")
            for m in a["members"]:
                print(f"   {str(m['name'] or '')[:50]:50} {m['format'] or '-':30} {m['kind'] or '':10} {m['how']}")
    return 0


def verb_ops(args: argparse.Namespace) -> int:
    with _engine(args) as eng:
        ops = eng.ops_available()
        if args.json:
            print(json.dumps(ops, ensure_ascii=False, indent=1))
        else:
            for name, spec in ops.items():
                flag = "" if spec.get("implemented") else f" [{t('cli.ops.not_implemented')}]"
                print(f"{name:18} {label(spec.get('label')):70} {','.join(sorted((spec.get('params') or {}).keys()))}{flag}")
    return 0


HANDLERS = {"inwentarz": verb_inventory, "os-czasu": verb_timeline, "roznica": verb_diff, "szukaj": verb_search,
            "wyjasnij": verb_explain, "zaznacz": verb_mark, "klastry": verb_clusters, "przepis": verb_recipe,
            "formaty": verb_formats, "operacje": verb_ops}


def main(argv: Optional[list[str]] = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    # language and data dir first, so even --help speaks the chosen language
    pre = argparse.ArgumentParser(add_help=False)
    pre.add_argument("--jezyk", "--lang", dest="lang")
    pre.add_argument("--dane", "--data-dir", dest="data_dir")
    pre.add_argument("--debug", action="store_true")
    pre_args, _ = pre.parse_known_args(argv)
    logging.basicConfig(level=logging.DEBUG if pre_args.debug else logging.WARNING, format="%(levelname)s %(name)s: %(message)s")
    if pre_args.data_dir:
        set_store(DataStore(overlay=Path(pre_args.data_dir).expanduser() / "extract"))
    set_language(resolve_language(pre_args.lang))
    parser = build_parser()
    args = parser.parse_args(argv)
    if not args.verb:
        parser.print_help()
        return 2
    try:
        return HANDLERS[args.verb](args)
    except (EngineError, RecipeError, DataFileError, ValueError, FileNotFoundError) as exc:
        print(f"{t('cli.error')}: {exc}", file=sys.stderr)
        if pre_args.debug:
            raise
        return 1


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
