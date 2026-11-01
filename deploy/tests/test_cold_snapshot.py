"""Practical refusals + metadata roundtrip; never create product/account fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import struct
import subprocess
import sys

p=argparse.ArgumentParser();p.add_argument('--work',required=True);p.add_argument('--closed-sqlite',required=True);a=p.parse_args()
os.umask(0o077);root=Path(a.work);root.mkdir(mode=0o700)
tool=Path(__file__).resolve().parents[1]/'cold_snapshot.py'
checks=[]
def invoke(*args,ok=True,error=None,errno=None):
 r=subprocess.run([sys.executable,str(tool),*map(str,args)],capture_output=True,text=True,timeout=20)
 data=json.loads(r.stdout);assert r.stderr==''
 assert data['ok']==ok,(args,data)
 assert r.returncode==(0 if ok else 2)
 if error is not None:assert data['error']==error,data
 if errno is not None:assert data['errno']==errno,data
 return data
src=root/'tree';src.mkdir();(src/'blob').write_bytes(b'synthetic cold metadata canary')
os.chmod(src,0o750);os.chmod(src/'blob',0o640)
os.setxattr(src/'blob','user.loom_cold_test',b'synthetic attribute')
# Linux POSIX ACL: owner rw-, named current uid r-- (namespace supports only this UID), group r--, mask r--, other ---.
acl=struct.pack('<I',2)+b''.join(struct.pack('<HHI',tag,perm,ident) for tag,perm,ident in [(1,6,0xffffffff),(2,4,os.getuid()),(4,4,0xffffffff),(16,4,0xffffffff),(32,0,0xffffffff)])
os.setxattr(src/'blob','system.posix_acl_access',acl)
pre=invoke('preflight','--source',src,'--allowed-root',root,'--receipt',root/'pre.json','--writers-stopped')
common=['--source',src,'--allowed-root',root,'--destination-root',root,'--preflight',root/'pre.json','--preflight-sha256',pre['receipt_sha256'],'--writers-stopped']
b=invoke('backup',*common,'--destination',root/'backup')
r=invoke('restore','--snapshot',root/'backup','--allowed-root',root,'--destination',root/'restored','--destination-root',root,'--manifest-sha256',b['manifest_sha256'],'--writers-stopped')
assert os.getxattr(root/'restored/data/blob','system.posix_acl_access')==acl
assert os.getxattr(root/'restored/data/blob','user.loom_cold_test')==b'synthetic attribute'
assert (root/'restored/data/blob').read_bytes()==b'synthetic cold metadata canary'
assert (root/'restored/data/blob').stat().st_mode & 0o777 == 0o640
checks.append('roundtrip bytes/uid/gid/modes/times/xattr/POSIX-ACL (tool compares all metadata)')
invoke('backup',*common,'--destination',root/'backup',ok=False,error='FileExistsError',errno=17)
checks.append('existing destination refused')
invoke('backup',*common,'--destination',src/'nested',ok=False,error='overlapping_paths')
checks.append('nested overlap refused')
invoke('preflight','--source',src,'--allowed-root',root,'--receipt',root/'without.json',ok=False,error='explicit_writers_stopped_required')
checks.append('missing cold declaration refused')
linkroot=root/'links';linkroot.mkdir();(linkroot/'escape').symlink_to('/etc')
invoke('preflight','--source',linkroot,'--allowed-root',root,'--receipt',root/'link.json','--writers-stopped',ok=False,error='symlink_or_special_file_refused')
checks.append('source escape symlink refused without traversal')
(root/'parent-link').symlink_to(src,target_is_directory=True)
invoke('preflight','--source',root/'parent-link','--allowed-root',root,'--receipt',root/'parent.json','--writers-stopped',ok=False,error='NotADirectoryError',errno=20)
checks.append('symlink in source path refused')
# Refresh receipt: previous explicit verification read changed atime legitimately.
pre2=invoke('preflight','--source',src,'--allowed-root',root,'--receipt',root/'pre2.json','--writers-stopped')
(src/'blob').write_bytes(b'changed after preflight')
invoke('backup','--source',src,'--allowed-root',root,'--destination-root',root,'--preflight',root/'pre2.json','--preflight-sha256',pre2['receipt_sha256'],'--writers-stopped','--destination',root/'stale',ok=False,error='preflight_or_snapshot_changed')
checks.append('stale preflight refused')
invoke('restore','--snapshot',root/'backup','--allowed-root',root,'--destination',root/'badsha','--destination-root',root,'--manifest-sha256','0'*64,'--writers-stopped',ok=False,error='receipt_hash_mismatch')
checks.append('wrong snapshot hash refused')
# A dangling completion marker must not be followed or treated as absent.
(root/'backup/INCOMPLETE.json').symlink_to(root/'never-created-marker-target')
invoke('restore','--snapshot',root/'backup','--allowed-root',root,'--destination',root/'incomplete','--destination-root',root,'--manifest-sha256',b['manifest_sha256'],'--writers-stopped',ok=False,error='incomplete_snapshot')
assert not (root/'incomplete').exists()
checks.append('dangling INCOMPLETE marker refused without following link')

# A dedicated COPY of the already cold accepted SQLite, no new account generator.
hot=root/'hot';hot.mkdir()
fd=os.open(a.closed_sqlite,os.O_RDONLY|os.O_NOFOLLOW|os.O_NOATIME)
try:
 before=os.fstat(fd)
 with (hot/'accounts.sqlite').open('xb') as target:
  while True:
   chunk=os.read(fd,1024*1024)
   if not chunk:break
   target.write(chunk)
 after=os.fstat(fd)
 assert (before.st_atime_ns,before.st_mtime_ns,before.st_ctime_ns)==(after.st_atime_ns,after.st_mtime_ns,after.st_ctime_ns)
finally:os.close(fd)
c=sqlite3.connect(hot/'accounts.sqlite')
try:
 c.execute('BEGIN IMMEDIATE')
 invoke('preflight','--source',hot,'--allowed-root',root,'--receipt',root/'hot.json','--writers-stopped',ok=False,error='BlockingIOError',errno=11)
finally:c.rollback();c.close()
checks.append('actual active SQLite transaction refused despite cold declaration')
print(json.dumps({'success':True,'checks':checks,'count':len(checks),'metadata_limit':'named other UID ACL was refused EINVAL in single-UID namespace; same-UID ACL tested, other-UID ownership/ACL and MAC require administrator gate'}))
