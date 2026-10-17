"""CLI verbs of slice B: cut/extract, cost, reproduce, hand-off, statistics,
check, attachments, profiles, flows, describe, edit/undo/redo, saved filters,
plus the task-file and sentence entry points.  Like every face, these only
build recipe steps and call the engine; verbs that work on an existing
package replay that package's ``recipe.json`` (location-independent) and
write it again, so the package stays exactly what its recipe says."""
import argparse
import json
import sys
from pathlib import Path
from typing import Any, Optional

from .datafiles import store, write_json
from .engine import Engine
from .i18n import label, t
from .recipe import load_recipe, save_recipe


def _o(*flags: str, **kw: Any) -> tuple[tuple[str, ...], dict[str, Any]]:
    return flags, kw


TOPIC = _o("--temat", "--topic", "-t", metavar="TEMAT", help="cli.opt.topic")
CUT_OPTS = [
    _o("--profil", "--profile", action="append", metavar="ID", help="cli.opt.profile"),
    _o("--zalaczniki", "--attachments", choices=["copy_used", "copy_all", "reference_only"], help="cli.opt.attachments"),
    _o("--redakcja", "--redaction", metavar="PROFIL", help="cli.opt.redaction"),
    _o("--slownik", "--dictionary", nargs="+", metavar="TERMIN", help="cli.opt.dictionary"),
    _o("--strefa-wyjscia", "--out-zone", dest="out_zone", metavar="STREFA", help="cli.opt.out_zone"),
    _o("--format-czasu", "--time-format", dest="time_format", choices=["iso8601", "epoch", "date", "source"], help="cli.opt.time_format"),
    _o("--przesun-czas", "--shift-time", dest="shift_time", metavar="CZAS", help="cli.opt.shift"),
    _o("--anonimizuj-czas", "--anonymise-time", dest="anon_time", nargs="?", const="package", choices=["package", "conversation"],
       help="cli.opt.anonymise_time"),
    _o("--ziarnistosc-czasu", "--time-granularity", dest="time_gran", choices=["minute", "hour", "day", "week", "month"], help="cli.opt.granularity"),
    _o("--kolejnosc", "--order", choices=["created", "updated", "title", "source_order", "package"], help="cli.opt.order"),
    _o("--przekaz", "--handoff", action="store_true", help="cli.opt.handoff"),
    _o("--z-grafem", "--with-graph", action="store_true", help="cli.opt.with_graph"),
    _o("--streszczenia", "--summaries", action="store_true", help="cli.opt.summaries"),
    _o("--tekst-zalacznikow", "--attachment-text", dest="att_text", action="store_true", help="cli.opt.attachment_text"),
]
MODEL_OPTS = [
    _o("--tak", "--yes", action="store_true", help="cli.opt.yes"),
    _o("--zgoda-x10", "--accept-x10", dest="accept_x10", action="store_true", help="cli.opt.accept_x10"),
    _o("--model-tani", "--cheap-model", dest="cheap_model", metavar="MODEL", help="cli.opt.cheap_model"),
    _o("--model-mocny", "--strong-model", dest="strong_model", metavar="MODEL", help="cli.opt.strong_model"),
    _o("--dostawca", "--provider-url", dest="provider_url", metavar="URL", help="cli.opt.provider_url"),
    _o("--tylko-z-pamieci", "--replay-only", dest="replay_only", action="store_true", help="cli.opt.replay_only"),
]


# ───────────────────────────── shared helpers ─────────────────────────────
# grants of THIS face invocation (``--kod-uzytkownika``): never read from a recipe, a task file or a manifest
FACE_GRANTS: dict[str, Any] = {}


def face_settings() -> dict[str, Any]:
    return {"allow_user_code": True} if FACE_GRANTS.get("allow_user_code") else {}


