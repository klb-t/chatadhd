"""Offline read-only archive text inventory and optional model-cost estimate.

SQLite groups all stored message rows, including excluded, tool, version and
deleted rows; projection scope is separate and explicit. Python retains no
archive-wide text or per-conversation list. Grouping/sorting can use temporary
files with a caller-selected SQLite cache size; source databases stay read-only.
Counts cover stored message text, not raw source JSON or attachment/OCR bodies.

Token counts are configurable character-based ESTIMATES, not tokenizer or
provider billing measurements. Prices must be supplied; the historical table is
opt-in. No network or model calls: private exports remain on the owner's machine.

    python3 loom/tools/eval/archive_cost.py --db ~/.chatadhd/chatadhd.db
    python3 loom/tools/eval/archive_cost.py --db DB --scope active --input-price 1 --output-price 5
    python3 loom/tools/eval/archive_cost.py --db DB --fraction 0.1 --json
"""
from __future__ import annotations

import argparse
from collections.abc import Mapping
import hashlib
import json
import math
import sqlite3
import sys
from pathlib import Path

_UNSET = object()
_REPO_ROOT = Path(__file__).resolve().parents[3]
BUILTIN_AUDIT_PRESET = _REPO_ROOT / "loom/data/presets/import_audit.pack"
STATUS_CLASSES = ("active", "version", "excluded", "deleted", "unknown")
TOOL_ROLES = ("tool", "function")


