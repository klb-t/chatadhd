"""Headless driver: a list of key names drives ``CommanderState`` without a terminal — for tests,
scripts (``komander --bez-ekranu --klawisze "…"``) and the replay tutorial (``--odtworz``).  After
every key the driver ticks the state; ``settle`` waits for a background job to finish (the screen
never waits — only the driver does, so a test can check the result)."""
import time
from typing import Any, Optional

from .state import CommanderState, parse_keys


class Driver:
    def __init__(self, state: CommanderState) -> None:
        self.state = state

    def press(self, *keys: str, settle: bool = True) -> "Driver":
        for k in keys:
            self.state.feed(k)
            if settle:
                self.settle()
            else:
                self.state.tick()
        return self

    def type(self, text: str, settle: bool = True) -> "Driver":
        return self.press(*["Space" if c == " " else c for c in text], settle=settle)

    def script(self, script: str, settle: bool = True) -> "Driver":
        return self.press(*parse_keys(script), settle=settle)

    def settle(self, timeout: Optional[float] = None) -> "Driver":
        st = self.state
        t0 = time.time()
        while True:
            job = st.job
            if job is not None:
                j = st.s.jobs.get(job)
                if j is not None and j.thread is not None:
                    j.thread.join(0.05)
                if j is not None and j.question and st.mode != "question":
                    st.tick()  # a question stops the job until somebody answers; the driver shows it and returns
                    return self
            st.tick()
            if st.job is None:
                return self
            if timeout is not None and time.time() - t0 > timeout:
                return self

    def screen(self) -> str:
        return self.state.text()

    def rows(self, pane: Optional[int] = None) -> list[dict[str, Any]]:
        p = self.state.panes[self.state.active if pane is None else pane]
        self.state.refresh(p)
        return p.rows
