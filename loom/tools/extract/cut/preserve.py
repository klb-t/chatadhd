"""Profile ``preserve``: the source format 1:1 with fewer conversations.

Per archive: the same container kind (zip → zip with the source's member
metadata, directory → directory, file → file), the same member names, every
kept conversation copied from its locator as the source's own bytes
(``jsonspan``), the separators of the source, the attachments the kept
conversations use in the source layout — so the references (file ids and
member names) stay valid inside the package and the cut re-imports into the
source app (where it can import) and into ``engine/importer.py`` and this tool.
Members that are not conversations follow the format's ``preserve`` table:
``keep_members`` are copied, ``filter_members`` keep the records of kept
conversations (by a field such as ``conversation_id``), everything else is left
out WITH an entry (name and reason) — nothing disappears silently.  A member
without per-conversation boundaries (a session JSONL, an HTML page, Markdown)
is copied whole when any of its conversations is kept, and counted as such.
With redaction active, kept elements are re-serialised with the masks applied
and the manifest says the copy is no longer byte-exact.
"""
import fnmatch
import io
import json
import logging
from pathlib import Path
from typing import Any, Optional

from ..jsonspan import SpanError, filter_array
from .common import DirSink, Outputs, ZipSink, archive_slugs, copy_stream, safe_name, zip_info_of

log = logging.getLogger(__name__)


def _indices(unit: Any) -> tuple[Optional[str], set[int]]:
    """(wrapper, array indices) a unit occupies in its member, from its locator and message pointers."""
    wrapper: Optional[str] = None
    out: set[int] = set()
    loc = (unit.source or {}).get("locator") or {}
    ptrs = [loc.get("json_pointer")] + list(loc.get("entries") or []) + [m.raw_pointer for m in unit.messages]
    for p in ptrs:
        if not isinstance(p, str) or not p.startswith("/"):
            continue
        parts = p.split("/")[1:]
        if parts and parts[0].isdigit():
            out.add(int(parts[0]))
        elif len(parts) >= 2 and parts[1].isdigit():
            wrapper = parts[0].replace("~1", "/").replace("~0", "~")
            out.add(int(parts[1]))
    return wrapper, out


def _match(name: str, patterns: Any) -> Optional[str]:
    if isinstance(patterns, dict):
        for pat, val in patterns.items():
            if fnmatch.fnmatchcase(name, pat):
                return val
        return None
    for pat in patterns or []:
        if fnmatch.fnmatchcase(name, pat):
            return pat
    return None


class PreserveResult:
    def __init__(self) -> None:
        self.archives: list[dict[str, Any]] = []
        self.byte_exact = True

    def as_dict(self, aggregate: bool) -> dict[str, Any]:
        arch = []
        for a in self.archives:
            d = dict(a)
            if aggregate:
                d["omitted"] = len(d.get("omitted") or [])
                d["indivisible"] = len(d.get("indivisible") or [])
                d["missing"] = len(d.get("missing") or [])
            arch.append(d)
        return {"archives": arch, "byte_exact": self.byte_exact,
                "conversations": sum(a["conversations_kept"] for a in self.archives),
                "attachments_copied": sum(a["attachments_copied"] for a in self.archives),
                "attachments_bytes": sum(a["attachments_bytes"] for a in self.archives),
                "attachments_missing": sum(len(a["missing"]) for a in self.archives)}


def cut_preserve(eng: Any, outputs: Outputs, origins: set[str], *, settings: dict[str, Any], redactor: Any = None,
                 attachments_mode: str = "copy_used") -> PreserveResult:
    res = PreserveResult()
    raw_by_archive: dict[tuple[str, str], list[Any]] = {}
    for u in eng.units_raw:
        src = u.source or {}
        raw_by_archive.setdefault((src.get("archive") or "", src.get("sha256") or ""), []).append(u)
    other_policy = settings.get("other_members", "omit")
    slugs = archive_slugs(eng.archives)
    for a in eng.archives:
        units = raw_by_archive.get((a.label, a.sha256), [])
        kept = [u for u in units if u.uid in origins and u.kind in ("conversation", "session")]
        if not kept:
            continue
        info = _cut_archive(eng, a, units, kept, outputs, settings, redactor, attachments_mode, other_policy, res, slugs[(a.label, a.sha256)])
        res.archives.append(info)
    return res


