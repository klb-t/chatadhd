#!/usr/bin/env python3
"""Reproduce only the synthetic DOCX graph fixtures; --check never writes."""
import json
from pathlib import Path
import sys
from test_projection import ROOT, fixture, reader

DEST = ROOT.parents[1] / 'tests/fixtures/docx_parts'


def main():
    limits = json.loads((ROOT / 'preset.json').read_text())
    expected = {}
    for variant in ('base', 'zip-change', 'text-change'):
        raw = fixture(variant)
        expected[variant + '.docx'] = raw
    expected['base.json'] = (json.dumps(reader.extract(expected['base.docx'], limits),
                                       ensure_ascii=False, indent=2) + '\n').encode()
    if '--check' in sys.argv:
        for name, data in expected.items():
            if (DEST / name).read_bytes() != data:
                raise SystemExit('stale synthetic fixture: ' + name)
    else:
        DEST.mkdir(parents=True, exist_ok=True)
        for name, data in expected.items():
            (DEST / name).write_bytes(data)
    print('synthetic DOCX fixtures: 4/4')


if __name__ == '__main__':
    main()
