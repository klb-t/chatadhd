#!/usr/bin/env python3
"""Merge machine translations (key = text lines) into a feature's `nl` locale of conversation_view.pack.
usage: merge_nl.py FEATURE FILE   -- keys not in the feature's `en` are refused; marks the language in the feature as machine translated."""
import json, sys
feature, path = sys.argv[1], sys.argv[2]
p = 'data/profiles/conversation_view.pack'
d = json.load(open(p, encoding='utf8'))
tr = {}
for line in open(path, encoding='utf8'):
    if ' = ' in line:
        k, v = line.rstrip('\n').split(' = ', 1); tr[k.strip()] = v
for e in d['entries']:
    v = e['value']
    if isinstance(v, dict) and v.get('feature') == feature:
        en = v['locales']['en']
        bad = [k for k in tr if k not in en]
        assert not bad, bad
        import re
        for k, t in tr.items():
            assert sorted(re.findall(r'\{\{\w+\}\}', t)) == sorted(re.findall(r'\{\{\w+\}\}', en[k])), (k, en[k], t)
        nl = v['locales'].setdefault('nl', {})
        for k in en:
            if k in tr: nl[k] = tr[k]
        print(feature, 'nl now', len(nl), 'of', len(en))
open(p, 'w', encoding='utf8').write(json.dumps(d, indent=2, ensure_ascii=False) + "\n")