def _cut_archive(eng: Any, a: Any, units: list[Any], kept: list[Any], outputs: Outputs, settings: dict[str, Any],
                 redactor: Any, attachments_mode: str, other_policy: str, res: PreserveResult, slug: str) -> dict[str, Any]:
    fmt_id = a.format or (kept[0].source or {}).get("format")
    fmt = eng.catalog.get(fmt_id) if fmt_id else {}
    pres = dict(fmt.get("preserve") or {})
    base_rel = "preserve/" + slug
    if a.kind == "zip":
        sink: Any = ZipSink(outputs.root / base_rel)
    elif a.kind == "dir":
        sink = DirSink(outputs.root / base_rel)
    else:
        sink = DirSink(outputs.root / "preserve")
    kept_ids = {u.uid for u in kept}
    by_member: dict[str, list[Any]] = {}
    for u in units:
        by_member.setdefault((u.source or {}).get("member") or "", []).append(u)
    ext_ids = {str(u.ext_id) for u in kept if u.ext_id}
    used_assets: dict[str, int] = {}
    missing: list[dict[str, Any]] = []
    for u in kept:
        for att in u.attachments:
            if att.status == "resolved" and att.member:
                used_assets[att.member] = used_assets.get(att.member, 0) + 1
            elif att.status != "resolved":
                missing.append({"ref": att.ref, "conversation": u.uid, "message": att.message})
    info: dict[str, Any] = {"archive": a.label, "kind": a.kind, "format": fmt_id, "output": base_rel,
                            "conversations_kept": 0, "conversations_total": sum(1 for u in units if u.kind in ("conversation", "session")),
                            "members_written": 0, "omitted": [], "indivisible": [], "partial_whole": 0,
                            "attachments_copied": 0, "attachments_bytes": 0, "missing": missing, "byte_exact": True}
    lead = bool(pres.get("lead"))
    try:
        for m in a.members:
            name = m.name
            target = name if a.kind != "file" else slug
            zinfo = zip_info_of(a, name)
            if m.kind == "container":
                info["omitted"].append({"member": name, "why": "nested archive (cut separately as its own archive)"})
                continue
            if m.kind == "asset":
                if name in used_assets and attachments_mode != "reference_only":
                    with m.open() as src, sink.open(target, zinfo) as dst:
                        n = copy_stream(src, dst)
                    info["attachments_copied"] += 1
                    info["attachments_bytes"] += n
                    info["members_written"] += 1
                elif attachments_mode == "copy_all":
                    with m.open() as src, sink.open(target, zinfo) as dst:
                        copy_stream(src, dst)
                    info["members_written"] += 1
                else:
                    info["omitted"].append({"member": name, "why": "attachment not used by a kept conversation"
                                            if attachments_mode != "reference_only" else "attachments: reference_only"})
                continue
            if m.kind == "conversations":
                mu = by_member.get(name, [])
                mk = [u for u in mu if u.uid in kept_ids]
                if not mk:
                    info["omitted"].append({"member": name, "why": "no kept conversation in this member"})
                    continue
                info["conversations_kept"] += len(mk)
                info["partial_whole"] += sum(1 for u in mk if _partial(eng, u))
                self_written = _write_conversations(m, mu, mk, target, zinfo, sink, fmt, pres, lead, redactor, info)
                if self_written:
                    info["members_written"] += 1
                continue
            keep = _match(name, pres.get("keep_members"))
            filt = _match(name, pres.get("filter_members") or {})
            if keep is not None or other_policy == "copy":
                with m.open() as src, sink.open(target, zinfo) as dst:
                    if redactor is not None and redactor.active:
                        _copy_redacted(src, dst, redactor, name, info)
                    else:
                        copy_stream(src, dst)
                info["members_written"] += 1
            elif filt is not None:
                if _write_filtered(m, target, zinfo, sink, str(filt), ext_ids, redactor if redactor is not None and redactor.active else None, info):
                    info["members_written"] += 1
            else:
                info["omitted"].append({"member": name, "why": f"{m.kind or 'unknown'} member not in the format's preserve table"})
    finally:
        sink.close()
    if a.kind == "zip":
        outputs.add_file(base_rel, profile="preserve", archive=a.label)
    else:
        for name in sink.written:
            rel = (base_rel + "/" + name) if a.kind == "dir" else f"preserve/{name}"
            outputs.add_file(rel, profile="preserve", archive=a.label)
    if not info["byte_exact"]:
        res.byte_exact = False
    return info


def _partial(eng: Any, u: Any) -> bool:
    sel = eng.selection.units.get(u.uid) if hasattr(eng, "selection") else None
    return sel is not None


def _write_conversations(m: Any, mu: list[Any], mk: list[Any], target: str, zinfo: Any, sink: Any, fmt: dict[str, Any],
                         pres: dict[str, Any], lead: bool, redactor: Any, info: dict[str, Any]) -> bool:
    wrapper: Optional[str] = None
    keep_idx: set[int] = set()
    have_idx = True
    for u in mk:
        w, idx = _indices(u)
        if not idx:
            have_idx = False
        wrapper = wrapper or w
        keep_idx |= idx
    redacting = redactor is not None and redactor.active
    if have_idx and (len(mk) < len(mu) or redacting):
        try:
            with m.open() as src, sink.open(target, zinfo) as dst:
                if redactor is not None and redactor.active:
                    info["byte_exact"] = False
                    _filter_redacted(src, dst, keep_idx, wrapper, lead, redactor, m.name)
                else:
                    filter_array(src, dst, lambda i, data: i in keep_idx, wrapper=wrapper, lead=lead)
            return True
        except SpanError as exc:
            log.debug("preserve: %s is not a plain JSON array (%s); copied whole", m.name, exc)
            info["indivisible"].append({"member": m.name, "why": f"no per-conversation boundary ({exc})",
                                        "other_conversations": len(mu) - len(mk)})
    elif len(mk) < len(mu):
        info["indivisible"].append({"member": m.name, "why": "the format has no per-conversation boundary in this member",
                                    "other_conversations": len(mu) - len(mk)})
    with m.open() as src, sink.open(target, zinfo) as dst:
        if redacting:
            _copy_redacted(src, dst, redactor, m.name, info)
        else:
            copy_stream(src, dst)
    return True


