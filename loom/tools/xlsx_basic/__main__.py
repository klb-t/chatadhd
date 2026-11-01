"""Read bounded XLSX bytes from stdin and emit one JSON result (no source paths)."""
import argparse
import json
import sys

from .container import XlsxError, check
from .reader import read, settings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sheet')
    parser.add_argument('--range', dest='selected_range')
    parser.add_argument('--sha256')
    parser.add_argument('--limits-json', help='Trusted limits object, not source content')
    args = parser.parse_args()
    try:
        limits = settings(json.loads(args.limits_json) if args.limits_json else None)
        ceiling = limits['max_input_bytes']
        data = sys.stdin.buffer.read(ceiling + 1 if ceiling else -1)
        check(limits, 'max_input_bytes', len(data))
        result = read(data, limits=limits, sheet=args.sheet,
                      selected_range=args.selected_range, expected_sha256=args.sha256)
        output = json.dumps(result, ensure_ascii=False)
        check(limits, 'max_output_bytes', len(output.encode('utf-8')) + 1)
        sys.stdout.buffer.write((output + '\n').encode('utf-8'))
        return 0
    except (XlsxError, ValueError) as exc:
        code = exc.code if isinstance(exc, XlsxError) else 'xlsx_configuration_invalid'
        sys.stdout.write(json.dumps({'error': {'code': code}}) + '\n')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
