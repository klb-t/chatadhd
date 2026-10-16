"""The curses renderer: draws ``CommanderState.frame()`` and turns terminal keys into key names.

Nothing else lives here — no selection, no edits, no files — so the commander's logic is tested
without a terminal.  Imported lazily by ``komander``; a Python without ``curses`` (Windows without
``windows-curses``) gets a readable message and keeps the CLI and the API.  Colours and border
characters come from ``data/commander/colors.json`` (``dark``, ``light``, ``mono`` = no colours,
ASCII, for 16-colour terminals and screen readers).  ``LOOM_XPORT_SCREEN_LOG=<file>`` appends the
plain text of every new frame to a file (used by the pty test; off by default)."""
import curses
import logging
import os
from typing import Any, Optional

from .state import CommanderState

log = logging.getLogger(__name__)

_SPECIAL = {curses.KEY_UP: "Up", curses.KEY_DOWN: "Down", curses.KEY_LEFT: "Left", curses.KEY_RIGHT: "Right",
            curses.KEY_PPAGE: "PageUp", curses.KEY_NPAGE: "PageDown", curses.KEY_HOME: "Home", curses.KEY_END: "End",
            curses.KEY_IC: "Insert", curses.KEY_DC: "Delete", curses.KEY_BACKSPACE: "Backspace", curses.KEY_ENTER: "Enter",
            curses.KEY_BTAB: "S-Tab", curses.KEY_RESIZE: "Resize"}
_COLORS = {"black": curses.COLOR_BLACK, "red": curses.COLOR_RED, "green": curses.COLOR_GREEN, "yellow": curses.COLOR_YELLOW,
           "blue": curses.COLOR_BLUE, "magenta": curses.COLOR_MAGENTA, "cyan": curses.COLOR_CYAN, "white": curses.COLOR_WHITE}
_STYLES = {"bold": curses.A_BOLD, "reverse": curses.A_REVERSE, "underline": curses.A_UNDERLINE, "dim": curses.A_DIM}


def key_name(ch: Any) -> Optional[str]:
    """A curses key (str from get_wch, or an int key code) → the key name the keymap uses."""
    if isinstance(ch, int):
        if ch in _SPECIAL:
            return _SPECIAL[ch]
        if curses.KEY_F0 < ch <= curses.KEY_F0 + 12:
            return f"F{ch - curses.KEY_F0}"
        return None
    if ch in ("\n", "\r"):
        return "Enter"
    if ch == "\t":
        return "Tab"
    if ch == "\x1b":
        return "Esc"
    if ch in ("\x7f", "\x08"):
        return "Backspace"
    if ch == " ":
        return "Space"
    if len(ch) == 1 and 1 <= ord(ch) <= 26:
        return "C-" + chr(ord(ch) + 96)
    if len(ch) == 1 and ch.isprintable():
        return ch
    return None


class Painter:
    def __init__(self, scr: Any, profile: dict[str, Any]) -> None:
        self.scr = scr
        self.attrs: dict[str, int] = {}
        use_color = curses.has_colors() and not profile.get("ascii")
        if use_color:
            curses.start_color()
            try:
                curses.use_default_colors()
                default = -1
            except curses.error:
                log.debug("terminal has no default colours", exc_info=True)
                default = curses.COLOR_BLACK
        for i, (name, (fg, bg, styles)) in enumerate(sorted((profile.get("attrs") or {}).items()), 1):
            a = 0
            for s in styles or []:
                a |= _STYLES.get(s, 0)
            if use_color and (fg or bg) and i < curses.COLOR_PAIRS:
                f = _COLORS.get(fg, default) if fg != "default" else default
                b = _COLORS.get(bg, default) if bg != "default" else default
                try:
                    curses.init_pair(i, f, b)
                    a |= curses.color_pair(i)
                except curses.error:
                    log.debug("colour pair %s not available", name, exc_info=True)
            self.attrs[name] = a

    def draw(self, frame: list[list[tuple[str, str]]]) -> None:
        h, w = self.scr.getmaxyx()
        self.scr.erase()
        for y, line in enumerate(frame[:h]):
            x = 0
            for text, attr in line:
                if x >= w:
                    break
                room = w - x - (1 if y == h - 1 else 0)  # the bottom-right cell cannot be written without scrolling
                if room <= 0:
                    break
                chunk = text[:room]
                try:
                    self.scr.addstr(y, x, chunk, self.attrs.get(attr, 0))
                except curses.error:
                    log.debug("addstr failed at %d,%d", y, x)
                x += len(chunk)
        self.scr.noutrefresh()
        curses.doupdate()


def run(state: CommanderState, *, colors: Optional[str] = None) -> int:
    os.environ.setdefault("ESCDELAY", "25")
    doc = state.ds.file("commander/colors.json")
    name = colors or state.ds.preset("commander.colors") or doc.get("default") or "dark"
    profile = (doc.get("profiles") or {}).get(name) or (doc.get("profiles") or {}).get("mono") or {}
    state.colors_ascii = bool(profile.get("ascii"))
    tick_ms = int(state.ds.preset("commander.tick_ms", 100) or 100)
    screen_log = os.environ.get("LOOM_XPORT_SCREEN_LOG")

    def main(scr: Any) -> None:
        curses.raw()  # Ctrl-Z, Ctrl-S, Ctrl-Y, Ctrl-C reach the keymap instead of the terminal driver
        curses.noecho()
        scr.keypad(True)
        try:
            curses.curs_set(0)
        except curses.error:
            log.debug("cursor cannot be hidden on this terminal")
        scr.timeout(tick_ms)
        painter = Painter(scr, profile)
        last_text = None
        while not state.quit:
            h, w = scr.getmaxyx()
            state.resize(w, h)
            frame = state.frame()
            painter.draw(frame)
            if screen_log:
                text = "\n".join("".join(s for s, _ in line).rstrip() for line in frame)
                if text != last_text:
                    with open(screen_log, "a", encoding="utf-8") as fh:
                        fh.write(text + "\n\x0c\n")
                    last_text = text
            try:
                ch = scr.get_wch()
            except curses.error:
                ch = None  # the tick timeout: no key, keep polling the background work
            try:
                if ch is not None:
                    key = key_name(ch)
                    if key is not None:
                        state.feed(key)
                state.tick()
            except Exception as exc:  # noqa: BLE001 - the screen never dies on one key; the error is shown and logged
                log.warning("commander: %s", exc, exc_info=True)
                state.close_overlay()
                state._msg(f"{type(exc).__name__}: {exc}", error=True)

    # while curses owns the terminal, log lines on stderr would tear the screen: warnings go to the message line
    class _ToMessage(logging.Handler):
        def emit(self, record: logging.LogRecord) -> None:
            state._msg(record.getMessage(), error=record.levelno >= logging.WARNING)

    root = logging.getLogger()
    streams = [h for h in root.handlers if isinstance(h, logging.StreamHandler) and not isinstance(h, logging.FileHandler)]
    for h in streams:
        root.removeHandler(h)
    to_msg = _ToMessage(level=logging.WARNING)
    root.addHandler(to_msg)
    try:
        curses.wrapper(main)
    finally:
        root.removeHandler(to_msg)
        for h in streams:
            root.addHandler(h)
    return 0
