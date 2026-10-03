"""One native child per fresh supervisor: preserve bytes and measure child usage."""
import argparse
import json
from pathlib import Path
import resource
import subprocess
import sys
import time


def run(command, output):
    start = time.monotonic()
    process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    wall = time.monotonic() - start
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    with Path(output).open('x') as handle:
        json.dump({'wall_seconds': wall, 'user_cpu_seconds': usage.ru_utime,
            'system_cpu_seconds': usage.ru_stime, 'max_rss_kib': usage.ru_maxrss,
            'child_processes': 1, 'native_exit_code': process.returncode,
            'measurement': 'fresh_linux_python_supervisor_RUSAGE_CHILDREN'}, handle)
        handle.write('\n')
    sys.stdout.buffer.write(process.stdout)
    sys.stderr.buffer.write(process.stderr)
    return process.returncode


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metric-output', required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('native child command required')
    sys.exit(run(command, args.metric_output))
