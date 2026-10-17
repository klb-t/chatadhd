"""Timestamps: parse any native form, keep the original, normalise to UTC.

Native forms seen in archives: epoch seconds (ChatGPT, float/int), ISO 8601
with zone (Claude, Claude Code, Gemini), ISO without zone (assumed zone from
the ``timeline.assume_zone`` preset, flagged ``inferred``), date only, and the
file mtime as the last resort (flagged ``mtime``).  The timeline is always
computed in UTC; outputs may show a chosen zone (``source`` = untouched).
"""
import re
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import Any, Optional

_ISO_RE = re.compile(
    r"^(?P<y>\d{4})-(?P<m>\d{2})-(?P<d>\d{2})"
    r"(?:[T ](?P<H>\d{2}):(?P<M>\d{2})(?::(?P<S>\d{2})(?:[.,](?P<f>\d{1,9}))?)?)?"
    r"\s*(?P<z>Z|z|[+-]\d{2}:?\d{2})?$")


@dataclass
class Stamp:
    """A parsed moment: ``utc`` (aware datetime) plus how it was obtained."""
    utc: Optional[datetime]
    src: Any                     # the original value, untouched
    how: str                     # epoch | iso | iso_nozone | date | mtime | none
    inferred: bool = False       # zone was assumed, not present in the source

    @property
    def iso(self) -> Optional[str]:
        return to_iso(self.utc)

    def as_dict(self) -> dict[str, Any]:
        return {"src": self.src, "utc": self.iso, "how": self.how, "inferred": self.inferred}


def parse_zone(spec: Optional[str]) -> timezone:
    """``"UTC"`` | ``"+02:00"`` | ``"-0530"`` → tzinfo (preset zone for naive stamps)."""
    if not spec or spec.upper() in ("UTC", "Z"):
        return timezone.utc
    m = re.match(r"^([+-])(\d{2}):?(\d{2})$", spec)
    if not m:
        if "/" in spec or spec.isalpha():  # IANA name (Europe/Warsaw) through the standard library
            try:
                from zoneinfo import ZoneInfo, ZoneInfoNotFoundError
                return ZoneInfo(spec)  # type: ignore[return-value]
            except (ImportError, ZoneInfoNotFoundError, ValueError) as exc:
                raise ValueError(f"unknown zone {spec!r}: {exc} (use UTC, +HH:MM or an IANA name with tzdata installed)") from None
        raise ValueError(f"unknown zone spec: {spec!r} (use UTC, +HH:MM or an IANA name)")
    sign = 1 if m.group(1) == "+" else -1
    return timezone(sign * timedelta(hours=int(m.group(2)), minutes=int(m.group(3))))


def parse(value: Any, *, assume_zone: Optional[str] = None, mtime: Optional[float] = None) -> Stamp:
    """Parse ``value`` to a Stamp; never raises on bad input (``how="none"``)."""
    if value is None or value == "":
        if mtime is not None:
            return Stamp(datetime.fromtimestamp(mtime, tz=timezone.utc), value, "mtime", True)
        return Stamp(None, value, "none")
    if isinstance(value, bool):
        return Stamp(None, value, "none")
    if isinstance(value, (int, float)):
        try:
            v = float(value)
            if v > 1e12:          # milliseconds
                v /= 1000.0
            return Stamp(datetime.fromtimestamp(v, tz=timezone.utc), value, "epoch")
        except (OverflowError, OSError, ValueError):
            return Stamp(None, value, "none")
    if isinstance(value, str):
        s = value.strip()
        if re.fullmatch(r"\d{9,13}(\.\d+)?", s):
            return parse(float(s), assume_zone=assume_zone)
        m = _ISO_RE.match(s)
        if not m:
            return Stamp(None, value, "none")
        y, mo, d = int(m.group("y")), int(m.group("m")), int(m.group("d"))
        H = int(m.group("H") or 0)
        M = int(m.group("M") or 0)
        S = int(m.group("S") or 0)
        frac = m.group("f") or ""
        us = int((frac + "000000")[:6]) if frac else 0
        try:
            naive = datetime(y, mo, d, H, M, S, us)
        except ValueError:
            return Stamp(None, value, "none")
        z = m.group("z")
        if z:
            tz = timezone.utc if z in ("Z", "z") else parse_zone(z)
            return Stamp(naive.replace(tzinfo=tz).astimezone(timezone.utc), value, "iso")
        tz = parse_zone(assume_zone)
        how = "date" if m.group("H") is None else "iso_nozone"
        return Stamp(naive.replace(tzinfo=tz).astimezone(timezone.utc), value, how, inferred=True)
    return Stamp(None, value, "none")


