"""Installation-owned projection entry point using the shared bounded transport."""
import json
from pathlib import Path
import resource
import runpy
import sys


def main():
    try:
        limits = json.loads(sys.argv[2])
        resource.setrlimit(resource.RLIMIT_CPU, (limits['cpu_seconds'], limits['cpu_seconds']))
        resource.setrlimit(resource.RLIMIT_AS, (limits['memory_bytes'], limits['memory_bytes']))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        raw = sys.stdin.buffer.read(limits['max_input_bytes'] + 1)
        if len(raw) > limits['max_input_bytes']:
            raise ValueError('input_limit')
        project = runpy.run_path(str(Path(__file__).resolve().parent/'projection.py'))['project']
        result = project(json.loads(raw))
        json.dump(result, sys.stdout, ensure_ascii=False, separators=(',', ':'), allow_nan=False)
        sys.stdout.write('\n')
        return 0
    except Exception:
        print(json.dumps({'error': {'code': 'pdf_structure_invalid_input'}}))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