def model_settings(args: argparse.Namespace) -> dict[str, Any]:
    m: dict[str, Any] = {}
    if getattr(args, "model", False):
        m["enabled"] = True
    if getattr(args, "tak", False):
        m["accept"] = True
    if getattr(args, "accept_x10", False):
        m["accept_x10"] = True
    if getattr(args, "replay_only", False):
        m["replay_only"] = True
    classes = {}
    if getattr(args, "cheap_model", None):
        classes["cheap"] = args.cheap_model
    if getattr(args, "strong_model", None):
        classes["strong"] = args.strong_model
    if classes:
        m["classes"] = classes
    if getattr(args, "provider_url", None):
        m["provider"] = {"base_url": args.provider_url}
    return {"flows": {"model": m}} if m else {}


def interactive_ask(args: argparse.Namespace) -> Any:
    def ask(q: dict[str, Any]) -> bool:
        if args.json or not sys.stdin.isatty():
            return False
        est = q.get("estimate") or {}
        tot = est.get("total") or {}
        amount = tot.get("amount") if tot.get("known") else t("cli.cost.unknown")
        if q.get("kind") == "x10":
            print(t("cli.cost.x10", amount=amount, last=q.get("last"), ratio=q.get("ratio")), file=sys.stderr)
        else:
            print(t("cli.cost.question", calls=est.get("calls"), amount=amount), file=sys.stderr)
        try:
            ans = input(t("cli.cost.prompt") + " ").strip().lower()
        except EOFError:
            return False
        return ans in ("t", "tak", "y", "yes")
    return ask


def make_engine(args: argparse.Namespace, progress: Any = None, settings: Optional[dict[str, Any]] = None) -> Engine:
    s = dict(settings or {})
    s.update(model_settings(args))
    s.update(face_settings())
    pk = getattr(args, "pakiet", None)
    if pk and getattr(args, "cache", None) != "off":
        s.setdefault("cache_dir", str(Path(pk).expanduser() / "cache"))
    if getattr(args, "cache", None) not in (None, "off", "auto"):
        s["cache_dir"] = args.cache
    eng = Engine(aggregate=bool(args.aggregate), progress=progress, settings=s)
    eng.ask = interactive_ask(args)
    return eng


def cut_params(args: argparse.Namespace) -> dict[str, Any]:
    p: dict[str, Any] = {}
    if getattr(args, "profil", None):
        p["profiles"] = list(args.profil)
    for src, dst in (("zalaczniki", "attachments"), ("redakcja", "redaction"), ("out_zone", "zone"), ("time_format", "time_format"),
                     ("shift_time", "shift"), ("anon_time", "anonymise"), ("time_gran", "granularity"), ("kolejnosc", "order")):
        v = getattr(args, src, None)
        if v is not None:
            p[dst] = v
    if getattr(args, "slownik", None):
        p["dictionary"] = list(args.slownik)
    if getattr(args, "att_text", False):
        p["extract_text"] = True
    return p


def topic_step(topic: str) -> tuple[str, dict[str, Any]]:
    import re
    cid = re.sub(r"[^a-z0-9_-]+", "", topic.lower())
    if cid in store().entries("criteria"):
        return ("select.criteria", {"criteria": cid})
    return ("select.criteria", {"terms": [topic]})


def print_result(args: argparse.Namespace, eng: Engine, results: list[dict[str, Any]], lines: list[str]) -> int:
    if args.json:
        print(json.dumps({"results": results, "recipe": eng.normalised_recipe(), "events": [e for e in eng.events if e.get("kind") in ("note", "question")]},
                         ensure_ascii=False, indent=1, default=str))
    else:
        for line in lines:
            print(line)
    return 0