_TEXT_EXT = (".md", ".markdown", ".txt", ".html", ".htm", ".mht", ".mhtml", ".js", ".csv", ".log")


def _copy_redacted(src: Any, dst: Any, redactor: Any, name: str, info: dict[str, Any]) -> None:
    """A whole member with the masks applied: JSON re-serialised, JSONL line by line, text as text; a binary member
    is copied unchanged and listed under ``not_redacted``."""
    info["byte_exact"] = False
    data = src.read()
    low = name.lower()
    where = {"member": name}
    try:
        if low.endswith(".jsonl"):
            out = []
            for i, line in enumerate(data.decode("utf-8", errors="replace").splitlines()):
                if not line.strip():
                    out.append(line)
                    continue
                try:
                    out.append(json.dumps(redactor.value(json.loads(line), {**where, "line": i + 1}), ensure_ascii=False))
                except json.JSONDecodeError:
                    out.append(redactor.text(line, {**where, "line": i + 1}))
            dst.write(("\n".join(out) + ("\n" if data.endswith(b"\n") else "")).encode("utf-8", errors="backslashreplace"))
            return
        if low.endswith(".json"):
            obj = json.loads(data.decode("utf-8", errors="replace"))
            dst.write(json.dumps(redactor.value(obj, where), ensure_ascii=False).encode("utf-8", errors="backslashreplace"))
            return
        if low.endswith(_TEXT_EXT) or not name:
            dst.write(redactor.text(data.decode("utf-8", errors="replace"), where).encode("utf-8", errors="backslashreplace"))
            return
    except json.JSONDecodeError:
        log.debug("redaction: %s is not valid JSON; masked as text", name, exc_info=True)
        dst.write(redactor.text(data.decode("utf-8", errors="replace"), where).encode("utf-8", errors="backslashreplace"))
        return
    dst.write(data)
    info.setdefault("not_redacted", []).append(name)


def _filter_redacted(src: Any, dst: Any, keep_idx: set[int], wrapper: Optional[str], lead: bool, redactor: Any, member: str) -> None:
    from ..jsonspan import iter_spans
    kept = 0
    pending: Optional[bytes] = None
    for kind, idx, data in iter_spans(src, wrapper=wrapper, lead=lead):
        if kind in ("prefix", "suffix"):
            dst.write(data)
        elif kind == "sep":
            pending = data
        elif kind == "element":
            if idx in keep_idx:
                obj = json.loads(data.decode("utf-8", errors="replace"))
                obj = redactor.value(obj, {"member": member, "element": idx})
                if kept:
                    dst.write(pending or b",")
                dst.write(json.dumps(obj, ensure_ascii=False).encode("utf-8", errors="backslashreplace"))
                kept += 1
            pending = None


def _write_filtered(m: Any, target: str, zinfo: Any, sink: Any, field: str, ext_ids: set[str], redactor: Any,
                    info: dict[str, Any]) -> bool:
    head = m.head(4096).lstrip()[:1]
    if head == b"[":
        def keep(i: int, data: bytes) -> bool:
            try:
                obj = json.loads(data.decode("utf-8", errors="replace"))
            except json.JSONDecodeError:
                return False
            return isinstance(obj, dict) and str(obj.get(field)) in ext_ids
        try:
            with m.open() as src, sink.open(target, zinfo) as dst:
                if redactor is not None:
                    buf = io.BytesIO()
                    r = filter_array(src, buf, keep)
                    buf.seek(0)
                    _copy_redacted(buf, dst, redactor, m.name, info)
                else:
                    r = filter_array(src, dst, keep)
            if r["kept"] < r["total"]:
                info["omitted"].append({"member": m.name, "why": f"{r['total'] - r['kept']} record(s) of other conversations left out",
                                        "kept": r["kept"]})
            return True
        except SpanError as exc:
            info["omitted"].append({"member": m.name, "why": f"filter member unreadable ({exc})"})
            return False
    try:
        obj = json.loads(m.read_bytes().decode("utf-8", errors="replace"))
    except json.JSONDecodeError as exc:
        info["omitted"].append({"member": m.name, "why": f"filter member unreadable ({exc})"})
        return False
    if isinstance(obj, dict) and str(obj.get(field)) in ext_ids:
        with m.open() as src, sink.open(target, zinfo) as dst:
            if redactor is not None:
                _copy_redacted(src, dst, redactor, m.name, info)
            else:
                copy_stream(src, dst)
        return True
    info["omitted"].append({"member": m.name, "why": "record of a conversation that is not kept"})
    return False