def to_iso(dt: Optional[datetime]) -> Optional[str]:
    """UTC ISO 8601 with ``Z``; microseconds only when non-zero."""
    if dt is None:
        return None
    dt = dt.astimezone(timezone.utc)
    if dt.microsecond:
        return dt.strftime("%Y-%m-%dT%H:%M:%S.%f") + "Z"
    return dt.strftime("%Y-%m-%dT%H:%M:%SZ")


def key(s: Optional[str]) -> str:
    """Order key of a ``to_iso`` string: fixed width (``.ffffff`` always present), so a whole-second stamp sorts
    before a fractional one in the same second ('.' < 'Z' made ``…:00.250000Z`` sort before ``…:00Z``).
    ``to_iso`` keeps its compact form for display and for the files; every comparison goes through this."""
    if not s:
        return ""
    if not s.endswith("Z") or "T" not in s:
        return s
    body = s[:-1]
    head, dot, frac = body.partition(".")
    if len(head) < 19:
        return s
    return f"{head}.{(frac if dot else '').ljust(6, '0')[:6]}Z"


def from_iso(s: Optional[str]) -> Optional[datetime]:
    if not s:
        return None
    st = parse(s)
    return st.utc


def in_zone(dt: Optional[datetime], zone: Optional[str]) -> Optional[str]:
    """Render ``dt`` in ``zone`` (``"source"``/None → UTC ISO)."""
    if dt is None:
        return None
    if not zone or zone == "source":
        return to_iso(dt)
    local = dt.astimezone(parse_zone(zone))
    return local.isoformat()


def shift(dt: Optional[datetime], seconds: float) -> Optional[datetime]:
    return None if dt is None else dt + timedelta(seconds=seconds)


def round_to(dt: Optional[datetime], granularity: Optional[str]) -> Optional[datetime]:
    """``minute`` | ``hour`` | ``day`` | ``week`` | ``month`` | None."""
    if dt is None or not granularity or granularity == "none":
        return dt
    if granularity == "minute":
        return dt.replace(second=0, microsecond=0)
    if granularity == "hour":
        return dt.replace(minute=0, second=0, microsecond=0)
    if granularity == "day":
        return dt.replace(hour=0, minute=0, second=0, microsecond=0)
    if granularity == "week":
        day = dt.replace(hour=0, minute=0, second=0, microsecond=0)
        return day - timedelta(days=day.weekday())
    if granularity == "month":
        return dt.replace(day=1, hour=0, minute=0, second=0, microsecond=0)
    raise ValueError(f"unknown granularity: {granularity!r}")


def bucket_start(dt: datetime, unit: str) -> datetime:
    return round_to(dt, unit)  # type: ignore[return-value]


def bucket_next(start: datetime, unit: str) -> datetime:
    if unit == "day":
        return start + timedelta(days=1)
    if unit == "week":
        return start + timedelta(days=7)
    if unit == "month":
        if start.month == 12:
            return start.replace(year=start.year + 1, month=1)
        return start.replace(month=start.month + 1)
    if unit == "hour":
        return start + timedelta(hours=1)
    raise ValueError(f"unknown bucket unit: {unit!r}")


def parse_date_bound(text: str, *, end: bool = False) -> Optional[datetime]:
    """``2025``, ``2025-06``, ``2025-06-14``, full ISO → UTC datetime.

    ``end=True`` returns the exclusive upper bound of the period named.
    """
    s = text.strip()
    if not s:
        return None
    if re.fullmatch(r"\d{4}", s):
        start = datetime(int(s), 1, 1, tzinfo=timezone.utc)
        return bucket_next(start.replace(month=12), "month") if end else start
    if re.fullmatch(r"\d{4}-\d{2}", s):
        start = datetime(int(s[:4]), int(s[5:7]), 1, tzinfo=timezone.utc)
        return bucket_next(start, "month") if end else start
    if re.fullmatch(r"\d{4}-\d{2}-\d{2}", s):
        start = datetime(int(s[:4]), int(s[5:7]), int(s[8:10]), tzinfo=timezone.utc)
        return start + timedelta(days=1) if end else start
    st = parse(s)
    if st.utc is None:
        raise ValueError(f"not a date: {text!r}")
    return st.utc