def summary_lines(results: list[dict[str, Any]], aggregate: bool) -> list[str]:
    lines: list[str] = []
    for r in results:
        op = r.get("op")
        c = r.get("counts") or {}
        if op == "select.criteria":
            lines.append(t("cli.search.cascade", include=c.get("include", 0), ambiguous=c.get("ambiguous", 0), exclude=c.get("exclude", 0),
                           grains=c.get("grains", 0), criteria=(r.get("criteria") or {}).get("id")))
            if r.get("model_estimate"):
                lines.append("  " + t("cli.cost.line", **_cost_fmt(r["model_estimate"])))
        elif op == "cut":
            cut = r.get("cut") or {}
            lines.append(t("cli.cut.line", profiles=", ".join(p["profile"] for p in cut.get("profiles") or []), conversations=c.get("conversations"),
                           files=c.get("files"), masked=c.get("masked", 0)))
            pres = cut.get("preserve")
            if pres:
                lines.append("  " + t("cli.cut.preserve", conversations=pres.get("conversations"), copied=pres.get("attachments_copied"),
                                      missing=pres.get("attachments_missing"), exact=t("cli.yes") if pres.get("byte_exact") else t("cli.no")))
            att = cut.get("attachments")
            if att:
                miss = att.get("missing")
                lines.append("  " + t("cli.cut.attachments", referenced=att.get("referenced"), copied=att.get("copied"),
                                      missing=miss if isinstance(miss, int) else len(miss or [])))
        elif op == "handoff":
            h = r.get("handoff") or {}
            lines.append(t("cli.handoff.line", conversations=h.get("conversations"), messages=h.get("messages"),
                           text=h.get("messages_with_text"), attachments=h.get("attachments")))
        elif op == "write":
            lines.append(t("cli.package_written", package=r["package"], outputs=r["counts"]["outputs"], hash=r["reproducible_hash"][:16]))
        elif op == "dry_run":
            lines.append(t("cli.search.dry", conversations=c["conversations"], messages=c["messages"], attachments=c["attachments"],
                           missing=c["attachments_missing"], tokens=c["tokens_est"], bytes=r["package_bytes_est"]))
        elif op and op.startswith("edit."):
            lines.append(t("cli.edit.line", op=op, changed=c.get("changed"), items=c.get("items")))
            for ln in r.get("lines") or []:
                lines.append("  " + ln)
        elif op == "stats":
            lines.append(t("cli.stats.line", **{k: (r["stats"]["total"]).get(k) for k in ("conversations", "messages", "words", "chars")}))
    return lines


def _cost_fmt(est: dict[str, Any]) -> dict[str, Any]:
    tot = est.get("total") or {}
    return {"calls": est.get("calls", 0), "amount": tot.get("amount") if tot.get("known") else t("cli.cost.unknown"),
            "currency": tot.get("currency", "USD")}


# ───────────────────────────── cut / extract ─────────────────────────────
def build_cut_steps(args: argparse.Namespace, select_steps: list[tuple[str, dict[str, Any]]]) -> list[tuple[str, dict[str, Any]]]:
    steps = list(select_steps)
    if getattr(args, "temat", None):
        steps.insert(0, topic_step(args.temat))
    if not steps:
        raise ValueError(t("cli.search.nothing"))
    if not any(s[0] == "expand" for s in steps):
        steps.append(("expand", {}))
    if args.na_sucho or getattr(args, "plan", False):
        steps.append(("dry_run", {}))
        return steps
    if getattr(args, "streszczenia", False):
        steps.append(("flow.run", {"flow": "summarize"}))
    steps.append(("cut", cut_params(args)))
    if getattr(args, "przekaz", False):
        steps.append(("handoff", {"graph": bool(getattr(args, "z_grafem", False))}))
    if not args.pakiet:
        raise ValueError(t("cli.cut.need_package"))
    steps.append(("write", {"package": args.pakiet}))
    return steps


def verb_cut(args: argparse.Namespace, select_steps: Any, progress: Any) -> int:
    steps = build_cut_steps(args, select_steps(args))
    with make_engine(args, progress) as eng:
        eng.set_sources(args.zrodla)
        eng.ensure_units()
        if not eng.conversations:
            print(t("cli.nothing_recognised", archives=len(eng.archives), units=len(eng.units)), file=sys.stderr)
            return 3
        res = [eng.op(name, params, by="cli") for name, params in steps]
        lines = summary_lines(res, eng.aggregate)
        if getattr(args, "plan", False):
            cost = eng.op("cost", {"summaries": bool(getattr(args, "streszczenia", False))}, by="cli")
            res.append(cost)
            lines.append(t("cli.cost.line", **_cost_fmt(cost["estimate"])))
            planned = [n for n, _ in build_cut_steps(argparse.Namespace(**{**vars(args), "plan": False, "na_sucho": False,
                                                                          "pakiet": args.pakiet or "<pakiet>"}), select_steps(args))]
            lines.append(t("cli.plan.steps", n=len(planned)) + ": " + " → ".join(planned))
        elif args.na_sucho:
            lines.append(t("cli.search.dry_only"))
        if getattr(args, "zapisz_przepis", None):
            save_recipe(eng.normalised_recipe(), args.zapisz_przepis)
            lines.append(t("cli.recipe_saved", path=args.zapisz_przepis))
        return print_result(args, eng, res, lines)


