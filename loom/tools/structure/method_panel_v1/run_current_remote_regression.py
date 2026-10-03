#!/usr/bin/env python3
"""Run existing tests belonging to a verified remote index, ignoring old copies."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import unittest

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--remote-index',required=True)
parser.add_argument('--inventory',required=True)
args=parser.parse_args()
structure=Path(__file__).resolve().parent.parent
repo=structure.parents[2]
index=json.loads(Path(args.remote_index).read_text())
lookup={r['path']:r for r in index}
selected=[]
excluded=[]
for f in sorted(structure.glob('test_*.py')):
    name=str(f.relative_to(repo))
    if name not in lookup:
        excluded.append(name)
        continue
    raw=f.read_bytes();blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
    if blob!=lookup[name]['sha']:
        raise ValueError('current_remote_test_hash_mismatch:'+name)
    selected.append({'path':name,'module':f.stem,'git_blob_sha':blob,'sha256':hashlib.sha256(raw).hexdigest()})
with Path(args.inventory).open('x')as out:
    out.write(json.dumps({'schema':'loom.research.current_remote_test_inventory/1','selected':selected,'excluded_untracked_oldcopy_not_in_remote_index':excluded,'gate_changes':False},indent=2)+'\n')
sys.path.insert(0,str(structure))
suite=unittest.defaultTestLoader.loadTestsFromNames([r['module']for r in selected])
result=unittest.TextTestRunner(verbosity=2).run(suite)
sys.exit(0 if result.wasSuccessful()else 1)
