"""Lossless tree walk with the semantics of ``walk_tree`` in
``loom/src/archive/ingest_parse.cpp``: document order, current path from the
provider's leaf pointer, forks with alternatives, linear chain when no parents.

Parity with the C++ importer is guarded by the same oracle
(``loom/tests/fixtures/exports/EXPECTED.json``), not by duplicated code.
"""
from dataclasses import dataclass, field
from typing import Any, Optional


@dataclass
class TreeNode:
    parent: Optional[str]
    emit: bool
    payload: Any = None


@dataclass
class Emitted:
    id: str
    parent: Optional[str]        # nearest EMITTED ancestor (None for a root message)
    branch: str                  # "" on the trunk, "0/1" below forks
    on_current: bool
    payload: Any


@dataclass
class WalkResult:
    messages: list[Emitted] = field(default_factory=list)
    forks: list[dict[str, Any]] = field(default_factory=list)
    roots: list[str] = field(default_factory=list)
    current_leaf: Optional[str] = None
    unreachable: list[str] = field(default_factory=list)   # nodes not reachable from any root (cycles), kept
    fork_points_raw: int = 0     # nodes with >= 2 children in the raw structure (oracle definition)
    leaves_raw: int = 0          # emitted nodes with no children (oracle definition)
    current_path_len: int = 0    # emitted nodes on the current path (oracle definition)


def walk_tree(order: list[str], nodes: dict[str, TreeNode], current_leaf: Optional[str]) -> WalkResult:
    out = WalkResult()
    children: dict[str, list[str]] = {}
    roots: list[str] = []
    for nid in order:
        n = nodes[nid]
        if not n.parent or n.parent not in nodes:
            roots.append(nid)
        else:
            children.setdefault(n.parent, []).append(nid)
    out.roots = list(roots)

    # pre-order to compute subtree emitted counts
    pre: list[str] = []
    stack = list(reversed(roots))
    while stack:
        nid = stack.pop()
        pre.append(nid)
        for c in reversed(children.get(nid, [])):
            stack.append(c)
    count: dict[str, int] = {}
    for nid in reversed(pre):
        c = 1 if nodes[nid].emit else 0
        for k in children.get(nid, []):
            c += count.get(k, 0)
        count[nid] = c

    on_current: set[str] = set()
    cur = current_leaf
    while cur and cur in nodes:
        if cur in on_current:
            break
        on_current.add(cur)
        cur = nodes[cur].parent
    out.current_leaf = current_leaf if current_leaf in nodes else None

    def first_emitted(start: str) -> str:
        st = [start]
        while st:
            nid = st.pop()
            if nodes[nid].emit:
                return nid
            for c in reversed(children.get(nid, [])):
                st.append(c)
        return ""

    frames: list[tuple[str, str, Optional[str]]] = [(r, "", None) for r in reversed(roots)]
    visited: set[str] = set()
    while frames or len(visited) < len(order):
        if not frames:
            # nodes unreachable from any root (cycles): kept as extra roots in document order, flagged
            for cand in order:
                if cand not in visited:
                    out.unreachable.append(cand)
                    out.roots.append(cand)
                    frames.append((cand, "", None))
                    break
        nid, branch, last = frames.pop()
        if nid in visited:
            continue
        visited.add(nid)
        n = nodes[nid]
        if n.emit:
            out.messages.append(Emitted(nid, last, branch, (not current_leaf) or (nid in on_current), n.payload))
            last = nid
        ch = children.get(nid)
        if not ch:
            continue
        live = [c for c in ch if count.get(c, 0) > 0 and c not in visited]
        if len(live) >= 2:
            alts = [{"first_node": first_emitted(c), "messages": count[c],
                     "current": bool(current_leaf) and c in on_current} for c in live]
            out.forks.append({"node": last, "alternatives": alts})
        for k in range(len(live) - 1, -1, -1):
            b = branch
            if len(live) >= 2:
                b = f"{branch}/{k}" if branch else str(k)
            frames.append((live[k], b, last))

    # oracle-style raw counts (independent of emission liveness)
    out.fork_points_raw = sum(1 for nid in order if len(children.get(nid, [])) >= 2)
    out.leaves_raw = sum(1 for nid in order if nodes[nid].emit and not children.get(nid))
    out.current_path_len = sum(1 for nid in on_current if nodes[nid].emit)
    return out


def linear_chain(order: list[str], nodes: dict[str, TreeNode]) -> None:
    """When no node has a parent, chain them in array order (Claude legacy)."""
    for i in range(1, len(order)):
        nodes[order[i]].parent = order[i - 1]