def verb_cost(args: argparse.Namespace, select_steps: Any, progress: Any) -> int:
    steps = list(select_steps(args))
    if getattr(args, "temat", None):
        steps.insert(0, topic_step(args.temat))
    steps = [(n, dict(p, model_count=True) if n == "select.criteria" else p) for n, p in steps]
    with make_engine(args, progress) as eng:
        eng.set_sources(args.zrodla)
        res = [eng.op(n, p, by="cli") for n, p in steps]
        if getattr(args, "streszczenia", False) and eng.package is None:
            eng.op("package.copy", {}, by="cli")
        r = eng.op("cost", {"summaries": bool(getattr(args, "streszczenia", False))}, by="cli")
        res.append(r)
        est = r["estimate"]
        lines = [t("cli.cost.line", **_cost_fmt(est))]
        for fid, f in (est.get("flows") or {}).items():
            cost = f.get("cost") or {}
            lines.append("  " + t("cli.cost.flow", flow=fid, model=f.get("model") or "-", calls=f.get("calls"),
                                  amount=cost.get("amount") if cost.get("known") else t("cli.cost.unknown"), note=f.get("note") or ""))
        return print_result(args, eng, res, lines)


# ───────────────────────────── package verbs (replay + change + write) ─────────────────────────────
def _package_recipe(pkg: Path) -> dict[str, Any]:
    p = pkg.expanduser() / "recipe.json"
    if not p.exists():
        raise FileNotFoundError(t("cli.package.no_recipe", path=str(p)))
    return load_recipe(p)


def _replay(eng: Engine, pkg: Path, recipe: dict[str, Any]) -> list[dict[str, Any]]:
    recipe = dict(recipe)
    recipe["steps"] = [s for s in recipe.get("steps") or [] if s.get("op") != "write"]
    return eng.run_recipe(recipe, base_dir=pkg.expanduser().resolve())


def verb_handoff(args: argparse.Namespace, progress: Any) -> int:
    pkg = Path(args.pakiet)
    with make_engine(args, progress) as eng:
        res = _replay(eng, pkg, _package_recipe(pkg))
        params: dict[str, Any] = {"graph": bool(args.z_grafem)}
        if args.galezie:
            params["branches"] = args.galezie
        res.append(eng.op("handoff", params, by="cli"))
        res.append(eng.op("write", {"package": str(pkg)}, by="cli"))
        return print_result(args, eng, res[-2:], summary_lines(res[-2:], eng.aggregate))


def verb_edit(args: argparse.Namespace, progress: Any) -> int:
    pkg = Path(args.pakiet)
    params: dict[str, Any] = {}
    if args.zakres:
        params["where"] = args.zakres
    for kv in args.param or []:
        k, _, v = kv.partition("=")
        try:
            params[k] = json.loads(v)
        except json.JSONDecodeError:
            params[k] = v
    if args.podglad:
        params["preview"] = True
    with make_engine(args, progress) as eng:
        _replay(eng, pkg, _package_recipe(pkg))
        if not eng.recipe.get("steps") or not any(s["op"] == "cut" for s in eng.recipe["steps"]):
            pass
        r = eng.op(f"edit.{args.operacja}", params, by="cli")
        out = [r]
        if not args.podglad:
            # the edit step goes before the cut so the profiles show it: re-run the recipe with the edit inserted
            rec = eng.normalised_recipe()
            steps = [s for s in rec["steps"] if s["op"] != "write"]
            edit_step = steps.pop()  # the edit just recorded
            edit_step.pop("id", None)  # ids are positional; the replay numbers the steps again
            cut_i = next((i for i, s in enumerate(steps) if s["op"] in ("cut", "handoff", "flow.run")), len(steps))
            steps.insert(cut_i, edit_step)
            rec["steps"] = steps
            with make_engine(args, progress) as eng2:
                eng2.run_recipe(rec, base_dir=pkg.expanduser().resolve())
                w = eng2.op("write", {"package": str(pkg)}, by="cli")
                out.append(w)
                return print_result(args, eng2, out, summary_lines(out, eng2.aggregate))
        lines = summary_lines(out, eng.aggregate) + [t("cli.edit.preview_only", patch=len(r.get("patch") or []) if isinstance(r.get("patch"), list) else r.get("patch"))]
        if not args.json and not eng.aggregate:
            for op in (r.get("patch") or [])[:40]:
                lines.append("    " + json.dumps(op, ensure_ascii=False)[:200])
        return print_result(args, eng, out, lines)