def parse_duration(text: str) -> timedelta:
    """``3d`` | ``12h`` | ``30m`` | ``2w`` | ``45s`` → timedelta."""
    m = re.fullmatch(r"\s*(\d+(?:\.\d+)?)\s*([smhdw])\s*", text)
    if not m:
        raise ValueError(f"not a duration: {text!r} (use e.g. 3d, 12h, 30m, 2w)")
    n = float(m.group(1))
    unit = m.group(2)
    return timedelta(**{{"s": "seconds", "m": "minutes", "h": "hours", "d": "days", "w": "weeks"}[unit]: n})


class TimePolicy:
    """How times look in a restructured output (profile ``timestamps`` + run overrides).

    ``zone``: ``source`` (ISO strings with their own offset stay as written, epoch → UTC ISO; preset),
    ``UTC``, ``+HH:MM`` or an IANA name; ``format``: ``iso8601`` | ``epoch`` | ``date`` | ``source``
    (the native value untouched); ``shift``: a duration (``-3d``, ``12h``) added to every time;
    ``anonymise``: ``package`` (the earliest time of the package becomes 1970-01-01T00:00:00Z,
    intervals kept) | ``conversation`` (each conversation starts at the epoch) | None;
    ``granularity``: ``minute`` | ``hour`` | ``day`` | ``week`` | ``month`` | None.
    """

    def __init__(self, spec: Optional[dict[str, Any]] = None, **over: Any) -> None:
        spec = dict(spec or {})
        spec.update({k: v for k, v in over.items() if v is not None})
        self.zone = spec.get("zone") or "source"
        self.format = spec.get("format") or "iso8601"
        sh = spec.get("shift")
        self.shift_seconds = parse_duration(sh.lstrip("+-")).total_seconds() * (-1 if str(sh).startswith("-") else 1) \
            if isinstance(sh, str) and sh else float(sh or 0)
        self.anonymise = spec.get("anonymise") or None
        if self.anonymise is True:
            self.anonymise = "package"
        self.granularity = spec.get("granularity") or None
        self.origin: Optional[datetime] = None          # package origin for anonymise=package
        self.conv_origin: Optional[datetime] = None     # set per conversation for anonymise=conversation
        if self.zone != "source":
            parse_zone(self.zone)  # explicit error on a bad zone, before anything is written

    def as_dict(self) -> dict[str, Any]:
        return {"zone": self.zone, "format": self.format, "shift_seconds": self.shift_seconds, "anonymise": self.anonymise,
                "granularity": self.granularity}

    @property
    def changes_times(self) -> bool:
        return bool(self.shift_seconds or self.anonymise or self.granularity or self.zone not in ("source",)
                    or self.format not in ("iso8601", "source"))

    def render(self, native: Any, utc_iso: Optional[str]) -> Any:
        if self.format == "source" and not (self.shift_seconds or self.anonymise or self.granularity):
            return native
        dt = from_iso(utc_iso) if utc_iso else None
        if dt is None:
            return None if native in (None, "") else native
        if self.shift_seconds:
            dt = dt + timedelta(seconds=self.shift_seconds)
        origin = self.conv_origin if self.anonymise == "conversation" else self.origin if self.anonymise == "package" else None
        if origin is not None:
            dt = datetime(1970, 1, 1, tzinfo=timezone.utc) + (dt - (origin + timedelta(seconds=self.shift_seconds)))
        dt = round_to(dt, self.granularity)
        if self.format == "epoch":
            v = dt.timestamp()
            return int(v) if v == int(v) else v
        if self.format == "date":
            return (dt.astimezone(parse_zone(self.zone)) if self.zone != "source" else dt).date().isoformat()
        if self.zone == "source":
            if isinstance(native, str) and _ISO_RE.match(native.strip()) and not (self.shift_seconds or origin or self.granularity):
                return native
            return to_iso(dt)
        return in_zone(dt, self.zone)
