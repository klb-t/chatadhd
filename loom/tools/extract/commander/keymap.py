"""Keymap as data: profiles with inheritance, sequences (``g g``), validation and generated help.

A child profile's binding replaces the parent's for the same key; a child's sequence shadows the
parent's single key that starts it (``vim``'s ``d d`` shadows ``fkeys``'s ``d``) — reported in
``shadowed``, never silently.  Problems (unknown action, a key that is both a binding and the
prefix of a sequence, an inheritance cycle) are listed in ``problems``; the commander shows them
and the tests require none in the shipped profiles."""
from typing import Any, Optional

from ..datafiles import DataStore
from ..i18n import label as _label


class Keymap:
    def __init__(self, profile: str, bindings: dict[str, str], actions: dict[str, dict[str, Any]],
                 problems: list[str], shadowed: list[str]) -> None:
        self.profile = profile
        self.bindings = bindings
        self.actions = actions
        self.problems = problems
        self.shadowed = shadowed
        self._prefixes = {" ".join(k.split()[:i]) for k in bindings for i in range(1, len(k.split()))}

    @classmethod
    def load(cls, ds: DataStore, profile: Optional[str] = None) -> "Keymap":
        doc = ds.file("commander/keymap.json")
        acts = {a["id"]: a for a in ds.file("commander/actions.json")["actions"]}
        profiles = doc.get("profiles") or {}
        name = profile or ds.preset("commander.keymap") or doc.get("default") or "fkeys"
        problems: list[str] = []
        shadowed: list[str] = []
        if name not in profiles:
            problems.append(f"unknown keymap profile {name!r} (known: {', '.join(sorted(profiles))}); using {doc.get('default', 'fkeys')!r}")
            name = doc.get("default") or "fkeys"
        chain: list[str] = []
        cur: Optional[str] = name
        while cur:
            if cur in chain:
                problems.append(f"inheritance cycle: {' → '.join(chain + [cur])}")
                break
            if cur not in profiles:
                problems.append(f"profile {chain[-1] if chain else name!r} inherits unknown {cur!r}")
                break
            chain.append(cur)
            cur = profiles[cur].get("inherits")
        eff: dict[str, str] = {}
        for pname in reversed(chain):
            own = (profiles[pname].get("bindings") or {})
            for key in own:
                parts = key.split()
                if len(parts) > 1 and parts[0] in eff and parts[0] not in own:
                    shadowed.append(f"{pname}: '{key}' shadows '{parts[0]}' → {eff[parts[0]]}")
                    eff.pop(parts[0])
            eff.update({" ".join(k.split()): v for k, v in own.items()})
        for key, act in sorted(eff.items()):
            if act not in acts:
                problems.append(f"{name}: key {key!r} → unknown action {act!r}")
        prefixes = {" ".join(k.split()[:i]) for k in eff for i in range(1, len(k.split()))}
        for key in sorted(eff):
            if key in prefixes:
                problems.append(f"{name}: key {key!r} is both a binding and the start of a sequence")
        return cls(name, eff, acts, problems, shadowed)

    def resolve(self, pending: list[str], key: str) -> tuple[Optional[str], list[str]]:
        """One key → (action or None, the pending prefix after it)."""
        seq = " ".join(pending + [key])
        if seq in self.bindings:
            return self.bindings[seq], []
        if seq in self._prefixes:
            return None, pending + [key]
        if pending:  # an unfinished sequence followed by something else: the key counts on its own
            return self.resolve([], key)
        return None, []

    def keys_for(self, action: str) -> list[str]:
        return sorted((k for k, a in self.bindings.items() if a == action), key=lambda k: (len(k) > 3, len(k), k))

    def help_lines(self) -> list[str]:
        """F1: every action of the profile with its keys and label, in actions.json order, grouped."""
        out: list[str] = []
        group = None
        for aid, a in self.actions.items():
            keys = self.keys_for(aid)
            if not keys:
                continue
            if a.get("group") != group:
                group = a.get("group")
                out.append(f"[{group}]")
            out.append(f"  {', '.join(keys):<18} {_label(a.get('label'))}")
        return out