def _redo_path(pkg: Path) -> Path:
    return pkg.expanduser() / "redo.json"


def verb_undo(args: argparse.Namespace, progress: Any, *, redo: bool = False) -> int:
    pkg = Path(args.pakiet)
    rec = _package_recipe(pkg)
    steps = [s for s in rec.get("steps") or [] if s.get("op") != "write"]
    rp = _redo_path(pkg)
    stack = json.loads(rp.read_text(encoding="utf-8")) if rp.exists() else []
    n = int(args.n or 1)
    moved = []
    if not redo:
        for _ in range(n):
            idx = next((i for i in range(len(steps) - 1, -1, -1) if str(steps[i].get("op", "")).startswith(("edit.", "package."))
                        and steps[i].get("note") != "implicit"), None)
            if idx is None:
                break
            moved.append(steps.pop(idx))
        stack.extend(moved)
    else:
        for _ in range(n):
            if not stack:
                break
            st = stack.pop()
            st.pop("id", None)
            cut_i = next((i for i, s in enumerate(steps) if s["op"] in ("cut", "handoff", "flow.run")), len(steps))
            steps.insert(cut_i, st)
            moved.append(st)
    if not moved:
        print(t("cli.undo.nothing"), file=sys.stderr)
        return 1
    rec["steps"] = steps
    with make_engine(args, progress) as eng:
        eng.run_recipe(rec, base_dir=pkg.expanduser().resolve())
        w = eng.op("write", {"package": str(pkg)}, by="cli")
        write_json(rp, stack)
        lines = [t("cli.redo.done" if redo else "cli.undo.done", n=len(moved), ops=", ".join(s["op"] for s in moved))] + summary_lines([w], eng.aggregate)
        return print_result(args, eng, [w], lines)


def verb_stats(args: argparse.Namespace, progress: Any) -> int:
    pkg = Path(args.pakiet).expanduser()
    sp = pkg / "stats.json"
    if sp.exists():
        stats = json.loads(sp.read_text(encoding="utf-8"))
    else:
        with make_engine(args, progress) as eng:
            _replay(eng, pkg, _package_recipe(pkg))
            stats = eng.op("stats", {}, by="cli")["stats"]
    if args.rozmowa:
        stats = {"conversations": [c for c in stats.get("conversations") or [] if args.rozmowa in (c.get("key"), c.get("uid"))]}
    if args.json:
        print(json.dumps(stats, ensure_ascii=False, indent=1))
        return 0
    tot = stats.get("total") or {}
    if tot:
        print(t("cli.stats.line", conversations=tot.get("conversations"), messages=tot.get("messages"), words=tot.get("words"), chars=tot.get("chars")))
        print("  " + t("cli.stats.period", **{"from": (stats.get("period") or {}).get("from") or "?", "to": (stats.get("period") or {}).get("to") or "?"}))
        for row in stats.get("topic_timeline") or []:
            print(f"  {row['month']}  {row['conversations']:4} {row['messages']:6}  " + ", ".join(f"{k}:{v}" for k, v in sorted(row["by_service"].items())))
    for c in stats.get("conversations") or []:
        print(f"  {c['key']}  {c.get('service') or '':10} {c['messages']:5} {c['words']:7}  {', '.join(sorted(c.get('tools') or {}))}")
    return 0