def archive_stats(db_path: str | Path, *, sqlite_cache_kib=_UNSET, audit_preset=_UNSET) -> dict:
    """Complete read-only inventory, grouped in SQLite without retaining text.

    ``raw`` includes every stored row. Historical aliases are preserved:
    ``messages``/``conversations`` exclude deleted, ``chars_by_role`` is active
    only, and ``conversation_chars`` omits excluded/deleted text. New consumers
    should use explicit ``raw`` and ``projected`` scopes instead. Counting one
    message may temporarily materialize its text; no archive-wide list exists.
    """
    values = load_audit_preset(audit_preset)
    sqlite_cache_kib = _effective(sqlite_cache_kib, values, "sqlite_cache_kib")
    _number(sqlite_cache_kib, "sqlite_cache_kib", integer=True)
    if not sqlite_cache_kib:
        raise ValueError("sqlite_cache_kib must be positive")
    # Quote URI-reserved filename characters; raw '?' or '#' changes the database
    # SQLite opens and can create a different sibling even with intended mode=ro.
    con = sqlite3.connect(Path(db_path).resolve().as_uri() + "?mode=ro", uri=True)
    try:
        con.execute("PRAGMA temp_store=FILE")
        con.execute(f"PRAGMA cache_size=-{sqlite_cache_kib}")
        con.execute(f"PRAGMA temp.cache_size=-{sqlite_cache_kib}")
        # SQLite length(TEXT) stops at NUL. The callback sees one row at a time;
        # Unicode len includes NUL and does not mistake UTF-8 bytes for chars.
        con.create_function("loom_text_chars", 1, lambda value: len(
            value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value or ""),
            deterministic=True)
        con.execute("BEGIN")  # one consistent snapshot across aggregates
        # The source connection uses mode=ro. Only SQLite's temporary database
        # is written: text becomes a count once, so later sorts never spool the
        # original message bodies and do not re-read/decode large text fields.
        con.execute("CREATE TEMP TABLE audit_rows AS SELECT conv_id,status,role,loom_text_chars(text) AS chars FROM main.messages")
        con.execute("PRAGMA query_only=ON")
        groups, status_counts, role_counts, active_roles = [], {}, {}, {}
        totals = {"messages": 0, "chars_active": 0, "chars_versions": 0,
                  "chars_excluded": 0, "chars_deleted": 0, "chars_unknown_status": 0,
                  "unknown_status_messages": 0, "tool_messages": 0, "chars_tools": 0}
        raw_messages = raw_chars = 0
        for status, role, count, chars in con.execute(
            "SELECT status, role, COUNT(*), SUM(chars) FROM audit_rows "
            "GROUP BY status, role ORDER BY status, role"
        ):
            category = status if status in STATUS_CLASSES[:-1] else "unknown"
            is_tool = role in TOOL_ROLES
            groups.append({"status": status, "status_class": category, "role": role,
                           "tool": is_tool, "messages": count, "chars": chars})
            for key, table in ((status, status_counts), (role, role_counts)):
                row = table.setdefault(key, {"messages": 0, "chars": 0})
                row["messages"] += count
                row["chars"] += chars
            raw_messages += count
            raw_chars += chars
            if category != "deleted":
                totals["messages"] += count
            field = {"active": "chars_active", "version": "chars_versions",
                     "excluded": "chars_excluded", "deleted": "chars_deleted",
                     "unknown": "chars_unknown_status"}[category]
            totals[field] += chars
            if category == "unknown":
                totals["unknown_status_messages"] += count
            if category == "active":
                active_roles[role] = active_roles.get(role, 0) + chars
            if is_tool:
                totals["tool_messages"] += count
                totals["chars_tools"] += chars

        # Five status classes x tool/non-tool = ten presence bits per convo.
        # Only <=1024 aggregated masks are returned, with no IDs or source text.
        bit_sql = ("(CASE status WHEN 'active' THEN 1 WHEN 'version' THEN 2 "
                   "WHEN 'excluded' THEN 4 WHEN 'deleted' THEN 8 ELSE 16 END) * "
                   "(CASE WHEN role IN ('tool','function') THEN 32 ELSE 1 END)")
        scope_groups = []
        raw_conversations = legacy_conversations = 0
        for mask, count in con.execute(
            "SELECT mask,COUNT(*) FROM (SELECT SUM(DISTINCT " + bit_sql + ")" +
            " AS mask FROM audit_rows GROUP BY conv_id) GROUP BY mask ORDER BY mask"
        ):
            non_tool = [name for index, name in enumerate(STATUS_CLASSES) if mask & (1 << index)]
            tool = [name for index, name in enumerate(STATUS_CLASSES) if mask & (1 << (index + 5))]
            scope_groups.append({"non_tool_statuses": non_tool, "tool_statuses": tool, "conversations": count})
            raw_conversations += count
            if any(name != "deleted" for name in non_tool + tool):
                legacy_conversations += count

        # SQL temporary grouping/sorting spills to files. Python retains only
        # the two middle values; legacy excluded-only conversations have size0.
        sizes_sql = ("SELECT SUM(CASE WHEN status='excluded' THEN 0 ELSE chars END) AS chars "
                     "FROM audit_rows WHERE status IS NULL OR status!='deleted' GROUP BY conv_id")
        maximum, median = con.execute(
            "SELECT COALESCE(MAX(chars),0),COALESCE(AVG(CASE WHEN position IN (?,?) THEN chars END),0) "
            "FROM (SELECT chars,ROW_NUMBER() OVER (ORDER BY chars) AS position FROM (" + sizes_sql + "))",
            ((legacy_conversations + 1) // 2, (legacy_conversations + 2) // 2),
        ).fetchone()
        result = {
            "schema": "loom.archive_text_audit/1",
            "measurement": "stored message text: Unicode characters including embedded NUL; not provider tokens",
            "coverage": "text only; retained source JSON, attachments/OCR, framing and hidden prompts excluded",
            "raw": {"conversations": raw_conversations, "messages": raw_messages, "chars": raw_chars},
            "conversations": legacy_conversations, **totals,
            "chars_by_role": dict(sorted(active_roles.items(), key=lambda row: (row[0] is not None, row[0] or ""))),
            "conversation_chars": {"median": median, "max": maximum},
            "status_counts": [{"status": status, **row} for status, row in status_counts.items()],
            "role_counts": [{"role": role, **row} for role, row in role_counts.items()],
            "message_groups": groups, "conversation_scope_groups": scope_groups,
            "execution": {"sqlite_cache_kib": sqlite_cache_kib, "temporary_storage": "file; counts, roles/statuses and conversation IDs only",
                          "python_retention": "status/role aggregates and <=1024 conversation-presence groups"},
        }
        # This inventory projection explicitly covers every stored status/role.
        result["projected"] = project_stats(result, include_active=True, include_versions=True,
            include_unknown_status=True, include_excluded=True, include_deleted=True,
            include_tools=True, audit_preset=values)
        return result
    finally:
        con.close()


def project_stats(stats: dict, *, include_active=_UNSET, include_versions=_UNSET,
                  include_unknown_status=_UNSET, include_excluded=_UNSET,
                  include_deleted=_UNSET, include_tools=_UNSET, audit_preset=_UNSET) -> dict:
    """Project a selected text scope from aggregates without reading content."""
    values = load_audit_preset(audit_preset)
    include_active = _effective(include_active, values, "api_include_active")
    include_versions = _effective(include_versions, values, "api_include_versions")
    include_unknown_status = _effective(include_unknown_status, values, "api_include_unknown_status")
    include_excluded = _effective(include_excluded, values, "api_include_excluded")
    include_deleted = _effective(include_deleted, values, "api_include_deleted")
    include_tools = _effective(include_tools, values, "api_include_tools")
    flags = {"active": include_active, "version": include_versions,
             "unknown": include_unknown_status, "excluded": include_excluded, "deleted": include_deleted}
    if any(type(flag) is not bool for flag in [*flags.values(), include_tools]):
        raise ValueError("include flags must be booleans")
    selected = [name for name in STATUS_CLASSES if flags[name]]
    scope = {"status_classes": selected, "include_tools": include_tools,
             "tool_roles": list(TOOL_ROLES), "coverage": "stored message text only"}
    if "message_groups" in stats:
        messages = chars = conversations = 0
        for row in stats["message_groups"]:
            if row["status_class"] in selected and (include_tools or not row["tool"]):
                messages += _number(row["messages"], "group.messages", integer=True)
                chars += _number(row["chars"], "group.chars", integer=True)
        for row in stats["conversation_scope_groups"]:
            present = row["non_tool_statuses"] + (row["tool_statuses"] if include_tools else [])
            if any(name in selected for name in present):
                conversations += _number(row["conversations"], "group.conversations", integer=True)
    else:
        if not include_tools:
            raise ValueError("tool exclusion needs archive_stats message-group counts")
        chars = sum(_number(stats.get(field, 0), field, integer=True) for name, field in (
            ("active", "chars_active"), ("version", "chars_versions"),
            ("unknown", "chars_unknown_status"), ("excluded", "chars_excluded"),
            ("deleted", "chars_deleted")) if flags[name])
        messages = None
        conversations = _number(stats["conversations"], "conversations", integer=True)
        scope["conversation_count"] = "legacy archive count; scope-specific overlap unavailable"
    return {"scope": scope, "conversations": conversations, "messages": messages, "chars": chars}


def _number(value, name, *, integer=False):
    if type(value) not in ((int,) if integer else (int, float)):
        raise ValueError(f"{name} must be a finite nonnegative {'integer' if integer else 'number'}")
    try:
        valid = value >= 0 and math.isfinite(value)
    except OverflowError:
        valid = False
    if not valid:
        raise ValueError(f"{name} must be finite and nonnegative")
    return value


def _effective(value, values, key):
    return values[key] if value is _UNSET else value


def _validated_preset_values(values):
    if not isinstance(values, Mapping):
        raise ValueError("audit preset values must be a complete object")
    numeric = ("chars_per_token_low", "chars_per_token_high", "output_ratio",
               "fraction", "pricing_batch_discount")
    integer = ("sqlite_cache_kib", "api_prefix_tokens", "cli_prefix_tokens")
    boolean = ("native_active_only", "api_include_active", "api_include_versions",
               "api_include_unknown_status", "api_include_excluded", "api_include_deleted",
               "api_include_tools", "cli_include_versions", "cli_include_unknown_status",
               "cli_include_tools")
    expected = {*numeric, *integer, *boolean, "cli_scope", "historical_pricing"}
    if set(values) != expected:
        missing, unknown = sorted(expected - set(values)), sorted(set(values) - expected, key=str)
        raise ValueError(f"audit preset values must be complete; missing={missing}, unknown={unknown}")
    values = dict(values)
    for name in numeric:
        _number(values[name], "audit preset " + name)
    for name in integer:
        _number(values[name], "audit preset " + name, integer=True)
    for name in boolean:
        if type(values[name]) is not bool:
            raise ValueError("audit preset " + name + " must be a boolean")
    if not 0 < values["fraction"] <= 1:
        raise ValueError("audit preset fraction must be in (0, 1]")
    if not values["sqlite_cache_kib"]:
        raise ValueError("audit preset sqlite_cache_kib must be positive")
    if values["chars_per_token_high"] <= 0 or values["chars_per_token_low"] < values["chars_per_token_high"]:
        raise ValueError("audit preset chars_per_token_low must be >= chars_per_token_high > 0")
    if values["cli_scope"] not in ("all", "active", "legacy"):
        raise ValueError("audit preset cli_scope must be all, active or legacy")
    if type(values["historical_pricing"]) is not str or not values["historical_pricing"]:
        raise ValueError("audit preset historical_pricing must be a nonempty path reference")
    return values


def inspect_audit_preset(audit_preset=_UNSET) -> dict:
    """Inspect one complete pack or already-resolved values; no layering/fallback.

    File identity is the hash of the exact source bytes. A Mapping is a complete
    effective-values object supplied by a caller, hashed as canonical JSON.
    Neither inspection nor estimation loads the historical pricing reference.
    """
    if isinstance(audit_preset, Mapping):
        values = _validated_preset_values(audit_preset)
        raw = json.dumps(values, sort_keys=True, separators=(",", ":")).encode()
        return {"source": "caller-supplied effective values", "sha256": hashlib.sha256(raw).hexdigest(),
                "hash_scope": "canonical effective-values JSON", "values": values}
    path = BUILTIN_AUDIT_PRESET if audit_preset is _UNSET else audit_preset
    if not isinstance(path, (str, Path)):
        raise ValueError("audit_preset must be a pack path or complete effective-values object")
    path = Path(path)
    raw = path.read_bytes()
    pack = json.loads(raw)
    if not isinstance(pack, dict) or set(pack) != {"schema", "id", "version", "values"}:
        raise ValueError("audit preset must contain schema, id, version and complete values")
    if pack["schema"] != "loom.import_audit_preset/1" or type(pack["id"]) is not str or not pack["id"]:
        raise ValueError("invalid audit preset schema/id")
    if type(pack["version"]) is not int or pack["version"] != 1:
        raise ValueError("unsupported audit preset version")
    values = _validated_preset_values(pack["values"])
    return {"source": str(path), "sha256": hashlib.sha256(raw).hexdigest(),
            "hash_scope": "exact pack source bytes", "values": values}


def load_audit_preset(audit_preset=_UNSET) -> dict:
    """Read and validate all effective audit values; an explicit bad input fails."""
    return inspect_audit_preset(audit_preset)["values"]


def __getattr__(name):
    # Preserve the public Path/dict aliases without forcing builtin data reads
    # before a caller can explicitly select a different complete preset.
    if name not in ("PRICING", "CHARS_PER_TOKEN"):
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    values = load_audit_preset()
    if name == "PRICING":
        return _REPO_ROOT / values["historical_pricing"]
    return {"low": values["chars_per_token_low"], "high": values["chars_per_token_high"]}


def __dir__():
    return sorted(set(globals()) | {"PRICING", "CHARS_PER_TOKEN"})


def estimate(stats: dict, pricing: dict | None = None, *, fraction=_UNSET, output_ratio=_UNSET,
             prefix_tokens=_UNSET, include_versions=_UNSET,
             include_unknown_status=_UNSET, include_active=_UNSET,
             include_excluded=_UNSET, include_deleted=_UNSET,
             include_tools=_UNSET, chars_per_token_low=_UNSET,
             chars_per_token_high=_UNSET, audit_preset=_UNSET) -> dict:
    """One pass over selected text, projected output and a caller-set prefix.

    Fraction is a proportional projection, not measured sample selection.
    Output includes thinking only insofar as the chosen output_ratio includes it.
    Historical estimate() defaults retain the legacy status scope and prefix;
    the CLI defaults to every stored row and no invented additional prefix.
    """
    values = load_audit_preset(audit_preset)
    fraction = _effective(fraction, values, "fraction")
    output_ratio = _effective(output_ratio, values, "output_ratio")
    prefix_tokens = _effective(prefix_tokens, values, "api_prefix_tokens")
    include_active = _effective(include_active, values, "api_include_active")
    include_versions = _effective(include_versions, values, "api_include_versions")
    include_unknown_status = _effective(include_unknown_status, values, "api_include_unknown_status")
    include_excluded = _effective(include_excluded, values, "api_include_excluded")
    include_deleted = _effective(include_deleted, values, "api_include_deleted")
    include_tools = _effective(include_tools, values, "api_include_tools")
    chars_per_token_low = _effective(chars_per_token_low, values, "chars_per_token_low")
    chars_per_token_high = _effective(chars_per_token_high, values, "chars_per_token_high")
    _number(fraction, "fraction")
    if not 0 < fraction <= 1:
        raise ValueError("fraction must be in (0, 1]")
    _number(output_ratio, "output_ratio")
    _number(prefix_tokens, "prefix_tokens", integer=True)
    for field in ("chars_active", "chars_versions", "conversations"):
        _number(stats[field], field, integer=True)
    _number(stats.get("chars_unknown_status", 0), "chars_unknown_status", integer=True)
    cpts = {"low": _number(chars_per_token_low, "chars_per_token_low"),
            "high": _number(chars_per_token_high, "chars_per_token_high")}
    if cpts["high"] <= 0 or cpts["low"] < cpts["high"]:
        raise ValueError("chars_per_token_low must be >= chars_per_token_high > 0")
    projected = project_stats(stats, include_active=include_active, include_versions=include_versions,
        include_unknown_status=include_unknown_status, include_excluded=include_excluded,
        include_deleted=include_deleted, include_tools=include_tools, audit_preset=values)
    if pricing is not None and (not isinstance(pricing, dict) or not isinstance(pricing.get("models"), dict)):
        raise ValueError("pricing must be an object with a models object")
    discount = _number((pricing or {}).get("batch_discount", values["pricing_batch_discount"]), "batch_discount")
    prices = pricing["models"] if pricing is not None else {}
    for p in prices.values():
        if not isinstance(p, dict):
            raise ValueError("each model price must be an object")
        for field in ("input", "output", "cache_read"):
            _number(p[field], "price." + field)
    chars = projected["chars"]
    try:
        n_conv = _number(projected["conversations"] * fraction, "estimated_conversations")
        projected_prefix = _number(n_conv * prefix_tokens, "estimated_prefix_tokens")
    except OverflowError:
        raise ValueError("estimated_prefix_tokens exceeds numeric representation") from None
    out = {"schema": "loom.archive_cost_projection/1", "model_calls": 0,
           "local_import_model_cost_usd": 0, "local_compute_cost_usd": None,
           "projected": projected,
           "assumptions": {"fraction": fraction, "fraction_method": "proportional projection; not a measured sample",
                           "output_ratio": output_ratio, "prefix_tokens": prefix_tokens,
                           "include_versions": include_versions, "include_unknown_status": include_unknown_status,
                           "chars_per_token": cpts, "token_measurement": "character-based estimate; not tokenizer/billed usage",
                           "prefix_cache": (pricing or {}).get("prefix_assumption", "all prefixes priced at cache-read; first-write and cache eligibility unverified"),
                           "batch": "uniform table discount; provider/model eligibility unverified",
                           "pricing_as_of": (pricing or {}).get("as_of"),
                           "pricing_source": (pricing or {}).get("source", "not supplied; no model USD estimate")},
           "tokens": {}, "output_tokens": {}, "prefix_tokens": projected_prefix,
           "models": {} if pricing is not None else None,
           "models_unrounded": {} if pricing is not None else None}
    for band, cpt in cpts.items():
        try:
            tokens = chars * fraction / cpt
            _number(tokens, "estimated_tokens")
            out["tokens"][band] = round(tokens)
            out["output_tokens"][band] = _number(tokens * output_ratio, "estimated_output_tokens")
        except OverflowError:
            raise ValueError("estimated_tokens exceeds numeric representation") from None
    for model, p in prices.items():
        row, unrounded = {}, {}
        for band, tok in out["tokens"].items():
            try:
                usd = (tok * p["input"] + tok * output_ratio * p["output"] + projected_prefix * p["cache_read"]) / 1e6
            except OverflowError:
                raise ValueError("estimated_usd exceeds numeric representation") from None
            _number(usd, "estimated_usd")
            _number(usd * discount, "estimated_batch_usd")
            row[band] = round(usd, 2)
            row[band + "_batch"] = round(usd * discount, 2)
            # Retain old rounded-token display arithmetic, but expose continuous
            # character-based projections for comparison with the native audit.
            try:
                continuous_tokens = chars * fraction / cpts[band]
                continuous_usd = _number((continuous_tokens * p["input"] +
                    continuous_tokens * output_ratio * p["output"] +
                    projected_prefix * p["cache_read"]) / 1e6, "estimated_continuous_usd")
            except OverflowError:
                raise ValueError("estimated_continuous_usd exceeds numeric representation") from None
            unrounded[band] = continuous_usd
            unrounded[band + "_batch"] = _number(continuous_usd * discount, "estimated_continuous_batch_usd")
        out["models"][model] = row
        out["models_unrounded"][model] = unrounded
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--db", required=True, help="path to chatadhd.db (opened read-only)")
    ap.add_argument("--audit-preset", default=_UNSET,
                    help="complete import_audit.pack; explicit flags override its effective values")
    ap.add_argument("--pricing", help="explicit historical/custom pricing JSON; never fetched online")
    ap.add_argument("--input-price", type=float, help="caller-supplied USD per million input tokens")
    ap.add_argument("--output-price", type=float, help="caller-supplied USD per million output tokens")
    ap.add_argument("--prefix-price", type=float, help="USD per million prefix tokens; defaults to input price")
    ap.add_argument("--scope", choices=("all", "active", "legacy"), default=_UNSET,
                    help="all stored rows, active rows, or historical active+version+unknown scope; default from preset")
    ap.add_argument("--fraction", type=float, default=_UNSET, help="share of conversations read by the model")
    ap.add_argument("--output-ratio", type=float, default=_UNSET)
    ap.add_argument("--prefix-tokens", type=int, default=_UNSET, help="additional prompt prefix tokens per selected conversation")
    ap.add_argument("--chars-per-token-low", type=float, default=_UNSET)
    ap.add_argument("--chars-per-token-high", type=float, default=_UNSET)
    ap.add_argument("--sqlite-cache-kib", type=int, default=_UNSET)
    ap.add_argument("--include-versions", dest="include_versions", action="store_true", default=_UNSET, help="include kept older branches")
    ap.add_argument("--no-versions", dest="include_versions", action="store_false", default=_UNSET, help="skip kept older branches")
    ap.add_argument("--include-unknown-status", dest="include_unknown_status", action="store_true", default=_UNSET, help="include counted unknown-status messages")
    ap.add_argument("--no-unknown-status", dest="include_unknown_status", action="store_false", default=_UNSET, help="omit explicitly counted unknown-status messages")
    ap.add_argument("--include-tools", dest="include_tools", action="store_true", default=_UNSET, help="include tool/function roles in projection")
    ap.add_argument("--no-tools", dest="include_tools", action="store_false", default=_UNSET, help="exclude tool/function roles from projection only")
    ap.add_argument("--include-excluded", action=argparse.BooleanOptionalAction, default=_UNSET)
    ap.add_argument("--include-deleted", action=argparse.BooleanOptionalAction, default=_UNSET)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    if (a.input_price is None) != (a.output_price is None):
        ap.error("--input-price and --output-price must be supplied together")
    if a.prefix_price is not None and a.input_price is None:
        ap.error("--prefix-price requires --input-price and --output-price")
    if a.pricing and a.input_price is not None:
        ap.error("choose --pricing or explicit prices")
    try:
        values = load_audit_preset(a.audit_preset)
        scope = _effective(a.scope, values, "cli_scope")
        fraction = _effective(a.fraction, values, "fraction")
        stats = archive_stats(a.db, sqlite_cache_kib=a.sqlite_cache_kib, audit_preset=values)
        pricing = json.loads(Path(a.pricing).read_text(encoding="utf-8")) if a.pricing else None
        if a.input_price is not None:
            pricing = {"source": "caller-supplied USD per million tokens; not verified current prices",
                "prefix_assumption": "prefix price caller-supplied; cache eligibility unverified" if a.prefix_price is not None else "prefix charged at input rate; cache eligibility unverified",
                "models": {"configured": {"input": a.input_price, "output": a.output_price,
                    "cache_read": a.prefix_price if a.prefix_price is not None else a.input_price}}}
        est = estimate(stats, pricing, fraction=fraction, output_ratio=a.output_ratio,
            prefix_tokens=_effective(a.prefix_tokens, values, "cli_prefix_tokens"),
            include_active=True,
            include_versions=(scope != "active" and values["cli_include_versions"]) if a.include_versions is _UNSET else a.include_versions,
            include_unknown_status=(scope != "active" and values["cli_include_unknown_status"]) if a.include_unknown_status is _UNSET else a.include_unknown_status,
            include_excluded=(scope == "all") if a.include_excluded is _UNSET else a.include_excluded,
            include_deleted=(scope == "all") if a.include_deleted is _UNSET else a.include_deleted,
            include_tools=_effective(a.include_tools, values, "cli_include_tools"),
            chars_per_token_low=a.chars_per_token_low,
            chars_per_token_high=a.chars_per_token_high, audit_preset=values)
    except (ValueError, OSError, sqlite3.Error, KeyError, TypeError) as exc:
        ap.error(str(exc))
    stats["projected"] = est["projected"]
    if a.json:
        json.dump({"stats": stats, "estimate": est}, sys.stdout, ensure_ascii=False, indent=2)
        print()
        return 0
    raw, projected = stats["raw"], est["projected"]
    print(f"Raw stored text: {raw['conversations']:,} conversations, {raw['messages']:,} messages, {raw['chars']:,} Unicode characters")
    print(f"Projection: {projected['conversations']:,} conversations, {projected['messages']:,} messages, {projected['chars']:,} characters; scope {projected['scope']}")
    print(f"Estimated text input tokens (fraction {fraction}): {est['tokens']['low']:,} - {est['tokens']['high']:,}; prefix tokens {est['prefix_tokens']:,.0f}")
    if est["models"] is None:
        print("Model USD estimate unavailable: supply input/output prices or an explicit pricing file. Local import model-call cost: $0; local compute cost unmeasured.")
    else:
        print(f"USD per reading pass (low - high; hypothetical batch in brackets), supplied prices as of {pricing.get('as_of')}:")
        for model, r in est["models"].items():
            print(f"  {model:20s} {r['low']:>10,.2f} - {r['high']:>10,.2f}   [{r['low_batch']:,.2f} - {r['high_batch']:,.2f}]")
    print("Character-based estimates only; source JSON, attachments/OCR, framing and hidden prompts are outside scope.")
    print("Cache first-write/eligibility and uniform batch discount are unverified planning assumptions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
