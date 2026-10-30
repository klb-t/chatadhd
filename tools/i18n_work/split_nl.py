#!/usr/bin/env python3
"""split a '## feature' sectioned file and run merge_nl.py for each section (run from loom/)."""
import sys, subprocess, re, tempfile
cur, buf, out = None, [], {}
for line in open(sys.argv[1], encoding='utf8'):
    m = re.match(r'## (\w+)', line)
    if m: cur = m.group(1); out[cur] = []
    elif cur: out[cur].append(line)
for f, lines in out.items():
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False, encoding='utf8') as t: t.writelines(lines)
    subprocess.check_call([sys.executable, 'tools/i18n_work/merge_nl.py', f, t.name])