def verb_check(args: argparse.Namespace) -> int:
    from .manifest import check_package
    res = check_package(args.pakiet)
    if args.json:
        print(json.dumps(res, ensure_ascii=False, indent=1))
    elif res["ok"]:
        print(t("cli.check.ok", outputs=res["outputs"]))
        if res.get("handoff_import"):
            print("  " + t("cli.check.handoff", **res["handoff_import"]))
    else:
        print(t("cli.check.problems", n=len(res["problems"])))
        for p in res["problems"]:
            print("  - " + p)
    return 0 if res["ok"] else 1


def verb_reproduce(args: argparse.Namespace, progress: Any) -> int:
    from .manifest import reproduce
    with make_engine(args, progress) as eng:
        res = reproduce(eng, args.manifest, target=args.pakiet)
    if args.json:
        print(json.dumps(res, ensure_ascii=False, indent=1))
    else:
        print(t("cli.reproduce.equal" if res["equal"] else "cli.reproduce.different", package=res["package"],
                original=str(res["original"])[:16], reproduced=str(res["reproduced"])[:16]))
        for d in res["differences"][:40]:
            print("  ~ " + d)
        for s in res["sources_changed"]:
            print("  ! " + t("cli.reproduce.source_changed", source=s))
    return 0 if res["equal"] else 1


def verb_attachments(args: argparse.Namespace, select_steps: Any, progress: Any) -> int:
    with make_engine(args, progress) as eng:
        eng.set_sources(args.zrodla)
        params: dict[str, Any] = {"missing": bool(args.brakujace)}
        if args.typ:
            params["mime"] = args.typ
        if args.rozmiar:
            params["min_size"] = int(str(args.rozmiar).lstrip(">"))
        if args.rozmowa:
            params["conversation"] = args.rozmowa
        r = eng.op("attachments", params, by="cli")
        lines = [t("cli.attachments.line", n=r["counts"]["attachments"], bytes=r["bytes"],
                   status=", ".join(f"{k[7:]}: {v}" for k, v in r["counts"].items() if k.startswith("status_")))]
        for mime, n in r["by_mime"].items():
            lines.append(f"  {mime:30} {n}")
        if not eng.aggregate:
            for row in r["rows"][:200]:
                lines.append(f"  {row['status']:9} {str(row.get('mime') or '?'):22} {str(row.get('size') or ''):>9}  {row.get('name') or row.get('ref')}  ← {row['conversation']}")
        return print_result(args, eng, [r], lines)


def verb_profile(args: argparse.Namespace) -> int:
    from .cut.profiles import check_profile
    ds = store()
    action = {"list": "lista", "show": "pokaz", "check": "sprawdz"}.get(args.akcja, args.akcja)
    if action == "lista":
        for pid, prof in sorted(ds.entries("profiles").items()):
            print(f"{pid:12} {prof.get('kind', 'restructure'):12} {label(prof.get('label'))}")
        return 0
    if not args.nazwa:
        print(t("cli.profile.need_name"), file=sys.stderr)
        return 2
    prof = json.loads(Path(args.nazwa).read_text(encoding="utf-8")) if Path(args.nazwa).exists() else ds.entry("profiles", args.nazwa)
    if action == "pokaz":
        print(json.dumps({k: v for k, v in prof.items() if not k.startswith("_")}, ensure_ascii=False, indent=1))
        return 0
    probs = check_profile(prof)
    print(t("cli.profile.ok", id=prof.get("id")) if not probs else t("cli.profile.problems", n=len(probs)))
    for p in probs:
        print("  - " + p)
    return 0 if not probs else 1


