"""X-Port Commander with the real curses renderer, in a child process on a pseudo-terminal.

Only where ``curses``, a pty and the ``xterm-256color`` terminfo entry exist; otherwise the test is
skipped with the reason in ctest's output (never failed for a missing optional piece).  The child
opens synthetic fixtures, the test sends keys as the terminal would (sequences from terminfo):
F1 (help), Esc, Enter, Insert, F5, F10.  Checked: no exception (exit code 0), the autosave holds the
recorded steps, and the help screen shows every action of the profile (read from the renderer's
plain-text frame log, ``LOOM_XPORT_SCREEN_LOG``)."""
import json
import os
import subprocess
import sys
import threading
import time
import unittest

from _support import EXPORTS, REPO, ExtractTest

from loom.tools.extract.commander.keymap import Keymap
from loom.tools.extract.datafiles import store
from loom.tools.extract.i18n import label

ROWS, COLS = 160, 200


def _unavailable() -> str:
    try:
        import curses
        import fcntl  # noqa: F401
        import pty  # noqa: F401
        import termios  # noqa: F401
    except ImportError as exc:
        return f"import: {exc}"
    try:
        master, slave = os.openpty()
    except OSError as exc:
        return f"openpty: {exc}"
    try:
        curses.setupterm("xterm-256color", slave)
        if not curses.tigetstr("kf1"):
            return "terminfo xterm-256color has no kf1"
    except curses.error as exc:
        return f"terminfo xterm-256color: {exc}"
    finally:
        os.close(master)
        os.close(slave)
    return ""


class CommanderCursesTest(ExtractTest):
    def test_curses_screen_keys_autosave_and_help(self):
        reason = _unavailable()
        if reason:
            self.skipTest(f"curses lub pty niedostępne: {reason}")
        import curses
        import fcntl
        import struct
        import termios
        keys = {name: curses.tigetstr(cap).decode("latin-1") for name, cap in
                (("F1", "kf1"), ("F5", "kf5"), ("F10", "kf10"), ("Insert", "kich1"), ("Down", "kcud1"))}
        master, slave = os.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
        log = self.tmp / "screen.log"
        autosave = self.tmp / "auto.json"
        env = {k: v for k, v in os.environ.items() if k not in ("CHATADHD_DATA", "LOOM_EXTRACT_LANG")}
        env.update(TERM="xterm-256color", LOOM_XPORT_SCREEN_LOG=str(log), LOOM_EXTRACT_DATA_DIR=str(self.tmp / "data"),
                   LOOM_EXTRACT_LANG="pl", LINES=str(ROWS), COLUMNS=str(COLS))
        code = ("import sys; sys.path.insert(0, sys.argv[1]); from loom.tools.extract.cli import main; "
                "sys.exit(main(sys.argv[2:]))")
        proc = subprocess.Popen([sys.executable, "-I", "-c", code, str(REPO), "komander", str(EXPORTS / "openai_2026_sharded.zip"),
                                 "--pakiet", str(self.tmp / "pkg"), "--autozapis", str(autosave), "--nie-wznawiaj"],
                                stdin=slave, stdout=slave, stderr=slave, env=env, close_fds=True)
        os.close(slave)
        out = bytearray()

        def drain() -> None:
            while True:
                try:
                    chunk = os.read(master, 65536)
                except OSError:
                    return
                if not chunk:
                    return
                out.extend(chunk)
        reader = threading.Thread(target=drain, daemon=True)
        reader.start()

        def screen() -> str:
            return log.read_text(encoding="utf-8") if log.exists() else ""

        def wait_for(text: str, timeout: float = 60) -> None:
            t0 = time.time()
            while text not in screen():
                if proc.poll() is not None:
                    self.fail(f"commander exited ({proc.returncode}) before showing {text!r}: {bytes(out[-2000:])!r}")
                if time.time() - t0 > timeout:
                    self.fail(f"{text!r} not on the screen; last frame:\n{screen()[-3000:]}")
                time.sleep(0.05)

        def send(seq: str, pause: float = 0.3) -> None:
            os.write(master, seq.encode("latin-1"))
            time.sleep(pause)
        try:
            wait_for("openai_2026_sharded.zip")
            send(keys["F1"])
            wait_for("Pomoc — X-Port Commander")
            km = Keymap.load(store(), "fkeys")
            help_text = screen()
            for act in sorted(set(km.bindings.values())):
                self.assertIn(label(km.actions[act]["label"]), help_text, act)
            send("\x1b", 0.5)
            send("\r")
            wait_for("Migracja bazy danych")
            send(keys["Insert"])
            wait_for("zazn.: 1 rozm.")
            send(keys["F5"])
            wait_for("pakiet: 1 rozm.")
            send(keys["F10"])
            proc.wait(timeout=60)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=10)
            os.close(master)
        self.assertEqual(proc.returncode, 0, bytes(out[-2000:]))
        doc = json.loads(autosave.read_text(encoding="utf-8"))
        ops = [s["op"] for s in doc["recipe"]["steps"] if s.get("note") != "implicit"]
        self.assertEqual(ops, ["select.items", "package.copy"])
        self.assertEqual({s["by"] for s in doc["recipe"]["steps"] if s.get("note") != "implicit"}, {"tui"})
        self.assertNotIn("Traceback", bytes(out).decode("utf-8", "replace"))


if __name__ == "__main__":
    unittest.main()
