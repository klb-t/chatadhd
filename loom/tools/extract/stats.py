"""Statistics per conversation and overall (messages, words, characters,
tools, languages, roles, periods) and the topic timeline (package activity per
month and per service) — measured from the package, never estimated."""
from typing import Any

from .cut.common import Entry
from .timestamps import key as _tskey


def _words(text: str) -> int:
    return len(text.split())


def conversation_stats(e: Entry) -> dict[str, Any]:
    u = e.unit
    msgs = [m for m in u.messages if e.messages is None or m.id in e.messages]
    roles: dict[str, int] = {}
    tools: dict[str, int] = {}
    kinds: dict[str, int] = {}
    words = chars = 0
    for m in msgs:
        roles[m.role] = roles.get(m.role, 0) + 1
        t = m.text()
        words += _words(t)
        chars += m.chars()
        for n in m.tool_names():
            tools[n] = tools.get(n, 0) + 1
        for b in m.blocks:
            kinds[b.kind] = kinds.get(b.kind, 0) + 1
    stamps = sorted((m.created_utc for m in msgs if m.created_utc), key=_tskey)
    st = u.stats or {}
    return {"key": e.key, "uid": u.uid, "service": u.service, "messages": len(msgs), "words": words, "chars": chars,
            "roles": dict(sorted(roles.items())), "tools": dict(sorted(tools.items())), "blocks": dict(sorted(kinds.items())),
            "lang": st.get("lang") or {}, "lang_main": st.get("lang_main"),
            "period": {"from": stamps[0] if stamps else u.created_utc, "to": stamps[-1] if stamps else u.updated_utc},
            "attachments": len(u.attachments), "branches": st.get("branches", 0), "agentic": u.is_agentic()}


def package_stats(entries: list[Entry], *, aggregate: bool = False) -> dict[str, Any]:
    per = [conversation_stats(e) for e in entries]
    tot: dict[str, Any] = {"conversations": len(per), "messages": 0, "words": 0, "chars": 0, "attachments": 0, "agentic": 0}
    roles: dict[str, int] = {}
    tools: dict[str, int] = {}
    langs: dict[str, int] = {}
    services: dict[str, int] = {}
    months: dict[str, dict[str, Any]] = {}
    for c in per:
        for k in ("messages", "words", "chars", "attachments"):
            tot[k] += c[k]
        tot["agentic"] += int(c["agentic"])
        for k, v in c["roles"].items():
            roles[k] = roles.get(k, 0) + v
        for k, v in c["tools"].items():
            tools[k] = tools.get(k, 0) + v
        if c["lang_main"]:
            langs[c["lang_main"]] = langs.get(c["lang_main"], 0) + 1
        s = c["service"] or "?"
        services[s] = services.get(s, 0) + 1
        mon = (c["period"]["from"] or "")[:7] or "?"
        b = months.setdefault(mon, {"month": mon, "conversations": 0, "messages": 0, "by_service": {}})
        b["conversations"] += 1
        b["messages"] += c["messages"]
        b["by_service"][s] = b["by_service"].get(s, 0) + 1
    froms = sorted(c["period"]["from"] for c in per if c["period"]["from"])
    tos = sorted(c["period"]["to"] for c in per if c["period"]["to"])
    out = {"schema": "loom.extract.stats/1", "total": tot, "roles": dict(sorted(roles.items())),
           "tools": dict(sorted(tools.items(), key=lambda kv: (-kv[1], kv[0]))), "languages": dict(sorted(langs.items())),
           "services": dict(sorted(services.items())), "period": {"from": froms[0] if froms else None, "to": tos[-1] if tos else None},
           "topic_timeline": [months[k] for k in sorted(months)],
           "conversations": [] if aggregate else per,
           "how": "measured from the package entries (selected messages only); words = whitespace-separated tokens of text blocks"}
    if aggregate:
        out["tools"] = {"distinct": len(tools), "calls": sum(tools.values())}
    return out