def verb_flow(args: argparse.Namespace) -> int:
    ds = store()
    action = {"list": "lista", "show": "pokaz", "check": "sprawdz"}.get(args.akcja, args.akcja)
    if action == "lista":
        for fid, f in sorted(ds.entries("flows").items()):
            print(f"{fid:18} {f.get('kind'):6} {str(f.get('class') or ''):7} {label(f.get('label'))}")
        for cid, c in sorted(ds.entries("cascades").items()):
            print(f"[{cid}] " + " → ".join(f"{lv['id']}:{lv['flow']}{'' if lv.get('enabled', True) else '(off)'}" for lv in c.get("levels") or []))
        return 0
    if not args.nazwa:
        print(t("cli.profile.need_name"), file=sys.stderr)
        return 2
    entries = ds.entries("flows")
    obj = entries.get(args.nazwa) or ds.entries("cascades").get(args.nazwa)
    if obj is None:
        print(t("cli.flow.unknown", id=args.nazwa), file=sys.stderr)
        return 2
    if action == "pokaz":
        print(json.dumps({k: v for k, v in obj.items() if not k.startswith("_")}, ensure_ascii=False, indent=1))
        return 0
    probs = []
    if obj.get("schema") == "loom.extract.cascade/1":
        for lv in obj.get("levels") or []:
            if lv.get("flow") not in entries:
                probs.append(f"level {lv.get('id')}: unknown flow {lv.get('flow')!r}")
    elif obj.get("kind") == "model" and not (obj.get("prompt") or {}).get("system"):
        probs.append("model flow without prompt.system")
    print(t("cli.profile.ok", id=args.nazwa) if not probs else t("cli.profile.problems", n=len(probs)))
    for p in probs:
        print("  - " + p)
    return 0 if not probs else 1


def verb_describe(args: argparse.Namespace, progress: Any) -> int:
    with make_engine(args, progress) as eng:
        r = eng.op("describe", {"text": args.opis, "model": bool(args.model)}, by="cli")
        crit = r["criteria"]
        if args.zapisz:
            from .paths import overlay_dir
            base = store().overlay or overlay_dir()
            if base is None:
                print(t("cli.describe.no_overlay"), file=sys.stderr)
                return 1
            crit = dict(crit, id=args.zapisz)
            path = Path(base) / "criteria" / f"{args.zapisz}.json"
            write_json(path, crit)
            print(t("cli.describe.saved", path=str(path)), file=sys.stderr)
        print(json.dumps(crit, ensure_ascii=False, indent=1))
        return 0


def verb_filter(args: argparse.Namespace) -> int:
    ds = store()
    base = ds.overlay
    action = {"save": "zapisz", "list": "lista", "remove": "usun", "delete": "usun"}.get(args.akcja, args.akcja)
    if action == "lista":
        for fid, f in sorted(ds.entries("filters").items()) if (ds.shipped / "filters").exists() or (base and (base / "filters").exists()) else []:
            print(f"{fid:20} {f.get('expr')}")
        return 0
    if base is None:
        print(t("cli.describe.no_overlay"), file=sys.stderr)
        return 1
    if not args.nazwa:
        print(t("cli.profile.need_name"), file=sys.stderr)
        return 2
    path = Path(base) / "filters" / f"{args.nazwa}.json"
    if action == "usun":
        if path.exists():
            path.unlink()
            print(t("cli.filter.removed", id=args.nazwa))
            return 0
        print(t("cli.flow.unknown", id=args.nazwa), file=sys.stderr)
        return 1
    if not args.wyrazenie:
        print(t("cli.filter.need_expr"), file=sys.stderr)
        return 2
    from .select_expr import validate as validate_expr
    validate_expr(args.wyrazenie)
    write_json(path, {"schema": "loom.extract.filter/1", "id": args.nazwa, "expr": args.wyrazenie})
    print(t("cli.filter.saved", id=args.nazwa, path=str(path)))
    return 0


