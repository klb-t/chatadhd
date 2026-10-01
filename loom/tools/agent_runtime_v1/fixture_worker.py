"""Real disposable worker killed by the crash experiment's parent process."""
import os
from pathlib import Path
import sys
import time

from .runtime import Tool, observed
from .experiments import run_agent


def main():
    path, phase, commit = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
    def effect(environment, arguments):
        if phase == 'after_effect':
            with (path / 'effect.txt').open('ab') as file:
                file.write(b'x'); file.flush(); os.fsync(file.fileno())
        with (path / 'ready').open('xb') as file:
            file.write(b'ready'); file.flush(); os.fsync(file.fileno())
        time.sleep(30)
        return observed({'effect': 'returned'})
    tool = Tool('fixture.effect', '1', {'type': 'object'}, {'type': 'object'}, (),
                {'kind': 'synthetic_file_append'}, effect, {})
    run_agent(path, [tool], lambda _: {'kind': 'action', 'tool': tool.id, 'arguments': {}},
              commit=commit, lease=.2)


if __name__ == '__main__':
    main()
