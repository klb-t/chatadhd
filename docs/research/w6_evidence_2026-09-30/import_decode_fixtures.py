#!/usr/bin/env python3
"""Restore frozen ZIP bytes from text distribution; do not overwrite mismatches."""
import base64
import hashlib
import json
from pathlib import Path

base = Path(__file__).resolve().parent / 'import_fixtures'
manifest = json.loads((base / 'manifest.json').read_text())
for name in ('openai.zip', 'anthropic.zip'):
    target = base / name
    key = 'docs/research/w6_evidence_2026-09-30/import_fixtures/' + name
    expected = manifest['files'][key]
    raw = base64.b64decode((base / (name + '.b64')).read_bytes())
    if hashlib.sha256(raw).hexdigest() != expected:
        raise SystemExit('Decoded bytes fail frozen manifest: ' + name)
    if target.exists():
        if target.read_bytes() != raw:
            raise SystemExit('Refusing to overwrite mismatched ZIP: ' + name)
    else:
        with target.open('xb') as f:
            f.write(raw)
    print(name, expected, len(raw))