# ───────────────────────────── task files and sentences ─────────────────────────────
def task_to_recipe(task: dict[str, Any]) -> dict[str, Any]:
    """``loom.extract.task/1`` → recipe: ``criteria`` / ``topic`` / ``profiles`` / ``package`` / ``flows`` /
    ``language``; ``recipe`` (a file) or ``steps`` replace the generated steps."""
    if task.get("schema") == "loom.extract.recipe/1" or (task.get("steps") and not task.get("criteria") and not task.get("topic")):
        rec = dict(task)
        rec["schema"] = "loom.extract.recipe/1"
        return rec
    if task.get("recipe"):
        rec = load_recipe(task["recipe"])
        if task.get("sources"):
            rec["sources"] = task["sources"]
        return rec
    steps: list[dict[str, Any]] = []
    crit = task.get("criteria")
    if crit:
        steps.append({"op": "select.criteria", "params": {"criteria": crit}, "by": "recipe"})
    elif task.get("topic"):
        n, p = topic_step(str(task["topic"]))
        steps.append({"op": n, "params": p, "by": "recipe"})
    for e in task.get("expr") or []:
        steps.append({"op": "select.expr", "params": {"expr": e, "mode": "intersect" if steps else "add"}, "by": "recipe"})
    steps.append({"op": "expand", "by": "recipe"})
    cut: dict[str, Any] = {}
    if task.get("profiles"):
        cut["profiles"] = list(task["profiles"])
    for k in ("redaction", "attachments", "zone", "order"):
        if task.get(k):
            cut[k] = task[k]
    steps.append({"op": "cut", "params": cut, "by": "recipe"})
    if task.get("handoff"):
        steps.append({"op": "handoff", "by": "recipe"})
    if task.get("package"):
        steps.append({"op": "write", "params": {"package": task["package"]}, "by": "recipe"})
    settings: dict[str, Any] = {}
    if task.get("flows"):
        settings["flows"] = task["flows"]
    if task.get("language"):
        settings["language"] = task["language"]
    return {"schema": "loom.extract.recipe/1", "id": str(task.get("id") or "task"), "label": task.get("label") or {},
            "sources": list(task.get("sources") or []), "settings": settings, "steps": steps}


def load_task(path: str) -> dict[str, Any]:
    """A task or recipe file, JSON or TOML (by content), read as written (no schema defaulting)."""
    if path == "-":
        text = sys.stdin.read()
    else:
        p = Path(path).expanduser()
        if not p.exists():
            raise FileNotFoundError(f"task file not found: {p}")
        text = p.read_text(encoding="utf-8")
    if text.lstrip().startswith("{"):
        return json.loads(text)
    import tomllib
    return tomllib.loads(text)


def run_recipe_face(args: argparse.Namespace, recipe: dict[str, Any], base: Optional[Path], progress: Any, *, show: bool = False,
                    sentence: Optional[str] = None) -> int:
    """Run a recipe from a task file or a sentence: show it first, honour --na-sucho / --plan / --pakiet."""
    from .recipe import normalise
    if getattr(args, "pakiet", None):
        writes = [s for s in recipe.get("steps") or [] if s.get("op") == "write"]
        if writes:
            for s in writes:
                s["params"] = dict(s.get("params") or {}, package=args.pakiet)
        else:
            recipe.setdefault("steps", []).append({"op": "write", "params": {"package": args.pakiet}, "by": "cli"})
    if args.na_sucho or getattr(args, "plan", False):
        recipe["steps"] = [s for s in recipe.get("steps") or [] if s.get("op") not in ("write", "cut", "handoff", "flow.run")
                           and not str(s.get("op", "")).startswith("edit.")]
        if not any(s.get("op") == "dry_run" for s in recipe["steps"]):
            recipe["steps"].append({"op": "dry_run", "by": "cli"})
    if show and not args.json:
        print(t("cli.sentence.task"), file=sys.stderr)
        shown = {"sources": len(recipe.get("sources") or []) if args.aggregate else recipe.get("sources"),  # aggregate: no paths
                 "steps": recipe.get("steps")}
        print(json.dumps(shown, ensure_ascii=False, indent=1), file=sys.stderr)
    if getattr(args, "plan", False) and not recipe.get("sources"):
        print(json.dumps(normalise(recipe, base_dir=base), ensure_ascii=False, indent=1))
        return 0
    settings = {"sentence": sentence} if sentence else {}
    with make_engine(args, progress, settings=settings) as eng:
        res = eng.run_recipe(recipe, base_dir=base, by="sentence" if sentence else "recipe")
        lines = summary_lines(res, eng.aggregate)
        if getattr(args, "plan", False):
            r = eng.op("cost", {}, by="cli")
            lines.append(t("cli.cost.line", **_cost_fmt(r["estimate"])))
            lines.append(t("cli.plan.steps", n=len(recipe["steps"])))
        return print_result(args, eng, res, lines)
