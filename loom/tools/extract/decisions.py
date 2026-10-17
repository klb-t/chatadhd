"""Manual decisions (``decisions.json``, ``loom.extract.decisions/1``): the user's
marks (include / exclude / ambiguous per unit, optionally per message) applied
with ``manual: wins`` in the cascade.  A mark records what the cascade said
(``was``) so a correction can be learned from; learning produces PROPOSALS
(new keywords, exclusions, band changes) once ``decisions.learn_min_corrections``
corrections agree — nothing becomes a rule without the user accepting it.
A mark is keyed by the content hash (``uid``) AND, when the source has an id,
by ``(service, ext_id)``: a conversation that continued in a newer export (a new
message or a new title gives a new ``uid``) gets its mark carried by identity,
and the run reports it as carried; a mark that matches neither is reported as
unknown, never dropped silently.
"""
import logging
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

from .datafiles import DataStore, read_json, store as default_store, write_json
from .lexical import Lexicon, lexicon as default_lexicon
from .model import Unit

log = logging.getLogger(__name__)
SCHEMA = "loom.extract.decisions/1"
DECISIONS = ("include", "exclude", "ambiguous")


@dataclass
class Decisions:
    criteria: Optional[str] = None
    entries: list[dict[str, Any]] = field(default_factory=list)
    proposals: list[dict[str, Any]] = field(default_factory=list)
    path: Optional[Path] = None
    source: str = "none"

    # -- persistence -----------------------------------------------------------
    @classmethod
    def load(cls, path: Optional[Any]) -> "Decisions":
        if path is None:
            return cls()
        p = Path(str(path)).expanduser()
        if not p.exists():
            return cls(path=p, source="new")
        obj = read_json(p)
        if not isinstance(obj, dict) or obj.get("schema") != SCHEMA:
            raise ValueError(f"not a decisions file ({SCHEMA}): {p}")
        d = cls(criteria=obj.get("criteria"), entries=list(obj.get("entries") or []),
                proposals=list(obj.get("proposals") or []), path=p, source=f"file:{p}")
        for e in d.entries:
            if e.get("decision") not in DECISIONS:
                raise ValueError(f"decision entry with unknown decision {e.get('decision')!r} in {p}")
        return d

    def as_dict(self) -> dict[str, Any]:
        return {"schema": SCHEMA, "criteria": self.criteria, "entries": self.entries, "proposals": self.proposals}

    def save(self, path: Optional[Any] = None) -> Path:
        p = Path(str(path)).expanduser() if path is not None else self.path
        if p is None:
            raise ValueError("decisions: no path to save to")
        write_json(p, self.as_dict())
        self.path = p
        return p

    # -- marks -----------------------------------------------------------------
    def _find(self, uid: str, grain: str, message: Optional[str]) -> Optional[int]:
        for i, e in enumerate(self.entries):
            if e.get("unit") == uid and e.get("grain", "conversation") == grain and e.get("message") == message:
                return i
        return None

    def mark(self, uid: str, decision: str, reason: str = "", *, grain: str = "conversation",
             message: Optional[str] = None, was: Optional[dict[str, Any]] = None, at: Optional[str] = None,
             by: str = "cli", service: Optional[str] = None, ext_id: Optional[str] = None) -> dict[str, Any]:
        if decision not in DECISIONS:
            raise ValueError(f"unknown decision {decision!r} (one of {', '.join(DECISIONS)})")
        entry: dict[str, Any] = {"unit": uid, "grain": grain, "decision": decision, "reason": reason, "by": by}
        if ext_id:  # the identity a newer export keeps when the content (and so the uid) changes
            entry["service"], entry["ext_id"] = service or "", ext_id
        if message is not None:
            entry["message"] = message
        if was is not None:
            entry["was"] = was
        if at is not None:
            entry["at"] = at
        idx = self._find(uid, grain, message)
        if idx is None:
            self.entries.append(entry)
        else:
            self.entries[idx] = entry
        return entry

    def unmark(self, uid: str, *, grain: str = "conversation", message: Optional[str] = None) -> bool:
        idx = self._find(uid, grain, message)
        if idx is None:
            return False
        del self.entries[idx]
        return True

    def resolve(self, units: Optional[list[Unit]] = None) -> list[tuple[dict[str, Any], Optional[str], str]]:
        """(entry, current uid or None, how) for every entry: ``uid`` (the same content), ``identity`` (carried to a
        newer version of the same conversation by (service, ext_id)) or ``unknown``."""
        if units is None:
            return [(e, e["unit"], "uid") for e in self.entries]
        by_uid = {u.uid for u in units}
        by_identity = {u.identity_key: u.uid for u in units if u.ext_id}
        out: list[tuple[dict[str, Any], Optional[str], str]] = []
        for e in self.entries:
            if e["unit"] in by_uid:
                out.append((e, e["unit"], "uid"))
            elif e.get("ext_id") and (e.get("service") or "", e["ext_id"]) in by_identity:
                out.append((e, by_identity[(e.get("service") or "", e["ext_id"])], "identity"))
            else:
                out.append((e, None, "unknown"))
        return out

    def as_manual(self, units: Optional[list[Unit]] = None) -> dict[str, dict[str, Any]]:
        """current uid → entry, for ``Cascade(manual=…)`` (conversation grain only; message marks are applied by the
        engine); with ``units``, a mark of an older version is carried to the newer one by identity."""
        out: dict[str, dict[str, Any]] = {}
        for e, uid, how in self.resolve(units):
            if uid is not None and e.get("grain", "conversation") == "conversation" and e.get("message") is None:
                out[uid] = {"decision": e["decision"], "reason": e.get("reason", ""), "grain": "conversation",
                            **({"carried": e["unit"]} if how == "identity" else {})}
        return out

    def message_marks(self) -> dict[str, dict[str, str]]:
        """uid → {message id → decision} for message-grain marks."""
        out: dict[str, dict[str, str]] = {}
        for e in self.entries:
            if e.get("message") is not None:
                out.setdefault(e["unit"], {})[e["message"]] = e["decision"]
        return out

    def corrections(self) -> list[dict[str, Any]]:
        """Entries where the user overrode the cascade (``was.decision`` differs)."""
        return [e for e in self.entries if isinstance(e.get("was"), dict) and e["was"].get("decision") != e["decision"]]

    # -- learning (proposals only) ---------------------------------------------
    def propose(self, units: list[Unit], ds: Optional[DataStore] = None, lex: Optional[Lexicon] = None,
                *, min_corrections: Optional[int] = None, top_terms: int = 5) -> list[dict[str, Any]]:
        """Deterministic proposals from corrections: terms shared by ≥ M corrected inclusions
        (→ keyword), terms shared by ≥ M corrected exclusions and absent from inclusions
        (→ exclude keyword), and band moves when ≥ M corrections of one direction were
        ``ambiguous``.  Stored in ``proposals`` with their support; never applied."""
        ds = ds or default_store()
        lex = lex or default_lexicon()
        m = int(ds.preset("decisions.learn_min_corrections", 2) if min_corrections is None else min_corrections)
        if m <= 0:
            m = 1
        by_uid = {u.uid: u for u in units}
        inc: list[dict[str, Any]] = []
        exc: list[dict[str, Any]] = []
        for e in self.corrections():
            if e["unit"] not in by_uid:
                continue
            (inc if e["decision"] == "include" else exc if e["decision"] == "exclude" else []).append(e)

        def term_sets(entries: list[dict[str, Any]]) -> list[set[str]]:
            out = []
            for e in entries:
                u = by_uid[e["unit"]]
                text = (u.title or "") + "\n" + u.text()
                out.append(set(lex.term_keys(text)))
            return out

        inc_sets, exc_sets = term_sets(inc), term_sets(exc)
        proposals: list[dict[str, Any]] = []

        def shared(sets: list[set[str]], other: list[set[str]]) -> list[tuple[str, int]]:
            df: dict[str, int] = {}
            for s in sets:
                for t in s:
                    df[t] = df.get(t, 0) + 1
            other_all: set[str] = set().union(*other) if other else set()
            cands = [(t, n) for t, n in df.items() if n >= m and t not in other_all]
            cands.sort(key=lambda x: (-x[1], x[0]))
            return cands[:top_terms] if top_terms > 0 else cands

        for term, n in shared(inc_sets, exc_sets):
            proposals.append({"kind": "keyword", "term": term, "support": n, "min": m,
                              "units": sorted(e["unit"] for e, s in zip(inc, inc_sets) if term in s)})
        for term, n in shared(exc_sets, inc_sets):
            proposals.append({"kind": "exclude_keyword", "term": term, "support": n, "min": m,
                              "units": sorted(e["unit"] for e, s in zip(exc, exc_sets) if term in s)})
        amb_inc = [e["was"].get("score") for e in inc if e["was"].get("decision") == "ambiguous" and isinstance(e["was"].get("score"), (int, float))]
        if len(amb_inc) >= m:
            proposals.append({"kind": "band", "band": "include", "value": round(min(amb_inc) - 0.005, 3), "support": len(amb_inc), "min": m})
        amb_exc = [e["was"].get("score") for e in exc if e["was"].get("decision") == "ambiguous" and isinstance(e["was"].get("score"), (int, float))]
        if len(amb_exc) >= m:
            proposals.append({"kind": "band", "band": "exclude", "value": round(max(amb_exc) + 0.005, 3), "support": len(amb_exc), "min": m})
        for e in self.entries:
            learn = e.get("learn")
            if isinstance(learn, dict):
                proposals.append({"kind": "user", "support": 1, "min": m, "units": [e["unit"]], **learn})
        self.proposals = proposals
        return proposals

    def apply_proposals(self, criteria_obj: dict[str, Any], *, accept: Optional[list[int]] = None) -> dict[str, Any]:
        """Return a criteria overlay object with the accepted proposals (all when ``accept`` is None)."""
        out = {"schema": "loom.extract.criteria/1", "id": criteria_obj.get("id", "criteria"), "keywords": [], "exclude": {"keywords": []},
               "bands": {}}
        for i, p in enumerate(self.proposals):
            if accept is not None and i not in accept:
                continue
            if p["kind"] == "keyword":
                out["keywords"].append({"term": p["term"], "inflect": "rules", "weight": 1.0, "from": "proposal"})
            elif p["kind"] == "exclude_keyword":
                out["exclude"]["keywords"].append({"term": p["term"], "from": "proposal"})
            elif p["kind"] == "band":
                out["bands"][p["band"]] = p["value"]
        return out
