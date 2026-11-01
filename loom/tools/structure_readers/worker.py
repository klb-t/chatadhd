"""Trusted package worker: immutable stdin bytes -> pinned structured receipt.
No uploaded paths/code are executed. argv[2] is constructed by the native host.
"""
import hashlib
import json
from pathlib import Path
import resource
import runpy
import sys


def main():
    try:
        job = json.loads(sys.argv[2])
        profile = job['profile']
        limits = profile['limits']
        resource.setrlimit(resource.RLIMIT_CPU, (limits['cpu_seconds'], limits['cpu_seconds']))
        resource.setrlimit(resource.RLIMIT_AS, (limits['memory_bytes'], limits['memory_bytes']))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        raw = sys.stdin.buffer.read(limits['max_input_bytes'] + 1)
        if len(raw) > limits['max_input_bytes']:
            raise ValueError('host_reader_input_limit')
        root = Path(__file__).resolve().parent
        key, name = job['source_key'], job['name']
        parts = []
        if job['format'] == 'csv':
            reader = runpy.run_path(str(root/'csv/reader.py'))
            options = reader['Options'](**profile['csv'])
            receipt = reader['recognize'](raw, source_key=key, name=name, options=options)
            if 'selection' in job:
                selection = reader['read_selection'](raw, job['selection'], source_key=key, name=name, options=options)
            else:
                selection = None
            by_id = {o['id']: o for o in receipt['observations']}
            for row, unit in zip(receipt['rows'], receipt['units']):
                unit["kind"] = profile["kinds"]["row"]
                parent = len(parts)
                parts.append({'unit': unit, 'parent': None, 'observations': row['cells']})
                for ref in row['cells']:
                    ob = by_id[ref]
                    loc = ob['locator']
                    cell = dict(unit, id=reader['stable_id']('un_', unit['source'], reader['canonical'](loc)),
                                locator=loc, kind=profile['kinds']['cell'], bytes=loc['byte_len'],
                                attrs={'csv_locator': ob['attrs']['csv_locator'],
                                       'physical_lines': ob['attrs']['physical_lines'],
                                       'decoded': ob['attrs']['decoded']})
                    parts.append({'unit': cell, 'parent': parent, 'observations': [ref]})
        elif job['format'] == 'python_ast':
            reader = runpy.run_path(str(root/'python_ast/reader.py'))
            receipt = reader['parse'](raw, source_key=key, name=name)
            for d, unit in zip(receipt['declarations'], receipt['units']):
                parts.append({'unit': unit, 'parent': d['parent'], 'observations': [d['observation_id']]})
            selection = None
        else:
            raise ValueError('host_reader_format_unavailable')
        output = {'schema': 'loom.host_structure/1', 'receipt': receipt, 'parts': parts}
        if selection is not None:
            output['selection'] = selection
        encoded = json.dumps(output, ensure_ascii=False, separators=(',', ':')).encode()
        if len(encoded) + 1 > profile['max_output_bytes']:
            raise ValueError('host_reader_output_limit')
        sys.stdout.buffer.write(encoded + b'\n')
        return 0
    except Exception as error:
        # Parser diagnostics are codes only; never relay source/path/traceback.
        code = getattr(error, 'code', None) or getattr(error, 'detail', {}).get('code')
        if code is None:
            code = 'host_reader_failed'
        print(json.dumps({'error': {'code': code}}))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
